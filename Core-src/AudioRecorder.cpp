/*
 * AudioRecorder - запись звука с аудиовхода Haiku в память BeAE.
 * Схема подключения взята из Haiku Softphone: узел входа через BMediaRoster::GetAudioInput(),
 * подключение BMediaRecorder к первому свободному выходу, приём формата «как есть».
 */
#include <MediaRoster.h>
#include <MediaAddOn.h>
#include <MediaRecorder.h>
#include <ParameterWeb.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Autolock.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "AudioRecorder.h"
#include "RecordConvert.h"
#include "Mixer.h"
#include "Preferences.h"

static const size_t kChunkFloats = 1 << 20;		// 4 МБ на блок

AudioRecorder Recorder;

AudioRecorder::AudioRecorder()
	:
	fRecorder(NULL),
	fExternalNode(false),
	fHaveNode(false),
	fPhysical(false),
	fRecording(false),
	fListening(false),
	fRingWritten(0),
	fRingRead(0),
	fChunkUsed(kChunkFloats),
	fFrames(0),
	fRate(44100.0f),
	fOutChannels(2),
	fSrcChannels(2),
	fSrcFormat(media_raw_audio_format::B_AUDIO_FLOAT),
	fSwap(false),
	fFormatKnown(false),
	fOutOfMemory(false),
	fPeakL(0.0f),
	fPeakR(0.0f),
	fFirstStamp(0),
	fFirstArrival(0),
	fInputLatency(0),
	fFirstStampReal(0),
	fTimeSource(NULL)
{
}

AudioRecorder::~AudioRecorder()
{
	Stop();
	fExternalNode = false;
	if (fRecorder) {
		fRecorder->Disconnect();
		fRecorder->SetHooks(NULL, NULL, NULL);
		delete fRecorder;
		fRecorder = NULL;
	}
	Clear();
}

void AudioRecorder::Clear()
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fChunks.size(); ++i)
		free(fChunks[i]);
	fChunks.clear();
	fChunkUsed = kChunkFloats;
	fFrames = 0;
	fPeaks.clear();
	fPeakAcc = 0;
	fPeakCount = 0;
	fFormatKnown = false;
	fOutOfMemory = false;
	fFirstStamp = 0;
	fFirstArrival = 0;
	fFirstStampReal = 0;
}

void AudioRecorder::CopyPeaks(std::vector<float> &out)
{
	BAutolock lock(fLock);
	out = fPeaks;
}

static const size_t kRingFrames = 1 << 17;		// около 3 с при 44,1 кГц

status_t AudioRecorder::Start(BString *error)
{
	if (fRecording)
		return B_OK;
	if (fListening)
		Stop();				// запись важнее тюнера: отпускаем вход
	return Open(error, false);
}

status_t AudioRecorder::StartListening(BString *error)
{
	if (fRecording) {
		if (error) *error = "TUNER_BUSY";
		return B_BUSY;
	}
	if (fListening)
		return B_OK;
	return Open(error, true);
}

size_t AudioRecorder::ReadListened(float *dst, size_t maxFrames)
{
	BAutolock lock(fLock);
	if (fRing.empty() || maxFrames == 0)
		return 0;
	int64 avail = fRingWritten - fRingRead;
	if (avail <= 0)
		return 0;
	if (avail > (int64)fRing.size())
		avail = (int64)fRing.size();					// тюнер отстал: берём самое свежее
	if (avail > (int64)maxFrames)
		avail = (int64)maxFrames;
	const int64 start = fRingWritten - avail;
	for (int64 i = 0; i < avail; i++)
		dst[i] = fRing[(size_t)((start + i) % (int64)fRing.size())];
	fRingRead = fRingWritten;
	return (size_t)avail;
}

