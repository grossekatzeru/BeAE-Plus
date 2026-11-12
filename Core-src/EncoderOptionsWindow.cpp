#include "EncoderOptionsWindow.h"

#include <Box.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Spinner.h>
#include <StringView.h>
#include <TextControl.h>

#include "Globals.h"
#include "Preferences.h"

#define MSG_ENC_OK		'eoOK'
#define MSG_ENC_MODE	'eoMd'

using namespace ExtEncoder;

// Подписи режимов зависят от кодека
static const char *ModeLabel(Id id, int mode)
{
	if (id == OGG) return Language.get(mode == VBR ? "ENC_MODE_QUALITY" : "ENC_MODE_BITRATE");
	return Language.get(mode == VBR ? "ENC_MODE_VBR" : (mode == CBR ? "ENC_MODE_CBR" : "ENC_MODE_ABR"));
}

EncoderOptionsWindow::EncoderOptionsWindow(Id id, BWindow *parent)
	: BWindow(BRect(0, 0, 10, 10), "", B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fId(id), fMode(NULL), fStereo(NULL), fQuality(NULL), fBitrate(NULL), fLevel(NULL),
	fArtist(NULL), fAlbum(NULL), fYear(NULL), fComment(NULL)
{
	const Profile &p = GetProfile(id);
	const Settings s = FromString(id, Prefs.encSettings[id].String());
	BString title(Language.get("SAVE_OPTIONS_TITLE"));
	title << " " << p.title;
	SetTitle(title.String());
	if (parent) AddToSubset(parent);

	BBox *box = new BBox("quality");
	box->SetLabel(p.title);
	BLayoutBuilder::Group<> inner(box, B_VERTICAL, B_USE_SMALL_SPACING);
	inner.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	if (p.hasMode) {
		BPopUpMenu *m = new BPopUpMenu("");
		const int last = (id == MP3) ? ABR : CBR;
		for (int i = VBR; i <= last; i++) {
			BMenuItem *it = new BMenuItem(ModeLabel(id, i), new BMessage(MSG_ENC_MODE));
			m->AddItem(it);
			if (i == s.mode) it->SetMarked(true);
		}
		fMode = new BMenuField("mode", Language.get("ENC_MODE"), m);
		inner.Add(fMode);
	}
	if (p.hasQuality) {
		fQuality = new BSpinner("q", Language.get(id == MP3 ? "ENC_QUALITY_MP3" : "ENC_QUALITY_OGG"), NULL);
		fQuality->SetRange(0, id == MP3 ? 9 : 10);
		fQuality->SetValue(s.quality);
		inner.Add(fQuality);
	}
	if (p.hasBitrate) {
		fBitrate = new BSpinner("br", Language.get("ENC_BITRATE"), NULL);
		fBitrate->SetRange(id == OPUS ? 6 : 32, id == OPUS ? 510 : 320);
		fBitrate->SetValue(s.bitrate);
		inner.Add(fBitrate);
	}
	if (p.hasStereo) {
		BPopUpMenu *m = new BPopUpMenu("");
		const char *keys[3] = { "ENC_STEREO_JOINT", "ENC_STEREO_STEREO", "ENC_STEREO_MONO" };
		for (int i = 0; i < 3; i++) {
			BMenuItem *it = new BMenuItem(Language.get(keys[i]), NULL);
			m->AddItem(it);
			if (i == s.stereo) it->SetMarked(true);
		}
		fStereo = new BMenuField("stereo", Language.get("ENC_STEREO"), m);
		inner.Add(fStereo);
	}
	if (p.hasLevel) {
		fLevel = new BSpinner("lv", Language.get("ENC_LEVEL"), NULL);
		fLevel->SetRange(0, 8);
		fLevel->SetValue(s.level);
		inner.Add(fLevel);
	}

	BBox *tags = new BBox("tags");
	tags->SetLabel(Language.get("ENC_TAGS"));
	fArtist = new BTextControl("artist", Language.get("ENC_TAG_ARTIST"), s.tags.artist.c_str(), NULL);
	fAlbum = new BTextControl("album", Language.get("ENC_TAG_ALBUM"), s.tags.album.c_str(), NULL);
	fYear = new BTextControl("year", Language.get("ENC_TAG_YEAR"), s.tags.year.c_str(), NULL);
	fComment = new BTextControl("comment", Language.get("ENC_TAG_COMMENT"), s.tags.comment.c_str(), NULL);
	BStringView *note = new BStringView(NULL, Language.get("ENC_TAG_NOTE"));
	note->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	BLayoutBuilder::Group<>(tags, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fArtist).Add(fAlbum).Add(fYear).Add(fComment).Add(note);

	BButton *ok = new BButton("ok", Language.get("OK"), new BMessage(MSG_ENC_OK));
	ok->MakeDefault(true);
	BButton *cancel = new BButton("cancel", Language.get("CANCEL"), new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(box)
		.Add(tags)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();

	UpdateEnabled();
	if (parent) CenterIn(parent->Frame()); else CenterOnScreen();
	Show();
}

// VBR: нужно качество, CBR/ABR: битрейт
void EncoderOptionsWindow::UpdateEnabled()
{
	const Profile &p = GetProfile(fId);
	int mode = VBR;
	if (fMode) {
		BMenuItem *m = fMode->Menu()->FindMarked();
		if (m) mode = fMode->Menu()->IndexOf(m);
	}
	if (fQuality && fBitrate && p.hasMode) {
		fQuality->SetEnabled(mode == VBR);
		fBitrate->SetEnabled(mode != VBR);
	}
	if (fId == OGG && fQuality && fBitrate) {			// у oggenc режим только выбирает одно из двух
		fQuality->SetEnabled(mode == VBR);
		fBitrate->SetEnabled(mode != VBR);
	}
}

void EncoderOptionsWindow::Apply()
{
	Settings s = Defaults(fId);
	if (fMode) { BMenuItem *m = fMode->Menu()->FindMarked(); if (m) s.mode = fMode->Menu()->IndexOf(m); }
	if (fQuality) s.quality = fQuality->Value();
	if (fBitrate) s.bitrate = fBitrate->Value();
	if (fStereo) { BMenuItem *m = fStereo->Menu()->FindMarked(); if (m) s.stereo = fStereo->Menu()->IndexOf(m); }
	if (fLevel) s.level = fLevel->Value();
	s.tags.artist = fArtist->Text();
	s.tags.album = fAlbum->Text();
	s.tags.year = fYear->Text();
	s.tags.comment = fComment->Text();
	Prefs.encSettings[fId].SetTo(ToString(fId, s).c_str());
}

void EncoderOptionsWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
	case MSG_ENC_MODE:
		UpdateEnabled();
		break;
	case MSG_ENC_OK:
		Apply();
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
