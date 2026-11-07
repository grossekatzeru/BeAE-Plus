#include <Slider.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>
#include <string.h>

#include "ParamSlider.h"

#define MSG_PS_SLIDER	'psSl'
#define MSG_PS_TEXT		'psTx'
#define TEXT_W			76

ParamSlider::ParamSlider(const char *label, BMessage *message, int32 minPos, int32 maxPos)
	: BControl(BRect(0, 0, 259, 43), NULL, "", message, B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW | B_FRAME_EVENTS)
{
	fLabel = new BStringView(BRect(0, 0, 100, 15), NULL, label);
	AddChild(fLabel);
	fSlider = new BSlider(BRect(0, 18, 100, 43), NULL, NULL, new BMessage(MSG_PS_SLIDER), minPos, maxPos);
	fSlider->SetModificationMessage(new BMessage(MSG_PS_SLIDER));
	AddChild(fSlider);
	fText = new BTextControl(BRect(0, 18, 100, 40), NULL, NULL, "", new BMessage(MSG_PS_TEXT));
	fText->SetDivider(0);
	AddChild(fText);
	Place();
	SetExplicitMinSize(BSize(200, 44));
	SetExplicitPreferredSize(BSize(260, 44));
}

ParamSlider::~ParamSlider()
{
}

void ParamSlider::Place()
{
	const BRect b = Bounds();
	fLabel->MoveTo(0, 0);
	fLabel->ResizeTo(b.Width(), 15);
	const float tw = TEXT_W;
	fSlider->MoveTo(0, 18);
	fSlider->ResizeTo(b.Width() - tw - 6, b.Height() - 18);
	fText->MoveTo(b.Width() - tw, 18);
	fText->ResizeTo(tw, fText->Bounds().Height());
}

void ParamSlider::AttachedToWindow()
{
	BControl::AttachedToWindow();
	fSlider->SetTarget(this);
	fText->SetTarget(this);
	RefreshText();
}

void ParamSlider::FrameResized(float w, float h)
{
	BControl::FrameResized(w, h);
	Place();
}

void ParamSlider::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);
	fSlider->SetEnabled(enabled);
	fText->SetEnabled(enabled);
}

int32 ParamSlider::Pos() const
{
	return fSlider->Value();
}

void ParamSlider::SetPosLimits(int32 minPos, int32 maxPos)
{
	fSlider->SetLimits(minPos, maxPos);
}

void ParamSlider::SetPos(int32 pos)
{
	fSlider->SetValue(pos);
}

void ParamSlider::RefreshText()
{
	BString s = FormatText();
	if (strcmp(fText->Text(), s.String()) != 0)
		fText->SetText(s.String());
}

void ParamSlider::MessageReceived(BMessage *msg)
{
	switch (msg->what){
	case MSG_PS_SLIDER:
		SliderMoved();
		RefreshText();
		Invoke();
		break;
	case MSG_PS_TEXT:
		ApplyText(fText->Text());		// не число - просто вернём прежнее значение
		RefreshText();
		Invoke();
		break;
	default:
		BControl::MessageReceived(msg);
	}
}
