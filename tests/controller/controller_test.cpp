#include "ControllerCore.h"
#include <stdio.h>
#include <math.h>

using namespace Ctrl;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool Near(double a, double b, double e = 1e-6) { return fabs(a - b) <= e; }

static Input In(int16_t a0, int16_t a1, uint32_t btn = 0)
{
	Input in; in.axes.push_back(a0); in.axes.push_back(a1); in.buttons = btn; return in;
}

int main()
{
	// --- строки привязок ---
	{
		Binding b;
		CHECK(b.FromString("A1-") && b.type == S_AXIS && b.index == 1 && b.sign == -1);
		CHECK(b.ToString() == "A1-");
		CHECK(b.FromString("A12+") && b.index == 12 && b.sign == 1);
		CHECK(b.FromString("B3") && b.type == S_BUTTON && b.index == 3 && b.ToString() == "B3");
		CHECK(b.FromString("-") && b.type == S_NONE && b.ToString() == "-");
		Binding keep(S_BUTTON, 7);
		Binding t = keep;
		CHECK(!t.FromString("A1") && !t.FromString("Q") && !t.FromString("B") && !t.FromString("A1*") && !t.FromString(NULL));
		CHECK(t == keep);					// неудачный разбор не портит значение
	}
	// --- карта: туда и обратно, по умолчанию ---
	{
		Map m;
		const std::string s = m.ToString();
		Map m2;
		for (int i = 0; i < A_COUNT; i++) m2.b[i] = Binding();
		CHECK(m2.FromString(s.c_str()));
		for (int i = 0; i < A_COUNT; i++) CHECK(m2.b[i] == m.b[i]);
		Map m3 = m;
		CHECK(!m3.FromString("A1-;B2"));						// мало
		CHECK(!m3.FromString((s + ";B1").c_str()));				// много
		CHECK(!m3.FromString("junk;;;;;;;;;"));
		for (int i = 0; i < A_COUNT; i++) CHECK(m3.b[i] == m.b[i]);	// карта не изменилась
	}
	// --- уровень оси: мёртвая зона и плавность ---
	{
		Binding up(S_AXIS, 1, -1);
		CHECK(Near(Level(up, In(0, 0), 0.3f), 0));
		CHECK(Near(Level(up, In(0, -32767), 0.3f), 1.0));
		CHECK(Near(Level(up, In(0, 32767), 0.3f), 0));					// другое направление
		CHECK(Near(Level(up, In(0, -9830), 0.3f), 0, 0.01));			// чуть меньше порога
		const float half = Level(up, In(0, -(int16_t)(0.65f * 32767)), 0.3f);
		CHECK(Near(half, 0.5, 0.01));									// середина между порогом и краем
		CHECK(Near(Level(Binding(S_AXIS, 5, 1), In(0, 0), 0.3f), 0));	// нет такой оси
		CHECK(Near(Level(Binding(S_BUTTON, 2), In(0, 0, 1u << 2), 0.3f), 1));
		CHECK(Near(Level(Binding(S_BUTTON, 2), In(0, 0, 1u << 1), 0.3f), 0));
	}
	// --- скорость громкости: вверх держим 0.5 с при 12 дБ/с = +6 дБ ---
	{
		Map m; Engine e; Output o; double sum = 0;
		for (int i = 0; i < 25; i++){ e.Tick(m, In(0, -32767), 0.02, o); sum += o.dVolDb; }
		CHECK(Near(sum, 6.0, 1e-4));
		CHECK(!o.master && !o.playStop);
		sum = 0;
		for (int i = 0; i < 25; i++){ e.Tick(m, In(0, 32767), 0.02, o); sum += o.dVolDb; }
		CHECK(Near(sum, -6.0, 1e-4));
		e.Tick(m, In(0, 0), 0.02, o);
		CHECK(Near(o.dVolDb, 0) && Near(o.dPan, 0));
	}
	// --- плавная ось: скорость пропорциональна уровню ---
	{
		Map m; Engine e; Output o1, o2;
		e.Tick(m, In(0, -(int16_t)(0.65f * 32767)), 0.25, o1);
		e.Tick(m, In(0, -32767), 0.25, o2);
		CHECK(Near(o1.dVolDb, 1.5, 0.05) && Near(o2.dVolDb, 3.0, 1e-4));
	}
	// --- панорама ---
	{
		Map m; Engine e; Output o;
		e.Tick(m, In(32767, 0), 0.25, o);
		CHECK(Near(o.dPan, 0.2, 1e-4));
		e.Tick(m, In(-32767, 0), 0.25, o);
		CHECK(Near(o.dPan, -0.2, 1e-4));
	}
	// --- события «по нажатию»: один раз, пока не отпустили ---
	{
		Map m; Engine e; Output o;
		e.Tick(m, In(0, 0, 1u << 0), 0.02, o); CHECK(o.playStop);
		e.Tick(m, In(0, 0, 1u << 0), 0.02, o); CHECK(!o.playStop);
		e.Tick(m, In(0, 0, 0), 0.02, o);       CHECK(!o.playStop);
		e.Tick(m, In(0, 0, 1u << 0), 0.02, o); CHECK(o.playStop);
		e.Tick(m, In(0, 0, 1u << 1), 0.02, o); CHECK(o.trackPrev && !o.playStop);
		e.Tick(m, In(0, 0, 1u << 2), 0.02, o); CHECK(o.trackNext);
		e.Tick(m, In(0, 0, 1u << 4), 0.02, o); CHECK(o.panCenter);
		e.Tick(m, In(0, 0, 1u << 5), 0.02, o); CHECK(o.volReset);
	}
	// --- мастер удерживается ---
	{
		Map m; Engine e; Output o;
		e.Tick(m, In(0, -32767, 1u << 3), 0.1, o);
		CHECK(o.master && o.dVolDb > 0);
		e.Tick(m, In(0, 0, 0), 0.1, o);
		CHECK(!o.master);
	}
	// --- паузу в опросе не превращаем в прыжок ---
	{
		Map m; Engine e; Output o;
		e.Tick(m, In(0, -32767), 5.0, o);
		CHECK(Near(o.dVolDb, 12.0 * 0.25, 1e-4));
	}
	// --- переназначение и пустая привязка ---
	{
		Map m; Engine e; Output o;
		m.b[A_VOL_UP] = Binding(S_BUTTON, 7);
		m.b[A_VOL_DOWN] = Binding();
		e.Tick(m, In(0, -32767, 1u << 7), 0.25, o);
		CHECK(Near(o.dVolDb, 3.0));
		e.Tick(m, In(0, 32767, 0), 0.25, o);
		CHECK(Near(o.dVolDb, 0));
	}
	printf(fails ? "FAILED: %d\n" : "OK: controller\n", fails);
	return fails ? 1 : 0;
}
