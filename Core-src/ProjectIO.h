/*
 * ProjectIO - формат проекта BeMIX: чтение и запись JSON и WAV (32-бит float).
 * Без зависимостей от Haiku, поэтому проверяется тестом на Linux.
 */
#ifndef PROJECT_IO_H
#define PROJECT_IO_H

#include <string>
#include <vector>
#include <utility>

struct JValue {
	enum Type { Null, Bool, Num, Str, Arr, Obj };
	Type	type;
	bool	b;
	double	n;
	std::string	s;
	std::vector<JValue>	a;
	std::vector<std::pair<std::string, JValue> > o;

	JValue() : type(Null), b(false), n(0) {}
	const JValue *Get(const char *key) const;		// поле объекта или NULL
	double	Number(const char *key, double def) const;
	bool	Boolean(const char *key, bool def) const;
	std::string	String(const char *key, const std::string &def = "") const;
};

bool		JsonParse(const std::string &text, JValue &out, std::string &err);
std::string	JsonEscape(const std::string &s);			// содержимое строки без кавычек, UTF-8 как есть

bool WavWriteFloat(const char *path, const float *data, long long frames, int channels, float rate);
bool WavRead(const char *path, std::vector<float> &out, long long &frames, int &channels,
			 float &rate, std::string &err);

#endif
