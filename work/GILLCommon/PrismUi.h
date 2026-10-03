#pragma once
#include "MaterialUi.h"
#include <juce_gui_basics/juce_gui_basics.h>
#if __has_include("PrismLogoData.h")
 #include "PrismLogoData.h"
#endif

// Shared PRISM BLUE rendering. Every knob, selector and meter remains a real
// JUCE control; this is not a screenshot skin. All cached images are software
// images and are built only on the message thread.
namespace gill::prism {
inline const juce::Colour navy{0xff102941}, dark{0xff071727}, ink{0xff102237};
inline const juce::Colour silver{0xffc5d2e2}, white{0xffe8f3ff}, cyan{0xff7fe3ff}, pink{0xffe7b5db};
inline juce::Font font(float height, bool bold=false) { return juce::Font(juce::FontOptions("Segoe UI", height, bold?juce::Font::bold:juce::Font::plain)); }

inline void rim(juce::Graphics& g, juce::Path path, float width=1.0f) {
    const auto r=path.getBounds(); juce::ColourGradient shine(cyan,r.getX(),r.getY(),pink,r.getRight(),r.getBottom(),false);
    shine.addColour(.18,white);shine.addColour(.31,juce::Colour(0xff486984)); shine.addColour(.48,white);shine.addColour(.58,juce::Colour(0xff263c56)); shine.addColour(.76,cyan);shine.addColour(.92,white);
    g.setColour(juce::Colour(0xff010811));g.strokePath(path,juce::PathStrokeType(width+4.8f));
    g.setColour(cyan.withAlpha(.20f));g.strokePath(path,juce::PathStrokeType(width+3.1f));
    g.setGradientFill(shine);g.strokePath(path,juce::PathStrokeType(width));
}
inline void texture(juce::Graphics& g, juce::Rectangle<float> r, float opacity=.40f) {
    g.setTiledImageFill(material::brushedMetal(),0,0,opacity);g.fillRect(r);
}
inline void glass(juce::Graphics& g, juce::Rectangle<float> r, float radius=8.f) {
    material::panel(g,r,dark,radius,true);
    g.setGradientFill(juce::ColourGradient(cyan.withAlpha(.025f),r.getX(),r.getY(),juce::Colours::transparentBlack,r.getRight(),r.getBottom(),false));
    g.fillRoundedRectangle(r.reduced(2),std::max(1.f,radius-2));
}
inline void deck(juce::Graphics& g, juce::Rectangle<float> r, float shoulder=64.f) {
    shoulder=std::min(shoulder,r.getWidth()*.22f);
    juce::Path p; p.startNewSubPath(r.getX(),r.getBottom()-12);p.cubicTo(r.getX()+shoulder,r.getBottom()-12,r.getX()+shoulder*.55f,r.getY()+12,r.getX()+shoulder*1.8f,r.getY()+4);
    p.lineTo(r.getRight()-17,r.getY()+4);p.quadraticTo(r.getRight(),r.getY()+4,r.getRight(),r.getY()+21);
    p.lineTo(r.getRight(),r.getBottom()-13);p.quadraticTo(r.getRight(),r.getBottom(),r.getRight()-15,r.getBottom());
    p.lineTo(r.getX()+10,r.getBottom());p.quadraticTo(r.getX(),r.getBottom(),r.getX(),r.getBottom()-12);p.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(.55f));g.fillPath(p,juce::AffineTransform::translation(0,3));
    juce::ColourGradient metal(juce::Colour(0xffeef5fb),r.getX(),r.getY(),juce::Colour(0xff93a5be),r.getRight(),r.getBottom(),false);
    metal.addColour(.11,juce::Colour(0xffd6e6f4));metal.addColour(.32,juce::Colour(0xffadbdce));metal.addColour(.52,juce::Colour(0xffd6dbe7));metal.addColour(.71,juce::Colour(0xffa4b4c7));metal.addColour(.90,juce::Colour(0xffc5ccdd));
    g.setGradientFill(metal);g.fillPath(p);
    {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(p);texture(g,r,.75f);
     g.setGradientFill(juce::ColourGradient(pink.withAlpha(.26f),r.getRight(),r.getBottom(),juce::Colours::transparentBlack,r.getCentreX(),r.getY(),true));g.fillRect(r);
     g.setGradientFill(juce::ColourGradient(cyan.withAlpha(.13f),r.getX(),r.getY(),juce::Colours::transparentBlack,r.getRight(),r.getBottom(),false));g.fillRect(r);}
    rim(g,p,1.5f);
    g.setColour(white.withAlpha(.72f));g.strokePath(p,juce::PathStrokeType(.55f),juce::AffineTransform::translation(0,1.9f));
}
inline void logo(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::Graphics::ScopedSaveState save(g);g.setOpacity(1.f);
#if __has_include("PrismLogoData.h")
    static const auto image=[] { auto decoded=juce::ImageFileFormat::loadFrom(logoPng,sizeof(logoPng));return juce::SoftwareImageType().convert(decoded); }();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageWithin(image,juce::roundToInt(r.getX()),juce::roundToInt(r.getY()),juce::roundToInt(r.getWidth()),juce::roundToInt(r.getHeight()),juce::RectanglePlacement::centred);
#else
    // The shipping build embeds the approved transparent Jade Ceramic emblem.
    g.setColour(white);g.setFont(font(r.getHeight()*.74f));g.drawText("GP",r,juce::Justification::centred,false);
#endif
}
inline void electricTrace(juce::Graphics&g,const juce::Path& path,juce::Colour accent,float intensity,float coreWidth=1.2f){
    intensity=juce::jlimit(0.f,1.f,intensity);
    const auto stroke=[](float width){return juce::PathStrokeType(width,juce::PathStrokeType::curved,juce::PathStrokeType::rounded);};
    g.setColour(accent.withAlpha(.055f+.09f*intensity));g.strokePath(path,stroke(coreWidth+8.f));
    g.setColour(accent.withAlpha(.13f+.15f*intensity));g.strokePath(path,stroke(coreWidth+3.5f));
    g.setColour(accent.withAlpha(.60f+.30f*intensity));g.strokePath(path,stroke(coreWidth+1.f));
    g.setColour(white.withAlpha(.82f+.18f*intensity));g.strokePath(path,stroke(coreWidth));
}
inline void noteGlass(juce::Graphics&g,juce::Rectangle<float> r,juce::Colour accent,float activity=0.f,bool selected=false){
    if(r.isEmpty())return;activity=juce::jlimit(0.f,1.f,activity);if(selected)accent=pink;
    const float radius=juce::jlimit(1.3f,4.5f,r.getHeight()*.26f);
    g.setColour(accent.withAlpha(.045f+.07f*activity));g.fillRoundedRectangle(r.expanded(5,3),radius+2);
    g.setColour(accent.withAlpha(.12f+.12f*activity));g.fillRoundedRectangle(r.expanded(1.7f,1.2f),radius+1);
    juce::ColourGradient body(accent.withAlpha(.60f),r.getX(),r.getY(),accent.withAlpha(.13f),r.getRight(),r.getBottom(),false);
    body.addColour(.45,accent.withAlpha(.30f+.18f*activity));body.addColour(.73,white.withAlpha(.21f));g.setGradientFill(body);g.fillRoundedRectangle(r,radius);
    g.setColour(accent.withAlpha(.88f));g.drawRoundedRectangle(r,radius,.9f);
    g.setColour(white.withAlpha(.55f+.35f*activity));g.drawRoundedRectangle(r.reduced(.8f),std::max(1.f,radius-.7f),.55f);
    g.setColour(white.withAlpha(.24f));g.fillRoundedRectangle(r.reduced(1.1f).withHeight(std::max(.8f,r.getHeight()*.24f)),std::max(1.f,radius-1));
}
inline void geometry(juce::Graphics&g,float w,float h,float deckY,float shoulder,float activity){
    if(deckY>=h-10)return;
    const float left=8.f,right=std::min(w*.22f,shoulder*1.95f+12.f),top=std::max(64.f,deckY-31.f),bottom=h-12.f,span=bottom-top;
    juce::Path facet;facet.startNewSubPath(left,top+span*.23f);facet.lineTo(right*.55f,top+span*.57f);facet.lineTo(right,top+span*.03f);facet.lineTo(right*.8f,bottom);facet.lineTo(left,bottom);facet.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff153c60),left,top,juce::Colour(0xff08172b),right,bottom,false));g.fillPath(facet);
    for(int n=0;n<35;++n){const float t=n/34.f;juce::Path wave;wave.startNewSubPath(left,top+span*(.10f+t*.81f));wave.cubicTo(left+right*.34f,top+span*(.77f-t*.12f),right*.61f,top+span*(.21f+t*.25f),right+9,top+span*(.23f+t*.50f));
        juce::ColourGradient tint(cyan.withAlpha(.24f+.30f*t),left,top,pink.withAlpha(.29f+.29f*(1-t)),right*.58f,bottom,false);g.setGradientFill(tint);g.strokePath(wave,juce::PathStrokeType(.52f));
        if(n==0||n==17||n==34)electricTrace(g,wave,n==17?pink:cyan,.14f+activity*.55f,.55f);
    }
}
inline void tuneKeyPanel(juce::Graphics&g){
    juce::Path panel;panel.startNewSubPath(10,64);panel.quadraticTo(10,59,18,59);panel.lineTo(119,59);panel.lineTo(119,243);panel.cubicTo(95,244,84,309,58,309);panel.cubicTo(38,309,18,292,10,266);panel.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(.45f));g.fillPath(panel,juce::AffineTransform::translation(1.5f,3));
    juce::ColourGradient face(juce::Colour(0xff173e61),10,66,juce::Colour(0xff07182b),119,279,false);face.addColour(.35,navy);g.setGradientFill(face);g.fillPath(panel);rim(g,panel,.7f);
    {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(panel);texture(g,{10,60,109,252},.55f);}
}
inline void chassis(juce::Graphics& g, float w, float h, float deckY, float headerHeight=57.f, bool drawLogo=true, float deckShoulder=23.f,float activity=0.f) {
    g.fillAll(dark);auto outer=juce::Rectangle<float>(3,3,w-6,h-6);
    juce::ColourGradient anodized(juce::Colour(0xff164b70),0,0,juce::Colour(0xff051021),w,h,false);anodized.addColour(.19,juce::Colour(0xff0e2c49));anodized.addColour(.42,juce::Colour(0xff071a30));anodized.addColour(.63,juce::Colour(0xff143956));anodized.addColour(.82,juce::Colour(0xff071528));g.setGradientFill(anodized);g.fillRoundedRectangle(outer,11);
    {juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addRoundedRectangle(outer,11);g.reduceClipRegion(clip);texture(g,outer,.65f);}
    // The luminous wave fan is confined to the empty sculpted left margin.
    {juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addRoundedRectangle(outer.reduced(3),9);g.reduceClipRegion(clip);geometry(g,w,h,deckY,deckShoulder,activity);}
    if(deckY<h-10)deck(g,{9,deckY,w-18,h-deckY-9},std::min(deckShoulder,w*.10f));
    auto header=juce::Rectangle<float>(8,8,w-16,headerHeight-8);material::panel(g,header,navy,8);
    juce::Path edge;edge.addRoundedRectangle(outer.reduced(1),10);rim(g,edge,2.f);
    juce::ColourGradient inner(white.withAlpha(.83f),0,4,juce::Colour(0xff405d7e),w,h,false);inner.addColour(.32,cyan.withAlpha(.60f));inner.addColour(.61,juce::Colour(0xff020710));inner.addColour(.91,pink.withAlpha(.65f));g.setGradientFill(inner);g.drawRoundedRectangle(outer.reduced(3.8f),8,1.0f);
    if(drawLogo)logo(g,{20,15,39,33});
}
inline void title(juce::Graphics&g,const juce::String& text,juce::Rectangle<float> r,float height=20.f){g.setColour(white);g.setFont(font(height));g.drawFittedText(text,r.toNearestInt(),juce::Justification::centredLeft,1);}