// Диагностика: все параметры узла системного входа (выбор входа, усиление) в файл ~/BeAE_input_params.txt.
// Пишется один раз за запуск при первом открытии системного входа.
static void DumpNodeParameters(BMediaRoster *roster, const media_node &node)
{
	static int dumps = 0;
	if (dumps >= 6)			// файл перезаписывается при каждом открытии входа, но не бесконечно
		return;
	dumps++;

	BPath path;
	if (find_directory(B_USER_DIRECTORY, &path) != B_OK)
		return;
	path.Append("BeAE_input_params.txt");
	FILE *f = fopen(path.Path(), "w");
	if (f == NULL)
		return;

	live_node_info info;
	if (roster->GetLiveNodeInfo(node, &info) == B_OK)
		fprintf(f, "Node: '%s' (id %d, kind 0x%x)\n", info.name, (int)node.node, (unsigned)node.kind);

	BParameterWeb *web = NULL;
	status_t err = roster->GetParameterWebFor(node, &web);
	fprintf(f, "GetParameterWebFor: %s\n", strerror(err));
	if (err == B_OK && web != NULL) {
		fprintf(f, "Parameters: %d\n\n", (int)web->CountParameters());
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter *p = web->ParameterAt(i);
			if (p == NULL)
				continue;
			const char *type = "?";
			switch (p->Type()) {
				case BParameter::B_NULL_PARAMETER: type = "null"; break;
				case BParameter::B_DISCRETE_PARAMETER: type = "discrete"; break;
				case BParameter::B_CONTINUOUS_PARAMETER: type = "continuous"; break;
				default: break;
			}
			fprintf(f, "[%d] id=%d name='%s' kind='%s' type=%s group='%s' in=%d out=%d\n",
				(int)i, (int)p->ID(), p->Name(), p->Kind(), type,
				p->Group() ? p->Group()->Name() : "-",
				(int)p->CountInputs(), (int)p->CountOutputs());

			if (p->Type() == BParameter::B_DISCRETE_PARAMETER) {
				BDiscreteParameter *d = dynamic_cast<BDiscreteParameter*>(p);
				int32 value = 0;
				size_t size = sizeof(value);
				bigtime_t when = 0;
				status_t verr = d ? d->GetValue(&value, &size, &when) : B_ERROR;
				fprintf(f, "    value=%d (%s) size=%d\n", (int)value, strerror(verr), (int)size);
				if (d != NULL) {
					for (int32 k = 0; k < d->CountItems(); k++)
						fprintf(f, "    item %d: '%s' = %d\n", (int)k, d->ItemNameAt(k), (int)d->ItemValueAt(k));
				}
			} else if (p->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
				BContinuousParameter *c = dynamic_cast<BContinuousParameter*>(p);
				float values[8] = {0};
				size_t size = sizeof(values);
				bigtime_t when = 0;
				status_t verr = c ? c->GetValue(values, &size, &when) : B_ERROR;
				if (c != NULL) {
					fprintf(f, "    min=%g max=%g step=%g unit='%s' channels=%d\n",
						c->MinValue(), c->MaxValue(), c->ValueStep(), c->Unit(), (int)c->CountChannels());
				}
				fprintf(f, "    values (%s, %d bytes):", strerror(verr), (int)size);
				for (size_t k = 0; k < size / sizeof(float) && k < 8; k++)
					fprintf(f, " %g", values[k]);
				fprintf(f, "\n");
			}
		}
		delete web;
	}
	{
		media_output outs[16];
		int32 n = 0;
		status_t oerr = roster->GetAllOutputsFor(node, outs, 16, &n);
		fprintf(f, "\nOutputs: %s, %d\n", strerror(oerr), (int)n);
		for (int32 i = 0; oerr == B_OK && i < n; i++) {
			fprintf(f, "  [%d] name='%s' source=%d/%d dest=%d/%d format type=%d",
				(int)i, outs[i].name, (int)outs[i].source.port, (int)outs[i].source.id,
				(int)outs[i].destination.port, (int)outs[i].destination.id, (int)outs[i].format.type);
			if (outs[i].format.type == B_MEDIA_RAW_AUDIO)
				fprintf(f, " rate=%g channels=%d", outs[i].format.u.raw_audio.frame_rate,
					(int)outs[i].format.u.raw_audio.channel_count);
			fprintf(f, "\n");
		}
	}
	fclose(f);
	fprintf(stderr, "BeAE: input node parameters written to %s\n", path.Path());
}

