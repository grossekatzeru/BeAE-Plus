/*
 * Tracks.h - переключение активной дорожки между Pool (то, с чем работают правка, фильтры,
 * undo) и микшером (хранилище всех дорожек).
 */
#ifndef TRACKS_H
#define TRACKS_H

#define DEFAULT_TRACKS	4

void	Tracks_Init();					// создать DEFAULT_TRACKS пустых дорожек (один раз при старте)
bool	Tracks_Activate(int index);		// сделать дорожку активной; false - нельзя (идёт запись)
void	Tracks_Stash();					// сохранить содержимое Pool в активную дорожку микшера
int		Tracks_Add();					// добавить пустую дорожку, вернуть индекс или -1
void	Tracks_RemoveActive();			// удалить активную дорожку (последнюю оставшуюся удалить нельзя)

#endif
