#include "../../../GILLCommon/HostKeyboardPolicy.h"
#include "../../../GILLCommon/PrismUi.h"
#include "../../../GILLCommon/MonolithUi.h"
#include "../../../GILLCommon/ScalpelUi.h"
#include "../../../Effects16/advanced-ui/AdvancedScalpel.h"
#include "IrFit.h"
#include "Processor.h"
#include <future>
namespace studio {
juce::AudioProcessorEditor* createRackEditor(Processor&);
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
  using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
  using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  // Bounds only user gestures; attachments retain the original automation range.
  struct CrossoverSlider : juce::Slider {
    std::function<double(double)> userBound;
    double snapValue(double value, DragMode mode) override {
      return userBound ? userBound(value) : juce::Slider::snapValue(value, mode);
    }
    bool keyPressed(const juce::KeyPress& key) override {
      if (userBound && !key.getModifiers().isAnyModifierKeyDown()) {
        const int direction = key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey ? 1
                            : key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey ? -1 : 0;
        if (direction) {
          setValue(userBound(getValue() + direction * std::max(1.0, getInterval())), juce::sendNotificationSync);
          return true;
        }
      }
      return juce::Slider::keyPressed(key);
    }
  };
  struct Control {
    juce::Label label;
    CrossoverSlider slider;
    juce::ComboBox choice;
    std::unique_ptr<SA> sa;
    std::unique_ptr<CA> ca;
  };
  struct Page {
    int group = 0;
    std::vector<int> indices;
  };
  Processor &p;
  gill::prism::Look look;
  gill::monolith::Look monolith;
  gill::advanced::ScalpelLook scalpel;
  juce::Slider strength;
  std::unique_ptr<SA> strengthA;
  bool advanced() const { return p.kind==Kind::Alpha || p.kind==Kind::Max || p.kind==Kind::Precision || p.kind==Kind::Deess; }
  int pageSize() const { return advanced()?6:12; }
  juce::TooltipWindow tips{this, 600};
  std::unique_ptr<gill::HostKeyboardPolicy> keyboard;
  juce::ComboBox preset, quality, pages, bypass, delta;
  std::unique_ptr<CA> qualityA, bypassA, deltaA;
  std::array<Control, 12> controls;
  std::array<Control, 3> global;
  std::array<juce::Label, 5> vitalRanges;
  juce::Label vitalStatus;
  std::array<double, 4> vitalValues() const {
    return {v("edge", 1), v("edge", 2), v("edge", 3), v("edge", 4)};
  }
  static std::array<int, 4> vitalOrder(const std::array<double, 4>& saved) {
    std::array<int, 4> order{1, 2, 3, 4};
    std::stable_sort(order.begin(), order.end(), [&saved](int a, int b) { return saved[size_t(a - 1)] < saved[size_t(b - 1)]; });
    return order;
  }
  double boundVital(int slot, double requested) const {
    const auto savedValues = vitalValues();
    const auto order = vitalOrder(savedValues);
    const int rank = int(std::find(order.begin(), order.end(), slot) - order.begin());
    const double current = savedValues[size_t(slot - 1)];
    // A small gap avoids swapping identity on the next drag event at equality.
    const double low = rank == 0 ? 20.0 : std::min(current, savedValues[size_t(order[size_t(rank - 1)] - 1)] + .1);
    const double high = rank == 3 ? 18000.0 : std::max(current, savedValues[size_t(order[size_t(rank + 1)] - 1)] - .1);
    return juce::jlimit(low, high, requested);
  }
  void refreshVital() {
    if (p.kind != Kind::Vital) return;
    const auto savedValues = vitalValues();
    const auto order = vitalOrder(savedValues);
    std::array<double, 4> edges{};
    bool reordered = false, clamped = false;
    for (int i = 0; i < 4; ++i) {
      const double saved = savedValues[size_t(order[size_t(i)] - 1)];
      // Mirrors Split::set and Low::coeff; these are targets during smoothing.
      edges[size_t(i)] = std::clamp(saved, 5.0, p.rate.load() * .45);
      reordered |= order[size_t(i)] != i + 1;
      clamped |= edges[size_t(i)] != saved;
    }
    auto hz = [](double value) { return juce::String(value, std::abs(value - std::round(value)) < .001 ? 0 : 1); };
    for (int i = 0; i < 5; ++i) {
      juce::String range = i == 0 ? "below " + hz(edges[0]) + " Hz"
                          : i == 4 ? "above " + hz(edges[3]) + " Hz"
                          : hz(edges[size_t(i - 1)]) + " - " + hz(edges[size_t(i)]) + " Hz";
      if (i > 0 && i < 4 && edges[size_t(i - 1)] == edges[size_t(i)]) range = hz(edges[size_t(i)]) + " Hz (coincident)";
      vitalRanges[size_t(i)].setText("BAND " + juce::String(i + 1) + "\n" + range, juce::dontSendNotification);
      vitalRanges[size_t(i)].setTooltip(range + ". Effective crossover targets; filters overlap and parameter changes are smoothed.");
    }
    vitalStatus.setText(juce::String(reordered ? "Saved slots reordered. " : "")
      + (clamped ? "Sample-rate limit applied. " : "") + "Drag stays between neighbouring crossovers.", juce::dontSendNotification);
    for (auto& c : controls) if (c.slider.userBound) {
      const int slot = c.slider.getComponentID().substring(1, 2).getIntValue();
      const int rank = int(std::find(order.begin(), order.end(), slot) - order.begin());
      c.label.setText("BOUNDARY " + juce::String(rank + 1) + " / SLOT " + juce::String(slot), juce::dontSendNotification);
      c.slider.setTooltip("Saved crossover slot " + juce::String(slot) + " controls effective boundary " + juce::String(rank + 1)
        + " (bands " + juce::String(rank + 1) + " / " + juce::String(rank + 2) + "): " + hz(edges[size_t(rank)])
        + " Hz. Host automation and saved values remain unchanged; UI edits stay between neighbours.");
    }
  }
  std::vector<Page> pageDefs;
  juce::TextButton undo{"UNDO"}, redo{"REDO"}, ab{"A / B"};
  juce::TextButton learnDrive{"LEARN DRIVE"};
  bool learningDrive = false;
  juce::TextButton importIr{"FIT IR"};
  std::unique_ptr<juce::FileChooser> chooser;
  std::future<IrFit> irJob;
  juce::String irStatus;
  juce::MemoryBlock memories[2];
  std::array<int, 8> rackTypes{};
  int bank = 0;
  int draggedNode = 0;
  juce::RangedAudioParameter *drag = nullptr, *dragGain = nullptr;
  juce::Rectangle<float> graph{25, 110, 710, 78};
  void clear(Control &c) {
    c.slider.userBound = {};
    c.sa.reset();
    c.ca.reset();
    c.slider.setVisible(false);
    c.choice.setVisible(false);
    c.label.setVisible(false);
  }
  void bind(Control &c, const Spec &s) {
    clear(c);
    c.slider.setComponentID(s.id); c.choice.setComponentID(s.id);
    c.label.setText(s.name, juce::dontSendNotification);
    c.label.setVisible(true);
    if (s.choices.empty()) {
      c.slider.setVisible(true);
      c.sa = std::make_unique<SA>(p.state, s.id, c.slider);
      c.slider.textFromValueFunction = [u = s.unit](double v) {
        return juce::String(v, std::abs(v) >= 100 ? 0 : 1) +
               (u.empty() ? "" : " " + juce::String(u));
      };
      c.slider.valueFromTextFunction = [](const juce::String &t) {
        return t.getDoubleValue();
      };
      c.slider.updateText();
      c.slider.setTooltip(s.name + " (" + s.unit + ")");
      if (p.kind == Kind::Vital && s.key == "edge") {
        c.label.setText("CROSSOVER SLOT " + juce::String(s.group), juce::dontSendNotification);
        c.slider.userBound = [this, slot = s.group](double value) { return boundVital(slot, value); };
        c.slider.valueFromTextFunction = [this, slot = s.group](const juce::String& text) { return boundVital(slot, text.getDoubleValue()); };
      }
    } else {
      c.choice.clear(juce::dontSendNotification);
      for (size_t i = 0; i < s.choices.size(); ++i)
        c.choice.addItem(s.choices[i], int(i + 1));
      c.choice.setVisible(true);
      c.ca = std::make_unique<CA>(p.state, s.id, c.choice);
    }
  }
  void selectPage() {
    if (dragGain) {
      dragGain->endChangeGesture();
      dragGain = nullptr;
    }
    if (drag) {
      drag->endChangeGesture();
      drag = nullptr;
    }
    const int which =
        juce::jlimit(0, int(pageDefs.size()) - 1, pages.getSelectedId() - 1);
    const auto &pg = pageDefs[size_t(which)];
    for (size_t i = 0; i < controls.size(); ++i) {
      if (i < pg.indices.size())
        bind(controls[i], p.definition.params[size_t(pg.indices[i])]);
      else
        clear(controls[i]);
    }
    refreshEnabled();
    if(advanced())resized();
    repaint();
  }
  void rebuildPages(int keepGroup = 0) {
    pages.clear(juce::dontSendNotification);
    pageDefs.clear();
    int selected = 1;
    for (size_t group = 0; group < p.definition.groups.size(); ++group) {
      std::vector<int> indices;
      for (size_t i = 0; i < p.definition.params.size(); ++i) {
        const auto &s = p.definition.params[i];
        if (s.group == int(group) &&
            (p.kind != Kind::Rack ||
             rackControlUsed(int(v("module", int(group))), s.key)))
          indices.push_back(int(i));
      }
      for (size_t offset = 0; offset < indices.size(); offset += pageSize()) {
        Page pg;
        pg.group = int(group);
        for (size_t i = offset; i < std::min(offset + pageSize(), indices.size()); ++i)
          pg.indices.push_back(indices[i]);
        pageDefs.push_back(pg);
        pages.addItem(p.definition.groups[group] +
                          (indices.size() > size_t(pageSize())
                               ? " / " + std::to_string(offset / pageSize() + 1)
                               : ""),
                      int(pageDefs.size()));
        if (int(group) == keepGroup && offset == 0)
          selected = int(pageDefs.size());
      }
    }
    pages.setSelectedId(selected, juce::dontSendNotification);
    selectPage();
  }
  void refreshEnabled() {
    const auto &pg = pageDefs[size_t(
        juce::jlimit(0, int(pageDefs.size()) - 1, pages.getSelectedId() - 1))];
    for (size_t i = 0; i < pg.indices.size(); ++i) {
      const auto &s = p.definition.params[size_t(pg.indices[i])];
      bool enabled = true;
      if (p.kind == Kind::Tape && (s.key == "wow" || s.key == "flutter")) {
        enabled = p.appliedQuality() != 0;
        controls[i].slider.setTooltip("PRO: tape speed modulation. LIVE keeps "
                                      "the direct path free of delay.");
      }
      controls[i].slider.setEnabled(enabled);
      controls[i].choice.setEnabled(enabled);
      controls[i].label.setAlpha(enabled ? 1.f : .4f);
    }
    refreshVital();
  }
  float v(const char *key, int group = 0) const {
    for (const auto &s : p.definition.params)
      if (s.key == key && s.group == group)
        return p.value(s.id);
    return 0;
  }
  float hzX(double hz) const {
    return graph.getX() +
           float(std::log(std::clamp(hz, 20., 20000.) / 20) / std::log(1000.)) *
               graph.getWidth();
  }
  void timerCallback() override {
    if (irJob.valid() &&
        irJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      const auto fitted = irJob.get();
      irStatus = fitted.status;
      importIr.setEnabled(true);
      if (fitted.valid) {
        p.undo.beginNewTransaction("Fit impulse response");
        p.set("decay", float(fitted.decay));
        p.set("predelay", float(fitted.predelay));
        p.set("decaymult", 1);
        for (int i = 0; i < 6; ++i)
          p.set("b7_decay" + juce::String(i), float(fitted.ratios[size_t(i)]));
      }
    }
    if (p.kind == Kind::Rack) {
      bool changed = false;
      for (int i = 0; i < 8; ++i) {
        const int m = int(v("module", i + 1));
        if (m != rackTypes[size_t(i)]) {
          rackTypes[size_t(i)] = m;
          changed = true;
        }
      }
      if (changed)
        rebuildPages(
            pageDefs[size_t(std::max(0, pages.getSelectedId() - 1))].group);
    }
    refreshEnabled();
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    undo.setEnabled(p.undo.canUndo());
    redo.setEnabled(p.undo.canRedo());
    repaint();
  }

public:
  explicit Editor(Processor &proc) : AudioProcessorEditor(proc), p(proc) {
    setLookAndFeel(advanced() ? (p.kind==Kind::Deess ? static_cast<juce::LookAndFeel*>(&scalpel) : static_cast<juce::LookAndFeel*>(&monolith)) : &look);
    setSize(advanced()?620:760, advanced()?660:548);
    for (auto *c : {&preset, &quality, &pages, &bypass, &delta})
      addAndMakeVisible(c);
    for (auto *b : {&undo, &redo, &ab})
      addAndMakeVisible(b);
    for (auto &c : controls)
      setup(c);
    for (auto &c : global)
      setup(c);
    for (size_t i = 0; i < 3; ++i)
      bind(global[i], p.definition.params[i]);
    if (p.kind == Kind::Vital) {
      for (int i = 0; i < 5; ++i) {
        auto& label = vitalRanges[size_t(i)];
        addAndMakeVisible(label); label.setComponentID("vital_range_" + juce::String(i + 1));
        label.setFont(gill::prism::font(12)); label.setColour(juce::Label::textColourId, gill::prism::white);
        label.setJustificationType(juce::Justification::centred); label.setBounds(30 + 141 * i, 126, 139, 43);
      }
      addAndMakeVisible(vitalStatus); vitalStatus.setComponentID("vital_status");
      vitalStatus.setFont(gill::prism::font(10)); vitalStatus.setColour(juce::Label::textColourId, gill::prism::cyan);
      vitalStatus.setBounds(30, 170, 702, 17);
    }
    rebuildPages();
    if (p.kind == Kind::Room) {
      addAndMakeVisible(importIr);
      importIr.setBounds(645, 112, 82, 21);
      importIr.setTooltip("Fit decay settings from a WAV impulse response. "
                          "This is algorithmic matching, not convolution.");
      importIr.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>(
            "Choose a reverb impulse response", juce::File(),
            "*.wav;*.aif;*.aiff");
        const juce::Component::SafePointer<Editor> safe(this);
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
            [safe](
                const juce::FileChooser &dialog) {
              if (!safe)
                return;
              const auto file = dialog.getResult();
              if (!file.existsAsFile())
                return;
              safe->importIr.setEnabled(false);
              safe->irStatus = "Analyzing IR...";
              safe->irJob = std::async(std::launch::async, [file] {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(
                    formats.createReaderFor(file));
                IrFit result;
                if (!reader ||
                    reader->lengthInSamples > reader->sampleRate * 30 ||
                    reader->lengthInSamples < reader->sampleRate * .08) {
                  result.status = "IR: use an 80 ms to 30 s WAV";
                  return result;
                }
                juce::AudioBuffer<float> buffer(2,
                                                int(reader->lengthInSamples));
                if (!reader->read(&buffer, 0, buffer.getNumSamples(), 0, true,
                                  true)) {
                  result.status = "IR could not be read";
                  return result;
                }
                std::vector<float> mono(size_t(buffer.getNumSamples()));
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                  mono[size_t(i)] = reader->numChannels > 1
                                        ? .5f * (buffer.getSample(0, i) +
                                                 buffer.getSample(1, i))
                                        : buffer.getSample(0, i);
                return fitImpulse(mono, reader->sampleRate);
              });
            });
      };
    }
    if (p.kind == Kind::Max) {
      addAndMakeVisible(learnDrive);
      learnDrive.setBounds(608, 112, 120, 21);
      learnDrive.setTooltip(
          "Play a representative passage, then click APPLY. Estimates drive "
          "from input LUFS; final loudness still depends on limiting.");
      learnDrive.onClick = [this] {
        if (!learningDrive) {
          p.resetLoudness = true;
          learningDrive = true;
          learnDrive.setButtonText("APPLY DRIVE");
        } else if (p.inputLufs.load() > -70) {
          p.set("drive", juce::jlimit(0.f, 24.f,
                                      p.value("target") - p.inputLufs.load()));
          learningDrive = false;
          learnDrive.setButtonText("LEARN DRIVE");
        }
      };
    }
    pages.setComponentID("pages");
    pages.setSelectedId(1);
    pages.onChange = [this] { selectPage(); };
    selectPage();
    for (int i = 0; i < p.getNumPrograms(); ++i)
      preset.addItem(p.getProgramName(i), i + 1);
    preset.setComponentID("preset"); quality.setComponentID("quality"); bypass.setComponentID("bypass"); delta.setComponentID("delta");
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    preset.onChange = [this] {
      p.setCurrentProgram(preset.getSelectedId() - 1);
    };
    quality.addItemList({"LIVE", "PRO"}, 1);
    qualityA = std::make_unique<CA>(p.state, gill::qualityParameterId, quality);
    quality.setTooltip("LIVE has no added audio buffering. Stop playback to "
                       "apply a pending quality change.");
    bypass.addItemList({"ACTIVE", "BYPASS"}, 1);
    delta.addItemList({"OUTPUT", "DIFFERENCE"}, 1);
    bypassA = std::make_unique<CA>(p.state, "bypass", bypass);
    deltaA = std::make_unique<CA>(p.state, "delta", delta);
    undo.onClick = [this] { p.undo.undo(); };
    redo.onClick = [this] { p.undo.redo(); };
    ab.onClick = [this] {
      p.getStateInformation(memories[bank]);
      bank = 1 - bank;
      if (memories[bank].getSize())
        p.setStateInformation(memories[bank].getData(),
                              int(memories[bank].getSize()));
      else
        memories[bank] = memories[1 - bank];
      ab.setButtonText(bank ? "BANK B" : "BANK A");
    };
    if(p.kind==Kind::Deess){addAndMakeVisible(strength);strength.setComponentID("depth");strength.setSliderStyle(juce::Slider::LinearVertical);strength.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);strength.textFromValueFunction=[](double v){return juce::String(v,1)+" %";};strengthA=std::make_unique<SA>(p.state,"depth",strength);strength.textFromValueFunction=[](double v){return juce::String(v,1)+" %";};strength.updateText();}
    keyboard = std::make_unique<gill::HostKeyboardPolicy>(*this);
    resized();
    startTimerHz(20);
  }
  ~Editor() override {
    stopTimer();
    if (drag)
      drag->endChangeGesture();
    if (dragGain)
      dragGain->endChangeGesture();
    keyboard.reset();
    setLookAndFeel(nullptr);
  }
  juce::RangedAudioParameter *nodeParameter(const char *key, int node) {
    return p.state.getParameter("b" + juce::String(node) + "_" + key);
  }
  int hitNode(juce::Point<float> point) {
    if (!graph.contains(point) ||
        (p.kind != Kind::Room && p.kind != Kind::Transient))
      return 0;
    int found = 0;
    float distance = 16;
    const int count = p.kind == Kind::Room ? 6 : 5;
    for (int i = 1; i <= count; ++i)
      if (v("enable", i) > .5) {
        const auto position = juce::Point<float>(
            hzX(v("frequency", i)),
            150 - juce::jlimit(-24.f, 24.f, v("gain", i)) * 1.3f);
        const auto d = position.getDistanceFrom(point);
        if (d < distance) {
          distance = d;
          found = i;
        }
      }
    return found;
  }
  void mouseDown(const juce::MouseEvent &e) override {
    draggedNode = hitNode(e.position);
    if (!draggedNode)
      return;
    p.undo.beginNewTransaction("Edit graph band");
    drag = nodeParameter("frequency", draggedNode);
    dragGain = nodeParameter("gain", draggedNode);
    if (p.kind == Kind::Room && v("type", draggedNode) >= 3)
      dragGain = nullptr;
    if (drag)
      drag->beginChangeGesture();
    if (dragGain)
      dragGain->beginChangeGesture();
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    if (!drag)
      return;
    const double hz =
        20 * std::pow(1000., juce::jlimit(0., 1.,
                                          double(e.position.x - graph.getX()) /
                                              graph.getWidth()));
    drag->setValueNotifyingHost(drag->convertTo0to1(float(hz)));
    if (dragGain)
      dragGain->setValueNotifyingHost(dragGain->convertTo0to1(
          juce::jlimit(-24.f, 24.f, (150 - e.position.y) / 1.3f)));
    repaint();
  }
  void mouseUp(const juce::MouseEvent &) override {
    if (drag) {
      drag->endChangeGesture();
      drag = nullptr;
    }
    if (dragGain) {
      dragGain->endChangeGesture();
      dragGain = nullptr;
    }
    draggedNode = 0;
  }
  void mouseDoubleClick(const juce::MouseEvent &e) override {
    if (!graph.contains(e.position) ||
        (p.kind != Kind::Room && p.kind != Kind::Transient))
      return;
    const int count = p.kind == Kind::Room ? 6 : 5;
    for (int i = 1; i <= count; ++i)
      if (v("enable", i) < .5) {
        p.undo.beginNewTransaction("Add graph band");
        for (const auto &item : std::array<std::pair<const char *, float>, 3>{
                 {{"enable", 1},
                  {"frequency",
                   float(20 *
                         std::pow(1000., double(e.position.x - graph.getX()) /
                                             graph.getWidth()))},
                  {"gain",
                   juce::jlimit(-24.f, 24.f, (150 - e.position.y) / 1.3f)}}})
          if (auto *parameter = nodeParameter(item.first, i)) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(
                parameter->convertTo0to1(item.second));
            parameter->endChangeGesture();
          }
        break;
      }
    repaint();
  }
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &w) override {
    const int node = hitNode(e.position);
    if (!node)
      return;
    if (auto *parameter = nodeParameter("q", node)) {
      parameter->beginChangeGesture();
      parameter->setValueNotifyingHost(
          parameter->convertTo0to1(v("q", node) * std::pow(2.f, w.deltaY)));
      parameter->endChangeGesture();
    }
    repaint();
  }
  void setup(Control &c) {
    c.label.setFont(gill::prism::font(10, true));
    c.label.setJustificationType(juce::Justification::centred);
    c.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 18);
    addAndMakeVisible(c.label);
    addAndMakeVisible(c.slider);
    addAndMakeVisible(c.choice);
  }

  void layoutAdvanced(){
    preset.setBounds(92,76,273,24);quality.setBounds(477,76,120,24);
    undo.setBounds(23,110,55,23);redo.setBounds(82,110,55,23);ab.setBounds(141,110,61,23);bypass.setBounds(386,110,97,23);delta.setBounds(488,110,109,23);pages.setBounds(210,110,170,23);
    const bool clean=p.kind==Kind::Deess;
    const bool single=!clean&&!pageDefs.empty()&&pageDefs[size_t(std::max(0,pages.getSelectedId()-1))].indices.size()<=3;
    for(int i=0;i<12;++i){auto&c=controls[size_t(i)];int x=clean?185+(i%3)*135:38+(i%2)*180,y=clean?359+(i/3)*79:154+(i/2)*114;
      c.slider.setSliderStyle(clean?juce::Slider::LinearHorizontal:juce::Slider::RotaryHorizontalVerticalDrag);c.label.setBounds(x,y,clean?125:155,18);c.slider.setBounds(x,y+19,clean?125:155,clean?59:84);c.choice.setBounds(x+4,y+43,clean?117:147,25);if(single){const int sy=154+i*114;c.label.setBounds(37,sy+31,145,21);c.slider.setBounds(198,sy+1,173,98);c.choice.setBounds(193,sy+35,182,28);}}
    strength.setBounds(45,185,101,302);
    graph=clean?juce::Rectangle<float>(186,161,399,158):juce::Rectangle<float>(424,395,155,76);
    for(int i=0;i<3;++i){auto&c=global[size_t(i)];const int x=i==0?33:i==1?220:412;c.label.setBounds(x,535,i==1?174:172,20);c.slider.setSliderStyle(i==1&&!clean?juce::Slider::RotaryHorizontalVerticalDrag:juce::Slider::LinearHorizontal);c.slider.setBounds(x,554,i==1?174:172,i==1&&!clean?77:62);}
    learnDrive.setBounds(425,451,165,24);
  }
  void paintAdvanced(juce::Graphics&g){
    using namespace gill::monolith;const bool clean=p.kind==Kind::Deess;
    if(clean){gill::scalpel::chassis(g,620,660,p.getName());gill::scalpel::panel(g,{22,147,141,365});gill::scalpel::text(g,"DYNAMICS",{29,157,128,21},13);gill::scalpel::text(g,"DEPTH",{30,488,126,18},10);gill::scalpel::panel(g,{175,147,423,192});gill::scalpel::panel(g,{175,349,423,163});gill::scalpel::panel(g,{22,525,576,109});}
    else {chassis(g,620,660,p.getName());frame(g,620,660,145,375,515);for(int row=0;row<3;++row){surface(g,{22,145.f+row*114,375,108},22);g.setColour(gold);g.fillEllipse(27,153.f+row*114,5,5);}meterWell(g,{414,145,184,370},p.inPeak.load(),p.outPeak.load(),p.reduction[0].load(),p.kind==Kind::Alpha?0.f:110.f);surface(g,{22,525,576,109},24);}
    if(p.kind==Kind::Precision || clean){
      if(!clean){g.setColour(ink);g.fillRoundedRectangle(graph.expanded(2),4);}const int count=clean?2:5;
      text(g,"BAND RANGES / GR",{graph.getX(),graph.getY(),graph.getWidth(),17},9,cream);
      for(int i=0;i<count;++i){const float left=hzX(v("low",i+1)),right=hzX(v("high",i+1));auto r=juce::Rectangle<float>(std::min(left,right),graph.getY()+24,std::max(2.f,std::abs(right-left)),graph.getHeight()-46);g.setColour(gold.withAlpha(.09f));g.fillRect(r);g.setColour(gold.withAlpha(.6f));g.drawRect(r,1);text(g,juce::String(i+1),{r.getX(),r.getY()+1,r.getWidth(),14},9);text(g,juce::String(p.reduction[size_t(i)].load(),1),{r.getX(),r.getBottom()-18,r.getWidth(),16},10,gold);}
      text(g,"20 Hz",{graph.getX(),graph.getBottom()-17,36,16},8,cream.withAlpha(.6f));text(g,"20 kHz",{graph.getRight()-44,graph.getBottom()-17,44,16},8,cream.withAlpha(.6f));
      if(clean) text(g,"MEASURED GAIN REDUCTION",{183,321,405,13},9,gold);
    }
    if(p.kind==Kind::Max){text(g,"IN "+juce::String(p.inputLufs.load(),1)+" LUFS",{426,402,160,20},11,gold);text(g,"OUT "+juce::String(p.outputLufs.load(),1)+" LUFS",{426,425,160,20},11,gold);}
    juce::String status=juce::String(p.appliedQuality()?"PRO":"LIVE")+" / "+juce::String(p.getLatencySamples())+" SAMPLES";if(p.pending())status+=" / STOP PLAYBACK TO APPLY";text(g,status,{23,640,574,13},9,cream.withAlpha(.65f));
  }

  void resized() override {
    if(advanced()){layoutAdvanced();return;}
    preset.setBounds(285, 20, 265, 25);
    quality.setBounds(603, 20, 130, 25);
    undo.setBounds(26, 71, 70, 25);
    redo.setBounds(100, 71, 70, 25);
    pages.setBounds(185, 71, 279, 25);
    ab.setBounds(479, 71, 70, 25);
    bypass.setBounds(558, 71, 82, 25);
    delta.setBounds(647, 71, 87, 25);
    for (int i = 0; i < 12; ++i) {
      const int x = 30 + (i % 6) * 118, y = 202 + (i / 6) * 115;
      auto &c = controls[size_t(i)];
      c.label.setBounds(x, y, 111, 20);
      c.slider.setBounds(x, y + 18, 111, 90);
      c.choice.setBounds(x + 3, y + 43, 105, 27);
    }
    for (int i = 0; i < 3; ++i) {
      auto &c = global[size_t(i)];
      c.label.setBounds(290 + i * 118, 442, 110, 16);
      c.slider.setSliderStyle(juce::Slider::LinearHorizontal);
      c.slider.setBounds(290 + i * 118, 458, 110, 47);
    }
  }
  void paint(juce::Graphics &g) override {
    if(advanced()){paintAdvanced(g);return;}
    using namespace gill::prism;
    chassis(g, 760, 548, 198);
    title(g, p.getName(), {72, 17, 215, 29}, 23);
    glass(g, graph, 3);
    g.setFont(font(10));
    g.setColour(white.withAlpha(.5f));
    g.drawHorizontalLine(150, graph.getX(), graph.getRight());
    const bool bands = p.kind == Kind::Multi || p.kind == Kind::Precision ||
                       p.kind == Kind::Deess;
    const bool eq = p.kind == Kind::Room || p.kind == Kind::Eq825 ||
                    p.kind == Kind::Transient;
    if (p.kind == Kind::Vital) {
      g.setColour(ink); g.fillRoundedRectangle(graph, 3);
      g.setColour(white); g.drawText("EFFECTIVE CROSSOVER TARGETS / FIVE ENHANCEMENT BANDS", 32, 112, 695, 13, juce::Justification::left);
      g.setColour(cyan.withAlpha(.25f));
      for (int i = 1; i < 5; ++i) g.drawVerticalLine(29 + 141 * i, 129, 166);
    } else if (bands) {
      const int n = p.kind == Kind::Multi       ? 6
                    : p.kind == Kind::Precision ? 5
                                                : 2;
      for (int i = 0; i < n; ++i) {
        const float left = hzX(v("low", i + 1)), right = hzX(v("high", i + 1)),
                    r = p.reduction[size_t(i)].load();
        g.setColour(cyan.withAlpha(.12f));
        g.fillRect(juce::Rectangle<float>(std::min(left, right), 126,
                                          std::abs(right - left), 48));
        g.setColour(
            i == pageDefs[size_t(std::max(0, pages.getSelectedId() - 1))]
                            .group -
                        1
                ? pink
                : cyan);
        g.drawRect(juce::Rectangle<float>(std::min(left, right), 126,
                                          std::abs(right - left), 48),
                   1);
        g.drawText(juce::String(i + 1) + "  " + juce::String(r, 1) + " dB",
                   int(left), 133, std::max(35, int(right - left)), 20,
                   juce::Justification::centred);
      }
      g.setColour(white);
      g.drawText("BAND RANGES / GAIN REDUCTION", 32, 112, 360, 13,
                 juce::Justification::left);
    } else if (eq) {
      juce::Path curve;
      gillstudio::spectral::Parameters weights;
      if (p.kind == Kind::Transient)
        for (int i = 0; i < 5; ++i) {
          auto &n = weights.nodes[size_t(i)];
          n.enabled = v("enable", i + 1) > .5;
          n.shape = gillstudio::spectral::Shape(int(v("shape", i + 1)));
          n.frequency =
              std::min(double(v("frequency", i + 1)), p.rate.load() * .49);
          n.gainDb = v("gain", i + 1);
          n.q = v("q", i + 1);
          n.slope = v("slope", i + 1);
        }
      for (int i = 0; i <= 710; ++i) {
        const double hz = 20 * std::pow(1000., i / 710.);
        double db = 0;
        if (p.kind == Kind::Transient)
          db = gillstudio::spectral::Processor::nodeWeightDbAt(weights, hz);
        else if (p.kind == Kind::Room) {
          std::complex<double> h = 1.;
          for (int b = 1; b <= 6; ++b)
            if (v("enable", b) > .5) {
              const auto c =
                  gill::designSection(int(v("type", b)),
                                      std::clamp(double(v("frequency", b)), 5.,
                                                 p.rate.load() * .49),
                                      v("gain", b), v("q", b), p.rate.load());
              h *= gill::sectionResponse(c, p.rate, hz);
            }
          db = 20 * std::log10(std::max(1e-10, std::abs(h)));
        } else {
          const int channel =
              pageDefs[size_t(std::max(0, pages.getSelectedId() - 1))].group ==
                      2
                  ? 2
                  : 1;
          const char *k[] = {"low", "lowmid", "mid", "highmid", "high"};
          const char *f[] = {"lowfreq", "lowmidfreq", "midfreq", "highmidfreq",
                             "highfreq"};
          std::complex<double> h = 1;
          for (int b = 0; b < 5; ++b) {
            const double a = v(k[b], channel),
                         q = v("q", channel) * (1 + std::abs(a) / 12);
            const auto c = gill::designSection(
                b == 0   ? gill::LowShelf
                : b == 4 ? gill::HighShelf
                         : gill::Bell,
                std::min(double(v(f[b], channel)), p.rate.load() * .49), a, q,
                p.rate.load());
            h *= gill::sectionResponse(c, p.rate, hz);
          }
          db = 20 * std::log10(std::max(1e-10, std::abs(h)));
        }
        const auto point = juce::Point<float>(
            25 + float(i), 150 - float(std::clamp(db, -24., 24.)) * 1.3f);
        if (i == 0)
          curve.startNewSubPath(point);
        else
          curve.lineTo(point);
      }
      electricTrace(g, curve, cyan, .3f);
      g.setColour(white);
      g.drawText(p.kind == Kind::Transient ? "DETECTOR WEIGHT"
                 : p.kind == Kind::Room    ? "POST REVERB EQ"
                                           : "STATIC EQ / SELECTED CHANNEL",
                 32, 112, 420, 13, juce::Justification::left);
    } else {
      juce::Path wave;
      for (int i = 0; i < 256; ++i) {
        const auto point =
            juce::Point<float>(25 + i * 710.f / 255,
                               150 - std::tanh(p.wave[size_t(i)].load()) * 30);
        if (i == 0)
          wave.startNewSubPath(point);
        else
          wave.lineTo(point);
      }
      electricTrace(g, wave, cyan, .3f);
      g.setColour(white);
      g.drawText("OUTPUT WAVEFORM", 32, 112, 300, 13,
                 juce::Justification::left);
    }
    if (p.kind == Kind::Transient || p.kind == Kind::Room) {
      const int count = p.kind == Kind::Room ? 6 : 5;
      for (int i = 1; i <= count; ++i)
        if (v("enable", i) > .5) {
          const float x = hzX(v("frequency", i)),
                      y = 150 - juce::jlimit(-24.f, 24.f, v("gain", i)) * 1.3f;
          g.setColour(cyan);
          g.fillEllipse(x - 5, y - 5, 10, 10);
          g.setColour(ink);
          g.setFont(font(8, true));
          g.drawText(juce::String(i), int(x - 5), int(y - 5), 10, 10,
                     juce::Justification::centred);
        }
    }
    if (p.kind == Kind::Room) {
      g.setColour(white);
      g.setFont(font(9));
      g.drawText(irStatus, 260, 114, 375, 15, juce::Justification::right);
    }
    if (p.kind == Kind::Max) {
      g.setColour(white);
      g.setFont(font(11, true));
      g.drawText("IN " + juce::String(p.inputLufs.load(), 1) + " LUFS   OUT " +
                     juce::String(p.outputLufs.load(), 1) + " LUFS   GR " +
                     juce::String(p.reduction[0].load(), 1) + " dB",
                 280, 166, 447, 18, juce::Justification::right);
    }
    g.setColour(ink);
    g.setFont(font(11, true));
    g.drawText(
        "IN  " +
            juce::String(20 * std::log10(std::max(1e-6f, p.inPeak.load())), 1) +
            " dBFS",
        30, 455, 225, 20, juce::Justification::left);
    g.drawText("OUT " +
                   juce::String(
                       20 * std::log10(std::max(1e-6f, p.outPeak.load())), 1) +
                   " dBFS",
               30, 479, 225, 20, juce::Justification::left);
    g.setFont(font(10));
    juce::String status = juce::String(p.appliedQuality() ? "PRO" : "LIVE") +
                          " | " + juce::String(p.getLatencySamples()) +
                          " samples";
    if (p.pending())
      status += " | Stop playback to apply requested mode";
    g.drawText(status, 26, 521, 705, 16, juce::Justification::left);
  }
};
juce::AudioProcessorEditor *Processor::createEditor() {
  if (kind == Kind::Rack) {
    return createRackEditor(*this);
  }
  return new Editor(*this);
}
} // namespace studio

