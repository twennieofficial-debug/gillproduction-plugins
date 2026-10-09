#pragma once
#include "../../GILLCommon/MaterialUi.h"
#include <juce_gui_basics/juce_gui_basics.h>
#if __has_include("../../GILLCommon/PrismLogoData.h")
 #include "../../GILLCommon/PrismLogoData.h"
#endif

// Shared PRISM BLUE rendering. Every knob, selector and meter remains a real
// JUCE control; this is not a screenshot skin. All cached images are software
// images and are built only on the message thread.
namespace mobina::skin {
namespace material = gill::material;
using gill::prism::logoPng;
inline float hue = .93f, saturation = .58f, brightness = 1.f;
inline juce::Colour tinted(juce::uint32 argb) {auto c=juce::Colour(argb); if(c.getSaturation()<.08f)return c;return c.withHue(hue).withSaturation(c.getSaturation()*saturation).withBrightness(juce::jlimit(0.f,1.f,c.getBrightness()*brightness));}

inline juce::Colour navy, dark, ink, silver, white, cyan, pink;
inline void palette(float h,float sat=1,float value=1){hue=h;saturation=sat;brightness=value;navy=tinted(0xff102941);dark=tinted(0xff071727);ink=tinted(0xff102237);silver=juce::Colour(0xffc5d2e2);white=juce::Colour(0xffe8f3ff);cyan=juce::Colour::fromHSV(h,.34f,.98f,1.f);pink=juce::Colour::fromHSV(std::fmod(h+.10f,1.f),.20f,.95f,1.f);}
inline juce::Font font(float height, bool bold=false) { return juce::Font(juce::FontOptions("Segoe UI", height, bold?juce::Font::bold:juce::Font::plain)); }

inline void rim(juce::Graphics& g, juce::Path path, float width=1.0f) {
    const auto r=path.getBounds(); juce::ColourGradient shine(cyan,r.getX(),r.getY(),pink,r.getRight(),r.getBottom(),false);
    shine.addColour(.18,white);shine.addColour(.31,tinted(0xff486984)); shine.addColour(.48,white);shine.addColour(.58,tinted(0xff263c56)); shine.addColour(.76,cyan);shine.addColour(.92,white);
    g.setColour(tinted(0xff010811));g.strokePath(path,juce::PathStrokeType(width+4.8f));
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
    juce::ColourGradient metal(tinted(0xffeef5fb),r.getX(),r.getY(),tinted(0xff93a5be),r.getRight(),r.getBottom(),false);
    metal.addColour(.11,tinted(0xffd6e6f4));metal.addColour(.32,tinted(0xffadbdce));metal.addColour(.52,tinted(0xffd6dbe7));metal.addColour(.71,tinted(0xffa4b4c7));metal.addColour(.90,tinted(0xffc5ccdd));
    g.setGradientFill(metal);g.fillPath(p);
    {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(p);texture(g,r,.75f);
     g.setGradientFill(juce::ColourGradient(pink.withAlpha(.26f),r.getRight(),r.getBottom(),juce::Colours::transparentBlack,r.getCentreX(),r.getY(),true));g.fillRect(r);
     g.setGradientFill(juce::ColourGradient(cyan.withAlpha(.13f),r.getX(),r.getY(),juce::Colours::transparentBlack,r.getRight(),r.getBottom(),false));g.fillRect(r);}
    rim(g,p,1.5f);
    g.setColour(white.withAlpha(.72f));g.strokePath(p,juce::PathStrokeType(.55f),juce::AffineTransform::translation(0,1.9f));
}
inline void logo(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::Graphics::ScopedSaveState save(g);g.setOpacity(1.f);
#if __has_include("../../GILLCommon/PrismLogoData.h")
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
    g.setGradientFill(juce::ColourGradient(tinted(0xff153c60),left,top,tinted(0xff08172b),right,bottom,false));g.fillPath(facet);
    for(int n=0;n<35;++n){const float t=n/34.f;juce::Path wave;wave.startNewSubPath(left,top+span*(.10f+t*.81f));wave.cubicTo(left+right*.34f,top+span*(.77f-t*.12f),right*.61f,top+span*(.21f+t*.25f),right+9,top+span*(.23f+t*.50f));
        juce::ColourGradient tint(cyan.withAlpha(.24f+.30f*t),left,top,pink.withAlpha(.29f+.29f*(1-t)),right*.58f,bottom,false);g.setGradientFill(tint);g.strokePath(wave,juce::PathStrokeType(.52f));
        if(n==0||n==17||n==34)electricTrace(g,wave,n==17?pink:cyan,.14f+activity*.55f,.55f);
    }
}
inline void tuneKeyPanel(juce::Graphics&g){
    juce::Path panel;panel.startNewSubPath(10,64);panel.quadraticTo(10,59,18,59);panel.lineTo(119,59);panel.lineTo(119,243);panel.cubicTo(95,244,84,309,58,309);panel.cubicTo(38,309,18,292,10,266);panel.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(.45f));g.fillPath(panel,juce::AffineTransform::translation(1.5f,3));
    juce::ColourGradient face(tinted(0xff173e61),10,66,tinted(0xff07182b),119,279,false);face.addColour(.35,navy);g.setGradientFill(face);g.fillPath(panel);rim(g,panel,.7f);
    {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(panel);texture(g,{10,60,109,252},.55f);}
}
inline void chassis(juce::Graphics& g, float w, float h, float deckY, float headerHeight=57.f, bool drawLogo=true, float deckShoulder=23.f,float activity=0.f) {
    g.fillAll(dark);auto outer=juce::Rectangle<float>(3,3,w-6,h-6);
    juce::ColourGradient anodized(tinted(0xff164b70),0,0,tinted(0xff051021),w,h,false);anodized.addColour(.19,tinted(0xff0e2c49));anodized.addColour(.42,tinted(0xff071a30));anodized.addColour(.63,tinted(0xff143956));anodized.addColour(.82,tinted(0xff071528));g.setGradientFill(anodized);g.fillRoundedRectangle(outer,11);
    {juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addRoundedRectangle(outer,11);g.reduceClipRegion(clip);texture(g,outer,.65f);}
    // The luminous wave fan is confined to the empty sculpted left margin.
    {juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addRoundedRectangle(outer.reduced(3),9);g.reduceClipRegion(clip);geometry(g,w,h,deckY,deckShoulder,activity);}
    if(deckY<h-10)deck(g,{9,deckY,w-18,h-deckY-9},std::min(deckShoulder,w*.10f));
    auto header=juce::Rectangle<float>(8,8,w-16,headerHeight-8);material::panel(g,header,navy,8);
    juce::Path edge;edge.addRoundedRectangle(outer.reduced(1),10);rim(g,edge,2.f);
    juce::ColourGradient inner(white.withAlpha(.83f),0,4,tinted(0xff405d7e),w,h,false);inner.addColour(.32,cyan.withAlpha(.60f));inner.addColour(.61,tinted(0xff020710));inner.addColour(.91,pink.withAlpha(.65f));g.setGradientFill(inner);g.drawRoundedRectangle(outer.reduced(3.8f),8,1.0f);
    if(drawLogo)logo(g,{20,15,39,33});
}
inline void title(juce::Graphics&g,const juce::String& text,juce::Rectangle<float> r,float height=20.f){g.setColour(white);g.setFont(font(height));g.drawFittedText(text,r.toNearestInt(),juce::Justification::centredLeft,1);}


