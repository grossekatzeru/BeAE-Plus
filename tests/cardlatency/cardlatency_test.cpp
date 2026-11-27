#include "CardLatency.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main()
{
	using namespace CardLat;
	Table t;
	CHECK(!t.Has("A") && t.Get("A") == 0);
	t.Set("A", 221);
	CHECK(t.Has("A") && t.Get("A") == 221);
	t.Set("A", 5000);
	CHECK(t.Get("A") == 1000);
	t.Set("A", -3);
	CHECK(t.Get("A") == 0 && t.Has("A"));
	t.Set("", 10);
	CHECK(t.All().size() == 1);

	std::vector<std::string> cards;
	cards.push_back("Realtek");
	cards.push_back("Logitech USB Headset");

	// перенос старого значения на системную карту, второй карте - вопрос
	{
		Table table;
		int legacy = 221;
		std::vector<std::string> p = Pending(cards, table, "Realtek", &legacy);
		CHECK(table.Get("Realtek") == 221 && legacy == 0);
		CHECK(p.size() == 1 && p[0] == "Logitech USB Headset");
		// при следующем запуске: ничего не переносится
		table.Set("Logitech USB Headset", 400);
		p = Pending(cards, table, "Realtek", &legacy);
		CHECK(p.empty());
	}
	// старого значения нет: спрашиваем про обе
	{
		Table table;
		int legacy = 0;
		std::vector<std::string> p = Pending(cards, table, "Realtek", &legacy);
		CHECK(p.size() == 2 && !table.Has("Realtek"));
	}
	// системной карты нет в списке: значение не теряется и не переносится
	{
		Table table;
		int legacy = 221;
		std::vector<std::string> p = Pending(cards, table, "Other", &legacy);
		CHECK(legacy == 221 && p.size() == 2 && table.All().empty());
	}
	// карта с уже заданным значением не спрашивается
	{
		Table table;
		table.Set("Realtek", 0);
		int legacy = 0;
		std::vector<std::string> p = Pending(cards, table, "Realtek", &legacy);
		CHECK(p.size() == 1);
	}
	if (fails == 0) printf("cardlatency: all passed\n");
	return fails ? 1 : 0;
}
