/*
 * Многополосный компрессор, плагин LADSPA без задержки.
 *
 * Звук делится на 4 полосы фильтрами Линквица-Райли 4 порядка (сумма полос без искажений амплитуды),
 * каждая полоса сжимается отдельным компрессором: порог, степень сжатия, атака, спад, усиление.
 * Детектор общий для обоих каналов (стереообраз не плывёт). Мягкое колено общее.
 */
#ifndef MULTIBAND_H
#define MULTIBAND_H

#include "../common/ladspa_abi.h"

namespace Multiband {

const int kBands = 4;
enum BandParam { BP_THRESHOLD = 0, BP_RATIO, BP_ATTACK, BP_RELEASE, BP_GAIN, BP_COUNT };

enum Port {
	P_BAND0 = 0,							// 4 полосы по 5 портов: порог -60..0 дБ, степень 1..20, атака 1..100 мс,
											// спад 20..1000 мс, усиление -12..12 дБ
	P_X1 = kBands * BP_COUNT,				// 60..500 Гц   граница 1 (между низами и низкой серединой)
	P_X2,									// 300..4000 Гц граница 2
	P_X3,									// 2000..16000 Гц граница 3
	P_KNEE,									// 0..12 дБ     мягкое колено
	P_OUTPUT,								// -12..12 дБ   общий выход
	P_SOLO,									// 0..4         слушать только одну полосу (0 = все)
	P_IN_L, P_IN_R, P_OUT_L, P_OUT_R,
	P_COUNT
};

const Ladspa::Descriptor *Descriptor();

}	// namespace Multiband

#endif
