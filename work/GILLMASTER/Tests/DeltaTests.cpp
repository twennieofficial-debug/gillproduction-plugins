#include "../Source/DeltaEngine.h"
#include "TestSupport.h"
using namespace gill::master;
void estimate(){std::uint32_t seed=1701;auto noise=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return (double(seed)/4294967295.-.5)*.3;};
    for(int delay:{0,1,17,511,4096,12000}){const int maximum=12000,count=16000;std::vector<double>pre(count+maximum),post(count);for(auto&x:pre)x=noise();for(int i=0;i<count;++i)post[i]=pre[maximum-delay+i]*1.7;auto a=DeltaEngine::estimate(pre,post,maximum);test::check(a.valid&&a.delay==delay&&std::abs(a.gain-1.7)<1e-9&&a.confidence>.999,"DELTA recovers exact delay and gain independently",a.delay);}
    std::vector<double>a(3000),b(2000);test::check(!DeltaEngine::estimate(a,b,1000).valid,"DELTA refuses to align silence");for(auto&v:a)v=noise();for(auto&v:b)v=noise();test::check(!DeltaEngine::estimate(a,b,1000).valid,"DELTA refuses unrelated signals");
}
void linked(){
    DeltaEngine source,ret,duplicate;source.configure(true,0);ret.configure(false,0);duplicate.configure(true,0);source.prepare(48000);ret.prepare(48000);duplicate.prepare(48000);ret.learn();
    constexpr int block=128,lag=377;std::vector<float>delay(lag);int pos=0;std::uint32_t seed=763;bool sourceDry=true;double nullPeak=0;
    for(int round=0;round<1600;++round){std::array<float,block>l{},r{},original{};for(int i=0;i<block;++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;l[i]=r[i]=original[i]=float((double(seed)/4294967295.-.5)*.2);}float*ptr[]{l.data(),r.data()};source.process(ptr,2,block,0,true,false,false);sourceDry=sourceDry&&l==original;
        for(int i=0;i<block;++i){const auto out=delay[pos]*1.5f;delay[pos]=l[i];pos=(pos+1)%lag;l[i]=r[i]=out;}ret.process(ptr,2,block,round>1200?2:0,true,false,false);
        if(round%10==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(round>1400)for(auto x:l)nullPeak=std::max(nullPeak,std::abs(double(x)));
        if(round==10){duplicate.process(ptr,2,block,0,true,false,false);test::check(duplicate.conflict(),"DELTA duplicate SOURCE cannot overwrite a live link");}
    }
    auto a=ret.alignment();test::check(sourceDry,"DELTA SOURCE is bit-exact passthrough");test::check(a.valid&&a.delay==lag&&std::abs(a.gain-1.5)<1e-5,"DELTA two real instances learn through the stamped shared ring",a.delay);test::check(a.valid&&nullPeak<1e-5,"DELTA matched delayed chain nulls below -100 dBFS",nullPeak);
    std::array<float,block>l{},r{};l.fill(.2f);r=l;float*ptr[]{l.data(),r.data()};source.process(ptr,2,block,0,true,false,false);ret.process(ptr,2,block,2,true,true,false);test::check(l[0]==.2f&&r[0]==.2f,"DELTA offline render always returns POST");
}
int main(){estimate();linked();return test::result();}
