/*
 * PrefControllers - страница настроек «Контроллеры»: джойстики/геймпады (Haiku BJoystick).
 * Пока диагностика: выбор устройства, число осей/кнопок и живые значения. Привязка осей к
 * параметрам микшера и запись автоматизации - следующий шаг.
 */
#ifndef _PREF_CONTROLLERS_H
#define _PREF_CONTROLLERS_H

#include <Application.h>
#include <InterfaceKit.h>
#include <Joystick.h>
#include <MessageRunner.h>
#include <String.h>
#include <vector>

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
	void	OpenDevice(const char *name);
	void	CloseDevice();
	void	Tick();							// опрос устройства (по таймеру)

	BJoystick		*fStick;
	BPopUpMenu		*fMenu;
	BMenuField		*fDevice;
	BStringView		*fInfo;
	BStringView		*fStatus;
	JoyLiveView		*fLive;
	BMessageRunner	*fRunner;
	int				fBadReads;				// сколько опросов подряд завершились ошибкой
};

#endif
