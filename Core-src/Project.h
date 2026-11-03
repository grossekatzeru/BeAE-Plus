/*
 * Project - проект BeMIX: папка <Имя>/ с WAV-файлами дорожек и описанием <Имя>.bemix (JSON).
 */
#ifndef PROJECT_H
#define PROJECT_H

#include <String.h>
#include <Window.h>
#include <Path.h>

// Сохранить проект: dir - выбранная папка, name - имя проекта (создаётся подпапка).
bool	Project_Save(const BPath &dir, const char *name, BString &err);
bool	Project_SaveAgain(BString &err);			// сохранить туда же, где проект уже лежит
bool	Project_Open(const char *bemixPath, BString &err);
bool	Project_HasFolder();
const char *Project_Name();						// "" если проекта нет
void	Project_Forget();							// «Файл → Новый»
BString	Project_Title(const char *item = NULL);	// заголовок окна: BeAE - Проект [- файл]

// Окно запроса имени проекта; результат уходит окну target сообщением msgWhat с полем "name".
class ProjectNameWindow : public BWindow {
public:
	ProjectNameWindow(BWindow *target, uint32 msgWhat, const char *defaultName);
	virtual void MessageReceived(BMessage *msg);
private:
	BWindow	*fTarget;
	uint32	fWhat;
	class BTextControl *fText;
};

#endif
