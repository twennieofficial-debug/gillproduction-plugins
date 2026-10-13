#include "../../Source/Spectral/Spectral.h"
#include <fstream>
#include <iostream>
#include <vector>
using namespace gillstudio::spectral;
int main(int argc,char**argv){
 if(argc!=8){std::cerr<<"input.f32 output.f32 rate learnSamples live amount maxReduction\n";return 2;}
 std::ifstream input(argv[1],std::ios::binary|std::ios::ate);if(!input)return 3;const auto bytes=input.tellg();
 if(bytes<=0||bytes%sizeof(float)!=0)return 4;std::vector<float> data(std::size_t(bytes)/sizeof(float));input.seekg(0);input.read(reinterpret_cast<char*>(data.data()),bytes);if(!input)return 5;
 const double fs=std::stod(argv[3]);const auto learn=std::stoull(argv[4]);if(learn>=data.size())return 6;
 Processor processor;processor.prepare(fs,257,1,2048);Parameters p;p.mode=std::stoi(argv[5])?Mode::Live:Mode::Pro;
 p.denoise=std::stod(argv[6]);p.maxReductionDb=std::stod(argv[7]);p.lowHz=1200;p.highHz=9000;p.preserveTransient=.6;p.learn=true;processor.setParameters(p);processor.reset();
 std::size_t pos=0;while(pos<data.size()){
  if(pos==learn){p.learn=false;processor.setParameters(p);processor.reset();}
  const auto count=std::min<std::size_t>(257,std::min(data.size()-pos,pos<learn?learn-pos:data.size()-pos));float*ptr[]{data.data()+pos};processor.process(ptr,count);pos+=count;
 }
 std::ofstream output(argv[2],std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),bytes);if(!output)return 7;
 std::cout<<"latency="<<processor.latencySamples()<<" profile="<<processor.hasProfile()<<"\n";return processor.hasProfile()?0:8;
}
