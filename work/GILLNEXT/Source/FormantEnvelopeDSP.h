#pragma once
#include "NextDSPCommon.h"
#include <vector>
namespace gillnext { namespace detail {
// Cepstral source/filter separation: remap the measured smooth log-spectral
// envelope while retaining each complex excitation bin's frequency and phase.
// This is a dynamic envelope transform, not a fixed EQ/formant preset.
class FormantEnvelopeDSP {
public:
    void prepare(double fs,int channels,int size){fs_=fs;channels_=channels;size_=size;hop_=size/4;mask_=size*4-1;
        for(auto&b:input_)b.assign(size,0);for(auto&b:output_)b.assign(size*4,0);for(auto&s:spectra_)s.resize(size);
        temporary_.resize(size);envelope_.assign(size/2+1,0);window_.resize(size);roots_.resize(size/2);reverse_.resize(size);
        int bits=0;while((1<<bits)<size)++bits;
        for(int i=0;i<size;++i){int r=0;for(int b=0;b<bits;++b)r=(r<<1)|((i>>b)&1);reverse_[i]=r;window_[i]=.5-.5*std::cos(2*pi*i/size);}
        for(int i=0;i<size/2;++i)roots_[i]=std::polar(1.,-2*pi*i/size);reset();}
    void reset()noexcept{for(auto&b:input_)std::fill(b.begin(),b.end(),0.f);for(auto&b:output_)std::fill(b.begin(),b.end(),0.);std::fill(envelope_.begin(),envelope_.end(),0.);clock_=0;filled_=0;envelopeReady_=false;}
    void setSemitones(double s)noexcept{factor_=std::exp2(std::clamp(finite(s),-24.,24.)/12.);}
    int latencySamples()const noexcept{return size_;}
    void process(float*const*in,float*const*out,int frames)noexcept{for(int n=0;n<frames;++n){const int pos=int(clock_)&mask_;for(int c=0;c<channels_;++c){input_[c][int(clock_)&(size_-1)]=float(input(in[c][n]));out[c][n]=float(output_[c][pos]);output_[c][pos]=0;}if(++filled_==hop_){frame();filled_=0;}++clock_;}}
private:
    void fft(std::vector<std::complex<double>>&v,bool inverse)noexcept{for(int i=0;i<size_;++i)if(reverse_[i]>i)std::swap(v[i],v[reverse_[i]]);for(int width=2;width<=size_;width*=2){const int stride=size_/width,half=width/2;for(int start=0;start<size_;start+=width)for(int j=0;j<half;++j){const auto w=inverse?std::conj(roots_[j*stride]):roots_[j*stride];const auto a=v[start+j],b=v[start+j+half]*w;v[start+j]=a+b;v[start+j+half]=a-b;}}if(inverse)for(auto&v0:v)v0/=size_;}
    void frame()noexcept{
        const auto first=clock_+1-size_;for(int c=0;c<channels_;++c){auto&v=spectra_[c];for(int i=0;i<size_;++i)v[i]=double(input_[c][int(first+i)&(size_-1)])*window_[i];fft(v,false);}
        double total=0;for(int k=0;k<=size_/2;++k){double p=0;for(int c=0;c<channels_;++c)p+=std::norm(spectra_[c][k])/channels_;total+=p;temporary_[k]=.5*std::log(std::max(1e-18,p));if(k>0&&k<size_/2)temporary_[size_-k]=temporary_[k];}
        if(total>1e-10){fft(temporary_,true);int period=std::clamp(int(fs_/180),1,size_/2);double peak=0;const int lo=std::max(2,int(fs_/500)),hi=std::min(size_/2-1,int(fs_/70));for(int k=lo;k<=hi;++k)if(temporary_[k].real()>peak){peak=temporary_[k].real();period=k;}
            const int cutoff=std::clamp(int(period*.70),std::max(2,int(fs_*.0015)),std::min(size_/2-1,int(fs_*.006)));
            for(int k=1;k<size_;++k){const int q=std::min(k,size_-k);const double fraction=std::clamp((double(q)/cutoff-.7)/.3,0.,1.);const double lifter=.5+.5*std::cos(pi*fraction);temporary_[k]*=lifter;}
            fft(temporary_,false);const double a=alpha(.025,fs_/hop_);for(int k=0;k<=size_/2;++k){const double v=temporary_[k].real();envelope_[k]=envelopeReady_?envelope_[k]+a*(v-envelope_[k]):v;}envelopeReady_=true;
            for(int k=0;k<=size_/2;++k){const double source=std::clamp(k/factor_,0.,double(size_/2));const int at=int(source);const double f=source-at;const double target=envelope_[at]+f*(envelope_[std::min(at+1,size_/2)]-envelope_[at]);const double ratio=std::exp(std::clamp(target-envelope_[k],-2.7631021116,2.7631021116));for(int c=0;c<channels_;++c){spectra_[c][k]*=ratio;if(k>0&&k<size_/2)spectra_[c][size_-k]*=ratio;}}
        }
        for(int c=0;c<channels_;++c){fft(spectra_[c],true);for(int i=0;i<size_;++i){const int pos=int(clock_+1+i)&mask_;output_[c][pos]+=spectra_[c][i].real()*window_[i]/1.5;}}
    }
    std::array<std::vector<float>,2>input_;std::array<std::vector<double>,2>output_;std::array<std::vector<std::complex<double>>,2>spectra_;std::vector<std::complex<double>>temporary_,roots_;std::vector<double>window_,envelope_;std::vector<int>reverse_;
    double fs_=48000,factor_=1;int channels_=1,size_=2048,hop_=512,mask_=8191,filled_=0;std::int64_t clock_=0;bool envelopeReady_=false;
};
} }
