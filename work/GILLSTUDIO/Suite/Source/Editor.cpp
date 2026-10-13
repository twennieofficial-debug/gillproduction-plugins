#include "../../../GILLCommon/HostKeyboardPolicy.h"
#include "../../../GILLCommon/PrismUi.h"
#include "../../../GILLCommon/MonolithUi.h"
#include "../../../GILLCommon/ScalpelUi.h"
#include "../../../Effects16/advanced-ui/AdvancedScalpel.h"
#include "IrFit.h"
#include "Processor.h"
#include "MasterViews21.h"
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
  std::array<juce::TextButton, 2> deessBands;
  int deessSelected = 1, deessMode = 0;
  float deessStartX = 0, deessStartY = 0, deessLow = 0, deessHigh = 0, deessThreshold = 0;
  juce::Rectangle<float> deessLane(int band) const { return {graph.getX(), 205.f + (band - 1) * 76.f, graph.getWidth(), 59.f}; }
  void endDeessGesture() {
    if (p.kind != Kind::Deess) return;
    if (drag) { drag->endChangeGesture(); drag = nullptr; }
    if (dragGain) { dragGain->endChangeGesture(); dragGain = nullptr; }
    deessMode = 0;
  }
  void chooseDeessBand(int band) {
    endDeessGesture();
    deessSelected = band;
    for (size_t i = 0; i < pageDefs.size(); ++i)
      if (pageDefs[i].group == band) { pages.setSelectedId(int(i + 1), juce::dontSendNotification); selectPage(); break; }
  }
  // Retain inverted legacy/automated values until an explicit edge edit. Equality
  // is allowed at the endpoint, where there is no room for a positive-width band.
  double boundDeess(int band, bool low, double value) const {
    return low ? juce::jlimit(20., double(v("high", band)), value)
               : juce::jlimit(double(v("low", band)), 20000., value);
  }
  void paintDeess(juce::Graphics& g) {
    using namespace gill::scalpel;
    chassis(g, 620, 660, p.getName());
    panel(g, {22, 145, 576, 222});
    text(g, "FREQUENCY BANDS", {37, 154, 208, 21}, 12, silver);
    for (double hz : {20., 100., 500., 2000., 10000., 20000.}) {
      float x = hzX(hz);
      g.setColour(silver.withAlpha(.13f)); g.drawVerticalLine(int(x), 197, 341);
      text(g, hz >= 1000 ? juce::String(hz / 1000., 0) + "k" : juce::String(hz, 0), {x-18, 344, 36, 15}, 10, silver);
    }
    for (int band = 1; band <= 2; ++band) {
      const auto lane = deessLane(band);
      const auto colour = band == 1 ? cyan : juce::Colour(0xffffba73);
      const bool selected = deessSelected == band, enabled = v("enable", band) > .5f;
      const float lo = v("low", band), hi = v("high", band), left = hzX(lo), right = hzX(hi);
      auto range = juce::Rectangle<float>(std::min(left, right), lane.getY(), std::max(2.f, std::abs(right-left)), lane.getHeight());
      g.setColour(colour.withAlpha(enabled ? .12f : .035f)); g.fillRoundedRectangle(range, 3);
      g.setColour(colour.withAlpha(selected ? .9f : .45f)); g.drawRoundedRectangle(range, 3, selected ? 1.5f : 1.f);
      for (float x : {left, right}) { g.setColour(colour); g.fillRoundedRectangle(x-3, lane.getY()+10, 6, lane.getHeight()-20, 2); }
      const float y = lane.getBottom() - (v("threshold",band)+80.f)/92.f*lane.getHeight();
      g.setColour(colour.withAlpha(.8f)); g.drawHorizontalLine(int(y), range.getX()+4, std::max(range.getX()+4, range.getRight()-4));
      text(g, juce::String(band), {28, lane.getY()+18, 25, 21}, 13, colour);
      const juce::String status = lo > hi ? "REVERSED SAVED EDGES" : lo == hi ? "COINCIDENT EDGES" : !enabled ? "OFF" : "";
      text(g, status, {64, lane.getY()+4, 235, 16}, 10, colour);
      text(g, "THR " + juce::String(v("threshold", band),1) + " dB", {64, lane.getY()+23, 145, 19}, 12, ink);
      const float reduction = p.reduction[size_t(band-1)].load();
      text(g, "GR " + juce::String(reduction,1) + " dB", {64, lane.getY()+42, 145, 16}, 11, colour);
      g.setColour(colour.withAlpha(.15f)); g.fillRoundedRectangle(220,lane.getY()+47,80,4,2);
      g.setColour(colour); g.fillRoundedRectangle(220,lane.getY()+47,80*juce::jlimit(0.f,1.f,reduction/24.f),4,2);
    }
    text(g, "DRAG EDGES: FREQUENCY   /   INSIDE: THRESHOLD   /   ALT: MOVE BAND", {25, 374, 570, 18}, 11, silver);
    panel(g,{22,397,576,125});
    panel(g,{22,535,576,99});
    text(g,"DEPTH",{29,539,110,16},10,silver);
    auto frequency = [](float hz) { return hz >= 1000 ? juce::String(hz/1000.f,2)+" kHz" : juce::String(hz,0)+" Hz"; };
    text(g, "BAND " + juce::String(deessSelected) + "   LOW " + frequency(v("low",deessSelected)) + "   /   HIGH " + frequency(v("high",deessSelected)), {29,178,560,18}, 12, deessSelected == 1 ? cyan : juce::Colour(0xffffba73));
    juce::String status = juce::String(p.appliedQuality()?"PRO":"LIVE")+" / "+juce::String(p.getLatencySamples())+" SAMPLES";
    if(p.pending()) status += " / STOP PLAYBACK TO APPLY";
    text(g,status,{23,640,574,13},9,silver);
  }
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
      if (p.kind == Kind::Deess && s.group > 0 && (s.key == "low" || s.key == "high")) {
        c.slider.userBound = [this, band=s.group, low=s.key=="low"](double value) { return boundDeess(band,low,value); };
        c.slider.valueFromTextFunction = [this, band=s.group, low=s.key=="low"](const juce::String& text) { return boundDeess(band,low,text.getDoubleValue()); };
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
    endDeessGesture();
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
    if (p.kind == Kind::Deess) {
      if (pg.group > 0) deessSelected = pg.group;
      for (int i=0; i<2; ++i) deessBands[size_t(i)].setToggleState(deessSelected==i+1,juce::dontSendNotification);
    }
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
        const std::array<const char*,4> deessPageNames{"RANGE", "TIMING", "DYNAMICS", "DETECTOR"};
        pages.addItem(p.kind==Kind::Deess && group>0 ? p.definition.groups[group]+" / "+deessPageNames[offset/pageSize()] : p.definition.groups[group] +
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
    rebuildPages(p.kind==Kind::Alpha || p.kind==Kind::Precision ? 1 : 0);
    if (p.kind == Kind::Deess) {
      for (int i=0; i<2; ++i) {
        auto& button=deessBands[size_t(i)]; addAndMakeVisible(button);
        button.setComponentID("deess_band_"+juce::String(i+1)); button.setButtonText("BAND "+juce::String(i+1));
        button.setBounds(359+i*112,155,104,27); button.onClick=[this,i]{chooseDeessBand(i+1);};
        button.setTooltip("Select band "+juce::String(i+1)+". More controls are available in the page menu.");
      }
    }
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
    pages.onChange = [this] { selectPage(); };
    selectPage();
    if (p.kind == Kind::Deess) chooseDeessBand(1);
    for (int i = 0; i < p.getNumPrograms(); ++i)
      preset.addItem(p.getProgramName(i), i + 1);
    preset.setComponentID("preset"); quality.setComponentID("quality"); bypass.setComponentID("bypass"); delta.setComponentID("delta");
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    preset.onChange = [this] {
      endDeessGesture();
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
    undo.onClick = [this] { endDeessGesture(); p.undo.undo(); };
    redo.onClick = [this] { endDeessGesture(); p.undo.redo(); };
    ab.onClick = [this] {
      endDeessGesture();
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
    if (p.kind == Kind::Deess) {
      endDeessGesture();
      for (int band=1;band<=2;++band) if(deessLane(band).expanded(6,0).contains(e.position)) {
        chooseDeessBand(band); deessStartX=e.position.x; deessStartY=e.position.y;
        deessLow=v("low",band); deessHigh=v("high",band); deessThreshold=v("threshold",band);
        const float dl=std::abs(e.position.x-hzX(deessLow)), dh=std::abs(e.position.x-hzX(deessHigh));
        if (std::min(dl,dh)<=9) {
          deessMode=std::abs(hzX(deessLow)-hzX(deessHigh))<9 ? (e.position.y<deessLane(band).getCentreY()?1:2) : dl<=dh?1:2;
          drag=nodeParameter(deessMode==1?"low":"high",band);
        }
        else if (e.mods.isAltDown() && deessLow<deessHigh) { deessMode=4;drag=nodeParameter("low",band);dragGain=nodeParameter("high",band); }
        else { deessMode=3;drag=nodeParameter("threshold",band); }
        p.undo.beginNewTransaction("Edit de-esser band");
        if(drag)drag->beginChangeGesture();if(dragGain)dragGain->beginChangeGesture();
        repaint(); return;
      }
      return;
    }
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
    if (p.kind == Kind::Deess) {
      if(!drag || !deessMode) return;
      if(deessMode<=2) {
        const double hz=20*std::pow(1000.,juce::jlimit(0.,1.,double(e.position.x-graph.getX())/graph.getWidth()));
        drag->setValueNotifyingHost(drag->convertTo0to1(float(boundDeess(deessSelected,deessMode==1,hz))));
      } else if(deessMode==3) {
        drag->setValueNotifyingHost(drag->convertTo0to1(juce::jlimit(-80.f,12.f,deessThreshold+(deessStartY-e.position.y)*92.f/deessLane(deessSelected).getHeight())));
      } else {
        const double ratio=juce::jlimit(20./deessLow,20000./deessHigh,std::pow(1000.,double(e.position.x-deessStartX)/graph.getWidth()));
        // Order writes to avoid a crossed intermediate range in host automation.
        auto put=[](juce::RangedAudioParameter* param,double value){param->setValueNotifyingHost(param->convertTo0to1(float(value)));};
        const double low=deessLow*ratio,high=deessHigh*ratio;
        if(low>v("high",deessSelected)){put(dragGain,high);put(drag,low);}else{put(drag,low);put(dragGain,high);}
      }
      repaint(); return;
    }
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
    endDeessGesture();
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
  bool keyPressed(const juce::KeyPress& key) override {
    if(p.kind==Kind::Deess && key==juce::KeyPress::escapeKey && deessMode){endDeessGesture();return true;}
    return juce::AudioProcessorEditor::keyPressed(key);
  }
  void visibilityChanged() override { if(!isVisible())endDeessGesture(); }
  void mouseMove(const juce::MouseEvent& e) override {
    if(p.kind!=Kind::Deess)return;
    auto cursor=juce::MouseCursor::NormalCursor;
    for(int band=1;band<=2;++band)if(deessLane(band).expanded(6,0).contains(e.position)) {
      cursor=std::min(std::abs(e.position.x-hzX(v("low",band))),std::abs(e.position.x-hzX(v("high",band))))<=9
        ?juce::MouseCursor::LeftRightResizeCursor: e.mods.isAltDown()?juce::MouseCursor::DraggingHandCursor:juce::MouseCursor::UpDownResizeCursor;
    }
    setMouseCursor(cursor);
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
    if(p.kind==Kind::Deess){
      preset.setBounds(92,76,273,24);quality.setBounds(477,76,120,24);
      undo.setBounds(23,110,55,23);redo.setBounds(82,110,55,23);ab.setBounds(141,110,61,23);bypass.setBounds(386,110,97,23);delta.setBounds(488,110,109,23);pages.setBounds(210,110,170,23);
      graph={59,197,510,144};
      for(int i=0;i<12;++i){auto&c=controls[size_t(i)];int x=32+(i%6)*94;c.label.setBounds(x,410,88,23);c.label.setFont(gill::prism::font(10));c.slider.setSliderStyle(juce::Slider::LinearHorizontal);c.slider.setBounds(x,437,88,69);c.choice.setBounds(x,450,88,28);}
      strength.setSliderStyle(juce::Slider::LinearHorizontal);strength.setBounds(30,563,111,58);
      for(int i=0;i<3;++i){auto&c=global[size_t(i)];int x=156+i*145;c.label.setBounds(x,539,132,20);c.slider.setSliderStyle(juce::Slider::LinearHorizontal);c.slider.setBounds(x,563,132,58);}
      return;
    }
    preset.setBounds(92,76,273,24);quality.setBounds(477,76,120,24);
    undo.setBounds(23,110,55,23);redo.setBounds(82,110,55,23);ab.setBounds(141,110,61,23);bypass.setBounds(386,110,97,23);delta.setBounds(488,110,109,23);pages.setBounds(210,110,170,23);
    const int firstRow=p.kind==Kind::Alpha?351:320;
    for(int i=0;i<12;++i){auto&c=controls[size_t(i)];const int x=35+(i%3)*193,y=firstRow+(i/3)*88;
      c.label.setBounds(x,y,168,18);c.slider.setSliderStyle(p.kind==Kind::Max?juce::Slider::LinearHorizontal:juce::Slider::RotaryHorizontalVerticalDrag);
      c.slider.setBounds(x,y+19,168,65);c.choice.setBounds(x+5,y+39,158,25);}
    graph={47,188,526,83};
    for(int i=0;i<3;++i){auto&c=global[size_t(i)];const int x=35+i*193;c.label.setBounds(x,535,168,20);c.slider.setSliderStyle(juce::Slider::LinearHorizontal);c.slider.setBounds(x,560,168,62);}
    learnDrive.setBounds(421,496,165,23);
  }
  void paintAdvanced(juce::Graphics&g){
    if(p.kind==Kind::Deess){paintDeess(g);return;}
    using namespace gill::monolith;
    chassis(g,620,660,p.getName());
    const float top=p.kind==Kind::Alpha?343.f:312.f;
    surface(g,{22,top,576,p.kind==Kind::Alpha?180.f:176.f},22);surface(g,{22,525,576,109},24);
    auto value=[this](const char* key,int band){return v(key,band);};
    if(p.kind==Kind::Alpha)masterui21::alpha(g,p,value);
    if(p.kind==Kind::Max)masterui21::maximizer(g,p);
    if(p.kind==Kind::Precision)masterui21::precision(g,p,value);
    juce::String status=juce::String(p.appliedQuality()?"PRO":"LIVE")+" / "+juce::String(p.getLatencySamples())+" SAMPLES";if(p.pending())status+=" / STOP PLAYBACK TO APPLY";text(g,status,{112,57,340,14},8.5f,cream.withAlpha(.65f),juce::Justification::left);
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

