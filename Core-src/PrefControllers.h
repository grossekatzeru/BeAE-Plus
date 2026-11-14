/*
 * PrefControllers - страница настроек «Контроллеры»: выбор джойстика/геймпада, живые значения,
 * таблица привязок (кнопка или направление оси -> действие) с переназначением и скорости.
 * Само устройство опрашивает JoyService (общий для приложения).
 */
#ifndef _PREF_CONTROLLERS_H
#define _PREF_CONTROLLERS_H

#include <Application.h>
#include <InterfaceKit.h>
#include <Joystick.h>
#include <MessageRunner.h>
#include <String.h>
#include <Slider.h>
#include <vector>

#include "JoyService.h"

// Живое состояние: полосы осей, «шляпки», кнопки
class JoyLiveView : public BView {
public:
	JoyLiveView();
	virtual void Draw(BRect);
	void	SetState(const std::vector<int16> &axes, const std::vector<uint8> &hats, int buttons, uint32 pressed);

private:
	std::vector<int16>	fAxes;
	std::vector<uint8>	fHats;
	int					fButtons;
	uint32				fPressed;
};

class PrefControllers : public BView {
public:
	PrefControllers();
	~PrefControllers();
	virtual void AttachedToWindow();
	virtual void DetachedFromWindow();
	virtual void MessageReceived(BMessage*);

private:
	void	Rescan();						// заново найти устройства
	void	Refresh();						// подписи: сведения об устройстве, кнопки привязок
	void	Tick();							// живые значения (по таймеру)

	BPopUpMenu		*fMenu;
	BMenuField		*fDevice;
	BStringView		*fInfo;
	BStringView		*fStatus;
	JoyLiveView		*fLive;
	BButton			*fBind[Ctrl::A_COUNT];
	BSlider			*fVolRate, *fPanRate, *fDead;
	BMessageRunner	*fRunner;
};

#endif
