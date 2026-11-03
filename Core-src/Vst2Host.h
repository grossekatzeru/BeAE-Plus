/*
 * Vst2Host - минимальный хост VST 2.x для эффектов (без Haiku API, на dlopen).
 *
 * Структуры и коды команд описаны здесь заново по документации VST 2.4 (ABI зафиксирован),
 * заголовки Steinberg не используются. Порядок полей и точка входа "main_plugin" проверены по
 * исходникам vst_host из Haiku (Gerasim Troeglazov, MIT). Поддерживаются только эффекты
 * (синтезаторы пропускаются): стерео in/out, моно in/out.
 */
#ifndef VST2_HOST_H
#define VST2_HOST_H

#include <string>
#include <vector>
#include <stddef.h>

namespace Vst2 {

struct Info {
	std::string	file, name, vendor, product;
	int			numParams, numIn, numOut;
	bool		synth;
};

std::vector<std::string> DefaultPaths();		// vstplugins в системных и пользовательских add-ons, плюс VST_PATH
std::vector<Info> Scan(const std::vector<std::string> &dirs, std::vector<std::string> *log = 0);

class Instance {
public:
	Instance();
	~Instance();

	bool	Load(const std::string &path, std::string *err);	// загрузить и открыть эффект
	bool	Start(float rate, int maxBlock);					// частота, блок; включить обработку
	void	Stop();
	bool	IsRunning() const { return fRunning; }

	const Info &GetInfo() const { return fInfo; }
	int		NumParams() const { return fInfo.numParams; }
	std::string	ParamName(int i) const;
	std::string	ParamUnit(int i) const;
	std::string	ParamDisplay(int i) const;		// текст текущего значения («-6.0» и т.п.)
	float	GetParam(int i) const;				// 0..1
	void	SetParam(int i, float v);

	// Обработка на месте: interleaved стерео, frames кадров.
	void	Process(float *buf, size_t frames);

private:
	struct Impl;
	Impl	*fImpl;
	Info	fInfo;
	bool	fRunning;
	Instance(const Instance &);
	Instance &operator=(const Instance &);
};

}	// namespace Vst2

#endif
