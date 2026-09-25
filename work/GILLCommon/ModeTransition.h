#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace gill {
// A finite 5 ms continuity ramp around a quality change. No delay or allocation.
// Host PDC still needs to resynchronise when the algorithmic latency changes.
class ModeTransition {
public:
 void prepare(double rate,int mode)noexcept{length_=std::max(1,int(rate*.005));mode_=mode;remaining_=0;last_={};anchor_={};}
 template<class T>void process(T*const*audio,int channels,int frames,int mode,bool enabled=true)noexcept{
  if(!audio||channels<=0||frames<=0)return;channels=std::min(channels,2);
  if(mode!=mode_){mode_=mode;if(enabled){anchor_=last_;remaining_=length_;}}
  for(int i=0;i<frames;++i){const double blend=remaining_>0?double(remaining_)/length_:0;
   for(int c=0;c<channels;++c){const double value=std::isfinite(double(audio[c][i]))?double(audio[c][i]):0;
    const double out=blend?value+(anchor_[c]-value)*blend:value;audio[c][i]=T(out);last_[c]=out;}
   if(remaining_>0)--remaining_;
  }
 }
private:std::array<double,2>last_{},anchor_{};int mode_=1,length_=240,remaining_=0;
};
}
