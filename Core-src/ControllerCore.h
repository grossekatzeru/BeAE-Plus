/*
 * ControllerCore - ядро управления джойстиком/геймпадом (без Haiku API, проверяется тестами на Linux).
 *
 * Привязка = источник (кнопка или направление оси) -> действие. Источники универсальные: цифровая крестовина
 * (оси только с крайними значениями) и аналоговые стики (плавные значения) - один и тот же код: у оси
 * «уровень» 0..1 растёт от мёртвой зоны до края, и скорость изменения громкости/панорамы пропорциональна уровню.
 * Значения меняются со скоростью (дБ/с, панорама/с): держите - растёт, отпустили - остановилось.
 */
#ifndef CONTROLLER_CORE_H
#define CONTROLLER_CORE_H

#include <stdint.h>
#include <string>
#include <vector>

namespace Ctrl {

enum Action {
	A_VOL_UP = 0,		// громкость выше (скорость)
	A_VOL_DOWN,			// громкость ниже
	A_PAN_LEFT,			// панорама влево
	A_PAN_RIGHT,		// панорама вправо
	A_PAN_CENTER,		// панорама в центр (по нажатию)
	A_VOL_RESET,		// громкость на 0 дБ (по нажатию)
	A_TRACK_PREV,		// предыдущая дорожка (по нажатию)
	A_TRACK_NEXT,		// следующая дорожка
	A_MASTER_HOLD,		// пока удерживается: громкость относится к мастеру, а не к дорожке
	A_PLAY_STOP,		// воспроизведение / стоп (по нажатию)
	A_WRITE_TOGGLE,		// включить/выключить запись автоматизации (по нажатию)
	A_COUNT
};

enum SrcType { S_NONE = 0, S_AXIS = 1, S_BUTTON = 2 };

struct Binding {
	int	type;			// S_NONE, S_AXIS, S_BUTTON
	int	index;			// номер оси / кнопки с нуля
	int	sign;			// ось: +1 или -1 - в какую сторону (у кнопки не используется)
	Binding() : type(S_NONE), index(0), sign(1) {}
	Binding(int t, int i, int s = 1) : type(t), index(i), sign(s) {}
	bool operator==(const Binding &o) const
	{
		return type == o.type && (type == S_NONE || (index == o.index && (type != S_AXIS || sign == o.sign)));
	}
	std::string	ToString() const;			// "A1-", "B3", "-"
	bool		FromString(const char *s);
};

struct Map {
	Binding	b[A_COUNT];
	float	volRate;		// дБ в секунду при полном уровне
	float	panRate;		// доля полного диапазона панорамы (-1..+1 = 2.0) в секунду
	float	deadZone;		// мёртвая зона оси, доля 0..0.9

	Map() { SetDefaults(); }
	void	SetDefaults();
	std::string	ToString() const;			// только привязки, через ';'
	bool		FromString(const char *s);	// ложь, если разбор не удался (карта при этом не меняется); недостающие в конце привязки (старый формат) остаются как были
	static const char *ActionKey(int a);	// имя действия для языковых файлов, например "CTRL_A_VOL_UP"
};

struct Input {
	std::vector<int16_t>	axes;
	uint32_t				buttons;
	Input() : buttons(0) {}
};

struct Output {
	double	dVolDb;			// сколько прибавить к громкости (дБ) за этот отрезок
	double	dPan;			// сколько прибавить к панораме (-1..+1)
	bool	master;			// относить громкость к мастеру
	bool	trackPrev, trackNext, playStop, panCenter, volReset, writeToggle;
	Output() { Clear(); }
	void	Clear() { dVolDb = dPan = 0; master = trackPrev = trackNext = playStop = panCenter = volReset = false; }
};

// Уровень источника 0..1 (ось: от мёртвой зоны до края; кнопка: 0 или 1)
float Level(const Binding &b, const Input &in, float deadZone);

class Engine {
public:
	Engine() { Reset(); }
	void	Reset();
	// dt - секунд с предыдущего вызова. Действия «по нажатию» срабатывают один раз при переходе через 0.5.
	void	Tick(const Map &map, const Input &in, double dt, Output &out);

private:
	bool	fPrev[A_COUNT];
};

}	// namespace Ctrl

#endif
