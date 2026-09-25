#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>

namespace
{
const juce::Colour ink (0xff25382b), sage (0xff567462);

juce::Font coreFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (gillInterfaceFontName(), std::max(12.f,height), bold ? juce::Font::bold : juce::Font::plain));
}

// Ordinary parameter synchronization never enters this user-interaction path.
// JUCE already scopes mouse drags, numeric commits, double clicks and accessible
// value edits; arrow keys need the same gesture explicitly.
class AmountSlider final : public juce::Slider
{
public:
    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool up = key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey;
        const bool down = key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey;
        if (isEnabled() && ! key.getModifiers().isAnyModifierKeyDown()
            && ((up && getValue() < getMaximum()) || (down && getValue() > getMinimum())))
        {
            juce::Slider::ScopedDragNotification gesture (*this);
            return juce::Slider::keyPressed (key);
        }
        return juce::Slider::keyPressed (key);
    }
};

class CoreLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    float scale = 1.0f;
    CoreLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, sage.withAlpha (0.24f));
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffffbef));
        setColour (juce::TextEditor::outlineColourId, sage.withAlpha (0.7f));
        setColour (juce::TextEditor::focusedOutlineColourId, sage);
        setColour (juce::TextEditor::highlightColourId, sage.withAlpha (0.24f));
        setColour (juce::TextEditor::highlightedTextColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xfff4efe0));
        setColour (juce::TooltipWindow::textColourId, ink);
    }
    juce::Font getLabelFont (juce::Label&) override { return coreFont (19.0f * scale, true); }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        juce::Slider::SliderLayout layout;
        const int textHeight=juce::roundToInt(31*scale);
        layout.sliderBounds=slider.getLocalBounds().withTrimmedBottom(textHeight);
        layout.textBoxBounds=slider.getLocalBounds().removeFromBottom(textHeight);
        return layout;
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down) override {
        const auto r=b.getLocalBounds().toFloat().reduced(1);
        const auto base=b.getToggleState()?sage:juce::Colour(0xffeae7dc);
        g.setGradientFill(juce::ColourGradient(base.brighter(down?0.f:.13f),r.getX(),r.getY(),base.darker(.1f),r.getX(),r.getBottom(),false));
        g.fillRoundedRectangle(r,7*scale);g.setColour(ink.withAlpha(.5f));g.drawRoundedRectangle(r,7*scale,1);
        if(over){g.setColour(juce::Colours::white.withAlpha(.1f));g.fillRoundedRectangle(r,7*scale);}
    }
    juce::Font getTextButtonFont(juce::TextButton&,int) override {return coreFont(13*scale,true);}
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setColour (juce::Label::textColourId, ink);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::TextEditor::textColourId, ink);
        label->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffffbef));
        label->setColour (juce::TextEditor::outlineColourId, sage.withAlpha (0.7f));
        label->setColour (juce::TextEditor::highlightColourId, sage.withAlpha (0.24f));
        label->setColour (juce::TextEditor::highlightedTextColourId, ink);
        return label;
    }
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (label.isBeingEdited()) return;
        const auto area = label.getLocalBounds().toFloat();
        g.setFont (getLabelFont (label));
        // A restrained inset highlight keeps the live readout in the wood theme.
        g.setColour (juce::Colour (0xffffedcd).withAlpha (0.70f));
        g.drawText (label.getText(), area.translated (0, 0.9f * scale), juce::Justification::centred, false);
        g.setColour (ink);
        g.drawText (label.getText(), area, juce::Justification::centred, false);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float proportion, float start, float end, juce::Slider&) override
    {
        const float s = (float) juce::jmin (width, height) / 252.0f;
        const float cx = (float) x + (float) width * 0.5f;
        const float cy = (float) y + (float) height * 0.5f;
        const float radius = 113.4f * s;
        const float angle = start + proportion * (end - start);
        juce::Path track, active;
        track.addCentredArc (cx, cy, radius, radius, 0, start, end, true);
        active.addCentredArc (cx, cy, radius, radius, 0, start, angle, true);
        const auto roundedStroke = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
        g.setColour (juce::Colour (0xff80694d).withAlpha (0.50f));
        g.strokePath (track, roundedStroke (5.0f*s));
        g.setColour (juce::Colour (0xffffedce).withAlpha (0.7f));
        g.strokePath (track, roundedStroke (1.0f*s), juce::AffineTransform::translation (0.8f*s, 0.8f*s));
        if (proportion > 0.00001f)
        {
            g.setColour (juce::Colour (0xff334b3a).withAlpha (0.35f));
            g.strokePath (active, roundedStroke (11.6f*s), juce::AffineTransform::translation (0, 0.6f*s));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3e5948), cx-radius, cy-radius,
                                                    juce::Colour (0xff779079), cx+radius, cy+radius, false));
            g.strokePath (active, roundedStroke (9.5f*s));
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (active, roundedStroke (0.8f*s), juce::AffineTransform::translation (-1.0f*s, -1.0f*s));
        }
        const float bodyRadius = 92.7f * s;
        auto body = juce::Rectangle<float> (bodyRadius*2, bodyRadius*2).withCentre ({cx, cy});
        juce::Path bodyPath; bodyPath.addEllipse (body);
        juce::DropShadow (juce::Colours::black.withAlpha (0.33f), juce::jmax (1, juce::roundToInt (9*s)),
                          {juce::roundToInt (2*s), juce::roundToInt (6*s)}).drawForPath (g, bodyPath);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), body.getX(), body.getY(),
                                                juce::Colour (0xffa5a18f), body.getRight(), body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xfffefbf5)); g.drawEllipse (body.reduced (0.7f*s), 1.1f*s);
        auto bevel = body.reduced (2.7f*s);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffefcf8), cx-bodyRadius*0.5f, cy-bodyRadius,
                                                juce::Colour (0xffd2cec3), cx+bodyRadius*0.7f, cy+bodyRadius, false));
        g.fillEllipse (bevel);
        auto face = body.reduced (7.0f*s);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffffefb), cx-bodyRadius*0.45f, cy-bodyRadius*0.7f,
                                                juce::Colour (0xffe7e4dc), cx+bodyRadius*0.7f, cy+bodyRadius, false));
        g.fillEllipse (face);
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip; clip.addEllipse (face); g.reduceClipRegion (clip);
            juce::Random grain (0x4750434f5245LL);
            for (int i=0; i<1500; ++i)
            {
                const auto px = face.getX()+grain.nextFloat()*face.getWidth();
                const auto py = face.getY()+grain.nextFloat()*face.getHeight();
                g.setColour (i%2 ? juce::Colours::white.withAlpha (0.23f) : juce::Colour (0xff988f7c).withAlpha (0.065f));
                g.fillEllipse (px, py, 0.75f*s, 0.75f*s);
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawEllipse (face, 1.3f*s);
        juce::Path mark;
        mark.startNewSubPath (cx+std::sin(angle)*bodyRadius*0.44f, cy-std::cos(angle)*bodyRadius*0.44f);
        mark.lineTo (cx+std::sin(angle)*bodyRadius*0.79f, cy-std::cos(angle)*bodyRadius*0.79f);
        g.setColour (juce::Colour (0xff283b2f).withAlpha (0.65f)); g.strokePath (mark, roundedStroke (8.7f*s));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff38523f), cx-bodyRadius, cy-bodyRadius,
                                                juce::Colour (0xff6a846b), cx+bodyRadius, cy+bodyRadius, false));
        g.strokePath (mark, roundedStroke (6.8f*s));
        g.setColour (juce::Colours::white.withAlpha (0.23f));
        g.strokePath (mark, roundedStroke (0.7f*s), juce::AffineTransform::translation (0.7f*s, 0.7f*s));
    }
};
}


