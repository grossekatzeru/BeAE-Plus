#include "../../Core-src/Mixer.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(){
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
	CHECK(m.AppendTrackData(r,a2,2)); CHECK(m.Track(r)->frames==2);
	CHECK(m.AppendTrackData(r,a2,2)); CHECK(m.Track(r)->frames==4);
	m.RemoveTrack(r); CHECK(m.Track(r)==NULL);
	m.RemoveTrack(0); CHECK(m.Track(0)!=NULL);
	// предел числа дорожек
	int n=0; while(m.AddEmptyTrack("x",1)>=0) n++;
	CHECK(m.TrackCount()==MIX_MAX_TRACKS);
	// активная дорожка: смена, сохранение данных, удаление активной
	Mixer q;
	int a=q.AddEmptyTrack("a",2), b2=q.AddEmptyTrack("b",1);
	CHECK(a==1 && b2==2);
	float st[4]={.1f,.2f,.3f,.4f};
	CHECK(q.SetTrackData(0,st,2,2,44100) && q.Track(0)->frames==2);
	q.SetActive(1); CHECK(q.Active()==1);
	// неактивная дорожка 0 играет, активная (1) - только баланс
	float o[4]={0}; q.Process(o,2,0,44100,44100,true);
	CHECK(fabsf(o[0]-.1f)<1e-6 && fabsf(o[3]-.4f)<1e-6);
	q.SetArmed(1,true); q.SetArmed(2,true);
	CHECK(q.ArmedTrack()==2 && !q.Track(1)->armed);
	q.RemoveTrack(1); CHECK(q.Active()==0);
	// другая частота: дорожка 22050 Гц на выходе 44100 -> каждый кадр дважды (линейная интерполяция)
	Mixer w; float ramp[4]={0,1,2,3};
	int rt=w.AddTrack("r",ramp,4,1); w.Track(rt)->rate=22050;
	float ob[16]={0}; w.Process(ob,8,0,44100,44100,true);
	CHECK(fabsf(ob[0]-0)<1e-6 && fabsf(ob[2]-0.5f)<1e-6 && fabsf(ob[4]-1)<1e-6);
	// смещения: дорожка с offset 1 с, активная без смещения
	Mixer mo; float one[4]={1,1,1,1};
	int ot=mo.AddTrack("o",one,4,1); mo.Track(ot)->rate=4;	// 4 кадра при 4 Гц = 1 с
	mo.SetOffset(ot,1.0);
	CHECK(fabsf((float)mo.EndTimeExtras()-2.0f)<1e-6);
	float s1[8]={0}; mo.Process(s1,4,0,4,4,true);		// t=0..1 с -> тишина
	CHECK(s1[0]==0 && s1[6]==0);
	float s2[8]={0}; mo.Process(s2,4,4,4,4,true);		// t=1..2 с -> звук
	CHECK(fabsf(s2[0]-1)<1e-6 && fabsf(s2[6]-1)<1e-6);
	mo.SetActive(0); mo.SetOffset(0,1.0);				// у активной смещение 1 с: t=1..2 при activePos 0
	float s3[8]={0}; mo.Process(s3,4,0,4,4,true);
	CHECK(fabsf(s3[0]-1)<1e-6);
	printf(fails? "FAILED %d\n":"all ok\n",fails);
	return fails!=0;
}
