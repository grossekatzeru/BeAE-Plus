/*
 * TunerWindow - «Анализ → Тюнер». Гитарный тюнер: берёт звук с аудиовхода (как запись, но без сохранения),
 * определяет ноту и показывает отклонение в центах зелёной линейкой на чёрном фоне.
 * Работает сам по себе, без проекта; не одновременно с записью (запись останавливает тюнер).
 * Пресеты: все ноты, гитара 6 и 7 струн, бас 4 и 5 струн; эталон A4 415..466 Гц; ноты латиницей или слогами.
 * Ядро определения высоты - TunerCore (проверяется тестами на Linux).
 */
#ifndef TUNER_WINDOW_H
#define TUNER_WINDOW_H

#include <Window.h>
#include <View.h>
#include <String.h>

#include "TunerCore.h"

class BButton;
class BSpinner;
class BMenuField;

class TunerView : public BView {
public:
	TunerView();
	virtual void	Draw(BRect updateRect);
	virtual void	Pulse();
	virtual void	AttachedToWindow();

	void	SetPreset(int index);
	void	SetA4(float hz);
	void	SetNotation(int notation);
	bool	Start();				// false - нельзя (запись или нет входа); причина в статусе
	void	Stop();
	bool	Running() const { return fRunning; }

private:
	void	SetStatus(const char *languageKey);
	void	NotifyState();

	Tuner::Core		fCore;
	float			fCoreRate;
	bool			fRunning;
	int				fPreset;
	float			fA4;
	int				fNotation;
	Tuner::Reading	fReading;
	float			fShown;			// плавно движущийся маркер, центы
	float			fLevelShown;
	BString			fStatus;		// ключ языка или пусто
};

class TunerWindow : public BWindow {
public:
	// Открыть окно или вывести уже открытое на передний план
	static void	Open(BPoint center);

	virtual bool	QuitRequested();
	virtual void	MessageReceived(BMessage *message);

private:
	TunerWindow(BPoint center);
	~TunerWindow();
	void	UpdateButton();

	TunerView	*fView;
	BButton		*fStartButton;
	BSpinner	*fA4Spinner;
};

#endif
