#include "../../GILLCommon/HostKeyboardPolicy.h"
#include "Processor.h"
#include "Theme.h"
namespace mobina {
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
  using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
  using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  Processor &p;
  skin::Look look;
  std::array<juce::Slider, 12> knobs;
  std::array<juce::Label, 12> labels;
  std::vector<std::unique_ptr<SA>> attach;
  juce::ComboBox preset, quality, bypass, sync;
  std::unique_ptr<CA> qa, ba, sy;
  std::unique_ptr<gill::HostKeyboardPolicy> keys;
  juce::TooltipWindow tips{this, 650};
  void timerCallback() override {
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    repaint();
  }

public:
  explicit Editor(Processor &a) : AudioProcessorEditor(a), p(a) {
    
    setLookAndFeel(&look);
    setSize(660, 490);
    for (int i = 0; i < 12; ++i) {
      auto &k = knobs[i];
      k.setComponentID("parameter" + juce::String(i));
      k.setName(specs[i].name);
      addAndMakeVisible(k);
      k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
      k.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 75, 19);
      k.setTextValueSuffix(juce::String(" ") + specs[i].unit);
      k.setNumDecimalPlacesToDisplay(i == 2 || i == 5 ? 2 : 0);
      attach.push_back(std::make_unique<SA>(p.state, specs[i].id, k));
      k.textFromValueFunction = [i](double value) {
        return juce::String(value, i == 2 || i == 5 ? 2 : 0);
      };
      k.updateText();
      addAndMakeVisible(labels[i]);
      labels[i].setText(specs[i].name, juce::dontSendNotification);
      labels[i].setJustificationType(juce::Justification::centred);
      labels[i].setFont(skin::font(10.5f, true));
    }
    for (auto *c : {&preset, &quality, &bypass, &sync})
      addAndMakeVisible(c);
    preset.setName("Preset"); quality.setName("Quality"); bypass.setName("Bypass"); sync.setName("Sync");
    for (int i = 0; i < 8; ++i)
      preset.addItem(p.getProgramName(i), i + 1);
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    preset.onChange = [this] {
      p.setCurrentProgram(preset.getSelectedId() - 1);
    };
    quality.addItemList({"LIVE", "PRO"}, 1);
    bypass.addItemList({"ACTIVE", "BYPASS"}, 1);
    sync.addItemList({"FREE SWIM", "2 BARS"}, 1);
    qa = std::make_unique<CA>(p.state, "gillQuality", quality);
    ba = std::make_unique<CA>(p.state, "bypass", bypass);
    sy = std::make_unique<CA>(p.state, "sync", sync);
    knobs[0].setTooltip("More soft ambience and stereo bloom. Start around "
                        "25–45 for a lead vocal.");
    knobs[1].setTooltip(
        "Softens upper frequencies and places the signal farther back.");
    knobs[4].setTooltip("Gentle stereo motion. Low bass stays centered.");
    knobs[9].setTooltip(
        "Pulls ambience back while the vocal or beat is active.");
    quality.setTooltip("Both modes add zero lookahead latency. PRO adds a "
                       "second moving delay voice.");
    keys = std::make_unique<gill::HostKeyboardPolicy>(*this);
    resized();
    startTimerHz(25);
  }
  ~Editor() override {
    stopTimer();
    keys.reset();
    setLookAndFeel(nullptr);
  }
  void resized() override {
    preset.setBounds(290, 29, 226, 30);quality.setBounds(530, 29, 105, 30);
    bypass.setBounds(522, 151, 110, 28);sync.setBounds(376, 151, 136, 28);
    for (int i = 0; i < 12; ++i) {
      const int x = 29 + (i % 6) * 102, y = 219 + (i / 6) * 117;
      labels[i].setBounds(x, y, 91, 18);knobs[i].setBounds(x, y + 16, 91, 91);
    }
  }
  void paint(juce::Graphics &g) override {
    using namespace skin;
    g.fillAll(juce::Colour(0xffefb7cc));candy(g,{5,5,650,477},blush,25);
    g.setColour(cream.withAlpha(.48f));for(int y=18;y<475;y+=17)for(int x=17+(y%2)*8;x<650;x+=18)g.fillEllipse(float(x),float(y),2,2);
    candy(g,{17,16,52,51},plum,15);logo(g,{24,24,38,34});
    g.setFont(juce::Font(juce::FontOptions("Arial Rounded MT Bold",32,juce::Font::bold)));g.setColour(plum);g.drawText("MOBINA",81,18,185,39,juce::Justification::left);

    g.setFont(font(9,true));g.drawText("PICK A DAYDREAM",294,14,213,12,juce::Justification::left);g.drawText("QUALITY",534,14,100,12,juce::Justification::left);
    juce::Path cloud;cloud.startNewSubPath(44,141);cloud.cubicTo(7,138,9,94,48,95);cloud.cubicTo(40,67,89,62,103,83);cloud.cubicTo(127,54,172,63,180,84);cloud.cubicTo(211,60,243,73,251,89);cloud.lineTo(566,89);cloud.cubicTo(604,69,637,91,625,112);cloud.cubicTo(654,125,637,150,613,146);cloud.lineTo(44,146);cloud.closeSubPath();
    g.setColour(plum.withAlpha(.19f));g.fillPath(cloud,juce::AffineTransform::translation(1,4));g.setColour(cream);g.fillPath(cloud);g.setColour(plum);g.strokePath(cloud,juce::PathStrokeType(1.8f));
    g.setColour(plum);g.setFont(font(11.5f,true));g.drawText("DREAMY R&B",49,97,145,20,juce::Justification::left);g.setFont(font(10));g.drawText("soft space. floating stereo.",49,117,170,16,juce::Justification::left);
    for(int j=0;j<3;++j){juce::Path wave;for(int i=0;i<285;++i){float x=249.f+i,y=116+std::sin(i*.026f+j*.7f+p.pan.load())*(5+j*3)*(0.65f+std::min(.6f,p.meter.load()));if(i==0)wave.startNewSubPath(x,y);else wave.lineTo(x,y);}g.setColour(j==0?rose:j==1?lilac:plum.withAlpha(.5f));g.strokePath(wave,juce::PathStrokeType(j==2?1.f:2.f));}
    star(g,552,116,10,juce::Colour(0xffffdfa0));heart(g,585,116,8);star(g,264,66,6,cream);
    g.setColour(plum);g.setFont(font(10.5f,true));g.drawText("VOCALS + BEATS",30,157,160,15,juce::Justification::left);g.setFont(font(9.5f));g.drawText("made to feel like a warm hug",30,172,250,14,juce::Justification::left);
    candy(g,{18,208,624,120},juce::Colour(0xffffeaf0),18);candy(g,{18,336,624,115},juce::Colour(0xffeee6f8),18);
    candy(g,{30,196,131,18},rose,7);g.setColour(plum);g.setFont(font(9,true));g.drawText("01  /  INTO THE CLOUDS",35,196,120,16,juce::Justification::centred);
    g.setColour(plum.withAlpha(.25f));for(int i=1;i<6;++i){g.drawVerticalLine(23+i*102,230,305);g.drawVerticalLine(23+i*102,354,431);}
    star(g,33,468,5,cream);heart(g,626,469,5);g.setColour(plum);g.setFont(font(9.5f,true));g.drawText("SOFT HEART. WIDE WORLD.",49,458,225,19,juce::Justification::left);g.setFont(font(9));g.drawText("0 SAMPLES ADDED LATENCY",395,458,212,19,juce::Justification::right);
  }
};
juce::AudioProcessorEditor *Processor::createEditor() {
  return new Editor(*this);
}
} // namespace mobina
