#include "../../../GILLCommon/HostKeyboardPolicy.h"
#include "../../../GILLCommon/PrismUi.h"
#include "Processor.h"
#include "RackMaterials.h"
namespace studio {
class DragHeader : public juce::TextButton {
public:
  std::function<void()> drag;
  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.getDistanceFromDragStart() > 5 && drag)
      drag();
    else
      TextButton::mouseDrag(e);
  }
};
class ModuleCard : public juce::Component {
public:
  int module = 0;
  std::function<void(int, juce::Component *)> dragged;
  std::function<void(int)> clicked;
  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().toFloat().reduced(2);
    gill::material::panel(g, r, juce::Colour(0xff162735), 5);
    {
      juce::Graphics::ScopedSaveState save(g);
      g.reduceClipRegion(3, 3, 34, 43);
      rackskin::drawFace(g, module, {3, 3, 34, 104});
    }
    gill::prism::logo(g, {7, 11, 26, 25});
    g.setColour(juce::Colour(rackskin::finish(module).accent));
    g.setFont(gill::prism::font(10, true));
    g.drawFittedText(rackModels[module].name, {43, 7, getWidth() - 47, 19},
                     juce::Justification::centredLeft, 1);
    g.setColour(gill::prism::white.withAlpha(.65f));
    g.setFont(gill::prism::font(8));
    g.drawFittedText(rackRole(module), {43, 27, getWidth() - 47, 14},
                     juce::Justification::centredLeft, 1);
  }
  void mouseDown(const juce::MouseEvent &) override {}
  void mouseUp(const juce::MouseEvent &e) override {
    if (e.getDistanceFromDragStart() < 5 && clicked)
      clicked(module);
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.getDistanceFromDragStart() > 5 && dragged)
      dragged(module, this);
  }
};
class RackEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer,
                         public juce::DragAndDropContainer,
                         public juce::DragAndDropTarget {
  using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
  using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  struct Control {
    juce::Slider knob;
    juce::Label label;
    juce::ComboBox choice;
    std::unique_ptr<SA> sa;
    std::unique_ptr<CA> ca;
  };
  struct Strip {
    juce::ComboBox module;
    DragHeader select;
    juce::TextButton on, solo;
    std::array<Control, 4> controls;
    int kind = -1;
  };
  Processor &p;
  rackskin::Look look;
  juce::Viewport browser;
  juce::Component library;
  std::array<ModuleCard, 30> cards;
  int dropColumn = -1;
  std::array<Strip, 8> strips;
  std::array<Control, 24> detail;
  std::array<Control, 3> global;
  juce::ComboBox quality, preset, pages, category;
  juce::TextButton left{"< MOVE"}, right{"MOVE >"}, undo{"UNDO"}, redo{"REDO"};
  std::unique_ptr<CA> qa;
  std::unique_ptr<gill::HostKeyboardPolicy> keys;
  juce::TooltipWindow tips{this, 600};
  std::vector<const Spec *> visible;
  int selected = 1;
  void filterLibrary() {
    const int selection=category.getSelectedId();int row=0;
    for(auto& card:cards){const auto family=rackModels[card.module].family;
      const bool eq=family==RackFamily::Eq||family==RackFamily::Passive;
      const bool dynamics=family==RackFamily::Comp||family==RackFamily::Channel||family==RackFamily::Gate||family==RackFamily::Drum||family==RackFamily::Deess||family==RackFamily::Transient;
      const bool motion=family==RackFamily::Chorus;
      const bool utility=family==RackFamily::Trim;
      const bool visible=selection==1||(selection==2&&eq)||(selection==3&&dynamics)||(selection==4&&!eq&&!dynamics&&!motion&&!utility)||(selection==5&&motion)||(selection==6&&utility);
      card.setVisible(visible);if(visible)card.setBounds(0,row++*51,200,49);
    }
    library.setSize(204,std::max(49,row*51));browser.setViewPosition(0,0);
  }
  std::array<int, 8> order{1, 2, 3, 4, 5, 6, 7, 8};
  void setup(Control &c) {
    for (auto *item :
         std::array<juce::Component *, 3>{&c.knob, &c.label, &c.choice})
      addAndMakeVisible(item);
    c.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c.knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 16);
    c.label.setJustificationType(juce::Justification::centred);
    c.label.setFont(gill::prism::font(9, true));
  }
  const Spec *spec(const char *key, int group) {
    for (auto &s : p.definition.params)
      if (s.group == group && s.key == key)
        return &s;
    return nullptr;
  }
  void bind(Control &c, const Spec *s) {
    c.sa.reset();
    c.ca.reset();
    c.knob.setVisible(false);
    c.choice.setVisible(false);
    c.label.setVisible(s != nullptr);
    if (!s)
      return;
    const int module = s->group > 0 ? int(v("module", s->group)) : 0;
    c.label.setText(rackControlLabel(module, s->key, s->name.c_str()), juce::dontSendNotification);
    c.knob.setComponentID(s->id);
    c.choice.setComponentID(s->id);
    c.knob.setTooltip(juce::String(rackControlHint(module, s->key)));
    c.choice.setTooltip(juce::String(rackControlHint(module, s->key)));
    c.knob.getProperties().set("rackModule",
                               s->group > 0 ? int(v("module", s->group)) : 0);
    if (s->choices.empty()) {
      c.knob.setVisible(true);

      c.knob.setNumDecimalPlacesToDisplay(s->hi > 500 ? 0 : 1);
      c.sa = std::make_unique<SA>(p.state, s->id, c.knob);
      const int decimals = s->hi > 500 ? 0 : 1;
      c.knob.textFromValueFunction = [decimals, module, key = s->key, unit = juce::String(s->unit)](double value) {
        if (module == 26 && key == "ratio") value = std::max(10., value);
        return juce::String(value, decimals) + (unit.isEmpty() ? "" : " " + unit);
      };
      c.knob.updateText();
    } else {
      c.choice.clear(juce::dontSendNotification);
      for (size_t i = 0; i < s->choices.size(); ++i) {
        auto text = juce::String(s->choices[i]);
        if (module == 30 && s->key == "chorusmode")
          text = i == 3 ? "TRIANGLE" : i == 0 ? "SINE" : "SINE (" + juce::String(int(i) + 1) + ")";
        c.choice.addItem(text, int(i) + 1);
        if (module == 30 && s->key == "chorusmode" && (i == 1 || i == 2))
          c.choice.setItemEnabled(int(i) + 1, false); // Identical legacy sine aliases remain recallable.
      }
      c.choice.setVisible(true);
      c.ca = std::make_unique<CA>(p.state, s->id, c.choice);
    }
  }
  float v(const char *key, int slot) const {
    return p.value("b" + juce::String(slot) + "_" + key);
  }
  void write(const char *key, int slot, float value) {
    if (auto *param =
            p.state.getParameter("b" + juce::String(slot) + "_" + key)) {
      param->beginChangeGesture();
      param->setValueNotifyingHost(param->convertTo0to1(value));
      param->endChangeGesture();
    }
  }
  void choose(int slot) {
    selected = slot;
    for (int index = 0; index < int(strips.size()); ++index)
      strips[size_t(index)].select.setToggleState(index + 1 == selected, juce::dontSendNotification);
    visible.clear();
    const int m = int(v("module", slot));
    for (auto key : rackPanelOrder(m))
      if (rackControlUsed(m, key))
        if (auto* parameter = spec(key, slot)) visible.push_back(parameter);
    pages.clear(juce::dontSendNotification);
    pages.addItem("ALL CONTROLS", 1);
    pages.setSelectedId(1, juce::dontSendNotification);
    showDetail();
    repaint();
  }
  void showDetail() {
    for (int i = 0; i < int(detail.size()); ++i)
      bind(detail[size_t(i)], i < int(visible.size()) ? visible[size_t(i)] : nullptr);
    resized();
  }

  void refreshStrip(int slot) {
    auto &s = strips[size_t(slot - 1)];
    const int m = std::clamp(int(v("module", slot)), 0, rackModelCount - 1);
    if (s.kind != m) {
      s.kind = m;
      s.module.clear(juce::dontSendNotification);
      s.module.addItem("EMPTY", 1);
      for (int type : rackCollection)
        s.module.addItem(rackModels[type].name, type + 1);
      if (m != 0 &&
          std::find(std::begin(rackCollection), std::end(rackCollection), m) ==
              std::end(rackCollection))
        s.module.addItem(juce::String(rackModels[m].name) + " (SAVED)", m + 1);
      int index = 0;
      for (auto key : rackPanelOrder(m)) {
        if (!rackControlUsed(m, key)) continue;
        if (index == int(s.controls.size())) break;
        bind(s.controls[size_t(index++)], spec(key, slot));
      }
      while (index < int(s.controls.size())) bind(s.controls[size_t(index++)], nullptr);
      if (selected == slot)
        choose(slot);
    }
    s.module.setVisible(m != 0);
    s.on.setVisible(m != 0);
    s.solo.setVisible(m != 0);
    s.module.setSelectedId(m + 1, juce::dontSendNotification);
    s.on.setToggleState(v("enable", slot) > .5, juce::dontSendNotification);
    s.solo.setToggleState(v("solo", slot) > .5, juce::dontSendNotification);
    s.select.setToggleState(selected == slot, juce::dontSendNotification);
  }
  void move(int step) {
    auto i = std::find(order.begin(), order.end(), selected);
    if (i == order.end())
      return;
    int from = int(i - order.begin()), to = std::clamp(from + step, 0, 7);
    if (from == to)
      return;
    p.undo.beginNewTransaction("Move rack module");
    std::swap(order[size_t(from)], order[size_t(to)]);
    for (int k = 0; k < 8; ++k)
      write("order", order[size_t(k)], float(k + 1));
    resized();
    repaint();
  }
  void timerCallback() override {
    auto previous = order;
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
      auto x = v("order", a), y = v("order", b);
      return x == y ? a < b : x < y;
    });
    if (previous != order)
      resized();
    for (int slot = 1; slot <= 8; ++slot)
      refreshStrip(slot);
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    repaint();
  }

