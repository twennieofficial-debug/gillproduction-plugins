#include "../Source/TuneDSP.h"
#define gill gill_v020
#include "TuneDSP-v020-reference.h"
#undef gill
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

// Original synthetic test voices. All four alternatives use one shared gain,
// and every rendered file has only its reported initial delay removed.
constexpr double pi=3.14159265358979323846;
constexpr int fs=48000,length=96000;
static void u16(std::ofstream& f,uint16_t x){f.put(static_cast<char>(x));f.put(static_cast<char>(x>>8));}
static void u32(std::ofstream& f,uint32_t x){u16(f,static_cast<uint16_t>(x));u16(f,static_cast<uint16_t>(x>>16));}
static void wav(const std::string& path,const std::vector<float>& samples,float gain){
    std::ofstream f(path,std::ios::binary);f.write("RIFF",4);u32(f,36+static_cast<uint32_t>(samples.size()*2));
    f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,1);u32(f,fs);u32(f,fs*2);u16(f,2);u16(f,16);
    f.write("data",4);u32(f,static_cast<uint32_t>(samples.size()*2));
    for(float x:samples)u16(f,static_cast<uint16_t>(static_cast<int16_t>(std::round(std::clamp(x*gain,-1.0f,1.0f)*32767))));
    if(!f)throw std::runtime_error("Could not write fixture: "+path);
}
std::vector<float> source(int kind){
    std::vector<float> result(length);double phase=0;
    constexpr int notes[]{57,60,64,62};constexpr double detune[]{37,-32,39,-40};
    constexpr double amplitudes[]{.35,.27,.18,.13,.07};
    for(int n=0;n<length;++n){
        const double t=n/static_cast<double>(fs),within=(n%24000)/static_cast<double>(fs);
        const double hz=kind==0?230:kind==1?220*std::exp2((20+30*std::sin(2*pi*5*t))/1200):440*std::exp2((notes[n/24000]-69)/12.0+(detune[n/24000]+8*std::sin(2*pi*5*t))/1200);
        phase+=2*pi*hz/fs;double x=0;
        if(kind<2){for(int h=1;h<=5;++h)x+=amplitudes[h-1]*std::sin(h*phase);x*=.4;}
        else {for(int h=1;h<=20;++h){const double f=hz*h;const double formant=.18+2*std::exp(-std::pow((f-700)/220,2))+1.4*std::exp(-std::pow((f-1200)/300,2))+.8*std::exp(-std::pow((f-2500)/450,2));x+=std::sin(h*phase)*formant/h;}x*=.16;}
        const double edge=std::min(1.,std::min((kind<2?t:within)/.012,(kind<2?2-t:.5-within)/.012));
        result[n]=static_cast<float>(x*std::sin(.5*pi*std::max(0.,edge)));
    }return result;
}
template<class DSP> std::vector<float> render(DSP& dsp,const std::vector<float>& input,int kind){
    dsp.prepare(fs,127,1);dsp.setParameters(kind<2?9:0,kind<2?2:0,0,0,100);
    std::vector<float> result(input.size()+dsp.latencySamples());std::copy(input.begin(),input.end(),result.begin());
    for(int at=0;at<static_cast<int>(result.size());at+=127){float* p[]{result.data()+at};dsp.process(p,1,std::min(127,static_cast<int>(result.size())-at));}
    return {result.begin()+dsp.latencySamples(),result.end()};
}
int main(int argc,char** argv){
    const std::string directory=argc>1?argv[1]:".";const char* names[]{"01-230HZ-HARD","02-VIBRATO-HARD","03-VOWEL-PHRASE"};
    for(int kind=0;kind<3;++kind){
        const auto input=source(kind);gill_v020::TuneDSP old;gill::TuneDSP studio,live;live.setQualityMode(1);
        const auto previous=render(old,input,kind),s=render(studio,input,kind),l=render(live,input,kind);
        float peak=0;for(const auto* v:{&input,&previous,&s,&l})for(float x:*v)peak=std::max(peak,std::abs(x));const float gain=peak>0?.70f/peak:1;
        const std::string base=directory+"/"+names[kind];wav(base+"-INPUT.wav",input,gain);wav(base+"-OLD-020.wav",previous,gain);wav(base+"-STUDIO-030.wav",s,gain);wav(base+"-LIVE-030.wav",l,gain);
        std::cout<<names[kind]<<": 4 files; mono PCM16 48000 Hz, 2 s; shared gain="<<gain<<"; RETUNE=0 HUMANIZE=0 MIX=100; aligned delays removed OLD="<<old.latencySamples()<<" STUDIO="<<studio.latencySamples()<<" LIVE="<<live.latencySamples()<<" samples\n";
    }
    std::cout<<"Original synthetic signals, no real singer and no listening-quality guarantee. No independent loudness normalization or post-processing.\n";
}
