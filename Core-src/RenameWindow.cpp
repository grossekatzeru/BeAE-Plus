#include <StorageKit.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <TextControl.h>
#include <Message.h>
#include <string.h>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"
#include "RenameWindow.h"

#define MSG_RENAME_OK		'rnok'
#define MSG_RENAME_CANCEL	'rncn'

RenameWindow::RenameWindow(int trackIndex, BWindow *parent)
	: BWindow(BRect(0, 0, 299, 89), Language.get("TRACK_RENAME"), B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTrack(trackIndex)
{
	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);

	MixTrack *t = Mix.Track(trackIndex);
	fText = new BTextControl(BRect(12, 12, 287, 32), "name", Language.get("TRACK_NAME"), t ? t->name : "", NULL);
	fText->SetDivider(70);
	fText->TextView()->SetMaxBytes(63);
	bg->AddChild(fText);

	BButton *ok = new BButton(BRect(210, 52, 287, 76), "ok", Language.get("OK"), new BMessage(MSG_RENAME_OK));
	ok->MakeDefault(true);
	bg->AddChild(ok);
	bg->AddChild(new BButton(BRect(128, 52, 204, 76), "cancel", Language.get("CANCEL"), new BMessage(MSG_RENAME_CANCEL)));

	if (parent)
		MoveTo(parent->Frame().left + 60, parent->Frame().top + 120);
	fText->MakeFocus(true);
	fText->TextView()->SelectAll();
}

void RenameWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case MSG_RENAME_OK:
		if (fText->Text()[0] != 0 && Mix.Track(fTrack) && strcmp(fText->Text(), Mix.Track(fTrack)->name) != 0){
			Tracks_SetName(fTrack, fText->Text());
			Pool.changed = true;
			Pool.RedrawWindow();
		}
		PostMessage(B_QUIT_REQUESTED);
		break;
	case MSG_RENAME_CANCEL:
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
