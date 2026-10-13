#pragma once
#include "../../../GILLCommon/MonolithUi.h"
#include "Parameters.h"
#include "Processor.h"

namespace studio::masterui21 {
using namespace gill::monolith;
inline void glass(juce::Graphics& g, juce::Rectangle<float> r) {
    lens(g, r); g.setColour(ink.withAlpha(.65f)); g.fillRoundedRectangle(r.reduced(8), 18);
}
inline float frequencyX(float frequency, juce::Rectangle<float> area) {
    return area.getX()+float(std::log(std::clamp(double(frequency),20.,20000.)/20.)/std::log(1000.))*area.getWidth();
}
template<class Value>
void alpha(juce::Graphics& g, Processor& p, Value value) {
    glass(g,{22,147,374,189});
    text(g,"STATIC TRANSFER / INPUT / OUTPUT",{36,157,346,18},10,champagne);
    const juce::Rectangle<float> plot{61,188,305,119};
    auto x=[&](double db){return plot.getX()+float((db+60.)/72.)*plot.getWidth();};
    auto y=[&](double db){return plot.getBottom()-float((db+60.)/72.)*plot.getHeight();};
    { juce::Graphics::ScopedSaveState saved(g); g.reduceClipRegion(plot.toNearestInt());
    for(int db:{-60,-48,-36,-24,-12,0,12}) {
        g.setColour(cream.withAlpha(.09f));g.drawLine(x(db),plot.getY(),x(db),plot.getBottom());g.drawLine(plot.getX(),y(db),plot.getRight(),y(db));
    }
    g.setColour(cream.withAlpha(.25f));g.drawLine(x(-60),y(-60),x(12),y(12),1);
    for(int channel=1;channel<=2;++channel) {
        gillstudio::dynamics::CurveParameters curve;
        curve.thresholdDb=value("threshold",channel);curve.ratio=value("ratio",channel);
        curve.kneeDb=value("knee",channel);curve.rangeDb=value("range",channel);
        juce::Path response;
        for(int n=0;n<=240;++n){const double input=-60.+72.*n/240., output=input+gillstudio::dynamics::gainDb(input,curve)+value("makeup",channel);if(n==0)response.startNewSubPath(x(input),y(output));else response.lineTo(x(input),y(output));}
        g.setColour(channel==1?gold:champagne);g.strokePath(response,juce::PathStrokeType(1.8f));
    }
    // Reduction is measured by the processor; the graph above is a static
    // feed-forward curve, explicitly separate from envelope/feedback behavior.
    }
    text(g,"-60",{39,294,29,14},9,cream.withAlpha(.6f));text(g,"+12 dB",{328,307,50,14},9,cream.withAlpha(.6f));
    meterWell(g,{410,147,188,189},p.inPeak.load(),p.outPeak.load(),p.reduction[0].load());
    text(g,"L/M  "+juce::String(p.reduction[0].load(),1)+" dB    R/S  "+juce::String(p.reduction[1].load(),1)+" dB",{35,320,350,14},10,gold);
}
inline void maximizer(juce::Graphics& g, Processor& p) {
    glass(g,{22,147,576,151});
    const auto numeric=[](float v){return std::isfinite(v)&&v>-99?juce::String(v,1):juce::String("--");};
    text(g,"INTEGRATED IN",{39,162,174,17},10,champagne);
    text(g,"INTEGRATED OUT",{225,162,174,17},10,champagne);
    text(g,"GAIN REDUCTION",{411,162,174,17},10,champagne);
    text(g,numeric(p.inputLufs.load()),{39,184,174,48},36,cream);
    text(g,numeric(p.outputLufs.load()),{225,184,174,48},36,gold);
    text(g,juce::String(p.reduction[0].load(),1),{411,184,174,48},36,champagne);
    text(g,"LUFS",{39,235,174,16},10);text(g,"LUFS",{225,235,174,16},10);text(g,"dB",{411,235,174,16},10);
    for(int i=0;i<2;++i){const float peak=i?p.outPeak.load():p.inPeak.load(),db=juce::Decibels::gainToDecibels(peak,-90.f),x=68.f+i*278;
        g.setColour(gold.withAlpha(.1f));g.fillRoundedRectangle(x,266,230,4,2);
        g.setColour(db>0?juce::Colour(0xffee7a79):gold);g.fillRoundedRectangle(x,266,230*juce::jlimit(0.f,1.f,(db+60)/66),4,2);
        text(g,i?"OUT":"IN",{x-27,258,24,18},9);text(g,dbLabel(peak)+" dBFS",{x,275,230,14},9,cream.withAlpha(.65f));}
}
template<class Value>
void precision(juce::Graphics& g, Processor& p, Value value) {
    glass(g,{22,147,576,158});
    text(g,"FIVE DYNAMIC RANGES / MEASURED REDUCTION",{39,158,542,18},10,champagne);
    const juce::Rectangle<float> area{47,188,526,83};
    const juce::Colour colours[]{gold,juce::Colour(0xff94b8ee),juce::Colour(0xffbaacdf),champagne,juce::Colour(0xffeaa99c)};
    for(int hz:{100,1000,10000}) {g.setColour(cream.withAlpha(.12f));g.drawVerticalLine(int(frequencyX(float(hz),area)),area.getY(),area.getBottom());}
    for(int n=1;n<=5;++n){const float low=std::min(value("low",n),value("high",n)),high=std::max(value("low",n),value("high",n)),maximum=float(p.rate.load()*.45);
        auto x=frequencyX(std::min(low,maximum),area),right=frequencyX(std::min(high,maximum),area);
        auto r=juce::Rectangle<float>(x,area.getY(),std::max(1.f,right-x),area.getHeight());const bool enabled=value("enable",n)>.5f;
        g.setColour(colours[n-1].withAlpha(enabled?.11f:.035f));g.fillRect(r);
        g.setColour(colours[n-1].withAlpha(enabled?.75f:.25f));g.drawRect(r,1);
        const float reduction=std::max(0.f,p.reduction[size_t(n-1)].load());
        g.setColour(colours[n-1].withAlpha(.35f));g.fillRect(r.withHeight(std::min(r.getHeight(),reduction*3)));
        text(g,juce::String(n),{r.getX(),r.getY()+3,r.getWidth(),16},10,colours[n-1]);
        text(g,juce::String(p.reduction[size_t(n-1)].load(),1),{r.getX(),r.getBottom()-20,r.getWidth(),16},11,colours[n-1]);
    }
    text(g,"20 Hz",{43,280,65,16},9,cream.withAlpha(.6f),juce::Justification::left);
    text(g,"1 kHz",{frequencyX(1000,area)-28,280,56,16},9,cream.withAlpha(.6f));
    text(g,"20 kHz",{509,280,65,16},9,cream.withAlpha(.6f),juce::Justification::right);
}
}