namespace {
class SliderBinding {
public:
    AmountSlider slider;
    explicit SliderBinding(GillDeEsserAudioProcessor& p,const char* id,const char* name,double initial)
        : parameter(*p.apvts.getParameter(id)) {
        slider.setName(name);slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,130,31);
        const auto& r=parameter.getNormalisableRange();slider.setRange(r.start,r.end,r.interval);slider.setSkewFactor(r.skew,r.symmetricSkew);
        slider.setDoubleClickReturnValue(true,initial);slider.setScrollWheelEnabled(false);
        attachment=std::make_unique<juce::ParameterAttachment>(parameter,[this](float v){const juce::ScopedValueSetter<bool> sync(hostSync,true);slider.setValue(v,juce::sendNotificationSync);});
        slider.onDragStart=[this]{if(depth++==0)attachment->beginGesture();};
        slider.onDragEnd=[this]{if(depth>0&&--depth==0)attachment->endGesture();};
        slider.onValueChange=[this]{if(hostSync)return;const float v=static_cast<float>(slider.getValue());if(std::abs(parameter.convertTo0to1(v)-parameter.getValue())<1.e-7f)return;
            if(depth>0)attachment->setValueAsPartOfGesture(v);else attachment->setValueAsCompleteGesture(v);};
        attachment->sendInitialUpdate();
    }
    ~SliderBinding(){slider.onValueChange={};slider.onDragStart={};slider.onDragEnd={};if(depth>0)attachment->endGesture();}
private:
    juce::RangedAudioParameter& parameter;std::unique_ptr<juce::ParameterAttachment> attachment;bool hostSync=false;int depth=0;
};
class SibilanceDisplay final : public juce::Component,public juce::SettableTooltipClient,private juce::Timer {
public:
    explicit SibilanceDisplay(GillDeEsserAudioProcessor& processor):p(processor),fft(11){
        setName("S-BAND DISPLAY");setWantsKeyboardFocus(true);setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip("S-BAND ZIEHEN: FREQUENZ | LISTEN S: BEREICH ABHOEREN | PRE / POST: LINKER KANAL, DBFS | SCHATTIERUNG: -3 DB FILTERBAND");
        pre.fill(-100);post.fill(-100);for(size_t i=0;i<window.size();++i){window[i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*static_cast<float>(i)/2047.f);windowSum+=window[i];}startTimerHz(25);
    }
    ~SibilanceDisplay()override{finish();}
    juce::Rectangle<float> plot()const{return getLocalBounds().toFloat().withTrimmedLeft(37).withTrimmedRight(12).withTrimmedTop(27).withTrimmedBottom(24);}
    float xFor(double hz)const{const auto r=plot();return r.getX()+static_cast<float>(std::log(juce::jlimit(20.,20000.,hz)/20.)/std::log(1000.))*r.getWidth();}
    double hzFor(float x)const{const auto r=plot();return 20*std::pow(1000.,juce::jlimit(0.f,1.f,(x-r.getX())/r.getWidth()));}
    double targetHz()const{return std::min(static_cast<double>(p.apvts.getRawParameterValue("frequency")->load()),.45*p.getUiSampleRate());}
    void paint(juce::Graphics& g)override{
        auto r=getLocalBounds().toFloat().reduced(1);g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd5dccc),0,0,juce::Colour(0xffa9b8a0),0,r.getBottom(),false));g.fillRoundedRectangle(r,11);
        g.setColour(ink.withAlpha(.5f));g.drawRoundedRectangle(r,11,1);g.setColour(juce::Colours::white.withAlpha(.6f));g.drawRoundedRectangle(r.reduced(2),9,.7f);
        const auto a=plot();const auto y=[&](float db){return a.getBottom()-juce::jlimit(0.f,1.f,(db+90.f)/90.f)*a.getHeight();};
        g.setFont(coreFont(10,true));g.setColour(ink.withAlpha(.65f));g.drawText("DBFS",4,8,35,14,juce::Justification::centred);
        g.drawText("PRE / POST | LEFT",static_cast<int>(a.getX()),7,150,14,juce::Justification::centredLeft);
        for(int db:{0,-24,-48,-72}){g.setColour(ink.withAlpha(.1f));g.drawHorizontalLine(static_cast<int>(y(static_cast<float>(db))),a.getX(),a.getRight());g.setColour(ink.withAlpha(.6f));g.drawText(juce::String(db),2,static_cast<int>(y(static_cast<float>(db)))-7,29,14,juce::Justification::right);}
        for(int hz:{20,100,1000,5000,10000,20000}){const float x=xFor(hz);g.setColour(ink.withAlpha(.10f));g.drawVerticalLine(static_cast<int>(x),a.getY(),a.getBottom());g.setColour(ink.withAlpha(.7f));g.drawText(hz>=1000?juce::String(hz/1000)+"K":juce::String(hz),static_cast<int>(x)-15,static_cast<int>(a.getBottom())+6,30,13,juce::Justification::centred);}
        const double fs=p.getUiSampleRate(),centre=targetHz();const auto edges=gilldeesser::bandEdges(centre,fs);
        const float left=xFor(edges[0]),right=xFor(edges[1]),cx=xFor(centre);
        {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(a.toNearestInt());
            g.setColour(juce::Colour(0xfff4f6e7).withAlpha(.35f));g.fillRect(left,a.getY(),right-left,a.getHeight());
            paintCurve(g,pre,sage.withAlpha(.50f),a,fs);paintCurve(g,post,ink,a,fs);
            g.setColour(sage.withAlpha(.60f));g.drawVerticalLine(static_cast<int>(left),a.getY(),a.getBottom());g.drawVerticalLine(static_cast<int>(right),a.getY(),a.getBottom());
            g.setColour(ink.withAlpha(.7f));g.drawVerticalLine(static_cast<int>(cx),a.getY(),a.getBottom());
            if(fs*.5<20000){g.setColour(juce::Colour(0xffe2e4d7).withAlpha(.65f));g.fillRect(xFor(fs*.5),a.getY(),a.getRight()-xFor(fs*.5),a.getHeight());}
        }
        g.setColour(sage);g.fillRoundedRectangle(cx-27,5,54,18,5);g.setFont(coreFont(10,true));g.setColour(juce::Colour(0xfffaf8ef));g.drawText("S-BAND",static_cast<int>(cx)-27,5,54,18,juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent& e)override{if(!e.mods.isLeftButtonDown()||!getLocalBounds().contains(e.getPosition()))return;finish();p.apvts.getParameter("frequency")->beginChangeGesture();dragging=true;update(e.position.x);}
    void mouseDrag(const juce::MouseEvent& e)override{if(dragging)update(e.position.x);}
    void mouseUp(const juce::MouseEvent&)override{finish();}
    void mouseDoubleClick(const juce::MouseEvent&)override{finish();auto* v=p.apvts.getParameter("frequency");v->beginChangeGesture();v->setValueNotifyingHost(v->convertTo0to1(6500));v->endChangeGesture();repaint();}
    bool keyPressed(const juce::KeyPress& key)override{const bool up=key==juce::KeyPress::rightKey,down=key==juce::KeyPress::leftKey;if(key.getModifiers().isAnyModifierKeyDown()||(!up&&!down))return false;
        auto* v=p.apvts.getParameter("frequency");const auto next=v->convertTo0to1(juce::jlimit(2500.f,12000.f,p.apvts.getRawParameterValue("frequency")->load()+(up?100.f:-100.f)));if(std::abs(next-v->getValue())>1.e-7f){v->beginChangeGesture();v->setValueNotifyingHost(next);v->endChangeGesture();repaint();}return true;}
