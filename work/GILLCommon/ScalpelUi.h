#pragma once
#include "PrismUi.h"
#include <functional>

// Spectral Scalpel: hardware controls on a dark, machined silver enclosure.
// Graphs below accept measured values only; decorative etching is kept outside them.
namespace gill::scalpel {
inline const juce::Colour ink{0xffe4e5df}, dark{0xff101514}, cyan{0xff26e4ed}, silver{0xffb8bab4};
inline void text(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size=12,juce::Colour c=ink){g.setColour(c);g.setFont(prism::font(size));g.drawText(s,r,juce::Justification::centred);}
inline void rim(juce::Graphics& g,juce::Rectangle<float> r,float radius=13){
    juce::ColourGradient metal(juce::Colour(0xffe9e8dd),r.getX(),r.getY(),juce::Colour(0xff676a68),r.getRight(),r.getBottom(),false);
    metal.addColour(.13,juce::Colour(0xff3a3f3d));metal.addColour(.27,juce::Colour(0xfff4f4e8));metal.addColour(.48,juce::Colour(0xff7c807c));metal.addColour(.73,juce::Colour(0xffe4e5de));metal.addColour(.9,juce::Colour(0xff444947));
    g.setColour(juce::Colours::black);g.drawRoundedRectangle(r,radius,7);g.setGradientFill(metal);g.drawRoundedRectangle(r,radius,3.5f);
    g.setColour(juce::Colours::white.withAlpha(.55f));g.drawRoundedRectangle(r.reduced(2.4f),radius-2,.6f);
}
inline void panel(juce::Graphics& g,juce::Rectangle<float> r){g.setGradientFill(juce::ColourGradient(juce::Colour(0xff152221),r.getX(),r.getY(),juce::Colour(0xff040b0a),r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,13);rim(g,r);}
inline void chassis(juce::Graphics& g,float w,float h,const juce::String& name){
    g.fillAll(juce::Colour(0xff080b0a));const juce::Rectangle<float> r(5,5,w-10,h-10);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff282d29),0,0,dark,w,h,false));g.fillRoundedRectangle(r,15);
    {juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addRoundedRectangle(r,15);g.reduceClipRegion(clip);g.setTiledImageFill(material::brushedMetal(),0,0,.11f);g.fillRect(r);}
    rim(g,r,15);prism::logo(g,{17,16,50,53});text(g,name,{70,29,w-86,25},20);
    g.setColour(silver.withAlpha(.3f));g.drawHorizontalLine(105,18,w-18);
    // Engraved wave motif, not a signal plot.
    for(int i=0;i<10;++i){juce::Path p;p.startNewSubPath(18,h-92+i*4);p.cubicTo(w*.25f,h-110+i*8,w*.43f,h-25-i*3,w-19,h-20+i*.45f);g.setColour(juce::Colours::black.withAlpha(.8f));g.strokePath(p,juce::PathStrokeType(2));g.setColour(silver.withAlpha(.32f));g.strokePath(p,juce::PathStrokeType(.65f),juce::AffineTransform::translation(0,-1));}
}
class Look : public juce::LookAndFeel_V4 {
public:
    float scale=1;
    Look(){setColour(juce::Slider::textBoxTextColourId,cyan);setColour(juce::Slider::textBoxBackgroundColourId,dark);setColour(juce::Slider::textBoxOutlineColourId,silver.withAlpha(.3f));setColour(juce::ComboBox::backgroundColourId,dark);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::outlineColourId,silver);setColour(juce::PopupMenu::backgroundColourId,dark);setColour(juce::PopupMenu::textColourId,ink);}
    juce::Font getLabelFont(juce::Label&)override{return prism::font(12*scale);}
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s)override{juce::Slider::SliderLayout l;auto r=s.getLocalBounds();l.textBoxBounds=r.removeFromBottom(25);l.sliderBounds=r.reduced(8,8);return l;}
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle style,juce::Slider&)override{
        const bool vertical=style==juce::Slider::LinearVertical;const float cx=x+w*.5f,cy=y+h*.5f;
        auto track=vertical?juce::Rectangle<float>(cx-5,float(y),10,float(h)):juce::Rectangle<float>(float(x),cy-4,float(w),8);
        g.setColour(juce::Colours::black);g.fillRoundedRectangle(track.expanded(3),7);g.setColour(silver.withAlpha(.7f));g.drawRoundedRectangle(track.expanded(3),7,1);
        auto fill=vertical?juce::Rectangle<float>(cx-1.5f,pos,3,std::max(0.f,y+h-pos)):juce::Rectangle<float>(float(x),cy-1.5f,std::max(0.f,pos-x),3);
        g.setColour(cyan.withAlpha(.2f));g.fillRoundedRectangle(fill.expanded(3),3);g.setColour(cyan);g.fillRoundedRectangle(fill,1.5f);
        auto cap=vertical?juce::Rectangle<float>(cx-23,pos-26,46,52):juce::Rectangle<float>(pos-7,cy-12,14,24);
        juce::ColourGradient metal(juce::Colour(0xff666b68),cap.getX(),cap.getY(),juce::Colour(0xffa0a49e),cap.getX(),cap.getBottom(),false);metal.addColour(.12,juce::Colour(0xfff3f2e7));metal.addColour(.27,juce::Colour(0xff9c9f96));metal.addColour(.53,juce::Colour(0xffdadbd3));metal.addColour(.78,juce::Colour(0xff515853));
        g.setColour(juce::Colours::black.withAlpha(.7f));g.fillRoundedRectangle(cap.translated(2,3),3);g.setGradientFill(metal);g.fillRoundedRectangle(cap,3);g.setColour(silver);g.drawRoundedRectangle(cap,3,1);
        g.setColour(juce::Colours::black);g.fillRoundedRectangle(cap.withSizeKeepingCentre(vertical?5.f:3.f,vertical?38.f:15.f),2);g.setColour(cyan);g.fillRoundedRectangle(cap.withSizeKeepingCentre(2,vertical?33.f:13.f),1);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff424945),0,0,dark,0,r.getBottom(),false));g.fillRoundedRectangle(r,5);g.setColour(b.getToggleState()?cyan:silver.withAlpha(over?.9f:.45f));g.drawRoundedRectangle(r,5,1);if(b.getToggleState()||down){g.setColour(cyan);g.fillRoundedRectangle(r.withTrimmedTop(r.getHeight()-3).reduced(8,0),1);}}
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool)override{text(g,b.getButtonText(),b.getLocalBounds().toFloat().reduced(3),11*scale,b.getToggleState()?cyan:ink);}
};
// Actual processor levels. Caller selects RMS or PEAK to match its telemetry; no spectrum or reduction is inferred.
class Levels final : public juce::Component,private juce::Timer {
public:
    juce::String measurement{"RMS"};
    Levels(std::function<float()> a,std::function<float()> b):input(std::move(a)),output(std::move(b)){setName("MEASURED INPUT OUTPUT LEVEL");startTimerHz(24);}
    void paint(juce::Graphics& g)override{const auto r=getLocalBounds().toFloat().reduced(2);panel(g,r);text(g,"LEVEL / "+measurement,r.withHeight(26),11);auto plot=r.reduced(14).withTrimmedTop(24).withTrimmedBottom(33);for(int db:{0,-12,-24,-36,-48,-60}){float y=plot.getY()-db/60.f*plot.getHeight();g.setColour(silver.withAlpha(.16f));g.drawHorizontalLine(int(y),plot.getX(),plot.getRight());text(g,juce::String(db),{plot.getRight()-22,y-7,25,14},9,silver);}
        for(int i=0;i<2;++i){const float db=juce::jlimit(-100.f,12.f,i?output():input());auto bar=juce::Rectangle<float>(plot.getX()+i*(plot.getWidth()-30)*.5f+6,plot.getY(),(plot.getWidth()-40)*.4f,plot.getHeight());g.setColour(cyan.withAlpha(.07f));g.fillRect(bar);const float amount=juce::jlimit(0.f,1.f,(db+60)/60);g.setColour(i?cyan:silver);g.fillRect(bar.withTop(bar.getBottom()-amount*bar.getHeight()));text(g,i?"OUT":"IN",{bar.getX()-5,r.getBottom()-41,bar.getWidth()+10,15},10);text(g,db<=-99?"-INF":juce::String(db,1),{bar.getX()-10,r.getBottom()-25,bar.getWidth()+20,16},11,i?cyan:ink);}}
private:void timerCallback()override{repaint();}std::function<float()> input,output;
};
}
