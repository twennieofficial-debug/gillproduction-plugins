#include SPECTRAL_HEADER
#include <iostream>
#include <stdexcept>
#include <cstring>
namespace sp=gillstudio::spectral;
int checks=0;void require(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
struct Render {std::vector<float> audio; double energy=0;};
Render render(int mode,double fs,int part,int shape,bool external=true,bool resonance=false,int mainChannels=1){
 const std::size_t n=std::size_t(fs*.45);std::array<std::vector<float>,2>a,k;
 for(auto&v:a)v.resize(n);for(auto&v:k)v.resize(n);
 for(std::size_t i=0;i<n;++i){const float x=float(.2*std::sin(2*sp::pi*3000*i/fs));a[0][i]=shape==6?0:x;a[1][i]=a[0][i];const float key=float(.3*std::sin(2*sp::pi*3000*i/fs));k[0][i]=shape==2||shape==4?0:key;k[1][i]=shape==1?-key:shape==3||shape==4?0:key;}
 sp::Parameters p;p.mode=sp::Mode(mode);p.externalKey=external;p.levelStrength=resonance?0:.8;p.levelThresholdDb=-35;p.resonance=resonance?.6:0;p.resonanceThresholdDb=6;p.maxReductionDb=30;p.attackMs=.05;p.releaseMs=5;
 sp::Processor d;d.prepare(fs,1024,mainChannels);d.setParameters(p);
 for(std::size_t i=0;i<n;){const auto count=std::min<std::size_t>(part,n-i);float* audio[]={a[0].data()+i,a[1].data()+i};const float* key[]={k[0].data()+i,k[1].data()+i};d.process(audio,count,shape==5?nullptr:key,shape==5?0:shape==7?1:2);i+=count;}
 Render r;r.audio=std::move(a[0]);for(std::size_t i=n/2;i<n;++i){require(std::isfinite(r.audio[i]),"nonfinite output");r.energy+=double(r.audio[i])*r.audio[i];}return r;
}
int main(){try{
 for(int mode:{0,1})for(double fs:{44100.,48000.,96000.})for(bool resonance:{false,true}){
  auto regular=render(mode,fs,257,0,true,resonance);auto anti=render(mode,fs,257,1,true,resonance);auto right=render(mode,fs,257,2,true,resonance);auto left=render(mode,fs,257,3,true,resonance);auto silent=render(mode,fs,257,4,true,resonance);auto mono=render(mode,fs,257,7,true,resonance);
  require(regular.energy<silent.energy*.85,"connected key has no positive reduction control");
  require(regular.audio==anti.audio,"anti-phase stereo detector cancelled in mono main");require(regular.audio==right.audio,"right-only detector attenuated in mono main");require(regular.audio==left.audio,"left-only detector attenuated in mono main");require(regular.audio==mono.audio,"mono key does not equal identical stereo key");
  for(int block:{1,17,1024})require(render(mode,fs,block,1,true,resonance).audio==anti.audio,"key output depends on host block partition");
  require(render(mode,fs,257,6,true,resonance).energy==0,"external key leaked to silent main");require(render(mode,fs,257,5,true,resonance).audio==render(mode,fs,257,0,false,resonance).audio,"absent key is not main fallback");
 }
 std::cout<<"SPECTRAL_MONO_KEY_PASS "<<checks<<" checks\n";
 }catch(const std::exception&e){std::cerr<<"SPECTRAL_MONO_KEY_FAIL "<<e.what()<<" checks="<<checks<<"\n";return 1;}}
