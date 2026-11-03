/*
 * OffsetWindow - маленькое окно: задать смещение дорожки в миллисекундах.
 */
#ifndef OFFSET_WINDOW_H
#define OFFSET_WINDOW_H

#include <Window.h>

class BTextControl;

class OffsetWindow : public BWindow {
public:
	OffsetWindow(int trackIndex, BWindow *parent);
	virtual void MessageReceived(BMessage *msg);
private:
	int			fTrack;
	BWindow		*fParent;
	BTextControl *fText;
};

#endif
