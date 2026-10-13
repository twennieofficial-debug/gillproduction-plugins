#include "../../Source/Spectral/Spectral.h"
#include <chrono>
#include <iostream>
#include <vector>
int main(){using namespace gillstudio::spectral;for(auto mode:{Mode::Live,Mode::Pro})for(size_t block:{16,64,256}){Processor processor;processor.prepare(48000,block,2);Parameters p;p.mode=mode;p.resonance=.5;for(size_t i=0;i<12;++i){auto&n=p.nodes[i];n.enabled=true;n.frequency=40*std::pow(400.,i/11.);n.gainDb=3;n.q=1;}std::vector<float>l(block,.01f),r(block,.01f);float*data[]={l.data(),r.data()};const auto start=std::chrono::steady_clock::now();for(size_t s=0;s<48000;s+=block){processor.setParameters(p);processor.process(data,block);}double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cout<<(mode==Mode::Live?"LIVE":"PRO")<<" block="<<block<<" 12 nodes CPU seconds per audio second="<<elapsed<<"\n";}}
