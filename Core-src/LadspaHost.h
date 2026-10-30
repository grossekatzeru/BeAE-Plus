/*
 * LadspaHost - минимальный хост LADSPA-плагинов для BeAE.
 *
 * Ядро не зависит от Haiku API (только стандартная библиотека и dlopen), поэтому его можно
 * проверить на любой системе. Структуры LADSPA описаны здесь заново по спецификации
 * (ABI зафиксирован: ladspa.h v1.1), заголовок ladspa.h не копируется.
 *
 * Лицензия: BSD (как у BeAE).
 */
#ifndef LADSPA_HOST_H
#define LADSPA_HOST_H

#include <string>
#include <vector>
#include <map>

namespace Ladspa {

// ---- ABI LADSPA (совместимо с ladspa.h) ----
typedef float Data;
struct PortRangeHint {
	int		hint;
	Data	lower;
	Data	upper;
};
struct Descriptor {
	unsigned long		UniqueID;
	const char			*Label;
	int					Properties;
	const char			*Name;
	const char			*Maker;
	const char			*Copyright;
	unsigned long		PortCount;
	const int			*PortDescriptors;
	const char * const	*PortNames;
	const PortRangeHint	*PortRangeHints;
	void				*ImplementationData;
	void*	(*instantiate)(const Descriptor *, unsigned long sampleRate);
	void	(*connect_port)(void *, unsigned long port, Data *location);
	void	(*activate)(void *);
	void	(*run)(void *, unsigned long sampleCount);
	void	(*run_adding)(void *, unsigned long sampleCount);
	void	(*set_run_adding_gain)(void *, Data gain);
	void	(*deactivate)(void *);
	void	(*cleanup)(void *);
};

enum {
	PORT_INPUT = 0x1, PORT_OUTPUT = 0x2, PORT_CONTROL = 0x4, PORT_AUDIO = 0x8
};
enum {
	HINT_BOUNDED_BELOW = 0x1, HINT_BOUNDED_ABOVE = 0x2, HINT_TOGGLED = 0x4,
	HINT_SAMPLE_RATE = 0x8, HINT_LOGARITHMIC = 0x10, HINT_INTEGER = 0x20,
	HINT_DEFAULT_MASK = 0x3C0,
	HINT_DEFAULT_NONE = 0x0, HINT_DEFAULT_MINIMUM = 0x40, HINT_DEFAULT_LOW = 0x80,
	HINT_DEFAULT_MIDDLE = 0xC0, HINT_DEFAULT_HIGH = 0x100, HINT_DEFAULT_MAXIMUM = 0x140,
	HINT_DEFAULT_0 = 0x200, HINT_DEFAULT_1 = 0x240, HINT_DEFAULT_100 = 0x280,
	HINT_DEFAULT_440 = 0x2C0
};

// ---- описание плагина ----
struct Port {
	int			index;
	std::string	name;
	int			descriptor;		// PORT_*
	int			hint;			// HINT_*
	float		lower, upper;
	bool IsInput() const	{ return descriptor & PORT_INPUT; }
	bool IsOutput() const	{ return descriptor & PORT_OUTPUT; }
	bool IsAudio() const	{ return descriptor & PORT_AUDIO; }
	bool IsControl() const	{ return descriptor & PORT_CONTROL; }
};

// Как плагин подключается к буферу BeAE (моно/стерео)
enum Mode {
	MODE_UNSUPPORTED = 0,
	MODE_MONO_MONO,		// 1 вход, 1 выход
	MODE_STEREO_STEREO,	// 2 входа, 2 выхода
	MODE_MONO_STEREO,	// 1 вход, 2 выхода
	MODE_STEREO_MONO	// 2 входа, 1 выход
};

struct Plugin {
	std::string		file;			// полный путь к .so
	std::string		label;			// уникальная метка в файле
	std::string		name;
	std::string		maker;
	unsigned long	uniqueId;
	std::vector<Port>	ports;
	int				audioIn, audioOut;
	Mode			mode;
	bool			realtimeSafe;

	// индексы управляющих входов (параметры пользователя)
	std::vector<int> ControlInputs() const;
	std::string		Key() const;	// file + "|" + label
};

struct Range {
	float lower, upper, def;
	bool integer, toggled, logarithmic;
};
// Вычисляет границы и значение по умолчанию с учётом подсказок LADSPA.
Range ResolveRange(const Port &port, float sampleRate);

// Папки поиска: переменная LADSPA_PATH (через ':'), затем стандартные.
std::vector<std::string> DefaultPaths();
// Просмотр папок. В *log пишутся причины, по которым файл пропущен.
std::vector<Plugin> Scan(const std::vector<std::string> &dirs, std::vector<std::string> *log = 0);

// ---- настройки (значения параметров) ----
typedef std::map<int, float> Values;		// индекс порта -> значение
bool LoadValues(const std::string &path, Values *values);
bool SaveValues(const std::string &path, const Values &values);

// ---- запуск плагина ----
class Runner {
public:
	Runner();
	~Runner();

	// channels = число каналов в буфере BeAE (1 или 2). Создаёт экземпляры и активирует их.
	bool Start(const Plugin &plugin, float sampleRate, int channels, std::string *error);
	void Stop();
	bool IsRunning() const { return fRunning; }

	// Значение управляющего порта (можно менять во время работы).
	void SetControl(int portIndex, float value);
	float Control(int portIndex) const;

	// Обработка на месте: interleaved-буфер из frames кадров (channels каналов, см. Start).
	void Process(float *buffer, size_t frames);

private:
	struct Impl;
	Impl *fImpl;
	bool fRunning;
	Runner(const Runner &);
	Runner &operator=(const Runner &);
};

}	// namespace Ladspa

#endif
