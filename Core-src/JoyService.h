/*
 * JoyService - общий опрос джойстика/геймпада (одно открытое устройство на всё приложение)
 * и применение действий к микшеру. Опрашивается из главного окна по таймеру.
 */
#ifndef _JOY_SERVICE_H
#define _JOY_SERVICE_H

#include <Joystick.h>
#include <Messenger.h>
#include <String.h>
#include <vector>

#include "ControllerCore.h"

#define JOY_TICK		'JoyT'		// сообщение главному окну по таймеру
#define JOY_LEARNED		'JoyL'		// привязка записана: "action" int32 (страница настроек)

class JoyService {
public:
	JoyService();
	~JoyService();

	void	SetTarget(BMessenger target) { fTarget = target; }	// куда слать команды транспорта
	void	Tick();								// вызывается главным окном ~30 раз в секунду

	// устройство
	std::vector<BString>	Devices();
	bool	Open(const char *name);				// запоминает имя в настройках
	void	Close();
	bool	IsOpen() const { return fStick != NULL; }
	const BString &Name() const { return fName; }
	int		CountAxes() const { return fAxes; }
	int		CountHats() const { return fHats; }
	int		CountButtons() const { return fButtons; }
	bool	HasData() const { return fHasData; }
	const Ctrl::Input &Last() const { return fLast; }
	const std::vector<uint8> &LastHats() const { return fLastHats; }

	// привязки
	Ctrl::Map	&Map() { return fMap; }
	void	LoadFromPrefs();
	void	SaveToPrefs();
	void	BeginLearn(int action, BMessenger notify);	// ждать нажатия кнопки / движения оси
	void	CancelLearn() { fLearn = -1; }
	int		LearnAction() const { return fLearn; }
	static BString	Describe(const Ctrl::Binding &b);	// "Кнопка 3", "Ось 2 -", "-"

private:
	void	Apply(const Ctrl::Output &o);
	void	DoLearn();

	BJoystick	*fStick;
	BString		fName;
	int			fAxes, fHats, fButtons;
	bool		fHasData;
	int			fBad;
	bigtime_t	fLastTick, fLastTry;
	Ctrl::Input	fLast;
	std::vector<uint8>	fLastHats;
	Ctrl::Map	fMap;
	Ctrl::Engine	fEngine;
	BMessenger	fTarget;
	int			fLearn;
	bool		fLearnBase;
	Ctrl::Input	fLearnStart;
	BMessenger	fLearnNotify;
};

extern JoyService Joy;

#endif
