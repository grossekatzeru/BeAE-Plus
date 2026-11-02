#include <StorageKit.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <TextControl.h>
#include <Message.h>
#include <stdio.h>
#include <stdlib.h>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "OffsetWindow.h"

#define MSG_OFFSET_OK		'ofok'
#define MSG_OFFSET_CANCEL	'ofcn'

OffsetWindow::OffsetWindow(int trackIndex, BWindow *parent)
	: BWindow(BRect(0, 0, 259, 89), "Offset", B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTrack(trackIndex)
{
	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);

	MixTrack *t = Mix.Track(trackIndex);
	char title[64], value[32];
	snprintf(title, sizeof(title), "%s - offset (ms)", t ? t->name : "Track");
	SetTitle(title);
	snprintf(value, sizeof(value), "%.0f", t ? t->offset * 1000.0 : 0.0);

	fText = new BTextControl(BRect(12, 12, 247, 32), "ms", "Offset (ms):", value, NULL);
	fText->SetDivider(90);
	bg->AddChild(fText);

	BButton *ok = new BButton(BRect(170, 52, 247, 76), "ok", Language.get("OK"), new BMessage(MSG_OFFSET_OK));
	ok->MakeDefault(true);
	bg->AddChild(ok);
	bg->AddChild(new BButton(BRect(88, 52, 164, 76), "cancel", Language.get("CANCEL"), new BMessage(MSG_OFFSET_CANCEL)));

	if (parent){
		MoveTo(parent->Frame().left + 60, parent->Frame().top + 120);
	}
	fText->MakeFocus(true);
}

void OffsetWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case MSG_OFFSET_OK:
		Mix.SetOffset(fTrack, atof(fText->Text()) / 1000.0);
		PostMessage(B_QUIT_REQUESTED);
		break;
	case MSG_OFFSET_CANCEL:
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
