#include "ControllerCore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Ctrl {

std::string Binding::ToString() const
{
	char s[24];
	if (type == S_AXIS) snprintf(s, sizeof(s), "A%d%c", index, sign < 0 ? '-' : '+');
	else if (type == S_BUTTON) snprintf(s, sizeof(s), "B%d", index);
	else return "-";
	return s;
}

bool Binding::FromString(const char *s)
{
	if (!s) return false;
	if (s[0] == '-' && s[1] == 0){ *this = Binding(); return true; }
	char *end = NULL;
	if (s[0] == 'A'){
		const long i = strtol(s + 1, &end, 10);
		if (end == s + 1 || i < 0 || i > 63 || (*end != '+' && *end != '-') || end[1] != 0) return false;
		*this = Binding(S_AXIS, (int)i, *end == '-' ? -1 : 1);
		return true;
	}
	if (s[0] == 'B'){
		const long i = strtol(s + 1, &end, 10);
		if (end == s + 1 || i < 0 || i > 31 || *end != 0) return false;
		*this = Binding(S_BUTTON, (int)i);
		return true;
	}
	return false;
}

void Map::SetDefaults()
{
	for (int i = 0; i < A_COUNT; i++) b[i] = Binding();
	// цифровая крестовина (две оси): вверх/вниз - громкость, влево/вправо - панорама.
	// У большинства геймпадов «вверх» - это отрицательное значение оси Y.
	b[A_VOL_UP]		= Binding(S_AXIS, 1, -1);
	b[A_VOL_DOWN]	= Binding(S_AXIS, 1, +1);
	b[A_PAN_LEFT]	= Binding(S_AXIS, 0, -1);
	b[A_PAN_RIGHT]	= Binding(S_AXIS, 0, +1);
	b[A_PLAY_STOP]	= Binding(S_BUTTON, 0);		// кнопка 1
	b[A_TRACK_PREV]	= Binding(S_BUTTON, 1);		// кнопка 2
	b[A_TRACK_NEXT]	= Binding(S_BUTTON, 2);		// кнопка 3
	b[A_MASTER_HOLD] = Binding(S_BUTTON, 3);	// кнопка 4 (удерживать)
	b[A_PAN_CENTER]	= Binding(S_BUTTON, 4);		// кнопка 5
	b[A_VOL_RESET]	= Binding(S_BUTTON, 5);		// кнопка 6
	volRate = 12.0f;
	panRate = 0.8f;
	deadZone = 0.3f;
}

std::string Map::ToString() const
{
	std::string s;
	for (int i = 0; i < A_COUNT; i++){
		if (i) s += ';';
		s += b[i].ToString();
	}
	return s;
}

bool Map::FromString(const char *str)
{
	if (!str) return false;
	Binding tmp[A_COUNT];
	const char *p = str;
	for (int i = 0; i < A_COUNT; i++){
		const char *e = strchr(p, ';');
		std::string tok = e ? std::string(p, e - p) : std::string(p);
		if (!tmp[i].FromString(tok.c_str())) return false;
		if (!e){
			if (i != A_COUNT - 1) return false;		// слишком мало
			p = NULL;
			break;
		}
		p = e + 1;
	}
	if (p && *p) return false;						// лишнее в конце
	for (int i = 0; i < A_COUNT; i++) b[i] = tmp[i];
	return true;
}

const char *Map::ActionKey(int a)
{
	static const char *k[A_COUNT] = {
		"CTRL_A_VOL_UP", "CTRL_A_VOL_DOWN", "CTRL_A_PAN_LEFT", "CTRL_A_PAN_RIGHT", "CTRL_A_PAN_CENTER",
		"CTRL_A_VOL_RESET", "CTRL_A_TRACK_PREV", "CTRL_A_TRACK_NEXT", "CTRL_A_MASTER_HOLD", "CTRL_A_PLAY_STOP"
	};
	return (a >= 0 && a < A_COUNT) ? k[a] : "";
}

float Level(const Binding &b, const Input &in, float dead)
{
	if (b.type == S_BUTTON)
		return (b.index >= 0 && b.index < 32 && (in.buttons & (1u << b.index))) ? 1.0f : 0.0f;
	if (b.type == S_AXIS){
		if (b.index < 0 || b.index >= (int)in.axes.size()) return 0.0f;
		float v = (float)in.axes[b.index] * (b.sign < 0 ? -1.0f : 1.0f) / 32767.0f;
		if (dead < 0) dead = 0;
		if (dead > 0.9f) dead = 0.9f;
		if (v <= dead) return 0.0f;
		v = (v - dead) / (1.0f - dead);
		return v > 1.0f ? 1.0f : v;
	}
	return 0.0f;
}

void Engine::Reset()
{
	for (int i = 0; i < A_COUNT; i++) fPrev[i] = false;
}

void Engine::Tick(const Map &map, const Input &in, double dt, Output &out)
{
	out.Clear();
	if (dt < 0) dt = 0;
	if (dt > 0.25) dt = 0.25;						// после паузы не прыгать
	float L[A_COUNT];
	for (int a = 0; a < A_COUNT; a++)
		L[a] = Level(map.b[a], in, map.deadZone);
	out.master = L[A_MASTER_HOLD] > 0.5f;
	out.dVolDb = (double)map.volRate * dt * (double)(L[A_VOL_UP] - L[A_VOL_DOWN]);
	out.dPan = (double)map.panRate * dt * (double)(L[A_PAN_RIGHT] - L[A_PAN_LEFT]);
	bool now[A_COUNT];
	for (int a = 0; a < A_COUNT; a++) now[a] = L[a] > 0.5f;
	out.volReset  = now[A_VOL_RESET]  && !fPrev[A_VOL_RESET];
	out.panCenter = now[A_PAN_CENTER] && !fPrev[A_PAN_CENTER];
	out.trackPrev = now[A_TRACK_PREV] && !fPrev[A_TRACK_PREV];
	out.trackNext = now[A_TRACK_NEXT] && !fPrev[A_TRACK_NEXT];
	out.playStop  = now[A_PLAY_STOP]  && !fPrev[A_PLAY_STOP];
	for (int a = 0; a < A_COUNT; a++) fPrev[a] = now[a];
}

}	// namespace Ctrl