// Аддон multi_audio (драйвер hda и др.) считает размер буфера КАЖДОГО выхода по всем входным каналам карты. Пока выход
// один стерео - всё сходится. Если у карты несколько стереовыходов (например две пары АЦП), буфер получается во столько раз
// больше данных, во сколько выходов: вторая половина буфера - нули, запись идёт «пачками» шума с паузами. Здесь размер
// сводится к реальному: буфер выхода * каналы выхода / сумма каналов всех выходов. Только для узла с выходами «input N» (так их называет multi_audio) и только когда
// размер делится ровно так, как считает аддон (иначе формат не трогаем).
static void FixMultiAudioBufferSize(BMediaRoster *roster, const media_node &node, media_format *fmt)
{
	if (roster == NULL || fmt == NULL || fmt->type != B_MEDIA_RAW_AUDIO)
		return;

	media_output outs[16];
	int32 n = 0;
	if (roster->GetAllOutputsFor(node, outs, 16, &n) != B_OK || n < 2)
		return;
	int32 sum = 0;
	for (int32 i = 0; i < n; i++) {
		// выходы multi_audio называются «input 0», «input 1» ...: по этому признаку узнаём аддон
		if (strncmp(outs[i].name, "input ", 6) != 0)
			return;
		if (outs[i].format.type == B_MEDIA_RAW_AUDIO)
			sum += (int32)outs[i].format.u.raw_audio.channel_count;
	}

	const media_raw_audio_format &raw = fmt->u.raw_audio;
	const int32 ch = (int32)raw.channel_count;
	const int32 size = (int32)(raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK);
	const size_t bytes = raw.buffer_size;
	if (ch < 1 || size < 1 || sum <= ch || bytes == 0 || bytes % ((size_t)sum * size) != 0)
		return;

	fmt->u.raw_audio.buffer_size = bytes / (size_t)sum * (size_t)ch;
	fprintf(stderr, "BeAE: multi_audio buffer size %d -> %d (channels %d of %d)\n",
		(int)bytes, (int)fmt->u.raw_audio.buffer_size, (int)ch, (int)sum);
}