class Look : public juce::LookAndFeel_V4 {
public:
    float scale=1.f;
    Look(){
        setColour(juce::Slider::textBoxTextColourId,white);setColour(juce::Slider::textBoxBackgroundColourId,dark);setColour(juce::Slider::textBoxOutlineColourId,cyan.withAlpha(.35f));
        setColour(juce::TextButton::buttonColourId,navy);setColour(juce::TextButton::buttonOnColourId,cyan);setColour(juce::TextButton::textColourOffId,white);setColour(juce::TextButton::textColourOnId,ink);
        setColour(juce::ComboBox::backgroundColourId,dark);setColour(juce::ComboBox::textColourId,white);setColour(juce::ComboBox::arrowColourId,white);setColour(juce::ComboBox::outlineColourId,cyan.withAlpha(.3f));
        setColour(juce::PopupMenu::backgroundColourId,dark);setColour(juce::PopupMenu::textColourId,white);setColour(juce::PopupMenu::highlightedBackgroundColourId,navy.brighter(.3f));setColour(juce::PopupMenu::highlightedTextColourId,white);
        setColour(juce::ToggleButton::textColourId,ink);setColour(juce::Label::textColourId,ink);setColour(juce::TextEditor::textColourId,white);setColour(juce::TextEditor::backgroundColourId,dark);setColour(juce::TextEditor::highlightColourId,cyan.withAlpha(.3f));
        setColour(juce::TooltipWindow::backgroundColourId,dark);setColour(juce::TooltipWindow::textColourId,white);setColour(juce::TooltipWindow::outlineColourId,cyan.withAlpha(.45f));
    }
    juce::Font getTextButtonFont(juce::TextButton&,int h)override{return font(juce::jlimit(10.f,14.f,h*.43f),true);}
    juce::Font getComboBoxFont(juce::ComboBox&)override{return font(12.f*scale);}
    juce::Label* createSliderTextBox(juce::Slider& s)override{auto* l=juce::LookAndFeel_V4::createSliderTextBox(s);l->setFont(font(12.f*scale));l->setColour(juce::Label::textColourId,white);l->setColour(juce::Label::backgroundColourId,dark);l->setColour(juce::Label::outlineColourId,cyan.withAlpha(.25f));l->setColour(juce::TextEditor::textColourId,white);l->setColour(juce::TextEditor::backgroundColourId,dark);return l;}
    void drawLabel(juce::Graphics&g,juce::Label&label)override{
        if(dynamic_cast<juce::Slider*>(label.getParentComponent())==nullptr){juce::LookAndFeel_V4::drawLabel(g,label);return;}
        auto r=label.getLocalBounds().toFloat().reduced(.6f);material::panel(g,r,dark,4.5f,true);
        if(!label.isBeingEdited()){g.setColour(white.withAlpha(label.isEnabled()?1.f:.45f));g.setFont(getLabelFont(label));g.drawFittedText(label.getText(),label.getLocalBounds().reduced(3,1),juce::Justification::centred,1);}
    }
    void drawButtonBackground(juce::Graphics&g,juce::Button&b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);auto colour=b.getToggleState()?silver:navy;if(over)colour=colour.brighter(.14f);material::panel(g,r,colour,6,down);if(b.getToggleState()){g.setColour(cyan);g.fillRoundedRectangle(r.getX()+6,r.getBottom()-3,std::max(1.f,r.getWidth()-12),1,1);}}
    void drawButtonText(juce::Graphics&g,juce::TextButton&b,bool,bool)override{g.setColour((b.getToggleState()?ink:white).withAlpha(b.isEnabled()?1.f:.4f));g.setFont(getTextButtonFont(b,b.getHeight()));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(4,1),juce::Justification::centred,1);}
    void drawComboBox(juce::Graphics&g,int w,int h,bool,int,int,int,int,juce::ComboBox&)override{material::panel(g,{1,1,float(w-2),float(h-2)},dark,6,true);juce::Path p;p.startNewSubPath(float(w-18),h*.43f);p.lineTo(float(w-13),h*.61f);p.lineTo(float(w-8),h*.43f);g.setColour(white);g.strokePath(p,juce::PathStrokeType(1.2f));}
    void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&)override{material::rotary(g,{float(x),float(y),float(w),float(h)},value,start,end,cyan);}
    void drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle style,juce::Slider&)override{
        const bool vertical=style==juce::Slider::LinearVertical;const float cx=x+w*.5f,cy=y+h*.5f;auto track=vertical?juce::Rectangle<float>(cx-4,float(y),8,float(h)):juce::Rectangle<float>(float(x),cy-4,float(w),8);
        material::panel(g,track.expanded(2),silver,6,true);g.setColour(dark);g.fillRoundedRectangle(track,4);
        auto fill=vertical?juce::Rectangle<float>(cx-2,pos,4,std::max(0.f,float(y+h)-pos)):juce::Rectangle<float>(float(x),cy-2,std::max(0.f,pos-x),4);
        g.setGradientFill(juce::ColourGradient(cyan,fill.getX(),fill.getY(),white,fill.getRight(),fill.getBottom(),false));g.fillRoundedRectangle(fill,2);
        material::disc(g,juce::Rectangle<float>(21,21).withCentre(vertical?juce::Point<float>(cx,pos):juce::Point<float>(pos,cy)),silver);
    }
};
} // namespace gill::prism
