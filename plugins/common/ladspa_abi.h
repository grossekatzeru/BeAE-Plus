/*
 * ladspa_abi.h - описание интерфейса LADSPA (v1.1) для плагинов BeAE-Plus.
 * Написано заново по спецификации (двоичная совместимость с ladspa.h); общего кода с
 * приложением нет, плагины собираются отдельно и работают в любом LADSPA-хосте.
 */
#ifndef BEAE_LADSPA_ABI_H
#define BEAE_LADSPA_ABI_H

/* Если рядом подключён заголовок хоста BeAE (LadspaHost.h), те же определения уже есть. */
#ifndef LADSPA_HOST_H
namespace Ladspa {

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
	HINT_DEFAULT_MINIMUM = 0x40, HINT_DEFAULT_LOW = 0x80, HINT_DEFAULT_MIDDLE = 0xC0,
	HINT_DEFAULT_HIGH = 0x100, HINT_DEFAULT_MAXIMUM = 0x140,
	HINT_DEFAULT_0 = 0x200, HINT_DEFAULT_1 = 0x240, HINT_DEFAULT_100 = 0x280, HINT_DEFAULT_440 = 0x2C0
};

}	// namespace Ladspa
#endif	// LADSPA_HOST_H

#endif
