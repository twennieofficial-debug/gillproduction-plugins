#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace studio {
// Up to 8x, linear-phase polyphase interpolation and matching decimator.
// 64*factor+1 taps: 32 + 32 base-rate samples of bulk delay.
class Antialias19 {
public:
  static constexpr int maxFactor = 8, maxTaps = 513, latency = 64;
  void prepare(int requestedFactor=8) noexcept {
    factor=requestedFactor; taps=64*factor+1;
    h.fill(0);
    double sum = 0;
    for (int i=0; i<taps; ++i) {
      const double n = i-(taps-1)*.5, cutoff=.475/factor;
      const double sinc = n == 0 ? 2*cutoff : std::sin(2*3.14159265358979323846*cutoff*n)/(3.14159265358979323846*n);
      const double phase=2*3.14159265358979323846*i/(taps-1);
      h[i]=sinc*(.35875-.48829*std::cos(phase)+.14128*std::cos(2*phase)-.01168*std::cos(3*phase));
      sum+=h[i];
    }
    for(auto &v:h) v/=sum;
    reset();
  }
  void reset() noexcept { input={}; key={}; output={}; inPos=outPos=0; }
  template<class F> std::array<double,2> tick(std::array<double,2> x,std::array<double,2> k,F&& process) noexcept {
    input[inPos]=x; key[inPos]=k;
    std::array<double,2> result{};
    for(int phase=0;phase<factor;++phase) {
      std::array<double,2> up{},uk{};
      for(int j=phase;j<taps;j+=factor) {
        const auto p=(inPos+input.size()-size_t(j/factor))%input.size();
        for(int c=0;c<2;++c) {up[c]+=factor*h[j]*input[p][c]; uk[c]+=factor*h[j]*key[p][c];}
      }
      output[outPos]=process(up,uk);
      // Output phase zero makes the combined group delay exactly 64 samples.
      if(phase==0) for(int j=0;j<taps;++j) {
        const auto p=(outPos+output.size()-size_t(j))%output.size();
        for(int c=0;c<2;++c) result[c]+=h[j]*output[p][c];
      }
      outPos=(outPos+1)%output.size();
    }
    inPos=(inPos+1)%input.size();
    return result;
  }
private:
  int factor=8,taps=513;
  std::array<double,maxTaps> h{};
  std::array<std::array<double,2>,65> input{},key{};
  std::array<std::array<double,2>,maxTaps> output{};
  size_t inPos=0,outPos=0;
};
// First-order antiderivative antialiasing, evaluated with current controls at
// both endpoints to avoid control-dependent integration discontinuities.
struct CausalTanh19 {
  double previous=0;
  void reset() noexcept { previous=0; }
  double tick(double x,double drive,double skew=0) noexcept {
    const double a=previous*drive+skew,b=x*drive+skew;
    previous=x;
    const auto integral=[](double t) {const double u=std::abs(t);return u+std::log1p(std::exp(-2*u));};
    const double y=std::abs(b-a)>1e-5 ? (integral(b)-integral(a))/(b-a) : std::tanh((a+b)*.5);
    return (y-std::tanh(skew))/drive;
  }
};
}

