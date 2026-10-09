#include "CreativeDSP.h"
#include <cstdio>
#include <fstream>
#include <string>
using namespace gill::creative;
static int failures=0;
void check(bool ok,const char* what,double value){printf("%s %s %.12g\n",ok?"PASS":"FAIL",what,value);if(!ok)++failures;}
void wav(const std::string& path,const std::vector<float>& a){std::ofstream o(path,std::ios::binary);auto put=[&](uint32_t x,int n){for(int i=0;i<n;++i)o.put(char(x>>(8*i)));};o.write("RIFF",4);put(36+a.size()*4,4);o.write("WAVEfmt ",8);put(16,4);put(3,2);put(1,2);put(48000,4);put(192000,4);put(4,2);put(32,2);o.write("data",4);put(a.size()*4,4);o.write(reinterpret_cast<const char*>(a.data()),a.size()*4);}
int main(int argc,char**argv){std::string prefix=argc>1?argv[1]:"after";for(auto k:{Kind::Phrase,Kind::Director,Kind::Reply}){
 const std::string name=k==Kind::Phrase?"GILLPHRASE":k==Kind::Director?"GILLDIRECTOR":"GILLREPLY";
 Engine e(k);e.prepare(48000);Controls c;c.bypass=true;c.dry=0;c.mix=1;Transport t;t.hasPPQ=t.hasSeconds=true;
 std::vector<float>l(12000),r(12000),src(12000);for(int i=0;i<12000;++i)src[i]=l[i]=r[i]=.2f*std::sin(2*pi*173*(i+400)/48000.);
 double error=0;for(int at=0;at<12000;at+=127){int n=std::min(127,12000-at);t.ppq=at/24000.;t.seconds=at/48000.;e.process(l.data()+at,r.data()+at,nullptr,nullptr,n,c,t);for(int i=at;i<at+n;++i)error=std::max(error,double(std::abs(l[i]-src[i])));}
 check(error==0,(name+" first-block bypass exact").c_str(),error);wav(prefix+"-"+name+"-bypass.wav",l);wav(prefix+"-"+name+"-source.wav",src);
 // A later toggle must retain a ramp; resets/seeks must not turn it into a jump.
 c.bypass=false;float a=.2f,b=.2f;t.seconds=.25;t.ppq=.5;e.process(&a,&b,nullptr,nullptr,1,c,t);check(a>.19f&&(k==Kind::Director||a<.2f),(name+" bypass off remains smoothed").c_str(),a);
 e.resetAudio();c.bypass=true;a=b=.2f;t.seconds=50;t.ppq=100;e.process(&a,&b,nullptr,nullptr,1,c,t);check(a>.19f,(name+" reset keeps established bypass ramp").c_str(),a);
 e.prepare(48000);c.bypass=false;c.mix=0;c.dry=0;a=b=.2f;e.process(&a,&b,nullptr,nullptr,1,c,t);check(a==0,(name+" saved zero-dry starts silent").c_str(),a);
 // Seconds-only hosts legitimately change BPM; this must not truncate capture.
 e.prepare(48000);e.request(1);t.hasPPQ=false;t.hasSeconds=true;t.playing=true;
 for(int at=0;at<96000;at+=127){int n=std::min(127,96000-at);t.seconds=12+at/48000.;t.bpm=at<48000?120:173;for(int i=0;i<n;++i){double s=(at+i)/48000.,phase=std::fmod(s,1.);l[i]=r[i]=phase>.08&&phase<.6?float(.2*std::sin(2*pi*173*s)):0;}e.process(l.data(),r.data(),nullptr,nullptr,n,c,t);}
 t.seconds=14;t.playing=false;a=b=0;e.process(&a,&b,nullptr,nullptr,1,c,t);auto p=e.snapshot();check(p.count==2&&std::abs(p.durationSeconds-2)<.0001,(name+" continuous seconds BPM change captures whole take").c_str(),p.durationSeconds);
 if(k==Kind::Reply){e.request(3);c.amount=1;c.length=.32f;c.mix=1;c.pro=false;double energy[2]{};for(int pass=0;pass<2;++pass){t.playing=true;t.bpm=pass?173:120;for(int at=0;at<144000;at+=127){int n=std::min(127,144000-at);t.seconds=12+at/48000.;std::fill(l.begin(),l.end(),0.f);std::fill(r.begin(),r.end(),0.f);e.process(l.data(),r.data(),nullptr,nullptr,n,c,t);for(int i=0;i<n;++i)energy[pass]+=l[i]*l[i]+r[i]*r[i];}}check(energy[0]>1.e-4&&std::abs(energy[0]-energy[1])<1.e-7,"time-anchored reply gap scheduling is independent of changed host BPM",energy[0]-energy[1]);t.playing=false;t.seconds=12.749;std::fill(l.begin(),l.end(),0.f);std::fill(r.begin(),r.end(),0.f);e.process(l.data(),r.data(),nullptr,nullptr,12000,c,t);double stopped=0;for(float x:l)stopped+=x*x;check(stopped==0,"seconds-only stopped transport cannot trigger captured replies",stopped);}
 // Real discontinuity remains a seek and finalizes the contiguous segment.
 e.request(1);t.playing=true;t.seconds=20;for(int at=0;at<24000;at+=127){int n=std::min(127,24000-at);t.seconds=20+at/48000.;for(int i=0;i<n;++i)l[i]=r[i]=float(.2*std::sin(2*pi*173*(at+i)/48000.));e.process(l.data(),r.data(),nullptr,nullptr,n,c,t);}t.seconds=80;a=b=0;e.process(&a,&b,nullptr,nullptr,1,c,t);p=e.snapshot();check(p.valid()&&std::abs(p.durationSeconds-.5)<.0001,(name+" actual time seek finalizes capture").c_str(),p.durationSeconds);
 }
 printf("EDGE_RESULT failures=%d\n",failures);return failures?1:0;
}
