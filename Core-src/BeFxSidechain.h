/*
 * BeFxSidechain.h - встроенный плагин «Сайдчейн-приседание» (группа BeFX).
 * LADSPA-дескриптор живёт в самой программе (Ladspa::RegisterBuiltin), своё окно параметров - в BeFxSidechainWindow.
 * Ключ берётся из Key::Cache по времени блока (Key::Scope), у плагина нет аудиовхода для ключа.
 */
#ifndef BEFX_SIDECHAIN_H
#define BEFX_SIDECHAIN_H

#include "LadspaHost.h"

namespace BeFx {

extern const char *kSidechainLabel;		// "beae_befx_sidechain"

enum SidechainPort {
	SC_KEY = 0,			// 0 - нет, n - дорожка n-1
	SC_RANGE,			// 0 весь звук, 1 низ
	SC_CUTOFF,			// Гц, для диапазона «низ»
	SC_THRESHOLD,		// дБ
	SC_DEPTH,			// дБ
	SC_ATTACK,			// мс
	SC_HOLD,			// мс
	SC_RELEASE,			// мс
	SC_KNEE,			// дБ
	SC_CURVE,			// 0 экспонента, 1 линейный
	SC_MIX,				// %
	SC_OUTPUT,			// дБ
	SC_REDUCTION,		// выход: текущее сжатие, дБ (для индикатора окна)
	SC_IN_L, SC_IN_R, SC_OUT_L, SC_OUT_R,
	SC_COUNT
};

// Регистрирует плагин во встроенных (вызвать до Ladspa::Scan). Повторный вызов безвреден.
void RegisterAll();

}	// namespace BeFx

#endif
