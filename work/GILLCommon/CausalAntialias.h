#pragma once
#include <algorithm>
#include <cmath>
namespace gill::causal {
// First-order antiderivative averaging. This has no lookahead, queued samples
// or allocation. Apply to the nonlinear residual, retaining the current clean
// sample: small-signal brightness and the exact zero-drive path stay intact.
template<class Function, class Primitive>
inline double average(double now,double previous,Function function,Primitive primitive) noexcept {
 const double difference=now-previous;
 if(std::abs(difference)<1.e-5*(1+std::max(std::abs(now),std::abs(previous))))return function(.5*(now+previous));
 return (primitive(now)-primitive(previous))/difference;
}
inline double rationalPrimitive(double x) noexcept {
 const double a=std::min(std::abs(x),3.);
 return a*a/18.+(4./3.)*std::log(a*a+3.)+std::max(0.,std::abs(x)-3.);
}
inline double logCosh(double x) noexcept {const double a=std::abs(x);return a+std::log1p(std::exp(-2*a))-std::log(2.);}
}
