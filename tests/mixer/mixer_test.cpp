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
	m.Process(b,4,0,true);
	for(int i=0;i<8;i++) CHECK(b[i]==1.0f);
	// доп. дорожка: моно 0.5, 4 кадра, суммируется
	float d[4]={.5,.5,.5,.5};
	int t=m.AddTrack("t",d,4,1);
	CHECK(t==1 && m.TrackCount()==2);
	float c[8]={0}; m.Process(c,4,0,true);
	for(int i=0;i<8;i++) CHECK(fabsf(c[i]-.5f)<1e-6);
	// смещение позиции: старт с кадра 2 - только 2 кадра звука
	float e[8]={0}; m.Process(e,4,2,true);
	CHECK(e[0]==.5f && e[3]==.5f && e[4]==0 && e[7]==0);
	// pan вправо до упора: лево 0
	m.SetPan(t,1); float f[8]={0}; m.Process(f,4,0,true);
	CHECK(f[0]==0 && fabsf(f[1]-.5f)<1e-6);
	// mute
	m.SetPan(t,0); m.SetMute(t,true); float g[8]={0}; m.Process(g,4,0,true);
	CHECK(g[0]==0);
	// solo на доп. дорожке глушит основную
	m.SetMute(t,false); m.SetSolo(t,true);
	float h[8]={1,1,1,1,1,1,1,1}; m.Process(h,4,0,true);
	CHECK(fabsf(h[0]-.5f)<1e-6);
	// без mixExtras - только основная
	m.SetSolo(t,false); float k[8]={0}; m.Process(k,4,0,false);
	CHECK(k[0]==0);
	// громкость основной
	m.SetGain(0,0.5f); float l[8]={1,1,1,1,1,1,1,1}; m.Process(l,4,0,false);
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
	printf(fails? "FAILED %d\n":"all ok\n",fails);
	return fails!=0;
}
