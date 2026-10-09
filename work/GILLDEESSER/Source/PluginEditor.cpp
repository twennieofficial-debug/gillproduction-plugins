#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "../../GILLCommon/ScalpelUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>

namespace
{
const juce::Colour ink=gill::scalpel::ink, sage=gill::scalpel::cyan;

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

class CoreLookAndFeel final : public gill::scalpel::Look {  };
}


namespace {
class SliderBinding {
public:
    AmountSlider slider;
    explicit SliderBinding(GillDeEsserAudioProcessor& p,const char* id,const char* name,double initial)
        : parameter(*p.apvts.getParameter(id)) {
        slider.setColour(juce::Slider::textBoxTextColourId,gill::prism::white);slider.setColour(juce::Slider::textBoxBackgroundColourId,gill::prism::dark);slider.setColour(juce::Slider::textBoxOutlineColourId,gill::prism::cyan.withAlpha(.3f));slider.setName(name);slider.setComponentID(id);slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
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
        auto r=getLocalBounds().toFloat().reduced(3);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff092623),0,0,juce::Colour(0xff020d0c),0,r.getBottom(),false));g.fillRoundedRectangle(r,11);
        g.setColour(gill::prism::white.withAlpha(.5f));g.drawRoundedRectangle(r,11,1);g.setColour(juce::Colours::white.withAlpha(.6f));g.drawRoundedRectangle(r.reduced(2),9,.7f);
        const auto a=plot();const auto y=[&](float db){return a.getBottom()-juce::jlimit(0.f,1.f,(db+90.f)/90.f)*a.getHeight();};
        g.setFont(coreFont(10,true));g.setColour(gill::prism::white.withAlpha(.65f));g.drawText("DBFS",4,8,35,14,juce::Justification::centred);
        g.drawText("PRE / POST | LEFT",static_cast<int>(a.getX()),7,150,14,juce::Justification::centredLeft);
        for(int db:{0,-24,-48,-72}){g.setColour(gill::prism::white.withAlpha(.1f));g.drawHorizontalLine(static_cast<int>(y(static_cast<float>(db))),a.getX(),a.getRight());g.setColour(gill::prism::white.withAlpha(.6f));g.drawText(juce::String(db),2,static_cast<int>(y(static_cast<float>(db)))-7,29,14,juce::Justification::right);}
        for(int hz:{100,1000,10000}){const float x=xFor(hz);g.setColour(gill::prism::white.withAlpha(.10f));g.drawVerticalLine(static_cast<int>(x),a.getY(),a.getBottom());g.setColour(gill::prism::white.withAlpha(.7f));g.drawText(hz>=1000?juce::String(hz/1000)+"K":juce::String(hz),static_cast<int>(x)-15,static_cast<int>(a.getBottom())+6,30,13,juce::Justification::centred);}
        const double fs=p.getUiSampleRate(),centre=targetHz();const auto edges=gilldeesser::bandEdges(centre,fs);
        const float left=xFor(edges[0]),right=xFor(edges[1]),cx=xFor(centre);
        {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(a.toNearestInt());
            g.setColour(juce::Colour(0xffa4d4ed).withAlpha(.35f));g.fillRect(left,a.getY(),right-left,a.getHeight());
            paintCurve(g,pre,sage.withAlpha(.50f),a,fs);paintCurve(g,post,gill::prism::white,a,fs);
            g.setColour(sage.withAlpha(.60f));g.drawVerticalLine(static_cast<int>(left),a.getY(),a.getBottom());g.drawVerticalLine(static_cast<int>(right),a.getY(),a.getBottom());
            g.setColour(gill::prism::white.withAlpha(.7f));g.drawVerticalLine(static_cast<int>(cx),a.getY(),a.getBottom());
            if(fs*.5<20000){g.setColour(juce::Colour(0xff314b65).withAlpha(.65f));g.fillRect(xFor(fs*.5),a.getY(),a.getRight()-xFor(fs*.5),a.getHeight());}
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
    void paint(juce::Graphics& g)override{
        const float gr=juce::jlimit(0.f,12.f,p.getReductionDb());const float h=float(getHeight());
        gill::scalpel::text(g,"REDUCTION",{0,0,float(getWidth()),18},10);
        const juce::Rectangle<float> bar(18,25,13,h-60);g.setColour(juce::Colours::black);g.fillRoundedRectangle(bar,3);
        for(int i=0;i<24;++i){const float y=bar.getY()+i*bar.getHeight()/24;g.setColour(i<gr*2?gill::scalpel::cyan:gill::scalpel::cyan.withAlpha(.13f));g.fillRect(bar.getX()+2,y,9.f,bar.getHeight()/24-1);}
        for(int db:{0,3,6,9,12})gill::scalpel::text(g,juce::String(db),{35,bar.getY()+db*bar.getHeight()/12-6,22,12},9);
        gill::scalpel::text(g,juce::String(gr,1)+" DB",{0,h-21,float(getWidth()),20},13,gill::scalpel::cyan);
    }
private:void timerCallback()override{repaint();}GillDeEsserAudioProcessor& p;
};
}
struct GillDeEsserAudioProcessorEditor::Impl {
    GillDeEsserAudioProcessorEditor& owner;GillDeEsserAudioProcessor& p;CoreLookAndFeel look;juce::Image reference;
    juce::TextButton bypass{"BYPASS"};std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    SliderBinding amount,frequency;SibilanceDisplay graph;ReductionMeter meter;juce::TextButton listen{"LISTEN S"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;juce::TooltipWindow tooltip;
    gill::QualitySelector quality{p.apvts, p};
    Impl(GillDeEsserAudioProcessorEditor& editor,GillDeEsserAudioProcessor& processor):owner(editor),p(processor),amount(p,"amount","AMOUNT",55),frequency(p,"frequency","FREQUENCY",6500),graph(p),meter(p),tooltip(&editor,600){
        amount.slider.setSliderStyle(juce::Slider::LinearVertical);frequency.slider.setSliderStyle(juce::Slider::LinearHorizontal);owner.setLookAndFeel(&look);owner.addAndMakeVisible(quality);quality.setLookAndFeel(&look);bypass.setName("BYPASS");bypass.setComponentID("bypass");bypass.setClickingTogglesState(true);owner.addAndMakeVisible(bypass);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);reference=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);
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
    void resized(){const float s=owner.getWidth()/420.f;look.scale=s;
        quality.setBounds(juce::Rectangle<float>(284*s,73*s,110*s,26*s).toNearestInt());
        bypass.setBounds(juce::Rectangle<float>(25*s,73*s,84*s,26*s).toNearestInt());
        graph.setBounds(juce::Rectangle<float>(163*s,121*s,230*s,345*s).toNearestInt());
        amount.slider.setBounds(juce::Rectangle<float>(31*s,155*s,106*s,387*s).toNearestInt());
        frequency.slider.setBounds(juce::Rectangle<float>(164*s,504*s,146*s,57*s).toNearestInt());
        meter.setBounds(juce::Rectangle<float>(317*s,482*s,79*s,132*s).toNearestInt());
        listen.setBounds(juce::Rectangle<float>(164*s,578*s,146*s,29*s).toNearestInt());
    }
    void paint(juce::Graphics& g){g.addTransform(juce::AffineTransform::scale(owner.getWidth()/420.f));
        gill::scalpel::chassis(g,420,640,"GILL-DE-ESSER");
        gill::scalpel::panel(g,{22,121,120,435});gill::scalpel::text(g,"C L E A N",{29,127,106,25},14);
        gill::scalpel::text(g,"AMOUNT",{29,540,106,17},10);
        gill::scalpel::panel(g,{156,478,244,137});gill::scalpel::text(g,"S-BAND FREQUENCY",{163,483,148,20},11);
    }
};
GillDeEsserAudioProcessorEditor::GillDeEsserAudioProcessorEditor(GillDeEsserAudioProcessor& p):juce::AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);setResizable(false,false);setResizeLimits(420,640,420,640);setSize(420,640);}
GillDeEsserAudioProcessorEditor::~GillDeEsserAudioProcessorEditor()=default;
void GillDeEsserAudioProcessorEditor::paint(juce::Graphics& g){impl->paint(g);}
void GillDeEsserAudioProcessorEditor::resized(){if(impl)impl->resized();}
