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
#include <vector>
#include <String.h>
#include "Mixer.h"

#define DEFAULT_TRACKS	4
#define TIMELINE_MARGIN	10.0		// секунд запаса после самой дальней дорожки

void	Tracks_Init();							// создать DEFAULT_TRACKS пустых дорожек (один раз при старте)
bool	Tracks_Activate(int lane, double cursorTime = -1.0, int clip = -1);	// сделать активным клип дорожки (clip<0 - тот, что под курсором); курсор - на ленте, сек; false - нельзя
void	Tracks_Stash();							// сохранить содержимое Pool в активную дорожку (без тишины по краям)
void	Tracks_PlaceActive(double offsetSeconds);	// Pool содержит свежий звук (файл, запись): положить на ленту
bool	Tracks_SetOffset(int index, double seconds);	// сдвинуть клип: активный (если дорожка активна) или первый клип дорожки
bool	Tracks_MoveClip(int lane, int clip, double seconds);	// сдвинуть клип из микшера (не активный)
bool	Tracks_MoveClipToLane(int fromLane, int clip, int toLane, double seconds);	// перенести клип (не активный) на другую дорожку в момент seconds
bool	Tracks_MoveActiveClipToLane(int toLane, double seconds);	// перенести активный клип на другую дорожку; она становится активной
// Положить звук (из файла) клипом на дорожку lane (-1: на первую пустую, иначе на новую) в момент seconds.
// name - имя файла: берётся как имя дорожки, если дорожка была пустой и называлась по умолчанию. Возвращает слот дорожки или -1.
int		Tracks_ImportClip(int lane, const SourceRef &src, double seconds, const char *name = NULL);
double	Tracks_Offset(int index);				// положение начала клипа на ленте, сек
bool	Tracks_ActiveRange(int64 &first, int64 &last);	// где в окне волны лежит звук активной дорожки (без тишины по краям)
int		Tracks_Add();							// добавить пустую дорожку, вернуть индекс или -1
void	Tracks_RemoveActive();					// удалить активную дорожку (последнюю оставшуюся удалить нельзя)
void	Tracks_SaveRange(int64 &start, int64 &end);	// диапазон кадров активной дорожки без добавленной тишины

bool	Tracks_SetName(int index, const char *name);	// переименовать (UTF-8, обрезается по границе символа)
// Для сохранения проекта: звук дорожки без тишины по краям, ничего не меняя. false - дорожка пуста.
std::vector<MixClip> Tracks_LaneClips(int lane);	// все клипы дорожки, включая активный (ничего не меняет)
bool	Tracks_ActiveClipSpan(double &start, double &end);	// где на ленте лежит звук активного клипа, сек

// Клипы: разрезать по курсору, копировать в буфер, вставить count копий подряд с позиции timePos, удалить
bool	Tracks_SplitAtCursor();
bool	Tracks_CopyClip();
bool	Tracks_HasClipboard();
bool	Tracks_PasteClip(double timePos, int count);
bool	Tracks_DeleteClip();
bool	Tracks_CanUndo();						// есть что отменить (операции с клипами)
bool	Tracks_CanRedo();
bool	Tracks_Redo();							// вернуть отменённую операцию с клипами
bool	Tracks_Undo();							// отменить последнюю операцию с клипами
void	Tracks_ClearUndo();
bool	Tracks_SaveActiveClip(const char *path, BString &err);	// активный клип в WAV-файл
void	Tracks_BeginProject();					// очистить всё перед открытием проекта
void	Tracks_FinishProject();					// после заполнения микшера: показать дорожку 0

// Раскладка дорожек по вертикали - одна и та же для панели слева и для полос с волнами.
#define MIN_LANE_HEIGHT	98
int		Tracks_Count();							// число дорожек
int		Tracks_Rank(int slot);					// номер дорожки сверху вниз (или -1)
int		Tracks_SlotAt(int rank);				// слот микшера по номеру сверху вниз (или -1)
float	Tracks_LaneHeight(float areaHeight);		// высота одной полосы: область делится поровну, но не меньше MIN_LANE_HEIGHT
void	Tracks_LaneSpan(int rank, float areaHeight, float &top, float &bottom);	// с учётом прокрутки
float	Tracks_Scroll();
void	Tracks_SetScroll(float scroll, float areaHeight);	// прокрутка по вертикали (с ограничением)

#endif
