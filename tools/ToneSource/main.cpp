// ToneSource - a test sound source for the Media Kit: a sine tone with an adjustable frequency
// (440 Hz by default). Start several copies to get several independent sources.
// Usage: ToneSource [frequency_in_Hz]

#include <Application.h>
#include <LayoutBuilder.h>
#include <MediaRoster.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ToneNode.h"

static const char* kSignature = "application/x-vnd.BeAE-ToneSource";
static const float kDefaultFrequency = 440.0f;
static const float kMinFrequency = 1.0f;
static const float kMaxFrequency = 20000.0f;

enum { MSG_FREQUENCY = 'freq' };


class ToneWindow : public BWindow {
public:
	ToneWindow(ToneNode* node, const char* name, float frequency)
		:
		BWindow(BRect(100, 100, 360, 180), name, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fNode(node)
	{
		BString text;
		text << (int)(frequency + 0.5f);
		fFrequency = new BTextControl("freq", "Frequency, Hz:", text.String(), new BMessage(MSG_FREQUENCY));
		fFrequency->SetModificationMessage(new BMessage(MSG_FREQUENCY));
		fStatus = new BStringView("status", "");
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fFrequency)
			.Add(fStatus);
		_Apply();
	}

	virtual bool QuitRequested()
	{
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == MSG_FREQUENCY)
			_Apply();
		else
			BWindow::MessageReceived(message);
	}

private:
	void _Apply()
	{
		float hz = atof(fFrequency->Text());
		if (hz < kMinFrequency || hz > kMaxFrequency) {
			fStatus->SetText("Allowed range: 1 - 20000 Hz");
			return;
		}
		fNode->SetFrequency(hz);
		BString status;
		status << "Playing " << (int)(hz + 0.5f) << " Hz (stereo, 48 kHz, float)";
		fStatus->SetText(status.String());
	}

	ToneNode*		fNode;
	BTextControl*	fFrequency;
	BStringView*	fStatus;
};


class ToneApp : public BApplication {
public:
	ToneApp(float frequency)
		: BApplication(kSignature), fFrequency(frequency), fNode(NULL)
	{
	}

	virtual void ReadyToRun()
	{
		BMediaRoster* roster = BMediaRoster::Roster();
		if (roster == NULL) {
			fprintf(stderr, "ToneSource: media server is not available\n");
			PostMessage(B_QUIT_REQUESTED);
			return;
		}

		BString name = _FreeName(roster);
		fNode = new ToneNode(name.String(), fFrequency);
		status_t err = roster->RegisterNode(fNode);
		if (err != B_OK) {
			fprintf(stderr, "ToneSource: RegisterNode failed: %s\n", strerror(err));
			fNode->Release();
			fNode = NULL;
			PostMessage(B_QUIT_REQUESTED);
			return;
		}

		(new ToneWindow(fNode, name.String(), fFrequency))->Show();
	}

	virtual bool QuitRequested()
	{
		if (fNode != NULL) {
			BMediaRoster* roster = BMediaRoster::CurrentRoster();
			if (roster != NULL) {
				roster->StopNode(fNode->Node(), 0, true);
				roster->UnregisterNode(fNode);
			}
			fNode->Release();
			fNode = NULL;
		}
		return true;
	}

private:
	// "Tone Source", "Tone Source 2", ... - the first name that no live node uses
	BString _FreeName(BMediaRoster* roster)
	{
		live_node_info infos[64];
		int32 count = 64;
		roster->GetLiveNodes(infos, &count);
		for (int n = 1; n < 100; n++) {
			BString name("Tone Source");
			if (n > 1)
				name << " " << n;
			bool used = false;
			for (int32 i = 0; i < count && !used; i++)
				used = (name == infos[i].name);
			if (!used)
				return name;
		}
		return BString("Tone Source");
	}

	float		fFrequency;
	ToneNode*	fNode;
};


int
main(int argc, char** argv)
{
	float frequency = kDefaultFrequency;
	if (argc > 1) {
		float hz = atof(argv[1]);
		if (hz >= kMinFrequency && hz <= kMaxFrequency)
			frequency = hz;
	}
	ToneApp app(frequency);
	app.Run();
	return 0;
}
