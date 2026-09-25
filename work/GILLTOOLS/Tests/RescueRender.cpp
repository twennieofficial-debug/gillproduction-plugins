#include "../Source/RescueDSP.h"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <cstdlib>
int main(int argc,char**argv){
    if(argc!=7){std::fprintf(stderr,"input.f32 output.f32 rate clipDb negativeDb live\n");return 2;}
    std::ifstream in(argv[1],std::ios::binary);std::vector<char>bytes((std::istreambuf_iterator<char>(in)),{});
    if(bytes.empty()||bytes.size()%4)return 3;
    const double rate=std::strtod(argv[3],nullptr);if(rate<8000||rate>192000)return 4;
    gill::tools::RescueParameters p;p.outputDb=0;p.clipDb=float(std::strtod(argv[4],nullptr));p.negativeClipDb=float(std::strtod(argv[5],nullptr));
    gill::tools::RescueDSP engine;engine.setLiveMode(std::atoi(argv[6])!=0);engine.setParameters(p);engine.prepare(rate,127,1);
    const auto frames=bytes.size()/4;std::vector<float>audio(frames+engine.latencySamples());std::memcpy(audio.data(),bytes.data(),bytes.size());
    for(int n=0;n<int(audio.size());n+=127){float*ch[]{audio.data()+n};engine.process(ch,1,std::min(127,int(audio.size())-n));}
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(audio.data()+engine.latencySamples()),std::streamsize(frames*4));
    std::printf("latency=%d repaired=%u rejected=%u\n",engine.latencySamples(),engine.repairs(),engine.rejected());return out?0:5;
}
