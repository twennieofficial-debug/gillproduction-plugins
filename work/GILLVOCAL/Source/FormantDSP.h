#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace gill {
// Original cepstral-envelope shifter. The harmonic frequencies and complex
// phases are retained; only the smooth vocal spectral envelope is transposed.
// This is not a resampler or a pitch control. All memory is prepared off-thread.
class FormantDSP {
public:
    void prepare(double fs, int channels) {
        rate_=fs; channels_=std::clamp(channels,1,2); size_=256;
        while(size_<fs*.042 && size_<32768)size_*=2;
        // Eight-way overlap reduces envelope modulation on low vocal notes.
        hop_=size_/8; int bits=0;while((1<<bits)<size_)++bits;
        reverse_.resize(size_);window_.resize(size_);twiddle_.resize(size_/2);
        for(int i=0;i<size_;++i){int v=i,r=0;for(int j=0;j<bits;++j){r=(r<<1)|(v&1);v>>=1;}reverse_[i]=r;window_[i]=std::sin(pi*i/size_);}
        for(int i=0;i<size_/2;++i)twiddle_[i]=std::polar(1.,-2*pi*i/size_);
        for(auto& a:input_)a.assign(size_,0);for(auto& a:output_)a.assign(size_*2,0);
        for(auto& a:spectra_)a.resize(size_);cepstrum_.resize(size_);envelope_.resize(size_/2+1);gains_.resize(size_/2+1);
        reset();
    }
    void reset() noexcept {for(auto&a:input_)std::fill(a.begin(),a.end(),0.f);for(auto&a:output_)std::fill(a.begin(),a.end(),0.);std::fill(gains_.begin(),gains_.end(),1.);in_=out_=until_=0;current_=target_;currentMix_=mix_;normalise_=1;started_=false;}
    void setSemitones(float semitones) noexcept {target_=std::isfinite(semitones)?std::clamp(double(semitones),-12.,12.):0;}
    void setMix(float percent)noexcept{mix_=std::isfinite(percent)?std::clamp(double(percent)*.01,0.,1.):0;if(!started_)currentMix_=mix_;}
    int latencySamples()const noexcept{return size_;}
    void process(float*const* audio,int channels,int count)noexcept{
        const int active=std::min(channels,channels_);if(!audio||size_==0||active<1)return;
        for(int c=0;c<active;++c)if(!audio[c])return;
        started_=true;const double fade=1-std::exp(-1/(rate_*.005));
        for(int n=0;n<count;++n){currentMix_+=fade*(mix_-currentMix_);if(std::abs(mix_-currentMix_)<1e-10)currentMix_=mix_;for(int c=0;c<channels_;++c){const float x=c<active?audio[c][n]:0;const float dry=input_[c][in_];input_[c][in_]=x;
                // Synthesize only the correction; zero formant is bit-exact dry.
                const double correction=output_[c][out_];output_[c][out_]=0;
                if(c<active)audio[c][n]=float(std::clamp(double(dry)+currentMix_*correction,-32.,32.));}
            in_=(in_+1)%size_;out_=(out_+1)%(2*size_);if(++until_==hop_){until_=0;frame(active);}}
    }
private:
    void fft(std::vector<std::complex<double>>&a,bool inverse)noexcept{
        for(int i=0;i<size_;++i)if(i<reverse_[i])std::swap(a[i],a[reverse_[i]]);
        for(int len=2;len<=size_;len*=2)for(int at=0;at<size_;at+=len)for(int j=0;j<len/2;++j){auto w=twiddle_[j*size_/len];if(inverse)w=std::conj(w);const auto x=a[at+j],y=a[at+j+len/2]*w;a[at+j]=x+y;a[at+j+len/2]=x-y;}
        if(inverse)for(auto&x:a)x/=size_;
    }
    void frame(int active)noexcept{
        const double follow=1-std::exp(-hop_/(rate_*.025));current_+=follow*(target_-current_);if(std::abs(current_-target_)<1e-8)current_=target_;
        if(current_==0&&target_==0){std::fill(gains_.begin(),gains_.end(),1);return;}
        for(int c=0;c<active;++c){for(int i=0;i<size_;++i)spectra_[c][i]=input_[c][(in_+i)%size_]*window_[i];fft(spectra_[c],false);}
        for(int k=0;k<size_;++k){double power=0;for(int c=0;c<active;++c)power+=std::norm(spectra_[c][k]);cepstrum_[k]=.5*std::log(power/active+1e-16);}
        fft(cepstrum_,true);
        const double qLimit=std::max(3.,rate_*.0012);
        for(int q=1;q<size_;++q){const double position=std::min(q,size_-q)/qLimit;cepstrum_[q]*=position<1?.5+.5*std::cos(pi*position):0;}
        fft(cepstrum_,false);for(int k=0;k<=size_/2;++k)envelope_[k]=cepstrum_[k].real();
        const double ratio=std::exp2(current_/12.),gainFollow=1-std::exp(-hop_/(rate_*.080));double originalEnergy=0,processedEnergy=0;
        for(int k=0;k<=size_/2;++k){const double source=std::clamp(k/ratio,0.,double(size_/2)),hz=k*rate_/size_;const int lo=int(source),hi=std::min(lo+1,size_/2);
            const double target=envelope_[lo]+(source-lo)*(envelope_[hi]-envelope_[lo]);
            // Do not turn a high-frequency noise floor into a resonant whistle.
            const double focus=std::clamp((hz-60)/120.,0.,1.)*std::clamp((rate_*.46-hz)/(rate_*.06),0.,1.);
            const double gain=std::exp(std::clamp((target-envelope_[k])*focus,-1.38,1.38));gains_[k]+=gainFollow*(gain-gains_[k]);
            for(int c=0;c<active;++c){const double e=std::norm(spectra_[c][k]);originalEnergy+=e;processedEnergy+=e*gains_[k]*gains_[k];}}
        normalise_+=gainFollow*(std::clamp(std::sqrt((originalEnergy+1e-20)/(processedEnergy+1e-20)),.5,2.)-normalise_);
        for(int c=0;c<active;++c){for(int k=0;k<size_;++k)spectra_[c][k]*=gains_[k<=size_/2?k:size_-k]*normalise_-1;fft(spectra_[c],true);
            for(int i=0;i<size_;++i)output_[c][(out_+i)%(2*size_)]+=spectra_[c][i].real()*window_[i]*.25;}
    }
    static constexpr double pi=3.14159265358979323846;
    double rate_=48000,target_=0,current_=0,mix_=1,currentMix_=1,normalise_=1;bool started_=false;int size_=0,hop_=0,channels_=1,in_=0,out_=0,until_=0;
    std::array<std::vector<float>,2>input_;std::array<std::vector<double>,2>output_;std::array<std::vector<std::complex<double>>,2>spectra_;
    std::vector<std::complex<double>>cepstrum_,twiddle_;std::vector<double>window_,envelope_,gains_;std::vector<int>reverse_;
};
}