private:
    void finish(){if(dragging){p.apvts.getParameter("frequency")->endChangeGesture();dragging=false;}}
    void update(float x){auto* v=p.apvts.getParameter("frequency");const auto maximum=std::min(12000.,.45*p.getUiSampleRate());v->setValueNotifyingHost(v->convertTo0to1(static_cast<float>(juce::jlimit(2500.,maximum,hzFor(x)))));repaint();}
    void transform(const std::array<float,2048>& input,std::array<float,1024>& out){std::array<float,4096> temp{};for(size_t i=0;i<input.size();++i)temp[i]=input[i]*window[i];fft.performFrequencyOnlyForwardTransform(temp.data());for(size_t i=0;i<out.size();++i)out[i]=juce::Decibels::gainToDecibels(std::max(1.e-9f,temp[i]*2.f/windowSum),-100.f);}
    void paintCurve(juce::Graphics& g,const std::array<float,1024>& bins,juce::Colour colour,juce::Rectangle<float> a,double fs){juce::Path path;bool started=false;
        for(int i=0;i<=static_cast<int>(a.getWidth());++i){const float x=a.getX()+static_cast<float>(i);const auto hz=hzFor(x);if(hz>=fs*.5)break;
            const double b=hz*2048/fs;const auto k=juce::jlimit(0,1022,static_cast<int>(b));const float mix=static_cast<float>(b-k);const float db=bins[static_cast<size_t>(k)]*(1-mix)+bins[static_cast<size_t>(k+1)]*mix;
            const float y=a.getBottom()-juce::jlimit(0.f,1.f,(db+90.f)/90.f)*a.getHeight();if(!started){path.startNewSubPath(x,y);started=true;}else path.lineTo(x,y);}
        g.setColour(colour);g.strokePath(path,juce::PathStrokeType(1.6f));}
    void timerCallback()override{std::array<float,2048> a{},b{};if(p.readSpectrum(a,b)){transform(a,pre);transform(b,post);}repaint();}
    GillDeEsserAudioProcessor& p;juce::dsp::FFT fft;std::array<float,2048>window{};std::array<float,1024>pre{},post{};float windowSum=0;bool dragging=false;
};
class ReductionMeter final : public juce::Component,private juce::Timer{
public:explicit ReductionMeter(GillDeEsserAudioProcessor& processor):p(processor){setName("GAIN REDUCTION METER");startTimerHz(25);}
    void paint(juce::Graphics& g)override{const float s=getWidth()/100.f;const auto gr=juce::jlimit(0.f,12.f,p.getReductionDb());
       #if JUCE_MAC
        // Mac font metrics need a separate row for the bottom tick and readout.
        const auto bar=juce::Rectangle<float>(22*s,24*s,19*s,86*s);
       #else
        const auto bar=juce::Rectangle<float>(22*s,24*s,19*s,96*s);
       #endif
        g.setFont(coreFont(12*s,true));g.setColour(ink);g.drawText("REDUCTION",0,0,getWidth(),static_cast<int>(18*s),juce::Justification::centred);
        g.setColour(juce::Colour(0xff594f3b));g.fillRoundedRectangle(bar,4*s);
       #if JUCE_MAC
        for(int i=0;i<24;++i){const float y=bar.getY()+3*s+i*(bar.getHeight()-8*s)/24.f;g.setColour(i<gr*2?juce::Colour(0xffc5ddbd):juce::Colour(0xff8a816b));g.fillRect(bar.getX()+4*s,y,11*s,2.1f*s);}
        g.setFont(coreFont(10*s));g.setColour(ink);for(int db:{0,3,6,9,12})g.drawText(juce::String(db),static_cast<int>(47*s),juce::roundToInt(bar.getY()+db*bar.getHeight()/12.f-7.5f*s),static_cast<int>(27*s),static_cast<int>(15*s),juce::Justification::left);
        g.setFont(coreFont(21*s,true));g.drawText((gr>.005f?"-":"")+juce::String(gr,1)+" DB",0,static_cast<int>(123*s),getWidth(),static_cast<int>(28*s),juce::Justification::centred);
       #else
        for(int i=0;i<24;++i){const float y=bar.getY()+3*s+i*3.7f*s;g.setColour(i<gr*2?juce::Colour(0xffc5ddbd):juce::Colour(0xff8a816b));g.fillRect(bar.getX()+4*s,y,11*s,2.1f*s);}
        g.setFont(coreFont(10*s));g.setColour(ink);for(int db:{0,3,6,9,12})g.drawText(juce::String(db),static_cast<int>(47*s),static_cast<int>((20+db*8)*s),static_cast<int>(27*s),static_cast<int>(15*s),juce::Justification::left);
        g.setFont(coreFont(21*s,true));g.drawText((gr>.005f?"-":"")+juce::String(gr,1)+" DB",0,static_cast<int>(119*s),getWidth(),static_cast<int>(28*s),juce::Justification::centred);
       #endif
    }
private:void timerCallback()override{repaint();}GillDeEsserAudioProcessor& p;
};
}
struct GillDeEsserAudioProcessorEditor::Impl {
    GillDeEsserAudioProcessorEditor& owner;GillDeEsserAudioProcessor& p;CoreLookAndFeel look;juce::Image reference;
    SliderBinding amount,frequency;SibilanceDisplay graph;ReductionMeter meter;juce::TextButton listen{"LISTEN S"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;juce::TooltipWindow tooltip;
    Impl(GillDeEsserAudioProcessorEditor& editor,GillDeEsserAudioProcessor& processor):owner(editor),p(processor),amount(p,"amount","AMOUNT",55),frequency(p,"frequency","FREQUENCY",6500),graph(p),meter(p),tooltip(&editor,600){
        owner.setLookAndFeel(&look);reference=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);
        owner.addAndMakeVisible(amount.slider);owner.addAndMakeVisible(frequency.slider);owner.addAndMakeVisible(graph);owner.addAndMakeVisible(meter);owner.addAndMakeVisible(listen);
        amount.slider.textFromValueFunction=[](double v){return juce::String(v,0)+" %";};amount.slider.valueFromTextFunction=[](const juce::String& s){return s.replaceCharacter(',','.').getDoubleValue();};amount.slider.updateText();
        frequency.slider.textFromValueFunction=[](double v){return juce::String(v/1000.,2)+" KHZ";};frequency.slider.valueFromTextFunction=[](const juce::String& s){const auto v=s.replaceCharacter(',','.').getDoubleValue();return s.containsIgnoreCase("k")||v<100?v*1000:v;};frequency.slider.updateText();
        amount.slider.setTooltip("S-LAUTE REDUZIEREN | ZIEHEN ODER ZAHL EINGEBEN | DOPPELKLICK: 55 %");
        frequency.slider.setTooltip("MITTE DES S-BANDS | AUCH IM SPEKTRUM ZIEHBAR | DOPPELKLICK: 6.50 KHZ | BEI NIEDRIGER SAMPLERATE AUF 45 % DER SAMPLERATE BEGRENZT");
        listen.setName("LISTEN S");listen.setClickingTogglesState(true);listen.setTooltip("NUR DAS AUSGEWAEHLTE FILTERBAND ABHOEREN; KEINE REINE SPRACHERKENNUNG | DANACH WIEDER AUSSCHALTEN");
        listen.setColour(juce::TextButton::textColourOffId,ink);listen.setColour(juce::TextButton::textColourOnId,juce::Colour(0xfffaf8ef));
        listenAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"listen",listen);
    }
    ~Impl(){listenAttachment.reset();owner.setLookAndFeel(nullptr);}
    void resized(){const float s=owner.getWidth()/480.f;look.scale=s;
        graph.setBounds(juce::Rectangle<float>(20*s,66*s,440*s,114*s).toNearestInt());
        amount.slider.setBounds(juce::Rectangle<float>(31*s,203*s,138*s,115*s).toNearestInt());
        frequency.slider.setBounds(juce::Rectangle<float>(185*s,203*s,135*s,87*s).toNearestInt());
        meter.setBounds(juce::Rectangle<float>(359*s,187*s,89*s,138*s).toNearestInt());
        listen.setBounds(juce::Rectangle<float>(193*s,293*s,120*s,28*s).toNearestInt());amount.slider.resized();frequency.slider.resized();
    }
    void paint(juce::Graphics& g){const float s=owner.getWidth()/480.f,w=static_cast<float>(owner.getWidth()),h=static_cast<float>(owner.getHeight());g.fillAll(juce::Colour(0xffc9ac85));
        if(reference.isValid()){g.drawImage(reference,0,0,static_cast<int>(w),static_cast<int>(h),950,285,170,805);
            const int edge=juce::roundToInt(10*s),corner=juce::roundToInt(20*s);
            g.drawImage(reference,corner,0,static_cast<int>(w)-corner*2,edge,150,82,955,42);g.drawImage(reference,corner,static_cast<int>(h)-edge,static_cast<int>(w)-corner*2,edge,150,1115,955,42);
            g.drawImage(reference,0,corner,edge,static_cast<int>(h)-corner*2,90,142,42,955);g.drawImage(reference,static_cast<int>(w)-edge,corner,edge,static_cast<int>(h)-corner*2,1123,142,42,955);
            g.drawImage(reference,0,0,corner,corner,90,82,60,60);g.drawImage(reference,static_cast<int>(w)-corner,0,corner,corner,1105,82,60,60);
            g.drawImage(reference,0,static_cast<int>(h)-corner,corner,corner,90,1097,60,60);g.drawImage(reference,static_cast<int>(w)-corner,static_cast<int>(h)-corner,corner,corner,1105,1097,60,60);
            g.drawImage(reference,juce::roundToInt(24*s),juce::roundToInt(16*s),juce::roundToInt(52*s),juce::roundToInt(36*s),224,146,174,122);}
        g.setColour(juce::Colour(0xff62462e));g.drawLine(90*s,16*s,90*s,52*s,.8f*s);g.setFont(coreFont(23*s));g.setColour(juce::Colour(0xff362c1f));g.drawText("GILL-DE-ESSER",juce::Rectangle<float>(109*s,19*s,345*s,30*s),juce::Justification::centredLeft,false);
        g.setFont(coreFont(12*s,true));g.setColour(ink);g.drawText("AMOUNT",juce::Rectangle<float>(31*s,185*s,138*s,17*s),juce::Justification::centred,false);g.drawText("FREQUENCY",juce::Rectangle<float>(185*s,185*s,135*s,17*s),juce::Justification::centred,false);
    }
};
GillDeEsserAudioProcessorEditor::GillDeEsserAudioProcessorEditor(GillDeEsserAudioProcessor& p):juce::AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);setResizable(true,true);setResizeLimits(480,330,960,660);if(auto* c=getConstrainer())c->setFixedAspectRatio(480./330.);setSize(480,330);}
GillDeEsserAudioProcessorEditor::~GillDeEsserAudioProcessorEditor()=default;
void GillDeEsserAudioProcessorEditor::paint(juce::Graphics& g){impl->paint(g);}
void GillDeEsserAudioProcessorEditor::resized(){if(impl)impl->resized();}
