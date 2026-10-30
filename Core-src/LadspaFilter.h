/*
 * LadspaFilter - LADSPA-плагин как фильтр BeAE (окно с ползунками, предпросмотр, применение).
 */
#ifndef LADSPA_FILTER_H
#define LADSPA_FILTER_H

#include <Menu.h>
#include <String.h>
#include <vector>

#include "RealtimeFilter.h"
#include "LadspaHost.h"

class BSlider;
class BCheckBox;
class LadspaSlider;

class LadspaFilter : public RealtimeFilter {
public:
	LadspaFilter(const Ladspa::Plugin &plugin, bool realtime);
	virtual ~LadspaFilter();

	virtual void		MessageReceived(BMessage *message);
	virtual BView*		ConfigView();
	virtual void		UpdateValues();
	virtual bool		InitFilter(float frequency, int32 channels = 2, int32 pass = 0, int32 total_samples = 0);
	virtual void		DeAllocate();
	virtual void		FilterBuffer(float *buffer, size_t size);
	virtual const char*	RepeatTag() const;

private:
	struct Control {
		int				port;
		Ladspa::Range	range;
		LadspaSlider	*slider;
		BCheckBox		*check;
	};

	void				LoadSaved();
	void				SaveSaved();
	void				ResetToDefaults();
	BString				SettingsPath() const;

	Ladspa::Plugin		fPlugin;
	Ladspa::Runner		fRunner;
	Ladspa::Values		fValues;			// порт -> значение
	std::vector<Control> fControls;
	BString				fTag;
};

// ---- список плагинов и меню ----
void				LadspaRescan();
const std::vector<Ladspa::Plugin>& LadspaList();
const Ladspa::Plugin* LadspaFind(const char *file, const char *label);
BMenu*				BuildLadspaMenu();
void				RunLadspaFilter(const char *file, const char *label);
// Тег для «Повторить последний фильтр»: "LADSPA:<файл>|<метка>"
bool				IsLadspaTag(const char *tag);
RealtimeFilter*		CreateLadspaFilterFromTag(const char *tag);
// Название для пункта меню «повторить»: для LADSPA — имя плагина, для остальных — перевод по ключу.
const char*			FilterTagDisplayName(const char *tag, BString *storage);

#endif
