/* Тестовый VST2-эффект (Linux): стерео in/out, 2 параметра: gain (0..1 -> 0..2), mute. Только для тестов. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct AEffect AEffect;
struct AEffect {
	int32_t magic; intptr_t (*dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
	void (*process)(AEffect*, float**, float**, int32_t);
	void (*setParameter)(AEffect*, int32_t, float); float (*getParameter)(AEffect*, int32_t);
	int32_t numPrograms, numParams, numInputs, numOutputs, flags; intptr_t r1, r2;
	int32_t initialDelay, realQ, offQ; float ioRatio; void *object, *user; int32_t uniqueID, version;
	void (*processReplacing)(AEffect*, float**, float**, int32_t);
};
typedef intptr_t (*Host)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
static float gain = 0.5f, mute = 0;
static intptr_t disp(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
	(void)e; (void)v; (void)o;
	switch (op) {
	case 8: strcpy((char*)p, idx == 0 ? "Gain" : "Mute"); return 1;
	case 7: if (idx == 0) sprintf((char*)p, "%.2f", gain * 2); else strcpy((char*)p, mute > 0.5f ? "on" : "off"); return 1;
	case 6: strcpy((char*)p, idx == 0 ? "x" : ""); return 1;
	case 45: strcpy((char*)p, "Test VST gain"); return 1;
	case 47: strcpy((char*)p, "BeAE tests"); return 1;
	}
	return 0;
}
static void setp(AEffect *e, int32_t i, float v) { (void)e; if (i == 0) gain = v; else mute = v; }
static float getp(AEffect *e, int32_t i) { (void)e; return i == 0 ? gain : mute; }
static void proc(AEffect *e, float **in, float **out, int32_t n) {
	(void)e; float g = mute > 0.5f ? 0 : gain * 2;
	for (int c = 0; c < 2; c++) for (int k = 0; k < n; k++) out[c][k] = in[c][k] * g;
}
static AEffect fx;
AEffect *VSTPluginMain(Host h) {
	(void)h;
	memset(&fx, 0, sizeof(fx));
	fx.magic = 0x56737450; fx.dispatcher = disp; fx.setParameter = setp; fx.getParameter = getp;
	fx.numParams = 2; fx.numInputs = 2; fx.numOutputs = 2; fx.flags = 1 << 4; fx.processReplacing = proc; fx.process = proc;
	return &fx;
}
