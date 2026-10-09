#pragma once
#include "PrismUi.h"
#include <array>

// The optical displays accept measurements only. No synthetic spectrum or animation.
namespace gill::twin {
inline const juce::Colour ivory{0xffddd5c9}, ink{0xff39363d}, purple{0xffb49af9}, glass{0xff101116};
inline void chassis(juce::Graphics& g,float w,float h,const juce::String& name){
    juce::ColourGradient porcelain(juce::Colour(0xfff5f0e8),0,0,juce::Colour(0xffbcb3a7),w,h,false);
    porcelain.addColour(.30,ivory);porcelain.addColour(.57,juce::Colour(0xfff5eee2));porcelain.addColour(.81,ivory);
    g.setGradientFill(porcelain);g.fillRoundedRectangle(2,2,w-4,h-4,16);
    g.setTiledImageFill(material::brushedMetal(),0,0,.12f);g.fillRoundedRectangle(5,5,w-10,h-10,13);
    g.setColour(ink.withAlpha(.7f));g.drawRoundedRectangle(3,3,w-6,h-6,15,2);
    g.setColour(juce::Colours::white.withAlpha(.85f));g.drawRoundedRectangle(6,6,w-12,h-12,12,1);
    {juce::Graphics::ScopedSaveState mouldingClip(g);juce::Path inset;inset.addRoundedRectangle(8,8,w-16,h-16,9);g.reduceClipRegion(inset);
    // Broad moulded porcelain shoulders, with the collar following both lenses.
    const bool compact=h>500;const float diameter=compact?210.f:270.f,cy=compact?170.f:207.f,left=compact?135.f:157.f,right=w-left,rad=diameter*.5f;
    juce::Path arch;arch.startNewSubPath(7,cy-rad*.38f);arch.cubicTo(left-rad*.78f,cy-rad*.40f,left-rad*.72f,cy-rad-16,left,cy-rad-14);arch.cubicTo(left+rad*.86f,cy-rad-14,w*.40f,cy-rad*.58f,w*.5f,cy-rad*.59f);arch.cubicTo(w*.60f,cy-rad*.58f,right-rad*.86f,cy-rad-14,right,cy-rad-14);arch.cubicTo(right+rad*.72f,cy-rad-16,right+rad*.78f,cy-rad*.40f,w-7,cy-rad*.38f);
    g.setColour(juce::Colour(0xff81776c).withAlpha(.10f));g.strokePath(arch,juce::PathStrokeType(27,juce::PathStrokeType::curved,juce::PathStrokeType::rounded),juce::AffineTransform::translation(0,8));
    g.setColour(juce::Colour(0xffb3a698).withAlpha(.22f));g.strokePath(arch,juce::PathStrokeType(15,juce::PathStrokeType::curved,juce::PathStrokeType::rounded),juce::AffineTransform::translation(0,5));
    juce::ColourGradient mould(juce::Colour(0xfffdf9f0),0,cy-rad-19,juce::Colour(0xffb6ab9c),0,cy-rad*.35f,false);mould.addColour(.45,juce::Colour(0xffe6ddcf));g.setGradientFill(mould);g.strokePath(arch,juce::PathStrokeType(13,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(.83f));g.strokePath(arch,juce::PathStrokeType(1.4f),juce::AffineTransform::translation(0,-5));
    juce::Path collar;const float base=cy+rad;collar.startNewSubPath(7,base-17);collar.cubicTo(left-rad*.40f,base+28,left+rad*.38f,base+31,left+rad*.89f,base-6);collar.cubicTo(w*.44f,base-32,w*.46f,base-26,w*.5f,base-17);collar.cubicTo(w*.54f,base-26,w*.56f,base-32,right-rad*.89f,base-6);collar.cubicTo(right-rad*.38f,base+31,right+rad*.40f,base+28,w-7,base-17);
    for(int layer=3;layer>=0;--layer){const float offset=layer*6.f;g.setColour(juce::Colour(0xff786e63).withAlpha(.16f));g.strokePath(collar,juce::PathStrokeType(10),juce::AffineTransform::translation(0,offset+4));g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff9f4e9),0,base,juce::Colour(0xffb5a999),0,base+32,false));g.strokePath(collar,juce::PathStrokeType(6),juce::AffineTransform::translation(0,offset));g.setColour(juce::Colours::white.withAlpha(.73f));g.strokePath(collar,juce::PathStrokeType(.8f),juce::AffineTransform::translation(0,offset-2));}
    }
    prism::logo(g,{20,14,44,44});g.setColour(ink);g.setFont(prism::font(21));g.drawText(name,juce::Rectangle<float>(76,23,w-320,28),juce::Justification::centredLeft);
}
inline juce::Rectangle<float> lens(juce::Graphics&g,juce::Rectangle<float>r,const juce::String&title,const juce::String&unit){
    r=r.withSizeKeepingCentre(std::min(r.getWidth(),r.getHeight()),std::min(r.getWidth(),r.getHeight()));
    g.setColour(juce::Colours::black.withAlpha(.23f));g.fillEllipse(r.translated(0,5).expanded(3));
    juce::ColourGradient rim(juce::Colour(0xfff9f5ec),r.getX(),r.getY(),juce::Colour(0xff48464a),r.getRight(),r.getBottom(),false);rim.addColour(.2,juce::Colour(0xffaaa298));rim.addColour(.40,juce::Colours::white);rim.addColour(.48,juce::Colour(0xff514c48));rim.addColour(.70,juce::Colour(0xffeee9df));rim.addColour(.9,juce::Colour(0xff938b80));g.setGradientFill(rim);g.fillEllipse(r);
    juce::Path chrome;chrome.addCentredArc(r.getCentreX(),r.getCentreY(),r.getWidth()*.485f,r.getHeight()*.485f,0,-1.15f,.72f,true);g.setColour(juce::Colours::white.withAlpha(.95f));g.strokePath(chrome,juce::PathStrokeType(2.5f));
    for(int i=2;i<12;i+=2){g.setColour((i%4?juce::Colours::white:ink).withAlpha(.28f));g.drawEllipse(r.reduced(float(i)),.6f);}
    const auto d=r.reduced(12);juce::ColourGradient optical(juce::Colour(0xff353641),d.getX(),d.getY(),glass,d.getRight(),d.getBottom(),false);optical.addColour(.43,juce::Colour(0xff14161e));optical.addColour(.8,juce::Colour(0xff090b10));g.setGradientFill(optical);g.fillEllipse(d);{juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addEllipse(d);g.reduceClipRegion(clip);const auto spot=juce::Point<float>(d.getX()+d.getWidth()*.27f,d.getY()+d.getHeight()*.08f);g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb8c5f6).withAlpha(.27f),spot.x,spot.y,juce::Colours::transparentBlack,spot.x+d.getWidth()*.56f,spot.y+d.getHeight()*.18f,true));g.fillEllipse(d);juce::Path glint;glint.addCentredArc(d.getCentreX(),d.getCentreY(),d.getWidth()*.477f,d.getHeight()*.477f,0,-.88f,.46f,true);g.setColour(juce::Colour(0xff95aaff).withAlpha(.13f));g.strokePath(glint,juce::PathStrokeType(9));g.setColour(juce::Colour(0xffe3e9ff).withAlpha(.64f));g.strokePath(glint,juce::PathStrokeType(1.2f));}g.setColour(purple.withAlpha(.7f));g.drawEllipse(d.reduced(2),1);g.setColour(juce::Colours::white.withAlpha(.3f));g.drawEllipse(d.reduced(4),.5f);
    g.setColour(juce::Colours::white.withAlpha(.87f));g.setFont(prism::font(12));g.drawText(title,d.withY(d.getY()+d.getHeight()*.16f).withHeight(18),juce::Justification::centred);
    g.setColour(purple);g.fillRoundedRectangle(d.getCentreX()-10,d.getY()+d.getHeight()*.26f,20,1.5f,.7f);
    auto plot=d.reduced(d.getWidth()*.15f,d.getHeight()*.32f).translated(0,8);for(int i=0;i<5;++i){g.setColour(purple.withAlpha(.10f));g.drawHorizontalLine(int(plot.getY()+plot.getHeight()*i/4),plot.getX(),plot.getRight());g.drawVerticalLine(int(plot.getX()+plot.getWidth()*i/4),plot.getY(),plot.getBottom());}
    g.setFont(prism::font(9));g.setColour(juce::Colours::white.withAlpha(.58f));g.drawText(unit,d.withY(d.getBottom()-d.getHeight()*.12f).withHeight(16),juce::Justification::centred);return plot;
}
template<class Values>inline void trace(juce::Graphics&g,juce::Rectangle<float>a,const Values&values,float minimum,float maximum){
    if(values.size()<2)return;juce::Path path;for(size_t i=0;i<values.size();++i){const float v=std::isfinite(values[i])?values[i]:minimum;const float x=a.getX()+float(i)*a.getWidth()/float(values.size()-1),y=a.getBottom()-juce::jlimit(0.f,1.f,(v-minimum)/(maximum-minimum))*a.getHeight();if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}
    auto fill=path;fill.lineTo(a.getRight(),a.getBottom());fill.lineTo(a.getX(),a.getBottom());fill.closeSubPath();g.setGradientFill(juce::ColourGradient(purple.withAlpha(.35f),0,a.getY(),purple.withAlpha(.015f),0,a.getBottom(),false));g.fillPath(fill);prism::electricTrace(g,path,purple,.3f,1.1f);
}
struct History {std::array<float,100> values;History(){values.fill(-90);}void push(float db){std::move(values.begin()+1,values.end(),values.begin());values.back()=db;}void gain(float gain){push(juce::Decibels::gainToDecibels(gain,-90.f));}};
inline void levelLens(juce::Graphics&g,juce::Rectangle<float>r,const juce::String&title,const History&history,const juce::String&unit="PEAK DBFS / 4 S HISTORY") {auto a=lens(g,r,title,unit);trace(g,a,history.values,-90,0);g.setFont(prism::font(15));g.setColour(juce::Colours::white);g.drawText(juce::String(history.values.back(),1)+" dB",a.withY(a.getBottom()+3).withHeight(20),juce::Justification::centred);}
class Look:public prism::Look {
public:
    void drawButtonBackground(juce::Graphics&g,juce::Button&b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);material::panel(g,r,b.getToggleState()?purple:ivory,6,down);if(over){g.setColour(purple.withAlpha(.15f));g.fillRoundedRectangle(r,6);}g.setColour(ink.withAlpha(.45f));g.drawRoundedRectangle(r,6,.6f);}
    void drawButtonText(juce::Graphics&g,juce::TextButton&b,bool,bool)override{g.setColour(ink.withAlpha(b.isEnabled()?1.f:.35f));g.setFont(getTextButtonFont(b,b.getHeight()));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(3),juce::Justification::centred,1);}
    void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&)override{material::rotary(g,{float(x),float(y),float(w),float(h)},value,start,end,purple);}
    void drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle style,juce::Slider&)override{const bool vertical=style==juce::Slider::LinearVertical;auto track=vertical?juce::Rectangle<float>(x+w*.5f-3,float(y),6,float(h)):juce::Rectangle<float>(float(x),y+h*.5f-3,float(w),6);material::panel(g,track.expanded(3),ivory,6,true);g.setColour(glass);g.fillRoundedRectangle(track,3);g.setColour(purple);g.fillRoundedRectangle(vertical?juce::Rectangle<float>(track.getX()+1,pos,4,std::max(0.f,float(y+h)-pos)):juce::Rectangle<float>(float(x),track.getY()+1,std::max(0.f,pos-x),4),2);material::disc(g,juce::Rectangle<float>(20,20).withCentre(vertical?juce::Point<float>(track.getCentreX(),pos):juce::Point<float>(pos,track.getCentreY())),ivory);}
};
}


