/*
 * FxWindow - эффекты дорожки: список плагинов по порядку, вкл/выкл, порядок, параметры.
 */
#ifndef FX_WINDOW_H
#define FX_WINDOW_H

#include <Window.h>
#include <vector>

class BListView;
class BCheckBox;
class BButton;
class BView;
class BScrollView;
class LadspaSlider;
class VstSlider;

class FxWindow : public BWindow {
public:
	static void Open(int track, BWindow *near);		// одно окно на дорожку
	virtual void MessageReceived(BMessage *msg);
	virtual bool QuitRequested();

private:
	FxWindow(int track, BWindow *near);
	void	RefreshList(int select);
	void	BuildParams();
	void	ShowAddMenu();

	int				fTrack;
	BListView		*fList;
	BCheckBox		*fEnabled;
	BButton			*fAdd, *fRemove, *fUp, *fDown;
	BScrollView		*fScroll;
	BView			*fParams;
	std::vector<LadspaSlider*>	fSliders;
	std::vector<class BCheckBox*>	fChecks;
	std::vector<VstSlider*>	fVstSliders;
	std::vector<int>	fPorts;
};

#endif
