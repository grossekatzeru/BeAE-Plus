/*
 * Tracks.h - дорожки и лента времени.
 *
 * Лента времени проекта длиннее самой дальней дорожки на 10 секунд. Каждая дорожка хранит
 * только свой звук и смещение его начала на ленте (Mixer). Активная дорожка лежит в Pool
 * целиком, вместе с тишиной слева (до смещения) и справа (до конца ленты), поэтому
 * номера кадров в окне волны и есть время на ленте, а все инструменты правки работают как раньше.
 * При переключении дорожки лишняя тишина отбрасывается (в смещение), при загрузке снова добавляется.
 */
#ifndef TRACKS_H
#define TRACKS_H

#include <SupportDefs.h>

#define DEFAULT_TRACKS	4
#define TIMELINE_MARGIN	10.0		// секунд запаса после самой дальней дорожки

void	Tracks_Init();							// создать DEFAULT_TRACKS пустых дорожек (один раз при старте)
bool	Tracks_Activate(int index, double cursorTime = -1.0);	// сделать дорожку активной (курсор - на ленте, сек; <0 - как был); false - нельзя
void	Tracks_Stash();							// сохранить содержимое Pool в активную дорожку (без тишины по краям)
void	Tracks_PlaceActive(double offsetSeconds);	// Pool содержит свежий звук (файл, запись): положить на ленту
bool	Tracks_SetOffset(int index, double seconds);	// сдвинуть дорожку на ленте
double	Tracks_Offset(int index);				// положение начала звука дорожки на ленте, сек
bool	Tracks_ActiveRange(int64 &first, int64 &last);	// где в окне волны лежит звук активной дорожки (без тишины по краям)
int		Tracks_Add();							// добавить пустую дорожку, вернуть индекс или -1
void	Tracks_RemoveActive();					// удалить активную дорожку (последнюю оставшуюся удалить нельзя)
void	Tracks_SaveRange(int64 &start, int64 &end);	// диапазон кадров активной дорожки без добавленной тишины

// Раскладка дорожек по вертикали - одна и та же для панели слева и для полос с волнами.
#define MIN_LANE_HEIGHT	52
int		Tracks_Count();							// число дорожек
int		Tracks_Rank(int slot);					// номер дорожки сверху вниз (или -1)
int		Tracks_SlotAt(int rank);				// слот микшера по номеру сверху вниз (или -1)
float	Tracks_LaneHeight(float areaHeight);		// высота одной полосы: область делится поровну, но не меньше MIN_LANE_HEIGHT
void	Tracks_LaneSpan(int rank, float areaHeight, float &top, float &bottom);	// с учётом прокрутки
float	Tracks_Scroll();
void	Tracks_SetScroll(float scroll, float areaHeight);	// прокрутка по вертикали (с ограничением)

#endif