// Живой узел-источник «сырого» звука с заданным именем; узел берётся (GetNodeFor), снимать - ReleaseNode.
static status_t FindSourceNode(BMediaRoster *roster, const char *name, media_node *node)
{
	media_format fmt;
	fmt.type = B_MEDIA_RAW_AUDIO;
	fmt.u.raw_audio = media_raw_audio_format::wildcard;
	live_node_info infos[64];
	int32 count = 64;
	status_t err = roster->GetLiveNodes(infos, &count, NULL, &fmt, NULL, B_BUFFER_PRODUCER);
	if (err == B_OK) {
		for (int32 i = 0; i < count; i++) {
			if (strcmp(infos[i].name, name) == 0)
				return roster->GetNodeFor(infos[i].node.node, node);
		}
	}
	// живой узел с таким именем, но без «сырого» формата в фильтре (выходы у него могут появиться позже)
	count = 64;
	err = roster->GetLiveNodes(infos, &count, NULL, NULL, name, B_BUFFER_PRODUCER);
	fprintf(stderr, "BeAE source '%s': live by name -> %s, %d found\n", name, strerror(err), (int)count);
	if (err == B_OK && count > 0) {
		media_output out;
		int32 nfree = 0;
		status_t ferr = roster->GetFreeOutputsFor(infos[0].node, &out, 1, &nfree, B_MEDIA_RAW_AUDIO);
		fprintf(stderr, "  free outputs of live node -> %s, %d\n", strerror(ferr), (int)nfree);
		if (ferr == B_OK && nfree > 0)
			return roster->GetNodeFor(infos[0].node.node, node);
		// живой узел есть, но выходов у него нет: пробуем поднять настоящий через список спящих
	}
	// спящий узел (карта, не выбранная «по умолчанию» в настройках Медиа): поднимаем его сами
	dormant_node_info dinfos[32];
	int32 dcount = 32;
	err = roster->GetDormantNodes(dinfos, &dcount, NULL, &fmt, NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	fprintf(stderr, "BeAE source '%s': dormant list -> %s, %d found\n", name, strerror(err), (int)dcount);
	if (err == B_OK) {
		for (int32 i = 0; i < dcount; i++) {
			fprintf(stderr, "  dormant: '%s'\n", dinfos[i].name);
			if (strcmp(dinfos[i].name, name) == 0) {
				status_t ierr = roster->InstantiateDormantNode(dinfos[i], node);
				fprintf(stderr, "  instantiate -> %s\n", strerror(ierr));
				if (ierr == B_OK) {
					media_output out;
					int32 nfree = 0;
					status_t ferr = roster->GetFreeOutputsFor(*node, &out, 1, &nfree, B_MEDIA_RAW_AUDIO);
					fprintf(stderr, "  free outputs of instantiated node -> %s, %d\n", strerror(ferr), (int)nfree);
				}
				return ierr;
			}
		}
	}
	return B_NAME_NOT_FOUND;
}

// Настоящий ли это вход карты: у драйвера в списке «Input» лежат и выходные пины (Line out, Speaker, Rear ...).
static bool IsCardInputName(const char *name)
{
	BString n(name);
	n.ToLower();
	if (n.FindFirst("mic") >= 0 || n.FindFirst("aux") >= 0 || n.FindFirst("cd") >= 0
			|| n.FindFirst("digital") >= 0 || n.FindFirst("spdif") >= 0 || n.FindFirst("stereo mix") >= 0)
		return n.FindFirst(" out") < 0;
	return n.Length() >= 3 && n.FindFirst(" in") == n.Length() - 3;		// заканчивается на « in» (Line in)
}

// Параметры выбора входа (kind «Input») узла; web принадлежит вызывающему.
static void CollectInputMuxes(BParameterWeb *web, std::vector<BDiscreteParameter*> &out)
{
	for (int32 i = 0; i < web->CountParameters(); i++) {
		BParameter *p = web->ParameterAt(i);
		if (p == NULL || p->Type() != BParameter::B_DISCRETE_PARAMETER || p->Kind() == NULL)
			continue;
		if (strcmp(p->Kind(), B_INPUT_MUX) != 0)
			continue;
		BDiscreteParameter *d = dynamic_cast<BDiscreteParameter*>(p);
		if (d != NULL)
			out.push_back(d);
	}
}

void AudioRecorder::ListCardInputs(std::vector<BString> &names, BString *current)
{
	names.clear();
	if (current) *current = "";
	BMediaRoster *roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;
	media_node node;
	if (roster->GetAudioInput(&node) != B_OK)
		return;
	BParameterWeb *web = NULL;
	if (roster->GetParameterWebFor(node, &web) == B_OK && web != NULL) {
		std::vector<BDiscreteParameter*> muxes;
		CollectInputMuxes(web, muxes);
		for (size_t m = 0; m < muxes.size(); m++) {
			BDiscreteParameter *d = muxes[m];
			if (m == 0) {
				for (int32 k = 0; k < d->CountItems(); k++) {
					if (!IsCardInputName(d->ItemNameAt(k)))
						continue;
					BString n(d->ItemNameAt(k));
					bool dup = false;
					for (size_t j = 0; j < names.size(); j++)
						if (names[j] == n) { dup = true; break; }
					if (!dup)
						names.push_back(n);
				}
			}
			if (current != NULL && current->Length() == 0) {
				int32 value = 0;
				size_t size = sizeof(value);
				bigtime_t when = 0;
				if (d->GetValue(&value, &size, &when) == B_OK) {
					for (int32 k = 0; k < d->CountItems(); k++) {
						if (d->ItemValueAt(k) == value && IsCardInputName(d->ItemNameAt(k))) {
							*current = d->ItemNameAt(k);
							break;
						}
					}
				}
			}
		}
		delete web;
	}
	roster->ReleaseNode(node);
}

status_t AudioRecorder::SetCardInput(const char *name, int muxIndex)
{
	if (name == NULL || name[0] == 0)
		return B_BAD_VALUE;
	BMediaRoster *roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;
	media_node node;
	status_t err = roster->GetAudioInput(&node);
	if (err != B_OK)
		return err;
	err = B_NAME_NOT_FOUND;
	BParameterWeb *web = NULL;
	if (roster->GetParameterWebFor(node, &web) == B_OK && web != NULL) {
		std::vector<BDiscreteParameter*> muxes;
		CollectInputMuxes(web, muxes);
		for (size_t m = 0; m < muxes.size(); m++) {
			if ((int)m != muxIndex)
				continue;
			BDiscreteParameter *d = muxes[m];
			for (int32 k = 0; k < d->CountItems(); k++) {
				if (strcmp(d->ItemNameAt(k), name) != 0)
					continue;
				int32 value = d->ItemValueAt(k);
				status_t serr = d->SetValue(&value, sizeof(value), 0);
				fprintf(stderr, "BeAE: card input '%s' -> parameter %d: %s\n", name, (int)d->ID(), strerror(serr));
				if (serr == B_OK)
					err = B_OK;
				break;
			}
		}
		delete web;
	}
	roster->ReleaseNode(node);
	return err;
}

void AudioRecorder::ListSources(std::vector<BString> &names)
{
	names.clear();
	BMediaRoster *roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	media_node systemInput;
	bool haveSystem = (roster->GetAudioInput(&systemInput) == B_OK);

	media_format fmt;
	fmt.type = B_MEDIA_RAW_AUDIO;
	fmt.u.raw_audio = media_raw_audio_format::wildcard;
	std::vector<BString> liveNames;						// имена всех живых узлов-источников (в том числе занятых)
	live_node_info infos[64];
	int32 count = 64;
	if (roster->GetLiveNodes(infos, &count, NULL, &fmt, NULL, B_BUFFER_PRODUCER) == B_OK) {
		for (int32 i = 0; i < count; i++) {
			liveNames.push_back(BString(infos[i].name));
			if (haveSystem && infos[i].node.node == systemInput.node)
				continue;							// системный вход - отдельный пункт списка
			if (strncmp(infos[i].name, "BeAE", 4) == 0)
				continue;							// наши собственные узлы
			media_output out;
			int32 nfree = 0;
			if (roster->GetFreeOutputsFor(infos[i].node, &out, 1, &nfree, B_MEDIA_RAW_AUDIO) != B_OK || nfree < 1)
				continue;							// выход занят (например, у микшера)
			BString n(infos[i].name);
			bool dup = false;
			for (size_t k = 0; k < names.size(); k++)
				if (names[k] == n) { dup = true; break; }
			if (!dup)
				names.push_back(n);
		}
	}
	// спящие аудиовходы (карты, не выбранные «по умолчанию»): BeAE поднимет их при записи
	dormant_node_info dinfos[32];
	int32 dcount = 32;
	if (roster->GetDormantNodes(dinfos, &dcount, NULL, &fmt, NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT) == B_OK) {
		for (int32 i = 0; i < dcount; i++) {
			BString n(dinfos[i].name);
			if (strncmp(n.String(), "BeAE", 4) == 0)
				continue;
			bool known = false;
			for (size_t k = 0; k < liveNames.size() && !known; k++)
				known = (liveNames[k] == n);
			for (size_t k = 0; k < names.size() && !known; k++)
				known = (names[k] == n);
			if (!known)
				names.push_back(n);
		}
	}
	if (haveSystem)
		roster->ReleaseNode(systemInput);
}

status_t AudioRecorder::Open(BString *error, bool listen)
{
	if (listen) {
		BAutolock lock(fLock);
		fRing.assign(kRingFrames, 0.0f);
		fRingWritten = 0;
		fRingRead = 0;
		fFormatKnown = false;
	} else {
		Clear();
	}
	fPeakL = fPeakR = 0.0f;

	BMediaRoster *roster = BMediaRoster::Roster();
	if (roster == NULL) {
		if (error) *error = "REC_ERR_ROSTER";
		return B_ERROR;
	}

	fPhysical = false;
	const bool external = !listen && fSource == MIX_SOURCE_EXTERNAL;
	const bool second = !listen && fSource == MIX_SOURCE_SYSTEM2;	// системный вход, второй выход узла
	const bool named = !listen && !external && !second && fSource.Length() > 0;

	status_t err = B_OK;
	if (external) {
		// узел-приёмник без подключения: источник соединит пользователь (Cortex)
	} else if (named) {
		err = FindSourceNode(roster, fSource.String(), &fInputNode);
		if (err != B_OK) {
			if (error) *error = "REC_ERR_SOURCE_NOT_FOUND";
			return err;
		}
		fHaveNode = true;
	} else {
		err = roster->GetAudioInput(&fInputNode);
		if (err != B_OK) {
			if (error) *error = "REC_ERR_NO_INPUT";
			return err;
		}
		fHaveNode = true;
		if (Prefs.rec_input_pin.Length() > 0)
			SetCardInput(Prefs.rec_input_pin.String());		// выбранный пользователем вход карты
		if (second && Prefs.rec_input_pin2.Length() > 0)
			SetCardInput(Prefs.rec_input_pin2.String(), 1);	// вход второго АЦП
		DumpNodeParameters(roster, fInputNode);			// после выбора входов: в файле видно, что реально выставлено
	}

	if (fHaveNode)
		fPhysical = (fInputNode.kind & B_PHYSICAL_INPUT) != 0;

	if (external) {
		if (fRecorder == NULL && !_CreateRecorder()) {
			if (error) *error = "REC_ERR_RECORDER";
			return B_ERROR;
		}
		fExternalNode = true;
		if (!fRecorder->IsConnected()) {
			// узел уже виден в Cortex: соедините с ним источник и нажмите Record ещё раз
			if (error) *error = "REC_ERR_EXTERNAL_NOT_CONNECTED";
			return B_MEDIA_NOT_CONNECTED;
		}
	} else {
		if (fRecorder != NULL && fExternalNode)
			PrepareExternal(false);
		if (!_CreateRecorder()) {
			if (error) *error = "REC_ERR_RECORDER";
			if (fHaveNode) roster->ReleaseNode(fInputNode);
			fHaveNode = false;
			return B_ERROR;
		}
	}

	media_output output;
	int32 count = 0;
	if (!external) {
		if (second) {
			// второй выход узла (по порядку в GetAllOutputsFor), если он свободен
			media_output all[16], freeOuts[16];
			int32 nAll = 0, nFree = 0;
			err = roster->GetAllOutputsFor(fInputNode, all, 16, &nAll);
			status_t ferr = roster->GetFreeOutputsFor(fInputNode, freeOuts, 16, &nFree, B_MEDIA_RAW_AUDIO);
			fprintf(stderr, "BeAE second input: outputs %d (%s), free %d (%s)\n", (int)nAll, strerror(err), (int)nFree, strerror(ferr));
			count = 0;
			if (err == B_OK && ferr == B_OK && nAll >= 2) {
				for (int32 i = 0; i < nFree; i++) {
					if (freeOuts[i].source.port == all[1].source.port && freeOuts[i].source.id == all[1].source.id) {
						output = freeOuts[i];
						count = 1;
						break;
					}
				}
			}
		} else {
			err = roster->GetFreeOutputsFor(fInputNode, &output, 1, &count, B_MEDIA_RAW_AUDIO);
		}
		if (err != B_OK || count < 1) {
			if (error) *error = "REC_ERR_NO_FREE_OUTPUT";
			if (err == B_OK) err = B_ERROR;
			goto fail;
		}

		media_format fmt;
		fmt.type = B_MEDIA_RAW_AUDIO;
		fmt.u.raw_audio = output.format.u.raw_audio;
		FixMultiAudioBufferSize(roster, fInputNode, &fmt);

		err = fRecorder->Connect(fInputNode, &output, &fmt);
		if (err != B_OK) {
			if (error) *error = "REC_ERR_CONNECT";
			fRecorder->SetHooks(NULL, NULL, NULL);
			goto fail;
		}
	}

	fInputLatency = 0;
	if (!external) {
		roster->GetLatencyFor(fInputNode, &fInputLatency);
		fTimeSource = roster->MakeTimeSourceFor(fInputNode);	// для перевода меток буферов в system_time
	}
	if (listen) fListening = true; else fRecording = true;
	err = fRecorder->Start(external);			// у внешнего узла соединение уже есть (сделано в Cortex)
	if (err != B_OK) {
		fRecording = false;
		fListening = false;
		if (error) *error = "REC_ERR_START";
		if (external)
			return err;							// узел оставляем, чтобы соединение не пропало
		fRecorder->Disconnect();
		fRecorder->SetHooks(NULL, NULL, NULL);
		goto fail;
	}
	return B_OK;

fail:
	if (fTimeSource) {
		fTimeSource->Release();
		fTimeSource = NULL;
	}
	delete fRecorder;
	fRecorder = NULL;
	if (fHaveNode) roster->ReleaseNode(fInputNode);
	fHaveNode = false;
	return err;
}

bool AudioRecorder::_CreateRecorder()
{
	fRecorder = new BMediaRecorder("BeAE recorder", B_MEDIA_RAW_AUDIO);
	if (fRecorder->InitCheck() != B_OK) {
		delete fRecorder;
		fRecorder = NULL;
		return false;
	}
	// принимаем любой «сырой» формат: частоту и каналы выбирает источник
	media_format accept;
	accept.type = B_MEDIA_RAW_AUDIO;
	accept.u.raw_audio = media_raw_audio_format::wildcard;
	fRecorder->SetAcceptedFormat(accept);
	fRecorder->SetHooks(&AudioRecorder::_RecordHook, NULL, this);
	return true;
}

void AudioRecorder::PrepareExternal(bool on)
{
	if (fRecording || fListening)
		return;
	if (on) {
		if (fRecorder == NULL && _CreateRecorder())
			fExternalNode = true;
	} else if (fRecorder != NULL && fExternalNode) {
		fRecorder->Stop(true);
		fRecorder->Disconnect();
		fRecorder->SetHooks(NULL, NULL, NULL);
		delete fRecorder;
		fRecorder = NULL;
		fExternalNode = false;
	}
}

void AudioRecorder::Stop()
{
	fRecording = false;
	fListening = false;
	if (fRecorder) {
		fRecorder->Stop(true);
		if (fExternalNode) {
			// внешний узел остаётся в Cortex вместе с соединением; хук пишет данные только при fRecording
		} else {
			fRecorder->Disconnect();
			fRecorder->SetHooks(NULL, NULL, NULL);
			delete fRecorder;
			fRecorder = NULL;
		}
	}
	if (fTimeSource) {
		fTimeSource->Release();
		fTimeSource = NULL;
	}
	if (fHaveNode) {
		BMediaRoster *roster = BMediaRoster::Roster();
		if (roster)
			roster->ReleaseNode(fInputNode);	// отпускаем микрофон для остальной системы
		fHaveNode = false;
		snooze(50000);
	}
}

void AudioRecorder::ReadLevels(float *left, float *right)
{
	if (left)	*left = fPeakL;
	if (right)	*right = fPeakR;
	fPeakL = fPeakR = 0.0f;
}

void AudioRecorder::_RecordHook(void *cookie, bigtime_t timestamp, void *data, size_t size,
								const media_format &format)
{
	AudioRecorder *self = static_cast<AudioRecorder *>(cookie);
	if (self->fRecording && self->fFirstArrival == 0) {
		self->fFirstArrival = system_time();
		self->fFirstStamp = timestamp;
		if (self->fTimeSource)
			self->fFirstStampReal = self->fTimeSource->RealTimeFor(timestamp, 0);
	}
	self->OnBuffer(data, size, format);
}

void AudioRecorder::OnBuffer(void *data, size_t size, const media_format &format)
{
	if (fListening) {
		OnListenBuffer(data, size, format);
		return;
	}
	if (!fRecording || fOutOfMemory)
		return;
	if (format.type != B_MEDIA_RAW_AUDIO)
		return;

	const media_raw_audio_format &raw = format.u.raw_audio;
	const int bps = RecConv::BytesPerSample(raw.format);
	const int srcCh = raw.channel_count;
	if (bps == 0 || srcCh < 1)
		return;

	BAutolock lock(fLock);

	if (!fFormatKnown) {
		fRate = raw.frame_rate > 0 ? raw.frame_rate : 44100.0f;
		fSrcFormat = raw.format;
		fSrcChannels = srcCh;
		fOutChannels = (srcCh == 1) ? 1 : 2;
		fSwap = (raw.byte_order != B_MEDIA_HOST_ENDIAN) && bps > 1;
		fFormatKnown = true;
	}
	// если формат внезапно изменился — игнорируем такие буферы (запись продолжается в первом формате)
	if ((int32)raw.channel_count != fSrcChannels || raw.format != fSrcFormat)
		return;

	size_t frames = size / ((size_t)bps * srcCh);
	if (frames == 0)
		return;

	float peakL = 0.0f, peakR = 0.0f;

	// временный буфер на стеке мал, поэтому конвертируем сразу в блоки
	size_t done = 0;						// обработано кадров
	const uint8 *src = (const uint8 *)data;
	const size_t frameBytes = (size_t)bps * srcCh;
	while (done < frames) {
		if (fChunkUsed >= kChunkFloats || fChunks.empty()
			|| (kChunkFloats - fChunkUsed) < (size_t)fOutChannels) {
			float *chunk = (float *)malloc(kChunkFloats * sizeof(float));
			if (chunk == NULL) {
				fOutOfMemory = true;			// остановимся на том, что уже есть
				return;
			}
			fChunks.push_back(chunk);
			fChunkUsed = 0;
		}
		size_t room = (kChunkFloats - fChunkUsed) / fOutChannels;	// кадров в блоке
		size_t n = frames - done;
		if (n > room)
			n = room;
		float pl, pr;
		RecConv::ConvertFrames(src + done * frameBytes, n, raw.format, srcCh, fSwap,
							   fOutChannels, fChunks.back() + fChunkUsed, &pl, &pr);
		if (pl > peakL) peakL = pl;
		if (pr > peakR) peakR = pr;
		{	// огибающая для показа записи в реальном времени
			const float *c = fChunks.back() + fChunkUsed;
			for (size_t k = 0; k < n; k++) {
				for (int32 q = 0; q < fOutChannels; q++) {
					float a = c[k * fOutChannels + q];
					if (a < 0) a = -a;
					if (a > fPeakAcc) fPeakAcc = a;
				}
				if (++fPeakCount >= kPeakBlock) {
					fPeaks.push_back(fPeakAcc);
					fPeakAcc = 0;
					fPeakCount = 0;
				}
			}
		}
		fChunkUsed += n * fOutChannels;
		done += n;
	}
	fFrames += (int64)frames;

	if (peakL > fPeakL) fPeakL = peakL;
	if (peakR > fPeakR) fPeakR = peakR;
}

// Прослушивание: переводим в моно и кладём в кольцо (без выделения памяти в потоке звука)
void AudioRecorder::OnListenBuffer(void *data, size_t size, const media_format &format)
{
	if (format.type != B_MEDIA_RAW_AUDIO)
		return;
	const media_raw_audio_format &raw = format.u.raw_audio;
	const int bps = RecConv::BytesPerSample(raw.format);
	const int srcCh = raw.channel_count;
	if (bps == 0 || srcCh < 1)
		return;
	BAutolock lock(fLock);
	if (fRing.empty())
		return;
	if (!fFormatKnown) {
		fRate = raw.frame_rate > 0 ? raw.frame_rate : 44100.0f;
		fSrcFormat = raw.format;
		fSrcChannels = srcCh;
		fSwap = (raw.byte_order != B_MEDIA_HOST_ENDIAN) && bps > 1;
		fFormatKnown = true;
	}
	if ((int32)raw.channel_count != fSrcChannels || raw.format != fSrcFormat)
		return;
	const size_t frameBytes = (size_t)bps * srcCh;
	const size_t frames = size / frameBytes;
	const uint8 *src = (const uint8 *)data;
	float tmp[1024];					// стерео; в кольцо кладём среднее каналов
	float peakL = 0.0f, peakR = 0.0f;
	size_t done = 0;
	while (done < frames) {
		size_t n = frames - done;
		if (n > 512)
			n = 512;
		float pl, pr;
		RecConv::ConvertFrames(src + done * frameBytes, n, raw.format, srcCh, fSwap, 2, tmp, &pl, &pr);
		if (pl > peakL) peakL = pl;
		if (pr > peakR) peakR = pr;
		for (size_t i = 0; i < n; i++)
			fRing[(size_t)((fRingWritten + (int64)i) % (int64)fRing.size())] = 0.5f * (tmp[i * 2] + tmp[i * 2 + 1]);
		fRingWritten += (int64)n;
		done += n;
	}
	if (peakL > fPeakL) fPeakL = peakL;
	if (peakR > fPeakR) fPeakR = peakR;
}

float* AudioRecorder::CopyData(size_t extraFloats) const
{
	if (fFrames <= 0)
		return NULL;
	size_t total = (size_t)fFrames * fOutChannels;
	float *mem = (float *)malloc((total + extraFloats) * sizeof(float));
	if (mem == NULL)
		return NULL;
	size_t left = total, pos = 0;
	for (size_t i = 0; i < fChunks.size() && left > 0; ++i) {
		size_t n = (i + 1 == fChunks.size()) ? fChunkUsed : kChunkFloats;
		if (n > left) n = left;
		memcpy(mem + pos, fChunks[i], n * sizeof(float));
		pos += n;
		left -= n;
	}
	memset(mem + total, 0, extraFloats * sizeof(float));
	return mem;
}
