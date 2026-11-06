/*
 * NoiseCore - ядро шумодава (спектральное вычитание). Без зависимостей от Haiku, проверяется на Linux (tests/noise).
 *
 * Окна по 1024 отсчёта с перекрытием 75%, БПФ, для каждой частоты свой коэффициент усиления.
 * Оценка шума:
 *  - авто: минимум мощности каждой частоты за последние ~1,5 с (профиль не нужен);
 *  - по профилю: средний спектр куска, где слышен один шум (LearnFrom).
 * Выход задержан на Latency() отсчётов; при разовой обработке выделения задержка убирается (см. Filters.cpp).
 */
#ifndef NOISE_CORE_H
#define NOISE_CORE_H

#include <memory>
#include <vector>

struct NoiseProfile {
	std::vector<float>	bins[2];		// средняя мощность по частотам, по каналам
	double				seconds;		// длина записанного куска
	NoiseProfile() : seconds(0) {}
};

class NoiseCore {
  public:
	static const int	kFrame = 1024;		// размер окна = задержка
	static int			Latency() { return kFrame; }

	NoiseCore(float rate);
	~NoiseCore();

	// reductionDb 0..40, sensitivity 1..3, smooth 0..1
	void	SetParams(float reductionDb, float sensitivity, float smooth, bool useProfile, bool listen);
	void	SetProfile(std::shared_ptr<const NoiseProfile> p);		// безопасно менять во время работы
	void	Reset();				// сбросить состояние (профиль остаётся)

	// На месте, перекрёстно записанные каналы (1 или 2). Размер блока любой.
	void	Process(float *buf, int frames, int channels);

	// Записать профиль шума из куска звука
	static std::shared_ptr<const NoiseProfile> LearnFrom(const float *buf, int frames, int channels, float rate);

	struct Impl;			// внутреннее устройство (в .cpp)

  private:
	Impl	*fImpl;
	NoiseCore(const NoiseCore &);
	NoiseCore &operator=(const NoiseCore &);
};

#endif
