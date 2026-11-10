/*
 * BeFxSidechainWindow - собственное окно плагина «Сайдчейн-приседание» (группа BeFX):
 * выбор дорожки-ключа по названию, диапазон ключа, параметры приседания, график огибающей с порогом
 * и индикатор сжатия, состояние огибающей (готова / строится / устарела) и кнопка «Обновить».
 * Параметры хранятся в обычных значениях слота FxChain (сохранение, копирование и экспорт работают как у других плагинов).
 */
#ifndef BEFX_SIDECHAIN_WINDOW_H
#define BEFX_SIDECHAIN_WINDOW_H

#include <Window.h>
#include <string>
#include <vector>

class BMenu;
class BMenuField;
class BStringView;
class BButton;
class LadspaSlider;
class SidechainGraph;
namespace Ladspa { class Runner; }

class BeFxSidechainWindow : public BWindow {
public:
	// Открывает окно слота (одно на слот; если уже открыто - выводит вперёд)
	static void Open(int track, int slot, BWindow *near);

	virtual void MessageReceived(BMessage *msg);
	virtual bool QuitRequested();
	virtual void WindowActivated(bool active);

	// Данные для графика
	struct State { int lane, range; float cutoff, threshold, depth, reduction; };
	bool GetState(State &st) const;

private:
	BeFxSidechainWindow(int track, int slot, BWindow *near);
	~BeFxSidechainWindow();
	int		Locate() const;							// номер слота в цепочке или -1
	void	SetPort(int port, float v);
	float	GetPort(int port) const;
	void	FillKeyMenu();
	void	Tick();
	void	RequestKey(bool force);

	int				fTrack;
	Ladspa::Runner	*fRunner;						// слот узнаём по его runner: номер слота в цепочке меняется
	BMenuField		*fKeyField, *fRangeField, *fCurveField;
	BMenu			*fKeyMenu;
	BStringView		*fStatus, *fReduction;
	BButton			*fRefresh;
	SidechainGraph	*fGraph;
	std::vector<LadspaSlider*>	fSliders;
	std::vector<int>			fPorts;
	std::string		fKeyNames;						// какие дорожки в списке (чтобы перестроить при изменении)
	int				fRevision;
	bool			fNeedCheck;
	bigtime_t		fCutoffChanged;					// когда последний раз двигали частоту среза (0 - нет); пересборка с задержкой
};

#endif
