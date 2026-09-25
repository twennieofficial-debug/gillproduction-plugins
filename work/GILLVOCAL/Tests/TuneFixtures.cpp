#include "../Source/TuneDSP.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

static void u16(std::ofstream& f,uint16_t x){f.put(static_cast<char>(x));f.put(static_cast<char>(x>>8));}
static void u32(std::ofstream& f,uint32_t x){u16(f,static_cast<uint16_t>(x));u16(f,static_cast<uint16_t>(x>>16));}
static void wav(const std::string& path,const std::vector<float>& samples,float gain){
    std::ofstream f(path,std::ios::binary);f.write("RIFF",4);u32(f,36+static_cast<uint32_t>(samples.size()*2));
    f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,1);u32(f,48000);u32(f,96000);u16(f,2);u16(f,16);
    f.write("data",4);u32(f,static_cast<uint32_t>(samples.size()*2));
    for(float x:samples)u16(f,static_cast<uint16_t>(static_cast<int16_t>(std::round(std::clamp(x*gain,-1.0f,1.0f)*32767))));
    if(!f)throw std::runtime_error("Could not write fixture: "+path);
}
int main(int argc,char** argv){
    const std::string directory=argc>1?argv[1]:".";
    constexpr int fs=48000,length=96000;
    constexpr double pi=3.14159265358979323846;
    constexpr int notes[]{57,60,64,62};
    constexpr double detune[]{37,-32,39,-40};
    std::vector<float> before(length);
    double phase=0;
    for(int n=0;n<length;++n){
        const int noteIndex=n/24000;const double within=(n%24000)/48000.0;
        const double hz=440*std::exp2((notes[noteIndex]-69)/12.0+(detune[noteIndex]+8*std::sin(2*pi*5*n/fs))/1200.0);
        phase+=2*pi*hz/fs;
        const double edge=std::min(1.0,std::min(within/0.012,(0.5-within)/0.012));
        const double envelope=std::sin(0.5*pi*std::max(0.0,edge));
        double x=0;
        for(int h=1;h<=20;++h){const double f=hz*h;
            const double formant=.18+2*std::exp(-std::pow((f-700)/220,2))+1.4*std::exp(-std::pow((f-1200)/300,2))+.8*std::exp(-std::pow((f-2500)/450,2));
            x+=std::sin(h*phase)*formant/h;}
        before[n]=static_cast<float>(.16*x*envelope);
    }
    gill::TuneDSP tune;tune.prepare(fs,128,1);tune.setParameters(0,0,0,0,100);
    std::vector<float> rendered(length+tune.latencySamples(),0);
    std::copy(before.begin(),before.end(),rendered.begin());
    for(int offset=0;offset<static_cast<int>(rendered.size());offset+=128){float* p[]{rendered.data()+offset};tune.process(p,1,std::min(128,static_cast<int>(rendered.size())-offset));}
    std::vector<float> after(rendered.begin()+tune.latencySamples(),rendered.end());
    float peak=0;for(float x:before)peak=std::max(peak,std::abs(x));for(float x:after)peak=std::max(peak,std::abs(x));
    const float gain=peak>0?.70f/peak:1;
    wav(directory+"/Tune-vowel-before.wav",before,gain);wav(directory+"/Tune-vowel-after.wav",after,gain);
    std::cout<<"Synthetic vowel fixtures: mono PCM16, 48000 Hz, exactly 2.000 seconds each. Common gain="<<gain
        <<", dry-aligned by removing "<<tune.latencySamples()<<" processing-delay samples from rendered result. No external voice recordings.\n";
}
