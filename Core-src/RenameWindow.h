/*
 * RenameWindow - маленькое окно: переименовать дорожку (двойной щелчок по её названию на полоске).
 */
#ifndef RENAME_WINDOW_H
#define RENAME_WINDOW_H

#include <Window.h>

class BTextControl;

class RenameWindow : public BWindow {
public:
	RenameWindow(int trackIndex, BWindow *parent);
	virtual void MessageReceived(BMessage *msg);
private:
	int			fTrack;
	BTextControl *fText;
};

#endif