inline const juce::Colour plum{0xff65384f}, cream{0xfffff8ec}, rose{0xffef9dbc}, blush{0xffffdbe7}, lilac{0xffdcd4f3};
inline void candy(juce::Graphics& g,juce::Rectangle<float> r,juce::Colour c,float radius=14){
 g.setColour(plum.withAlpha(.23f));g.fillRoundedRectangle(r.translated(0,3),radius);
 g.setGradientFill(juce::ColourGradient(c.brighter(.14f),r.getX(),r.getY(),c,r.getX(),r.getBottom(),false));g.fillRoundedRectangle(r,radius);
 g.setColour(plum);g.drawRoundedRectangle(r,radius,1.7f);g.setColour(cream.withAlpha(.85f));g.drawRoundedRectangle(r.reduced(2.5f),radius-2,1.1f);
}
inline void star(juce::Graphics&g,float x,float y,float size,juce::Colour c){
 juce::Path p;p.startNewSubPath(x,y-size);p.quadraticTo(x+size*.18f,y-size*.18f,x+size,y);p.quadraticTo(x+size*.18f,y+size*.18f,x,y+size);p.quadraticTo(x-size*.18f,y+size*.18f,x-size,y);p.quadraticTo(x-size*.18f,y-size*.18f,x,y-size);p.closeSubPath();g.setColour(c);g.fillPath(p);g.setColour(plum);g.strokePath(p,juce::PathStrokeType(1.3f));
}
inline void heart(juce::Graphics&g,float x,float y,float s){
 juce::Path p;p.startNewSubPath(x,y+s*.6f);p.cubicTo(x-s*1.6f,y-s*.3f,x-s*.7f,y-s*1.4f,x,y-s*.65f);p.cubicTo(x+s*.7f,y-s*1.4f,x+s*1.6f,y-s*.3f,x,y+s*.6f);p.closeSubPath();g.setColour(rose);g.fillPath(p);g.setColour(plum);g.strokePath(p,juce::PathStrokeType(1.5f));
}
class Look : public juce::LookAndFeel_V4 {
public:
 Look(){
  setColour(juce::Label::textColourId,plum);setColour(juce::ComboBox::textColourId,plum);
  setColour(juce::PopupMenu::backgroundColourId,cream);setColour(juce::PopupMenu::textColourId,plum);setColour(juce::PopupMenu::highlightedBackgroundColourId,blush);setColour(juce::PopupMenu::highlightedTextColourId,plum);
  setColour(juce::TextEditor::textColourId,plum);setColour(juce::TextEditor::backgroundColourId,cream);setColour(juce::TextEditor::highlightColourId,rose);
  setColour(juce::TooltipWindow::backgroundColourId,cream);setColour(juce::TooltipWindow::textColourId,plum);setColour(juce::TooltipWindow::outlineColourId,plum);
 }
 juce::Font getComboBoxFont(juce::ComboBox&)override{return font(11.5f,true);}
 juce::Label* createSliderTextBox(juce::Slider&s)override{auto*l=juce::LookAndFeel_V4::createSliderTextBox(s);l->setFont(font(11.5f,true));l->setColour(juce::Label::textColourId,plum);l->setColour(juce::TextEditor::textColourId,plum);l->setColour(juce::TextEditor::backgroundColourId,cream);return l;}
 void drawLabel(juce::Graphics&g,juce::Label&l)override{
  if(dynamic_cast<juce::Slider*>(l.getParentComponent())==nullptr){juce::LookAndFeel_V4::drawLabel(g,l);return;}
  auto r=l.getLocalBounds().toFloat().reduced(.8f);g.setColour(cream);g.fillRoundedRectangle(r,6);g.setColour(plum.withAlpha(.35f));g.drawRoundedRectangle(r,6,.8f);
  if(!l.isBeingEdited()){g.setColour(plum);g.setFont(getLabelFont(l));g.drawFittedText(l.getText(),l.getLocalBounds().reduced(2,0),juce::Justification::centred,1);}
 }
 void drawComboBox(juce::Graphics&g,int w,int h,bool,int,int,int,int,juce::ComboBox&)override{
  candy(g,{1,1,float(w-2),float(h-5)},cream,9);juce::Path p;p.startNewSubPath(float(w-17),h*.38f);p.lineTo(float(w-12),h*.55f);p.lineTo(float(w-7),h*.38f);g.setColour(plum);g.strokePath(p,juce::PathStrokeType(1.8f));
 }
 void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&s)override{
  auto box=juce::Rectangle<float>(float(x),float(y),float(w),float(h));auto c=box.getCentre();const float radius=std::min(w,h)*.5f-8;const float angle=start+value*(end-start);const auto tone=s.getComponentID().getTrailingIntValue()>=6?lilac:rose;
  juce::Path track;track.addCentredArc(c.x,c.y,radius+4,radius+4,0,start,end,true);g.setColour(plum.withAlpha(.18f));g.strokePath(track,juce::PathStrokeType(3.2f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
  juce::Path active;active.addCentredArc(c.x,c.y,radius+4,radius+4,0,start,angle,true);g.setColour(plum);g.strokePath(active,juce::PathStrokeType(3.2f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
  auto r=juce::Rectangle<float>(radius*2,radius*2).withCentre(c);g.setColour(plum.withAlpha(.24f));g.fillEllipse(r.translated(1,3));g.setGradientFill(juce::ColourGradient(cream,r.getX(),r.getY(),tone,r.getRight(),r.getBottom(),false));g.fillEllipse(r);g.setColour(plum);g.drawEllipse(r,1.8f);
  g.setColour(cream.withAlpha(.85f));g.drawEllipse(r.reduced(3),1.1f);g.setColour(tone.withAlpha(.7f));g.fillEllipse(r.reduced(8));
  const juce::Point<float> v(std::sin(angle),-std::cos(angle));g.setColour(plum);g.drawLine({c+v*5,c+v*(radius-6)},3.2f);g.setColour(cream);g.fillEllipse(c.x-radius*.43f,c.y-radius*.46f,7,4);
 }
};
} // namespace mobina::skin
