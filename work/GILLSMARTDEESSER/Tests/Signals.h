#pragma once
#include "Learning.h"
#include <vector>
#include <iostream>
#include <limits>
namespace test {
inline int passed=0,failed=0;
inline void check(bool ok,const char* description){if(ok)++passed;else{++failed;std::cerr<<"FAIL "<<description<<'\n';}}
inline int finish(){std::cout<<passed<<" checks passed, "<<failed<<" failed\n";return failed?1:0;}
inline std::vector<float> vocal(double fs,double seconds=9,double sCentre=6200){
    std::vector<float> result(static_cast<size_t>(fs*seconds));std::uint32_t random=0x12abcd;gillsmart::detail::Biquad filter;const auto coefficients=gillsmart::designBand(sCentre,fs,1.3);
    for(size_t i=0;i<result.size();++i){random=random*1664525u+1013904223u;const double noise=static_cast<double>(random)/2147483648.-1;const double t=i/fs,local=t-std::floor(t);const double burst=local>.60&&local<.84?.46:.003;
        result[i]=static_cast<float>(.13*std::sin(2*gillsmart::pi*220*t)+.028*std::sin(2*gillsmart::pi*440*t)+burst*filter.process(noise,coefficients));}
    return result;
}
inline double energy(const std::vector<float>& a,size_t start=0){double sum=0;for(size_t i=start;i<a.size();++i)sum+=a[i]*static_cast<double>(a[i]);return sum/std::max(size_t(1),a.size()-start);}
inline std::vector<float> noise(double fs,int count){std::vector<float> a(static_cast<size_t>(count));std::uint32_t rng=981723;gillsmart::detail::Biquad b;auto c=gillsmart::designBand(6500,fs,1.3);for(auto& v:a){rng=rng*1664525u+1013904223u;v=static_cast<float>(b.process((static_cast<double>(rng)/2147483648.-1)*.7,c));}return a;}
}
