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
bool	Tracks_IsEmpty();						// в проекте нет ни одного клипа и звука
void	Tracks_PublishActiveSpan();		// сообщить микшеру, где на ленте лежит активный клип (для кроссфейда); перед запуском воспроизведения
bool	Tracks_Activate(int lane, double cursorTime = -1.0, int clip = -1);	// сделать активным клип дорожки (clip<0 - тот, что под курсором); курсор - на ленте, сек; false - нельзя
void	Tracks_Stash();							// сохранить содержимое Pool в активную дорожку (без тишины по краям)
void	Tracks_PlaceActive(double offsetSeconds);	// Pool содержит свежий звук (файл, запись): положить на ленту
bool	Tracks_SetOffset(int index, double seconds);	// false, если клип закреплён во времени	// сдвинуть клип: активный (если дорожка активна) или первый клип дорожки
bool	Tracks_MoveClip(int lane, int clip, double seconds);	// сдвинуть клип из микшера (не активный)
bool	Tracks_ActiveClipLocked();				// активный клип закреплён во времени (его нельзя сдвигать)
void	Tracks_ToggleActiveClipLock();			// закрепить / освободить активный клип
// Выделение нескольких клипов: активный клип выделен всегда, остальные добавляются Ctrl+щелчком
void	Tracks_SelectLaneClips();				// двойной щелчок: выделить все клипы активной дорожки
void	Tracks_ClearClipSelection();			// снять выделение со всех клипов, кроме активного
bool	Tracks_ToggleClipSelected(int lane, int clip);	// добавить / убрать клип микшера из выделения
int		Tracks_SelectedCount();					// число выделенных клипов, включая активный
bool	Tracks_SelectionLocked();				// все выделенные клипы закреплены во времени
void	Tracks_ToggleSelectionLock();			// закрепить все выделенные (если не все закреплены) или освободить все
int		Tracks_ActiveClipGroup();				// номер группы активного клипа (0 - нет)
void	Tracks_SelectGroupOf(int lane, int clip);	// выделить всю группу, к которой относится клип микшера
void	Tracks_SelectActiveGroup();				// выделить остальные клипы группы активного клипа
bool	Tracks_SelectionGrouped();				// выделенные клипы образуют одну группу
bool	Tracks_ToggleSelectionGroup();			// «Group»: сгруппировать выделенные (2 и более) или разгруппировать
bool	Tracks_SelectionCanShiftLanes(int rankDelta);	// все выделенные клипы можно сдвинуть на rankDelta дорожек
bool	Tracks_MoveSelection(double timeDelta, int rankDelta);	// сдвиг выделенных (группы) по времени и на rankDelta дорожек вместе
bool	Tracks_MoveSelectionBy(double delta, bool includeActive);	// сдвинуть выделенные незакреплённые клипы на delta секунд (одна правка в отмене)
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
bool	Tracks_PasteFits();						// для вставки хватает дорожек (недостающие добавятся сами) - false только на пределе их числа
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
#define MIN_LANE_HEIGHT	133
int		Tracks_Count();							// число дорожек
int		Tracks_Rank(int slot);					// номер дорожки сверху вниз (или -1)
int		Tracks_SlotAt(int rank);				// слот микшера по номеру сверху вниз (или -1)
float	Tracks_LaneHeight(float areaHeight);		// высота одной полосы: область делится поровну, но не меньше MIN_LANE_HEIGHT
void	Tracks_LaneSpan(int rank, float areaHeight, float &top, float &bottom);	// с учётом прокрутки
// Полоса автоматизации (громкость и панорама) под каждой дорожкой: свёрнутая тонкая или развёрнутая.
#define STRIP_CLOSED_H	14		// свёрнутая: тонкая, только треугольник
#define STRIP_OPEN_H	72		// развёрнутая: чекбоксы «Громкость» и «Панорама» слева, кривые справа
bool	Tracks_StripOpen(int slot);					// развёрнута ли полоса автоматизации
void	Tracks_SetStripOpen(int slot, bool open);
float	Tracks_StripHeight(int rank);				// высота полосы автоматизации дорожки (по номеру сверху вниз)
float	Tracks_TotalHeight(float areaHeight);		// полная высота всех дорожек вместе с полосами автоматизации
void	Tracks_StripSpan(int rank, float areaHeight, float &top, float &bottom);	// полоса автоматизации под дорожкой, с учётом прокрутки
float	Tracks_Scroll();
void	Tracks_SetScroll(float scroll, float areaHeight);	// прокрутка по вертикали (с ограничением)

#endif
