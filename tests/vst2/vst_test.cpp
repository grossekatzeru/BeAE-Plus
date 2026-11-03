#include "../../Core-src/Vst2Host.h"
#include "../../Core-src/Mixer.h"
#include <stdio.h>
#include <math.h>
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(int argc, char **argv){
	int fails=0;
	std::vector<std::string> dirs(1, argv[1]);
	std::vector<Vst2::Info> l = Vst2::Scan(dirs);
	CHECK(l.size()==1 && l[0].name=="Test VST gain" && l[0].numParams==2 && !l[0].synth);
	if(l.empty()) return 1;
	Vst2::Instance v; std::string err;
	CHECK(v.Load(l[0].file,&err) && v.Start(44100,1024));
	CHECK(v.ParamName(0)=="Gain" && v.ParamDisplay(0)=="1.00");
	v.SetParam(0,0.25f);
	CHECK(v.ParamDisplay(0)=="0.50");
	float b[6000*2]; for(int i=0;i<6000*2;i++) b[i]=1;
	v.Process(b,6000);				// больше блока: идёт по кускам
	CHECK(fabsf(b[0]-0.5f)<1e-6 && fabsf(b[11999]-0.5f)<1e-6);
	// цепочка и микшер
	Mixer m; float d[8]={1,1,1,1,1,1,1,1};
	int t=m.AddTrack("v",d,8,1);
	CHECK(m.Fx(t).AddVst(l[0].file,44100,&err));
	CHECK(m.Fx(t).At(0)->kind==FxChain::KIND_VST2 && m.Fx(t).At(0)->values.size()==2);
	m.Fx(t).SetValue(0,0,0.25f);
	float o[8]={0}; m.Process(o,4,0,44100,44100,true);
	CHECK(fabsf(o[0]-0.5f)<1e-6);
	m.Fx(t).SetValue(0,1,1.0f);					// mute-параметр плагина
	float o2[8]={0}; m.Process(o2,4,0,44100,44100,true);
	CHECK(o2[0]==0);
	FxChain c2; CHECK(c2.AddCopyOf(*m.Fx(t).At(0),44100,&err) && c2.Count()==1 && c2.At(0)->values.at(0)==0.25f);
	m.RemoveTrack(t); CHECK(m.Fx(t).Count()==0);
	printf(fails?"FAILED %d\n":"all ok\n",fails);
	return fails!=0;
}