public:
  explicit RackEditor(Processor &processor)
      : AudioProcessorEditor(processor), p(processor) {
    setLookAndFeel(&look);
    setSize(1160, 700);
    addAndMakeVisible(browser);
    browser.setViewedComponent(&library, false);
    browser.setScrollBarsShown(true, false);
    category.setComponentID("module-category");category.setName("MODULE CATEGORY");
    category.addItemList({"ALL MODULES","EQ","DYNAMICS","COLOR / TONE","STEREO","UTILITY"},1);
    category.setSelectedId(1,juce::dontSendNotification);addAndMakeVisible(category);
    category.onChange=[this]{filterLibrary();};
    library.setSize(204, 30 * 51);
    for (int i = 0; i < 30; ++i) {
      auto &card = cards[size_t(i)];
      card.module = rackCollection[i];
      card.setName(juce::String(rackModels[card.module].name) + " / " +
                   rackRole(card.module));
      card.setComponentID("module-card-" + juce::String(card.module));
      library.addAndMakeVisible(card);
      card.setBounds(0, i * 51, 200, 49);
      card.dragged = [this](int m, juce::Component *source) {
        if (!isDragAndDropActive())
          startDragging("module:" + juce::String(m), source);
      };
      card.clicked = [this](int m) { assign(selected, m); };
    }
    for (auto *c : std::array<juce::Component *, 7>{
             &quality, &preset, &pages, &left, &right, &undo, &redo})
      addAndMakeVisible(c);
    pages.setComponentID("pages");
    pages.setVisible(false);
    pages.onChange = [this] { showDetail(); };
    quality.addItemList({"LIVE", "PRO"}, 1);
    qa = std::make_unique<CA>(p.state, "gillQuality", quality);
    for (int i = 0; i < 6; ++i)
      preset.addItem(p.getProgramName(i), i + 1);
    preset.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    preset.onChange = [this] {
      p.setCurrentProgram(preset.getSelectedId() - 1);
    };
    left.onClick = [this] { move(-1); };
    right.onClick = [this] { move(1); };
    undo.onClick = [this] { p.undo.undo(); };
    redo.onClick = [this] { p.undo.redo(); };
    for (int slot = 1; slot <= 8; ++slot) {
      auto &s = strips[size_t(slot - 1)];
      for (auto *c : std::array<juce::Component *, 4>{&s.module, &s.select,
                                                      &s.on, &s.solo})
        addAndMakeVisible(c);
      s.select.setButtonText("SLOT " + juce::String(slot));
      s.select.onClick = [this, slot] {
        choose(slot);
        if (v("module", slot) == 0)
          pick(slot);
      };
      s.select.drag = [this, slot] {
        if (!isDragAndDropActive())
          startDragging("slot:" + juce::String(slot),
                        &strips[size_t(slot - 1)].select);
      };
      s.on.setButtonText("ON");
      s.solo.setButtonText("SOLO");
      s.on.onClick = [this, slot] {
        write("enable", slot, v("enable", slot) > .5 ? 0.f : 1.f);
      };
      s.solo.onClick = [this, slot] {
        write("solo", slot, v("solo", slot) > .5 ? 0.f : 1.f);
      };
      s.module.onChange = [this, slot] {
        assign(slot,strips[size_t(slot-1)].module.getSelectedId()-1);
      };
      for (auto &c : s.controls) {
        setup(c);
        c.label.setColour(juce::Label::textColourId, gill::prism::white);
      }
      refreshStrip(slot);
    }
    for (auto &c : detail) {
      setup(c);
      c.label.setColour(juce::Label::textColourId, gill::prism::white);
      c.label.setFont(gill::prism::font(9.5f, true));
    }
    for (int i = 0; i < 3; ++i) {
      setup(global[i]);
      bind(global[i], &p.definition.params[size_t(i)]);
    }
    choose(1);
    keys = std::make_unique<gill::HostKeyboardPolicy>(*this);
    resized();
    startTimerHz(20);
  }
  ~RackEditor() override {
    stopTimer();
    keys.reset();
    setLookAndFeel(nullptr);
  }

  void assign(int slot, int module) {
    if (slot < 1 || slot > 8 || module < 0 || module >= rackModelCount)
      return;
    // Flush asynchronous parameter changes before opening the insertion transaction.
    // Otherwise the first edit can capture stale values and Undo loses the prior slot.
    p.state.copyState();
    p.undo.beginNewTransaction("Insert rack module");
    for (const auto &param : p.definition.params)
      if (param.group == slot && param.key != "order")
        write(param.key.c_str(), slot, param.def);
    write("module", slot, float(module));
    write("enable", slot, 1);
    const auto family = rackModels[module].family;
    if (family == RackFamily::Drive || family == RackFamily::Console)
      write("amount", slot, 28);
    if (family == RackFamily::Air || family == RackFamily::Revive) {
      write("amount", slot, 25);
      write("frequency", slot, 9500);
    }
    if (family == RackFamily::Earth) {
      write("amount", slot, 20);
      write("frequency", slot, 120);
    }
    if (family == RackFamily::Deess) {
      write("frequency", slot, 6500);
      write("threshold", slot, module == 41 ? -12.f : -24.f);
      write("range", slot, 6);
    }
    if (family == RackFamily::Chorus)
      write("wet", slot, 45);
    if (family == RackFamily::Transient) {
      write("attackamount", slot, 3);
      write("sustain", slot, -2);
    }
    if (family == RackFamily::Lift)
      write("amount", slot, 25);
    if (family == RackFamily::Gate)
      write("threshold", slot, -42);
    if (family == RackFamily::Drum) {
      write("threshold", slot, -42);
      write("range", slot, 36);
    }
    p.state.copyState();
    p.undo.beginNewTransaction();
    refreshStrip(slot);
    choose(slot);
    repaint();
  }
  void pick(int slot) {
    juce::PopupMenu menu;
    for (int m : rackCollection)
      menu.addItem(m + 1, rackModels[m].name);
    menu.addSeparator();
    menu.addItem(1, "EMPTY SLOT");
    juce::Component::SafePointer<RackEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(
                           &strips[size_t(slot - 1)].select),
                       [safe, slot](int id) {
                         if (safe && id)
                           safe->assign(slot, id - 1);
                       });
  }
  int columnAt(juce::Point<int> pos) const {
    return pos.y >= 109 && pos.y < 350 && pos.x >= 243 && pos.x < 1141
               ? std::clamp((pos.x - 243) / 112, 0, 7)
               : -1;
  }
  bool isInterestedInDragSource(const SourceDetails &d) override {
    auto v = d.description.toString();
    return v.startsWith("module:") || v.startsWith("slot:");
  }
  void itemDragMove(const SourceDetails &d) override {
    dropColumn = columnAt(d.localPosition);
    repaint();
  }
  void itemDragEnter(const SourceDetails &d) override { itemDragMove(d); }
  void itemDragExit(const SourceDetails &) override {
    dropColumn = -1;
    repaint();
  }
  void itemDropped(const SourceDetails &d) override {
    const int col = columnAt(d.localPosition);
    dropColumn = -1;
    if (col < 0)
      return;
    const auto desc = d.description.toString();
    const int id = desc.fromFirstOccurrenceOf(":", false, false).getIntValue();
    if (desc.startsWith("module:"))
      assign(order[size_t(col)], id);
    else if (id >= 1 && id <= 8) {
      auto source = std::find(order.begin(), order.end(), id);
      if (source == order.end())
        return;
      int from = int(source - order.begin());
      if (from < col)
        std::rotate(order.begin() + from, order.begin() + from + 1,
                    order.begin() + col + 1);
      else if (from > col)
        std::rotate(order.begin() + col, order.begin() + from,
                    order.begin() + from + 1);
      p.undo.beginNewTransaction("Drag rack order");
      for (int i = 0; i < 8; ++i)
        write("order", order[size_t(i)], float(i + 1));
      choose(id);
      resized();
      repaint();
    }
  }
  void mouseDown(const juce::MouseEvent &e) override {
    int c = columnAt(e.getPosition());
    if (c >= 0) {
      choose(order[size_t(c)]);
      if (v("module", selected) == 0)
        pick(selected);
    }
  }
  void resized() override {
    preset.setBounds(486, 20, 310, 25);
    quality.setBounds(1001, 20, 130, 25);
    undo.setBounds(26, 70, 86, 25);
    redo.setBounds(119, 70, 86, 25);
    left.setBounds(883, 70, 116, 25);
    right.setBounds(1006, 70, 116, 25);
    pages.setBounds(502, 360, 377, 25);
    category.setBounds(25,110,198,25);
    browser.setBounds(21, 141, 208, 511);
    for (int column = 0; column < 8; ++column) {
      int x = 246 + column * 112;
      auto &s = strips[size_t(order[column] - 1)];
      s.select.setBounds(x, 115, 102, 22);
      s.module.setBounds(x, 143, 102, 24);
      s.on.setBounds(x, 207, 48, 20);
      s.solo.setBounds(x + 54, 207, 48, 20);
      for (int i = 0; i < 4; ++i) {
        int y = 232 + (i / 2) * 55;
        int cx = x + (i % 2) * 51;
        auto &c = s.controls[size_t(i)];
        c.label.setFont(gill::prism::font(7.4f, true));
        c.label.setBounds(cx, y, 51, 13);
        c.knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 51, 13);
        c.knob.setBounds(cx, y + 10, 51, 44);
        c.choice.setBounds(cx, y + 21, 51, 23);
      }
    }
    const int module = std::clamp(int(v("module", selected)), 0, rackModelCount - 1);
    std::array<int, 3> count{}, position{};
    for (auto* parameter : visible) ++count[size_t(rackControlSection(module, parameter->key))];
    for (int i = 0; i < int(visible.size()) && i < int(detail.size()); ++i) {
      const int section = rackControlSection(module, visible[size_t(i)]->key);
      const int columns = std::max(4, count[size_t(section)]);
      const int cell = 880 / columns;
      const bool twoRows = count[1] == 0;
      int x = 250 + position[size_t(section)]++ * cell;
      int y = 404 + (twoRows ? (section == 2 ? 112 : 0) : section * 76);
      auto &c = detail[size_t(i)];
      c.label.setBounds(x, y, cell - 5, 15);
      c.knob.setBounds(x + 4, y + 13, cell - 13, twoRows ? 81 : 49);
      c.choice.setBounds(x + 3, y + (twoRows ? 38 : 27), cell - 11, 24);
    }
    for (int i = 0; i < 3; ++i) {
      global[i].label.setBounds(686 + i * 149, 636, 135, 14);
      global[i].knob.setSliderStyle(juce::Slider::LinearHorizontal);
      global[i].knob.setBounds(686 + i * 149, 651, 135, 40);
    }
  }
  void paint(juce::Graphics &g) override {
    using namespace gill::prism;
    chassis(g, 1160, 700, 501, 57, true, 35);
    title(g, "GILLRACK", {72, 17, 255, 30}, 24);
    g.setFont(font(11));
    g.setColour(white);
    g.drawText("DRAG MODULES INTO THE RACK  /  30 MODULES", 262, 70, 603, 25,
               juce::Justification::centredLeft);
    glass(g, {17, 106, 215, 551}, 5);
    for (int column = 0; column < 8; ++column) {
      int slot = order[column], m = strips[size_t(slot - 1)].kind;
      auto r = juce::Rectangle<float>(242.f + column * 112, 109, 110, 241);
      const float x = r.getX();
      if (m > 0) {
        rackskin::drawFace(g, m, r);
        gill::material::panel(g, {x + 7, 174, 96, 29}, juce::Colour(0xff101b24),
                              4, true);
        logo(g, {x + 9, 177, 27, 23});
        g.setColour(juce::Colour(rackskin::finish(m).accent));
        g.setFont(font(7.8f, true));
        g.drawFittedText(rackRole(m),
                         juce::Rectangle<int>(int(x) + 39, 178, 60, 21),
                         juce::Justification::centred, 2);
      } else {
        glass(g, r, 4);
        g.setColour(white.withAlpha(.13f));
        g.drawRoundedRectangle(r.reduced(5), 3, 1);
        g.setColour(cyan.withAlpha(.58f));
        g.setFont(font(44));
        g.drawText("+", r.withY(190).withHeight(65),
                   juce::Justification::centred, false);
        g.setFont(font(10, true));
        g.drawFittedText("DROP MODULE\nOR CLICK",
                         r.withY(270).withHeight(50).reduced(7).toNearestInt(),
                         juce::Justification::centred, 2);
      }
      g.setColour(column == dropColumn ? juce::Colours::white
                  : slot == selected   ? cyan
                                       : cyan.withAlpha(.15f));
      g.drawRoundedRectangle(r, 4,
                             column == dropColumn ? 2.5f
                             : slot == selected   ? 1.3f
                                                  : .6f);
    }
    const int module = std::clamp(int(v("module", selected)), 0, rackModelCount - 1);
    auto panel = juce::Rectangle<float>(243, 357, 896, 271);
    rackskin::drawFace(g, module, panel);
    g.setColour(juce::Colours::black.withAlpha(.32f));
    g.fillRoundedRectangle(panel.reduced(4), 3);
    std::array<int, 3> sectionCounts{};
    for (auto* parameter : visible) ++sectionCounts[size_t(rackControlSection(module, parameter->key))];
    const bool equalizer = rackModels[module].family == RackFamily::Eq || rackModels[module].family == RackFamily::Passive;
    for (int section = 0; section < 3; ++section) {
      if (!sectionCounts[size_t(section)]) continue;
      const bool twoRows = sectionCounts[1] == 0;
      const int top = 389 + (twoRows ? (section == 2 ? 112 : 0) : section * 76);
      const int groups = equalizer && section == 0 ? 4 : 1;
      for (int group = 0; group < groups; ++group) {
        const float width = 880.f / groups;
        auto groupBounds = juce::Rectangle<float>(249.f + group * width, float(top), width - 4, twoRows ? 109.f : 73.f);
        g.setColour(juce::Colour(0xff07121b).withAlpha(.73f));
        g.fillRoundedRectangle(groupBounds, 3);
        g.setColour(juce::Colour(rackskin::finish(module).accent).withAlpha(.65f));
        g.drawRoundedRectangle(groupBounds, 3, .6f);
        g.setFont(font(8, true));
        const char* bandNames[]{"LOW SHELF", "MID BELL", "UPPER BELL", "HIGH SHELF"};
        const char* name = section == 2 ? "SLOT LEVELS / MIX / POLARITY" : section == 1 ? (equalizer ? "FILTERS / SHARED Q / DRIVE / ATTENUATION" : "DETECTOR / SIDECHAIN / GATE") : equalizer ? bandNames[group] : rackRole(module);
        g.drawText(name, groupBounds.reduced(7, 0).withHeight(14), juce::Justification::centredLeft, false);
      }
    }
    logo(g, {253, 362, 30, 24});
    g.setColour(juce::Colour(rackskin::finish(module).accent));
    g.setFont(font(12, true));
    g.drawText("SLOT " + juce::String(selected) + "  /  " + rackModels[module].name,
               293, 362, 400, 24, juce::Justification::centredLeft);
    g.setColour(white.withAlpha(.65f));
    g.setFont(font(9));
    g.drawText(module ? rackPanelHint(module) : "CLICK A LIBRARY CARD OR DROP IT INTO A SLOT",
               660, 362, 462, 24, juce::Justification::centredRight);
    g.setColour(ink);
    g.drawText("DRAG SLOT HEADERS TO REORDER", 261, 640, 410, 20,
               juce::Justification::left);
    g.drawText(juce::String(p.appliedQuality() ? "PRO" : "LIVE") + "  /  " +
                   juce::String(p.getLatencySamples()) + " samples",
               261, 666, 400, 17, juce::Justification::left);
  }
};
juce::AudioProcessorEditor *createRackEditor(Processor &p) {
  return new RackEditor(p);
}
} // namespace studio
