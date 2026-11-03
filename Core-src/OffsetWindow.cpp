#include <StorageKit.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <TextControl.h>
#include <Message.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <String.h>

#include "CommonPool.h"
#include "Globals.h"
#include "Mixer.h"
#include "Tracks.h"
#include "OffsetWindow.h"
#include "FxWindow.h"

#define MSG_OFFSET_OK		'ofok'
#define MSG_OFFSET_CANCEL	'ofcn'
#define MSG_OFFSET_FX		'offx'

OffsetWindow::OffsetWindow(int trackIndex, BWindow *parent)
	: BWindow(BRect(0, 0, 259, 119), "Track", B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	  fTrack(trackIndex)
{
	BView *bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
	bg->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(bg);

	MixTrack *t = Mix.Track(trackIndex);
	char title[64], value[32];
	snprintf(title, sizeof(title), "%s", t ? t->name : "Track");
	SetTitle(title);
	snprintf(value, sizeof(value), "%.0f", t ? Tracks_Offset(trackIndex) * 1000.0 : 0.0);

	fName = new BTextControl(BRect(12, 12, 247, 32), "name", Language.get("TRACK_NAME"), t ? t->name : "", NULL);
	fName->SetDivider(90);
	fName->TextView()->SetMaxBytes(63);
	bg->AddChild(fName);

	fText = new BTextControl(BRect(12, 42, 247, 62), "ms", Language.get("TRACK_OFFSET"), value, NULL);
	fText->SetDivider(90);
	bg->AddChild(fText);

	BButton *ok = new BButton(BRect(170, 82, 247, 106), "ok", Language.get("OK"), new BMessage(MSG_OFFSET_OK));
	ok->MakeDefault(true);
	bg->AddChild(ok);
	bg->AddChild(new BButton(BRect(12, 82, 82, 106), "fx", Language.get("FX_BUTTON"), new BMessage(MSG_OFFSET_FX)));
	bg->AddChild(new BButton(BRect(88, 82, 164, 106), "cancel", Language.get("CANCEL"), new BMessage(MSG_OFFSET_CANCEL)));

	if (parent){
		MoveTo(parent->Frame().left + 60, parent->Frame().top + 120);
	}
	fName->MakeFocus(true);
	fName->TextView()->SelectAll();
}

void OffsetWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case MSG_OFFSET_OK:
	case MSG_OFFSET_FX:
		{
			BString n(fName->Text());
			n.Trim();
			if (n.Length() > 0) Tracks_SetName(fTrack, n.String());
		}
		if (fabs(atof(fText->Text()) / 1000.0 - Tracks_Offset(fTrack)) > 0.0000005)
			Tracks_SetOffset(fTrack, atof(fText->Text()) / 1000.0);
		if (msg->what == MSG_OFFSET_FX)
			FxWindow::Open(fTrack, fParent);		// имя и смещение применены, дальше - эффекты
		PostMessage(B_QUIT_REQUESTED);
		break;
	case MSG_OFFSET_CANCEL:
		PostMessage(B_QUIT_REQUESTED);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
