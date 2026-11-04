#include "../../Core-src/Mixer.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(int argc,char**argv){
	int fails=0;
	Mixer m;
	CHECK(m.TrackCount()==1);
	// основная дорожка по умолчанию не меняет буфер
	float b[8]={1,1,1,1,1,1,1,1};
	m.Process(b,4,0,44100,44100,true);
	for(int i=0;i<8;i++) CHECK(b[i]==1.0f);
	// доп. дорожка: моно 0.5, 4 кадра, суммируется
	float d[4]={.5,.5,.5,.5};
	int t=m.AddTrack("t",d,4,1);
	CHECK(t==1 && m.TrackCount()==2);
	float c[8]={0}; m.Process(c,4,0,44100,44100,true);
	for(int i=0;i<8;i++) CHECK(fabsf(c[i]-.5f)<1e-6);
	// смещение позиции: старт с кадра 2 - только 2 кадра звука
	float e[8]={0}; m.Process(e,4,2,44100,44100,true);
	CHECK(e[0]==.5f && e[3]==.5f && e[4]==0 && e[7]==0);
	// pan вправо до упора: лево 0
	m.SetPan(t,1); float f[8]={0}; m.Process(f,4,0,44100,44100,true);
	CHECK(f[0]==0 && fabsf(f[1]-.5f)<1e-6);
	// mute
	m.SetPan(t,0); m.SetMute(t,true); float g[8]={0}; m.Process(g,4,0,44100,44100,true);
	CHECK(g[0]==0);
	// solo на доп. дорожке глушит основную
	m.SetMute(t,false); m.SetSolo(t,true);
	float h[8]={1,1,1,1,1,1,1,1}; m.Process(h,4,0,44100,44100,true);
	CHECK(fabsf(h[0]-.5f)<1e-6);
	// без mixExtras - только основная
	m.SetSolo(t,false); float k[8]={0}; m.Process(k,4,0,44100,44100,false);
	CHECK(k[0]==0);
	// громкость основной
	m.SetGain(0,0.5f); float l[8]={1,1,1,1,1,1,1,1}; m.Process(l,4,0,44100,44100,false);
	CHECK(fabsf(l[0]-.5f)<1e-6);
	m.SetGain(0,1);
	// запись: Append/Remove
	int r=m.AddEmptyTrack("rec",2); CHECK(r==2);
	float a2[4]={.1f,.2f,.3f,.4f};
	SourceRef sr2=MakeSource(a2,2,2,44100);
	CHECK(m.AddClip(r,sr2,0,2,0)==0 && m.ClipCount(r)==1);
	CHECK(m.AddClip(r,sr2,0,2,1.0)==1 && m.ClipCount(r)==2 && sr2.use_count()==3);	// два клипа - один источник
	m.RemoveTrack(r); CHECK(m.Track(r)==NULL); CHECK(sr2.use_count()==1);
	m.RemoveTrack(0); CHECK(m.Track(0)!=NULL);
	// предел числа дорожек
	int n=0; while(m.AddEmptyTrack("x",1)>=0) n++;
	CHECK(m.TrackCount()==MIX_MAX_TRACKS);
	// активная дорожка: смена, сохранение данных, удаление активной
	Mixer q;
	int a=q.AddEmptyTrack("a",2), b2=q.AddEmptyTrack("b",1);
	CHECK(a==1 && b2==2);
	float st[4]={.1f,.2f,.3f,.4f};
	CHECK(q.AddClip(0,MakeSource(st,2,2,44100),0,2,0)==0 && q.ClipCount(0)==1);
	q.SetActive(1); CHECK(q.Active()==1);
	// неактивная дорожка 0 играет, активная (1) - только баланс
	float o[4]={0}; q.Process(o,2,0,44100,44100,true);
	CHECK(fabsf(o[0]-.1f)<1e-6 && fabsf(o[3]-.4f)<1e-6);
	q.SetArmed(1,true); q.SetArmed(2,true);
	CHECK(q.ArmedTrack()==2 && !q.Track(1)->armed);
	q.RemoveTrack(1); CHECK(q.Active()==0);
	// другая частота: дорожка 22050 Гц на выходе 44100 -> каждый кадр дважды (линейная интерполяция)
	Mixer w; float ramp[4]={0,1,2,3};
	int rt=w.AddTrack("r",ramp,4,1,22050);
	float ob[16]={0}; w.Process(ob,8,0,44100,44100,true);
	CHECK(fabsf(ob[0]-0)<1e-6 && fabsf(ob[2]-0.5f)<1e-6 && fabsf(ob[4]-1)<1e-6);
	// смещения: дорожка с offset 1 с, активная без смещения
	Mixer mo; float one[4]={1,1,1,1};
	int ot=mo.AddTrack("o",one,4,1,4);	// 4 кадра при 4 Гц = 1 с
	CHECK(mo.SetClipPos(ot,0,1.0));
	CHECK(fabsf((float)mo.EndTimeExtras()-2.0f)<1e-6);
	float s1[8]={0}; mo.Process(s1,4,0,4,4,true);		// t=0..1 с -> тишина
	CHECK(s1[0]==0 && s1[6]==0);
	float s2[8]={0}; mo.Process(s2,4,4,4,4,true);		// t=1..2 с -> звук
	CHECK(fabsf(s2[0]-1)<1e-6 && fabsf(s2[6]-1)<1e-6);
	// клипы: разрезание - два клипа одного источника играют без шва
	{
		Mixer cm; float ramp8[8]={0,1,2,3,4,5,6,7};
		SourceRef rs=MakeSource(ramp8,8,1,8);					// 8 кадров при 8 Гц = 1 с
		int la=cm.AddEmptyTrack("c",1);
		CHECK(cm.AddClip(la,rs,0,4,0)==0 && cm.AddClip(la,rs,4,4,0.5)==1);	// левая и правая половины
		cm.SetActive(0);
		float whole[16]={0}; cm.Process(whole,8,0,8,8,true);
		// на активной дорожке 0 ничего нет, а дорожка la играет обе половины подряд
		float chk[16]={0}; cm.SetActive(la); cm.Process(chk,8,0,8,8,true);
		for(int i=0;i<8;i++) CHECK(fabsf(chk[2*i]-(float)i)<1e-5);
		CHECK(cm.ClipAt(la,0.25)==0 && cm.ClipAt(la,0.75)==1 && cm.ClipAt(la,1.5)==-1);
		// копии: три раза подряд один и тот же кусок
		Mixer km; float blip[4]={1,1,1,1};
		SourceRef bs=MakeSource(blip,4,1,4);						// 1 с при 4 Гц
		int kl=km.AddEmptyTrack("k",1);
		for(int i=0;i<3;i++) CHECK(km.AddClip(kl,bs,0,4,2.0*i)>=0);	// в 0, 2 и 4 с
		CHECK(bs.use_count()==4 && fabsf((float)km.EndTimeExtras()-5.0f)<1e-6);
		float k1[8]={0}; km.Process(k1,4,8,4,4,true);				// t=2..3 с: вторая копия
		CHECK(fabsf(k1[0]-1)<1e-6 && fabsf(k1[6]-1)<1e-6);
		float k2[8]={0}; km.Process(k2,4,4,4,4,true);				// t=1..2 с: пауза между копиями
		CHECK(k2[0]==0 && k2[6]==0);
		// клип уходит в Pool: TakeClip убирает его из микшера, ссылка на источник остаётся у забравшего
		MixClip took; CHECK(km.TakeClip(kl,1,took) && km.ClipCount(kl)==2 && took.src==bs);
		// диапазон клипа
		CHECK(km.SetClipRange(kl,0,1,2) && km.Clip(kl,0)->start==1 && km.Clip(kl,0)->len==2);
		CHECK(!km.SetClipRange(kl,0,3,2));						// за пределами источника
		// другой клип на активной дорожке складывается с буфером активного клипа
		Mixer am; float one4[4]={.25f,.25f,.25f,.25f};
		am.AddClip(0,MakeSource(one4,4,1,4),0,4,0);
		float ab[8]={1,1,1,1,1,1,1,1}; am.Process(ab,4,0,4,4,true);
		CHECK(fabsf(ab[0]-1.25f)<1e-6);
		// ResetAll освобождает клипы
		SourceRef rr=MakeSource(one4,4,1,4); am.AddClip(0,rr,0,4,1.0); CHECK(rr.use_count()==2);
		am.ResetAll(); CHECK(rr.use_count()==1 && am.ClipCount(0)==0);
	}
	// сетка: темп 120, глубина 1/4 -> шаг 0.5 с; привязка только у дорожки с Grid
	{
		Mixer gm; gm.AddEmptyTrack("g",1);
		CHECK(gm.Bpm()==120.0 && gm.GridDiv()==4 && fabs(gm.GridStep()-0.5)<1e-9);
		CHECK(fabs(gm.Snap(0,0.74)-0.74)<1e-9);					// Grid выключен
		gm.SetGrid(0,true);
		CHECK(fabs(gm.Snap(0,0.74)-0.5)<1e-9 && fabs(gm.Snap(0,0.76)-1.0)<1e-9);
		gm.SetGridDiv(16); CHECK(fabs(gm.GridStep()-0.125)<1e-9);
		gm.SetGridDiv(1);  CHECK(fabs(gm.GridStep()-2.0)<1e-9);
		gm.SetGridDiv(7);  CHECK(gm.GridDiv()==1);					// недопустимая глубина игнорируется
		gm.SetBpm(60); gm.SetGridDiv(4); CHECK(fabs(gm.GridStep()-1.0)<1e-9);
		gm.SetBpm(1000); CHECK(gm.Bpm()==300.0); gm.SetBpm(1); CHECK(gm.Bpm()==20.0);
		CHECK(gm.Snap(0,-0.3)==0);
		gm.ResetAll(); CHECK(gm.Bpm()==120.0 && gm.GridDiv()==4 && !gm.Track(0)->grid);
	}
	// эффекты: плагин amp (из tests/ladspa) на неактивной и на активной дорожке
	if(argc>1){
		std::vector<std::string> dirs(1,argv[1]);
		std::vector<Ladspa::Plugin> pl=Ladspa::Scan(dirs);
		const Ladspa::Plugin *amp=NULL;
		for(size_t i=0;i<pl.size();i++) if(pl[i].label=="amp") amp=&pl[i];
		CHECK(amp!=NULL);
		if(amp){
			Mixer fm; float dd[8]={1,1,1,1,1,1,1,1};
			int ft=fm.AddTrack("f",dd,8,1);
			std::string err;
			CHECK(fm.Fx(ft).Add(*amp,44100,&err));
			CHECK(fm.Fx(ft).Count()==1 && fm.Fx(ft).HasEffects());
			int gport=amp->ControlInputs()[0];
			fm.Fx(ft).SetValue(0,gport,0.25f);
			float o1[8]={0}; fm.Process(o1,4,0,44100,44100,true);
			CHECK(fabsf(o1[0]-0.25f)<1e-6 && fabsf(o1[7]-0.25f)<1e-6);
			fm.Fx(ft).SetEnabled(0,false);
			float o2[8]={0}; fm.Process(o2,4,0,44100,44100,true);
			CHECK(fabsf(o2[0]-1.0f)<1e-6);
			fm.Fx(ft).SetEnabled(0,true);
			// цепочка: два плагина подряд = 0.25*0.25
			CHECK(fm.Fx(ft).Add(*amp,44100,&err));
			fm.Fx(ft).SetValue(1,gport,0.5f);
			float o3[8]={0}; fm.Process(o3,4,0,44100,44100,true);
			CHECK(fabsf(o3[0]-0.125f)<1e-6);
			CHECK(fm.Fx(ft).Move(1,-1) && fm.Fx(ft).At(0)->values.at(gport)==0.5f);
			fm.Fx(ft).Remove(0); fm.Fx(ft).Remove(0);
			CHECK(fm.Fx(ft).Count()==0 && !fm.Fx(ft).HasEffects());
			// на активной дорожке: буфер уже заполнен
			fm.Fx(0).Add(*amp,44100,&err); fm.Fx(0).SetValue(0,gport,0.5f);
			float o4[8]={1,1,1,1,1,1,1,1}; fm.Process(o4,4,0,44100,44100,false);
			CHECK(fabsf(o4[0]-0.5f)<1e-6);
			// удаление дорожки очищает её цепочку
			fm.Fx(ft).Add(*amp,44100,&err); fm.RemoveTrack(ft);
			CHECK(fm.Fx(ft).Count()==0);
		}
	}
	printf(fails? "FAILED %d\n":"all ok\n",fails);
	return fails!=0;
}
