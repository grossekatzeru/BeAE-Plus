/* Тестовый LADSPA-плагин для проверки LadspaHost (Linux). Не входит в сборку BeAE для Haiku. */
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef float LADSPA_Data;
typedef struct { int HintDescriptor; LADSPA_Data LowerBound, UpperBound; } PRH;
typedef struct D {
	unsigned long UniqueID; const char *Label; int Properties; const char *Name, *Maker, *Copyright;
	unsigned long PortCount; const int *PortDescriptors; const char * const *PortNames; const PRH *PortRangeHints;
	void *ImplementationData;
	void *(*instantiate)(const struct D *, unsigned long);
	void (*connect_port)(void *, unsigned long, LADSPA_Data *);
	void (*activate)(void *);
	void (*run)(void *, unsigned long);
	void (*run_adding)(void *, unsigned long);
	void (*set_run_adding_gain)(void *, LADSPA_Data);
	void (*deactivate)(void *);
	void (*cleanup)(void *);
} D;

#define IN 1
#define OUT 2
#define CTL 4
#define AUD 8

typedef struct { LADSPA_Data *p[8]; float rate; float state; } Inst;

static void *inst(const D *d, unsigned long rate) { (void)d; Inst *i = calloc(1, sizeof(Inst)); i->rate = (float)rate; return i; }
static void conn(void *h, unsigned long port, LADSPA_Data *loc) { ((Inst *)h)->p[port] = loc; }
static void act(void *h) { ((Inst *)h)->state = 0; }
static void clean(void *h) { free(h); }

/* 1. amp: gain, in, out */
static void run_amp(void *h, unsigned long n) { Inst *i = h; for (unsigned long k = 0; k < n; k++) i->p[2][k] = i->p[1][k] * *i->p[0]; }
/* 2. stereo: gain, invertR (toggled), inL, inR, outL, outR */
static void run_st(void *h, unsigned long n) {
	Inst *i = h; float g = *i->p[0]; float s = (*i->p[1] > 0.5f) ? -1.0f : 1.0f;
	for (unsigned long k = 0; k < n; k++) { i->p[4][k] = i->p[2][k] * g; i->p[5][k] = i->p[3][k] * g * s; } }
/* 3. widen mono->stereo: in, outL, outR  */
static void run_widen(void *h, unsigned long n) { Inst *i = h; for (unsigned long k = 0; k < n; k++) { i->p[1][k] = i->p[0][k]; i->p[2][k] = i->p[0][k] * 0.5f; } }
/* 4. sum stereo->mono: inL, inR, out */
static void run_sum(void *h, unsigned long n) { Inst *i = h; for (unsigned long k = 0; k < n; k++) i->p[2][k] = i->p[0][k] + i->p[1][k]; }
/* 5. meter: freq(log,sr), peak(control out), in, out ; запоминает состояние между блоками (счётчик) */
static void run_meter(void *h, unsigned long n) {
	Inst *i = h; float pk = 0;
	for (unsigned long k = 0; k < n; k++) { float v = i->p[2][k]; i->p[3][k] = v; if (fabsf(v) > pk) pk = fabsf(v); }
	i->state += (float)n; *i->p[1] = pk;
}
/* 6. generator без входа (должен быть помечен как неподдерживаемый) */
static void run_gen(void *h, unsigned long n) { Inst *i = h; for (unsigned long k = 0; k < n; k++) i->p[0][k] = 0.1f; }

static const int d_amp[] = { IN|CTL, IN|AUD, OUT|AUD };
static const char *n_amp[] = { "Gain", "Input", "Output" };
static const PRH h_amp[] = { { 0x1|0x2|0x240, 0.0f, 4.0f }, {0,0,0}, {0,0,0} };

static const int d_st[] = { IN|CTL, IN|CTL, IN|AUD, IN|AUD, OUT|AUD, OUT|AUD };
static const char *n_st[] = { "Gain", "Invert right", "In L", "In R", "Out L", "Out R" };
static const PRH h_st[] = { { 0x1|0x2|0x240, 0.0f, 2.0f }, { 0x4|0x200, 0, 1 }, {0,0,0},{0,0,0},{0,0,0},{0,0,0} };

static const int d_wd[] = { IN|AUD, OUT|AUD, OUT|AUD };
static const char *n_wd[] = { "In", "Out L", "Out R" };
static const PRH h_wd[] = { {0,0,0},{0,0,0},{0,0,0} };

static const int d_sm[] = { IN|AUD, IN|AUD, OUT|AUD };
static const char *n_sm[] = { "In L", "In R", "Out" };
static const PRH h_sm[] = { {0,0,0},{0,0,0},{0,0,0} };

static const int d_mt[] = { IN|CTL, OUT|CTL, IN|AUD, OUT|AUD };
static const char *n_mt[] = { "Cutoff", "Peak", "In", "Out" };
static const PRH h_mt[] = { { 0x1|0x2|0x8|0x10|0xC0, 0.001f, 0.45f }, {0x1|0x2, 0, 1}, {0,0,0}, {0,0,0} };

static const int d_gn[] = { OUT|AUD };
static const char *n_gn[] = { "Out" };
static const PRH h_gn[] = { {0,0,0} };

#define MK(id,lab,nm,cnt,dd,nn,hh,rr) { id, lab, 0, nm, "BeAE tests", "none", cnt, dd, nn, hh, 0, inst, conn, act, rr, 0, 0, 0, clean }
static const D descs[] = {
	MK(9001, "amp",    "Test amp (mono)",       3, d_amp, n_amp, h_amp, run_amp),
	MK(9002, "stereo", "Test stereo",           6, d_st,  n_st,  h_st,  run_st),
	MK(9003, "widen",  "Test widen (1->2)",     3, d_wd,  n_wd,  h_wd,  run_widen),
	MK(9004, "sum",    "Test sum (2->1)",       3, d_sm,  n_sm,  h_sm,  run_sum),
	MK(9005, "meter",  "Test meter (log/rate)", 4, d_mt,  n_mt,  h_mt,  run_meter),
	MK(9006, "gen",    "Test generator",        1, d_gn,  n_gn,  h_gn,  run_gen)
};
const D *ladspa_descriptor(unsigned long i) { return i < sizeof(descs) / sizeof(descs[0]) ? &descs[i] : 0; }
