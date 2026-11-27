// Задержка записи по звуковым картам (без Haiku API, проверяется тестами на Linux).
// Ключ - имя карты (имя узла Media Kit), значение - компенсация в миллисекундах.
#ifndef CARD_LATENCY_H
#define CARD_LATENCY_H

#include <map>
#include <string>
#include <vector>

namespace CardLat {

class Table {
public:
	bool Has(const std::string &card) const { return fMap.find(card) != fMap.end(); }

	// мс для карты; 0, если для неё ничего не задано
	int Get(const std::string &card) const
	{
		std::map<std::string, int>::const_iterator it = fMap.find(card);
		return it == fMap.end() ? 0 : it->second;
	}

	void Set(const std::string &card, int ms)
	{
		if (card.empty())
			return;
		if (ms < 0) ms = 0;
		if (ms > 1000) ms = 1000;
		fMap[card] = ms;
	}

	void Clear() { fMap.clear(); }
	const std::map<std::string, int> &All() const { return fMap; }

private:
	std::map<std::string, int> fMap;
};

// Какие из найденных карт ещё не измерены. Общая задержка из старых настроек (legacyMs) была измерена на системном
// входе, поэтому один раз переносится на системную карту; после этого *legacyMs обнуляется, чтобы при смене карты
// по умолчанию значение не перенеслось на другую карту.
inline std::vector<std::string> Pending(const std::vector<std::string> &cards, Table &table,
	const std::string &systemCard, int *legacyMs)
{
	if (legacyMs != NULL && *legacyMs > 0 && !systemCard.empty() && !table.Has(systemCard)) {
		bool present = false;
		for (size_t i = 0; i < cards.size(); i++)
			if (cards[i] == systemCard)
				present = true;
		if (present) {
			table.Set(systemCard, *legacyMs);
			*legacyMs = 0;
		}
	}
	std::vector<std::string> result;
	for (size_t i = 0; i < cards.size(); i++)
		if (!table.Has(cards[i]))
			result.push_back(cards[i]);
	return result;
}

}	// namespace CardLat

#endif
