#include "../../Core-src/ProjectIO.h"
#include <stdio.h>
#include <math.h>
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(){
	int fails=0;
	// JSON
	std::string err; JValue v;
	std::string doc = "{\n \"format\":\"BeMIX\", \"version\":1, \"name\":\"\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB5\xD0\xBA\xD1\x82 \\\"1\\\"\",\n"
		" \"tracks\":[{\"name\":\"\\u0413\\u0438\\u0442\\u0430\\u0440\\u0430\",\"file\":null,\"offset\":1.25,\"gain\":0.5,\"mute\":true,\"effects\":[]}]}";
	CHECK(JsonParse(doc, v, err));
	CHECK(v.String("format") == "BeMIX");
	CHECK(v.String("name") == "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB5\xD0\xBA\xD1\x82 \"1\"");
	const JValue *tr = v.Get("tracks");
	CHECK(tr && tr->type==JValue::Arr && tr->a.size()==1);
	CHECK(tr->a[0].String("name") == "\xD0\x93\xD0\xB8\xD1\x82\xD0\xB0\xD1\x80\xD0\xB0");
	CHECK(tr->a[0].Number("offset",0)==1.25 && tr->a[0].Boolean("mute",false)==true);
	CHECK(tr->a[0].Get("file")->type==JValue::Null);
	CHECK(!JsonParse("{\"a\":", v, err));
	CHECK(!JsonParse("{} x", v, err));
	CHECK(JsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
	// WAV
	float d[8]={0,.5f,-.5f,1,-1,.25f,0,.125f};
	CHECK(WavWriteFloat("/tmp/claude-0/-home-claude/81642f1a-7c2f-5739-ac37-24dba7c31b09/scratchpad/t.wav", d, 4, 2, 48000));
	std::vector<float> r; long long fr; int ch; float rate;
	CHECK(WavRead("/tmp/claude-0/-home-claude/81642f1a-7c2f-5739-ac37-24dba7c31b09/scratchpad/t.wav", r, fr, ch, rate, err));
	CHECK(fr==4 && ch==2 && rate==48000);
	for(int i=0;i<8;i++) CHECK(r[i]==d[i]);
	CHECK(!WavRead("/nonexistent.wav", r, fr, ch, rate, err));
	printf(fails?"FAILED %d\n":"all ok\n",fails);
	return fails!=0;
}
