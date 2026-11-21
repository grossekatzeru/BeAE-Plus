// BeAETap - a Media Kit node "BeAE Tap" that passes the sound on and makes a copy for another node.
// Put it into the chain in Cortex:   application -> BeAE Tap -> Audio Mixer
//                                                        \-> (copy) -> BeAE-Plus
// Start several copies to tap several sources: "BeAE Tap", "BeAE Tap 2", ...

#include <Application.h>
#include <LayoutBuilder.h>
#include <MediaRoster.h>
#include <MessageRunner.h>
#include <String.h>
#include <StringView.h>
#include <TimeSource.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>

#include "TapNode.h"

static const char* kSignature = "application/x-vnd.BeAE-Tap";
enum { MSG_TICK = 'tick' };


class TapWindow : public BWindow {
public:
	TapWindow(TapNode* node, const char* name)
		:
		BWindow(BRect(100, 100, 420, 200), name, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fNode(node),
		fRunner(NULL)
	{
		fInput = new BStringView("input", "");
		fPass = new BStringView("pass", "");
		fCopy = new BStringView("copy", "");
		fStats = new BStringView("stats", "");
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fInput)
			.Add(fPass)
			.Add(fCopy)
			.Add(fStats);
		_Update();
		fRunner = new BMessageRunner(BMessenger(this), new BMessage(MSG_TICK), 500000);
	}

	~TapWindow() { delete fRunner; }

	virtual bool QuitRequested()
	{
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == MSG_TICK)
			_Update();
		else
			BWindow::MessageReceived(message);
	}

private:
	void _Update()
	{
		fInput->SetText(fNode->InputConnected() ? "Input: connected" : "Input: not connected");
		fPass->SetText(fNode->OutputConnected(0) ? "Output Pass: connected" : "Output Pass: not connected");
		fCopy->SetText(fNode->OutputConnected(1) ? "Output Copy: connected" : "Output Copy: not connected");
		BString stats;
		if (fNode->HasFormat()) {
			media_raw_audio_format f = fNode->Format();
			stats << (int)f.frame_rate << " Hz, " << (int)f.channel_count << " ch, ";
		}
		stats << "buffers: " << (int)fNode->BuffersPassed() << ", lost: " << (int)fNode->BuffersLost();
		fStats->SetText(stats.String());
	}

	TapNode*		fNode;
	BMessageRunner*	fRunner;
	BStringView*	fInput;
	BStringView*	fPass;
	BStringView*	fCopy;
	BStringView*	fStats;
};


class TapApp : public BApplication {
public:
	TapApp() : BApplication(kSignature), fNode(NULL) {}

	virtual void ReadyToRun()
	{
		BMediaRoster* roster = BMediaRoster::Roster();
		if (roster == NULL) {
			fprintf(stderr, "BeAETap: media server is not available\n");
			PostMessage(B_QUIT_REQUESTED);
			return;
		}

		BString name = _FreeName(roster);
		fNode = new TapNode(name.String());
		status_t err = roster->RegisterNode(fNode);
		if (err != B_OK) {
			fprintf(stderr, "BeAETap: RegisterNode failed: %s\n", strerror(err));
			fNode->Release();
			fNode = NULL;
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		// nobody else starts the node in the middle of a chain: it runs at once
		roster->StartNode(fNode->Node(), fNode->TimeSource()->Now());

		(new TapWindow(fNode, name.String()))->Show();
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
	BString _FreeName(BMediaRoster* roster)
	{
		live_node_info infos[64];
		int32 count = 64;
		roster->GetLiveNodes(infos, &count);
		for (int n = 1; n < 100; n++) {
			BString name("BeAE Tap");
			if (n > 1)
				name << " " << n;
			bool used = false;
			for (int32 i = 0; i < count && !used; i++)
				used = (name == infos[i].name);
			if (!used)
				return name;
		}
		return BString("BeAE Tap");
	}

	TapNode*	fNode;
};


int
main()
{
	TapApp app;
	app.Run();
	return 0;
}
