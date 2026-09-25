#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <array>
#include <cmath>

namespace
{
const juce::Colour ink (0xff26362e), sage (0xff527561), sageLight (0xff8ca18d);
const juce::Colour ivory (0xffe9e6dc), muted (0xff747b70), line (0xffbab9ad);
juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (gillInterfaceFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
}
juce::String bandId (int band, const char* suffix) { return "band" + juce::String (band + 1) + "_" + suffix; }
float value (GilleqAudioProcessor& p, int band, const char* suffix)
{
    if (auto* raw = p.apvts.getRawParameterValue (bandId (band, suffix))) return raw->load();
    return 0.0f;
}
void setValue (GilleqAudioProcessor& p, int band, const char* suffix, float v, bool gesture = true)
{
    if (auto* parameter = p.apvts.getParameter (bandId (band, suffix)))
    {
        if (gesture) parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (v));
        if (gesture) parameter->endChangeGesture();
    }
}
juce::String frequencyText (double v)
{
    return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " KHZ" : juce::String (v, 1) + " HZ";
}

class OakLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OakLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xfff4f2e9));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xffadb2a5));
        setColour (juce::Slider::textBoxHighlightColourId, sage.withAlpha (0.2f));
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfff4f2e9));
        setColour (juce::TextEditor::outlineColourId, sage);
        setColour (juce::TextEditor::highlightColourId, sage.withAlpha (0.25f));
        setColour (juce::TextEditor::highlightedTextColourId, ink);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::backgroundColourId, ivory);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::PopupMenu::backgroundColourId, ivory);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, sage);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, ivory);
        setColour (juce::TooltipWindow::textColourId, ink);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font (juce::jlimit (10.0f, 14.0f, height * 0.37f), true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (15.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (16.0f); }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        if (s.getProperties().getWithDefault ("GRAPH VALUE", false))
            return { {}, s.getLocalBounds() };
        return juce::LookAndFeel_V4::getSliderLayout (s);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position, float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const auto cy = (float) y + (float) height * 0.5f;
        const auto alpha = slider.isEnabled() ? 1.0f : 0.35f;
        g.setColour (line.withAlpha (alpha)); g.fillRoundedRectangle ((float) x, cy - 2, (float) width, 4, 2);
        g.setColour (sage.withAlpha (alpha)); g.fillRoundedRectangle ((float) x, cy - 2, juce::jmax (1.0f, position - (float) x), 4, 2);
        g.setColour (juce::Colours::black.withAlpha (0.14f * alpha)); g.fillEllipse (position - 6, cy - 5, 12, 12);
        g.setColour (ivory.brighter (0.2f).withAlpha (alpha)); g.fillEllipse (position - 6, cy - 6, 12, 12);
        g.setColour (sage.withAlpha (alpha)); g.drawEllipse (position - 6, cy - 6, 12, 12, 1.5f);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const bool on = b.getToggleState();
        const auto base = on ? sage : ivory;
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 6.0f);
        g.setGradientFill (juce::ColourGradient (base.brighter (down ? 0.0f : 0.12f), r.getX(), r.getY(), base.darker (down ? 0.10f : 0.02f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? sage.darker (0.25f) : line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.25f : 0.75f));
        g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 0.75f);
        if (over) { g.setColour (sage.withAlpha (0.08f)); g.fillRoundedRectangle (r, 6.0f); }
        if (! b.isEnabled()) { g.setColour (ivory.withAlpha (0.55f)); g.fillRoundedRectangle (r, 6.0f); }
    }
    void drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.08f)); g.fillRoundedRectangle (r.translated (0, 1), 6);
        g.setGradientFill (juce::ColourGradient (ivory.brighter (0.08f), 0, 0, ivory.darker (down ? 0.08f : 0.03f), 0, (float) height, false));
        g.fillRoundedRectangle (r, 6);
        g.setColour (line); g.drawRoundedRectangle (r, 6, 1);
        g.setColour (juce::Colours::white.withAlpha (0.65f)); g.drawRoundedRectangle (r.reduced (1), 5, 0.8f);
        juce::Path arrow;
        const auto x = (float) width - 20.0f, y = (float) height * 0.45f;
        arrow.startNewSubPath (x - 4, y); arrow.lineTo (x, y + 4); arrow.lineTo (x + 4, y);
        g.setColour (ink); g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float start, float end, juce::Slider& slider) override
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.beginTransparencyLayer(slider.isEnabled() ? 1.0f : 0.38f);
        gill::material::rotary(g, {static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)}, pos, start, end, sage);
        g.endTransparencyLayer();
    }
};

class GraphValue final : public juce::Slider
{
public:
    GraphValue() { getProperties().set ("GRAPH VALUE", true); setWantsKeyboardFocus (true); }
    void paint (juce::Graphics&) override {} // The editable value is the control.
    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool arrow = key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey
                        || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey;
        if (isEnabled() && arrow && ! key.getModifiers().isAnyModifierKeyDown())
        { juce::Slider::ScopedDragNotification gesture (*this); return Slider::keyPressed (key); }
        return Slider::keyPressed (key);
    }
};

class DirectBandControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    DirectBandControl (GilleqAudioProcessor& processor, const char* parameterSuffix, float lo, float hi, float reset)
        : p (processor), suffix (parameterSuffix), minimum (lo), maximum (hi), defaultValue (reset)
    { setWantsKeyboardFocus (true); setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    ~DirectBandControl() override { finishGesture(); }
    int selected = 3;
    std::function<void()> onEdit;
    void finishGesture()
    {
        if (gestureParameter != nullptr) gestureParameter->endChangeGesture();
        gestureParameter = nullptr; gestureBand = -1;
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (! isEnabled() || key.getModifiers().isAnyModifierKeyDown()) return false;
        float next = value (p, selected, suffix);
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey) next -= 0.1f;
        else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey) next += 0.1f;
        else if (key == juce::KeyPress::homeKey) next = minimum;
        else if (key == juce::KeyPress::endKey) next = maximum;
        else return false;
        setValue (p, selected, suffix, juce::jlimit (minimum, maximum, next));
        if (onEdit) onEdit(); return true;
    }
    void mouseUp (const juce::MouseEvent&) override { finishGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    { finishGesture(); setValue (p, selected, suffix, defaultValue); if (onEdit) onEdit(); }
protected:
    GilleqAudioProcessor& p;
    const char* suffix;
    const float minimum, maximum, defaultValue;
    int gestureBand = -1;
    juce::RangedAudioParameter* gestureParameter = nullptr;
    bool beginGesture (const juce::MouseEvent& e)
    {
        if (! isEnabled() || ! e.mods.isLeftButtonDown()) return false;
        finishGesture(); gestureBand = selected;
        gestureParameter = p.apvts.getParameter (bandId (gestureBand, suffix));
        if (gestureParameter != nullptr) gestureParameter->beginChangeGesture();
        if (isShowing()) grabKeyboardFocus(); return gestureParameter != nullptr;
    }
    void edit (float next)
    {
        if (gestureParameter == nullptr) return;
        setValue (p, gestureBand, suffix, juce::jlimit (minimum, maximum, next), false);
        if (onEdit) onEdit();
    }
};

class RangeHandle final : public DirectBandControl
{
public:
    explicit RangeHandle (GilleqAudioProcessor& p) : DirectBandControl (p, "range", -24, 24, -6)
    { setName ("RANGE HANDLE"); setTooltip ("RANGE | DRAG UP / DOWN | ARROWS: 0.1 DB | DOUBLE CLICK: -6 DB | EXACT VALUE IN BAND CARD"); }
    float dbPerPixel = 0.2f;
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (3);
        juce::Path diamond; diamond.startNewSubPath (r.getCentreX(), r.getY()); diamond.lineTo (r.getRight(), r.getCentreY());
        diamond.lineTo (r.getCentreX(), r.getBottom()); diamond.lineTo (r.getX(), r.getCentreY()); diamond.closeSubPath();
        g.setColour (ivory); g.fillPath (diamond); g.setColour (sage); g.strokePath (diamond, juce::PathStrokeType (2));
        if (hasKeyboardFocus (true)) { g.setColour (ink); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 3, 1); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    { if (beginGesture (e)) { startY = e.getScreenY(); startValue = value (p, gestureBand, "range"); } }
    void mouseDrag (const juce::MouseEvent& e) override { edit (startValue + (float) (startY - e.getScreenY()) * dbPerPixel); }
private:
    int startY = 0; float startValue = 0;
};

class ResponseGraph final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ResponseGraph (GilleqAudioProcessor& processor) : p (processor), fft (11)
    {
        setName ("RESPONSE GRAPH");
        setTooltip ("DRAG A NODE TO CHANGE FREQUENCY AND GAIN | SHIFT DRAG OR SCROLL FOR Q | MID REF: L=R INPUT | SIDE REF: L=-R INPUT | LEFT / RIGHT REF: ONE INPUT CHANNEL | CURVE INCLUDES OUTPUT TRIM");
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        for (size_t i = 0; i < window.size(); ++i)
        {
            window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (window.size() - 1));
            windowSum += window[i];
        }
        spectrumPre.fill (-120.0f); spectrumPost.fill (-120.0f);
        startTimerHz (25);
    }
    ~ResponseGraph() override { finishGesture(); }
    std::function<void(int)> onSelection;
    int selected = 3;
    bool analyzing = true, frozen = false;
    void attachOverlay (juce::Component& card, RangeHandle& handle)
    {
        overlay = &card; rangeHandle = &handle; addAndMakeVisible (card); addChildComponent (handle);
        handle.onEdit = [this] { updateOverlayLayout(); repaint(); };
    }
    void showDynamics (bool show) { dynamicOverlay = show; updateOverlayLayout(); }
    void updateOverlayLayout()
    {
        if (overlay == nullptr || rangeHandle == nullptr || getWidth() < 350 || getHeight() < 150) return;
        const auto r = plot(); const auto node = nodePosition (selected);
        const int width = dynamicOverlay ? 318 : 192, height = dynamicOverlay ? 140 : 38;
        const auto area = r.reduced (7).withTrimmedTop (24).withTrimmedBottom (17);
        auto x = node.x + 31.0f;
        if (x + (float) width > area.getRight()) x = node.x - 31.0f - (float) width;
        x = juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight() - width), x);
        auto y = node.y - (float) height - 20;
        if (y < area.getY()) y = node.y + 20;
        y = juce::jlimit (area.getY(), juce::jmax (area.getY(), area.getBottom() - height), y);
        overlay->setBounds (juce::roundToInt (x), juce::roundToInt (y), width, height);
        rangeHandle->selected = selected; rangeHandle->dbPerPixel = 48.0f / r.getHeight();
        const auto endY = yFor (juce::jlimit (-24.0f, 24.0f, value (p, selected, "gain") + value (p, selected, "range")));
        const float handleX = node.x + (overlay->getBounds().getCentreX() > node.x ? 18.0f : -18.0f);
        rangeHandle->setBounds (juce::Rectangle<float> (22, 22).withCentre ({ handleX, endY }).toNearestInt());
        rangeHandle->setVisible (dynamicOverlay);
        rangeHandle->toFront (false);
    }
    void resized() override { updateOverlayLayout(); }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft (42).withTrimmedRight (13).withTrimmedTop (15).withTrimmedBottom (29); }
    float xFor (double hz) const { auto r = plot(); return r.getX() + (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)) * r.getWidth(); }
    float yFor (double db) const { auto r = plot(); return r.getCentreY() - (float) juce::jlimit (-24.0, 24.0, db) * r.getHeight() / 48.0f; }
    double hzFor (float x) const { auto r = plot(); return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
    float gainFor (float y) const { auto r = plot(); return juce::jlimit (-24.0f, 24.0f, (r.getCentreY() - y) * 48.0f / r.getHeight()); }
    float maxHz() const { gill::BandParams band; band.frequency = 20000.0; return (float) gill::sanitize (band, sampleRate()).frequency; }
    double sampleRate() const { return p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0; }
    juce::Point<float> nodePosition (int i) const
    {
        const auto type = (int) value (p, i, "type");
        return { xFor (juce::jmin (maxHz(), value (p, i, "freq"))), yFor (type >= 3 ? 0.0 : (double) value (p, i, "gain")) };
    }
    void paint (juce::Graphics& g) override
    {
        auto r = plot();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5d9cc), r.getX(), r.getY(), juce::Colour (0xffc5cebd), r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 11);
        g.setColour (juce::Colours::white.withAlpha (0.7f)); g.drawRoundedRectangle (r.translated (0, 1), 11, 1.0f);
        g.setFont (font (14.0f));
        for (int db = -24; db <= 24; db += r.getHeight() < 160 ? 12 : 6)
        {
            auto y = yFor ((double) db);
            g.setColour (sage.withAlpha (db == 0 ? 0.34f : 0.10f)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (muted); g.drawText ((db > 0 ? "+" : "") + juce::String (db), 0, (int) y - 8, 32, 16, juce::Justification::right);
        }
        for (int decade = 10; decade <= 10000; decade *= 10)
            for (int multiple = 1; multiple <= 9; ++multiple)
            {
                const int hz = decade * multiple;
                if (hz < 20 || hz > 20000) continue;
                g.setColour (sage.withAlpha (multiple == 1 ? 0.17f : 0.075f));
                g.drawVerticalLine ((int) xFor (hz), r.getY(), r.getBottom());
            }
        const std::array<int, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (int hz : frequencies)
        {
            g.setColour (muted);
            const auto label = hz >= 1000 ? juce::String (hz / 1000) + "K" : juce::String (hz);
            g.drawText (label, (int) xFor (hz) - 21, (int) r.getBottom() + 7, 42, 15, juce::Justification::centred);
        }
        g.setFont (font (12.0f, true)); g.setColour (muted); g.drawText ("DB", 6, 0, 28, 13, juce::Justification::right);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());
            if (analyzing)
            {
                paintSpectrum (g, spectrumPre, sage.withAlpha (0.09f), sage.withAlpha (0.23f));
                paintSpectrum (g, spectrumPost, sage.withAlpha (0.13f), sage.withAlpha (0.45f));
            }
            juce::Path response, fill, selectedResponse;
            const int points = juce::jmax (1, (int) r.getWidth());
            const float nyquistX = xFor (sampleRate() * 0.5);
            const int mode = (int) value (p, selected, "channel");
            // One atomic audio snapshot supplies all points in this paint frame.
            const auto snapshot = p.getResponseSnapshot();
            const auto selectedDesign = gill::designFilter (snapshot.bands[(size_t) selected], snapshot.sampleRate);
            for (int i = 0; i <= points; ++i)
            {
                auto x = r.getX() + (float) i;
                if (x > nyquistX) break;
                auto hz = hzFor (x);
                const auto db = p.getResponseDb (hz, mode, snapshot);
                auto y = yFor (std::isfinite (db) ? db : 0.0);
                std::complex<double> bandResponse (1.0, 0.0);
                for (int section = 0; section < selectedDesign.count; ++section)
                    bandResponse *= gill::sectionResponse (selectedDesign.sections[(size_t) section], snapshot.sampleRate, hz);
                const auto selectedY = yFor (20.0 * std::log10 (juce::jmax (1.0e-12, std::abs (bandResponse))));
                if (i == 0) selectedResponse.startNewSubPath (x, selectedY);
                else selectedResponse.lineTo (x, selectedY);
                if (i == 0) { response.startNewSubPath (x, y); fill.startNewSubPath (x, r.getCentreY()); fill.lineTo (x, y); }
                else { response.lineTo (x, y); fill.lineTo (x, y); }
            }
            fill.lineTo (juce::jmin (r.getRight(), nyquistX), r.getCentreY()); fill.closeSubPath();
            g.setColour (sage.withAlpha (0.07f)); g.fillPath (fill);
            juce::Path dashedSelected;
            const float dashes[] { 4.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashedSelected, selectedResponse, dashes, 2);
            g.setColour (sage.withAlpha (0.58f)); g.fillPath (dashedSelected);
            g.setColour (juce::Colours::white.withAlpha (0.65f)); g.strokePath (response, juce::PathStrokeType (4.5f));
            g.setColour (ink); g.strokePath (response, juce::PathStrokeType (2.2f));
            if (sampleRate() * 0.5 < 20000.0)
            {
                g.setColour (ivory.withAlpha (0.72f)); g.fillRect (r.withLeft (nyquistX));
                g.setColour (muted); g.setFont (font (12)); g.drawText ("NYQUIST", r.withLeft (nyquistX).reduced (3), juce::Justification::centred, false);
            }
            for (int i = 0; i < 8; ++i)
            {
                const auto pos = nodePosition (i);
                const bool enabled = value (p, i, "enabled") >= 0.5f;
                const bool active = i == selected;
                const auto& liveBand = snapshot.bands[(size_t) i];
                if (enabled && value (p, i, "dynamic") >= 0.5f && gill::supportsDynamics ((int) value (p, i, "type")))
                {
                    const auto limitY = yFor (juce::jlimit (-24.0f, 24.0f, value (p, i, "gain") + value (p, i, "range")));
                    g.setColour (sage.withAlpha (0.25f)); g.drawLine (pos.x, pos.y, pos.x, limitY, active ? 8.0f : 5.0f);
                    g.setColour (sage.withAlpha (0.6f)); g.drawHorizontalLine ((int) limitY, pos.x - 5, pos.x + 5);
                    const juce::Point<float> livePosition { xFor (liveBand.frequency), yFor (liveBand.gainDb) };
                    g.setColour (ink); g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (livePosition));
                    g.setColour (ivory); g.drawEllipse (juce::Rectangle<float> (6, 6).withCentre (livePosition), 1);
                }
                const float radius = active ? 10.0f : 7.0f;
                auto circle = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (pos);
                if (active) { g.setColour (juce::Colours::black.withAlpha (0.13f)); g.fillEllipse (circle.expanded (3).translated (0, 2)); g.setColour (ivory); g.fillEllipse (circle.expanded (3)); }
                g.setColour (enabled ? sage : juce::Colour (0xffaeb5a7)); g.fillEllipse (circle);
                g.setColour (juce::Colours::white.withAlpha (0.8f)); g.drawEllipse (circle, 1);
                if (active) { g.setFont (font (13, true)); g.setColour (juce::Colours::white); g.drawText (juce::String (i + 1), circle, juce::Justification::centred); }
            }
        }
        g.setColour (sage.withAlpha (0.23f)); g.drawRoundedRectangle (r, 11, 1.0f);
        g.setFont (font (14, true)); g.setColour (muted);
        auto status = juce::String (frozen ? "FROZEN | LEFT CHANNEL" : analyzing ? "PRE / POST | LEFT CHANNEL | -96 TO 0 DBFS" : "ANALYZER OFF");
        const auto channel = (int) value (p, selected, "channel");
        const auto displayedBands = p.getBands();
        const bool mixedChannels = std::any_of (displayedBands.begin(), displayedBands.end(), [] (const gill::BandParams& band) { return band.enabled && band.channel != gill::Stereo; });
        const std::array<const char*, 5> channels { mixedChannels ? "MID REF" : "STEREO", mixedChannels ? "MID REF" : "STEREO", "SIDE REF", "LEFT REF", "RIGHT REF" };
        status += "  |  " + juce::String (channels[(size_t) juce::jlimit (0, 4, channel)]);
        if (auto* deltaParameter = p.apvts.getRawParameterValue ("delta"))
            if (deltaParameter->load() > 0.5f) status += "  |  DELTA";
        g.drawText (status, r.toNearestInt().reduced (12, 7).removeFromTop (17), juce::Justification::topRight);
        if (gill::dynamicsActive (displayedBands[(size_t) selected]))
        {
            g.setFont (font (12, true)); g.setColour (ink.withAlpha (0.8f));
            g.drawText ("LIVE CURVE | NODE: GAIN | DIAMOND: RANGE", r.toNearestInt().reduced (12, 7).removeFromBottom (15), juce::Justification::bottomLeft);
        }
        if (overlay != nullptr)
        {
            const auto node = nodePosition (selected); const auto card = overlay->getBounds().toFloat();
            g.setColour (sage.withAlpha (0.4f));
            g.drawLine (node.x, node.y, juce::jlimit (card.getX(), card.getRight(), node.x), juce::jlimit (card.getY(), card.getBottom(), node.y), 1);
            if (rangeHandle != nullptr && rangeHandle->isVisible())
            {
                const auto end = rangeHandle->getBounds().toFloat().getCentre();
                g.setColour (sage); g.drawLine (node.x, end.y, end.x, end.y, 1.5f);
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plot().expanded (10).contains (e.position)) return;
        int nearest = nearestBand (e.position, 23.0f);
        if (nearest < 0) return;
        selected = nearest;
        if (onSelection) onSelection (selected);
        if (e.mods.isRightButtonDown()) { setValue (p, selected, "enabled", value (p, selected, "enabled") < 0.5f ? 1.0f : 0.0f); return; }
        dragBand = selected; dragStart = e.position; dragQ = value (p, dragBand, "q");
        for (auto suffix : { "freq", "gain", "q" })
            if (auto* parameter = p.apvts.getParameter (bandId (dragBand, suffix))) parameter->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0) return;
        if (e.mods.isShiftDown())
            setValue (p, dragBand, "q", juce::jlimit (0.1f, 18.0f, dragQ * std::exp ((dragStart.y - e.position.y) * 0.02f)), false);
        else
        {
            setValue (p, dragBand, "freq", juce::jmin (maxHz(), (float) hzFor (e.position.x)), false);
            if (value (p, dragBand, "type") < 3.0f) setValue (p, dragBand, "gain", gainFor (e.position.y), false);
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { finishGesture(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! plot().contains (e.position)) return;
        int nearest = nearestBand (e.position, 22);
        if (nearest >= 0) { setValue (p, nearest, "gain", 0.0f); return; }
        setValue (p, selected, "freq", juce::jmin (maxHz(), (float) hzFor (e.position.x)));
        if (value (p, selected, "type") < 3.0f) setValue (p, selected, "gain", gainFor (e.position.y));
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto nearest = nearestBand (e.position, 28.0f);
        if (nearest < 0) return;
        selected = nearest; if (onSelection) onSelection (selected);
        const auto delta = wheel.deltaY != 0 ? wheel.deltaY : wheel.deltaX;
        setValue (p, nearest, "q", juce::jlimit (0.1f, 18.0f, value (p, nearest, "q") * std::exp (delta * 2.0f)));
    }
private:
    GilleqAudioProcessor& p;
    juce::Component* overlay = nullptr;
    RangeHandle* rangeHandle = nullptr;
    bool dynamicOverlay = false;
    juce::dsp::FFT fft;
    std::array<float, 2048> window {}, timePre {}, timePost {};
    std::array<float, 4096> fftBuffer {};
    std::array<float, 1025> spectrumPre {}, spectrumPost {};
    float windowSum = 0;
    int dragBand = -1;
    float dragQ = 1;
    juce::Point<float> dragStart;
    int nearestBand (juce::Point<float> point, float limit) const
    {
        int result = -1;
        for (int i = 0; i < 8; ++i) { auto distance = nodePosition (i).getDistanceFrom (point); if (distance < limit) { result = i; limit = distance; } }
        return result;
    }
    void finishGesture()
    {
        if (dragBand < 0) return;
        for (auto suffix : { "freq", "gain", "q" })
            if (auto* parameter = p.apvts.getParameter (bandId (dragBand, suffix))) parameter->endChangeGesture();
        dragBand = -1;
    }
    void transform (const std::array<float, 2048>& samples, std::array<float, 1025>& destination)
    {
        fftBuffer.fill (0);
        for (size_t i = 0; i < samples.size(); ++i) fftBuffer[i] = samples[i] * window[i];
        fft.performFrequencyOnlyForwardTransform (fftBuffer.data());
        for (size_t i = 0; i < destination.size(); ++i)
        {
            const auto amplitude = fftBuffer[i] * ((i == 0 || i == 1024) ? 1.0f : 2.0f) / windowSum;
            const auto db = juce::Decibels::gainToDecibels (amplitude, -120.0f);
            destination[i] = db > destination[i] ? db : destination[i] * 0.82f + db * 0.18f;
        }
    }
    void paintSpectrum (juce::Graphics& g, const std::array<float, 1025>& levels, juce::Colour fillColour, juce::Colour strokeColour)
    {
        auto r = plot(); juce::Path path;
        path.startNewSubPath (r.getX(), r.getBottom());
        float lastX = r.getX();
        for (size_t i = 1; i < levels.size(); ++i)
        {
            const auto hz = (double) i * sampleRate() / 2048.0;
            if (hz < 20) continue;
            if (hz > 20000) break;
            const auto x = xFor (hz), y = r.getBottom() - juce::jlimit (0.0f, 1.0f, (levels[i] + 96.0f) / 96.0f) * r.getHeight();
            path.lineTo (x, y); lastX = x;
        }
        path.lineTo (lastX, r.getBottom()); path.closeSubPath();
        g.setColour (fillColour); g.fillPath (path);
        g.setColour (strokeColour); g.strokePath (path, juce::PathStrokeType (0.8f));
    }
    void timerCallback() override
    {
        if (analyzing && ! frozen && p.readSpectrum (timePre, timePost)) { transform (timePre, spectrumPre); transform (timePost, spectrumPost); }
        updateOverlayLayout();
        repaint();
    }
};

class Meter final : public juce::Component
{
public:
    float db = -100.0f;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setFont (font (13, true)); g.setColour (muted); g.drawText ("OUT", r.removeFromTop (20), juce::Justification::centred);
        auto reading = r.removeFromBottom (23);
        g.setFont (font (13)); g.setColour (db > 0.0f ? juce::Colour (0xffab4b34) : ink);
        g.drawText (db < -90.0f ? "-INF" : juce::String (db, 1), reading, juce::Justification::centred);
        r = r.reduced (12, 1);
        constexpr int count = 42;
        for (int i = 0; i < count; ++i)
        {
            const float threshold = -60.0f + (float) i * 66.0f / (float) count;
            const bool lit = db > threshold;
            g.setColour (lit ? (threshold >= 0 ? juce::Colour (0xffab4b34) : sage) : juce::Colour (0xffc7c5bb));
            const auto h = r.getHeight() / (float) count;
            g.fillRect (r.getX(), r.getBottom() - (float) (i + 1) * h, r.getWidth(), juce::jmax (1.0f, h - 2));
        }
    }
};

class ThresholdSurface final : public DirectBandControl
{
public:
    explicit ThresholdSurface (GilleqAudioProcessor& processor) : DirectBandControl (processor, "threshold", -80, 0, -24)
    {
        setName ("THRESHOLD DETECTOR"); setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTooltip ("THRESHOLD | DRAG ON THIS RMS DBFS AXIS | SHADED LEVEL: INPUT DETECTOR | ARROWS: 0.1 DB | DOUBLE CLICK: -24 DBFS");
    }
    juce::Rectangle<float> axis() const { return getLocalBounds().toFloat().reduced (10, 0).withY (28).withHeight (6); }
    void paint (juce::Graphics& g) override
    {
        const auto track = axis();
        g.setFont (font (12, true)); g.setColour (ink);
        g.drawText ("THRESHOLD / DBFS", 1, 0, getWidth()-2, 17, juce::Justification::left);
        g.setColour (line); g.fillRoundedRectangle (track, 3);
        const auto level = juce::jlimit (0.0f, 1.0f, (p.getBandDetectorDb (selected) + 80) / 80);
        g.setColour (sage.withAlpha (0.7f)); g.fillRoundedRectangle (track.withWidth (level * track.getWidth()), 3);
        const auto x = track.getX() + (value (p, selected, "threshold") + 80) / 80 * track.getWidth();
        g.setColour (ink); g.drawLine (x, track.getY() - 7, x, track.getBottom() + 3, 2);
        juce::Path marker; marker.addTriangle (x - 5, track.getY() - 8, x + 5, track.getY() - 8, x, track.getY() - 3);
        g.fillPath (marker);
        g.setFont (font (11)); g.setColour (muted);
        for (int tick : { -80, -40, 0 })
        {
            const auto tx = track.getX() + (tick + 80) / 80.0f * track.getWidth();
            g.drawText (juce::String (tick), juce::roundToInt (tx) - 12, 40, 24, 15, juce::Justification::centred);
        }
        if (hasKeyboardFocus (true)) { g.setColour (sage); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 4, 1); }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (beginGesture (e)) mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { const auto r = axis(); edit (-80.0f + 80.0f * (e.position.x - r.getX()) / r.getWidth()); }
};

class DynamicCard final : public juce::Component
{
public:
    explicit DynamicCard (GilleqAudioProcessor& processor) : detector (processor), p (processor)
    { setName ("BAND DYNAMICS METER"); addChildComponent (detector); }
    int selected = 3;
    bool dynamicMode = false;
    ThresholdSurface detector;
    void configure (juce::Button& modeButton, juce::Slider& thresholdValue, juce::Slider& rangeValue, juce::Slider& attackValue, juce::Slider& releaseValue)
    {
        mode = &modeButton; fields = { &thresholdValue, &rangeValue, &attackValue, &releaseValue };
        addAndMakeVisible (modeButton); for (auto* field : fields) addChildComponent (*field);
    }
    void refresh (int band, bool enabled)
    {
        if (selected != band || (dynamicMode && ! enabled)) detector.finishGesture();
        selected = detector.selected = band; dynamicMode = enabled;
        detector.setVisible (enabled); for (auto* field : fields) if (field != nullptr) field->setVisible (enabled);
        resized(); repaint();
    }
    void resized() override
    {
        if (mode == nullptr) return;
        mode->setBounds (getWidth() - 105, 7, 95, 24);
        if (! dynamicMode) return;
        fields[1]->setBounds (130, 8, 74, 23);
        detector.setBounds (10, 39, 188, 59);
        fields[0]->setBounds (208, 41, 100, 23);
        fields[2]->setBounds (61, 106, 71, 24);
        fields[3]->setBounds (231, 106, 77, 24);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.7f);
        g.setColour (juce::Colours::black.withAlpha (0.13f)); g.fillRoundedRectangle (r.translated (0, 2), 8);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f4e9), 0, 0, ivory, 0, (float) getHeight(), false)); g.fillRoundedRectangle (r, 8);
        g.setColour (sage.withAlpha (0.65f)); g.drawRoundedRectangle (r, 8, 1);
        g.setColour (ink); g.setFont (font (12, true)); g.drawText ("BAND " + juce::String (selected + 1), 10, 9, 65, 21, juce::Justification::left);
        if (! dynamicMode) return;
        g.setColour (muted); g.setFont (font (11, true)); g.drawText ("RANGE", 79, 10, 49, 20, juce::Justification::left);
        g.setColour (line); g.drawHorizontalLine (35, 10, (float) getWidth()-10);
        const auto level = p.getBandDetectorDb (selected), change = p.getBandDynamicGainDb (selected);
        g.setColour (muted); g.setFont (font (11));
        g.drawText ("IN " + (level < -99 ? juce::String ("-INF") : juce::String (level, 1)) + " DBFS", 205, 68, 105, 14, juce::Justification::centredRight);
        g.setColour (sage); g.setFont (font (11, true));
        g.drawText ("LIVE " + juce::String (change > 0 ? "+" : "") + juce::String (change, 1) + " DB", 205, 83, 105, 14, juce::Justification::centredRight);
        g.setColour (muted); g.setFont (font (11, true));
        g.drawText ("ATTACK", 10, 108, 50, 20, juce::Justification::left);
        g.drawText ("RELEASE", 168, 108, 61, 20, juce::Justification::left);
    }
private:
    GilleqAudioProcessor& p;
    juce::Button* mode = nullptr;
    std::array<juce::Slider*,4> fields {};
};
}

struct GilleqAudioProcessorEditor::Impl final : private juce::Timer
{
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    GilleqAudioProcessorEditor& owner;
    GilleqAudioProcessor& p;
    OakLookAndFeel look;
    juce::Image wood;
    ResponseGraph graph;
    Meter meter;
    DynamicCard dynamicsMeter;
    RangeHandle rangeHandle;
    juce::TooltipWindow tips;
    std::array<juce::TextButton, 8> bandButtons;
    juce::TextButton copy { "A > B" }, swap { "A / B" }, reset { "RESET" };
    juce::TextButton enabled { "ON" }, analyzer { "ANALYZER" }, freeze { "FREEZE" }, delta { "DELTA LISTEN" }, bypass { "BYPASS" }, dynamic { "STATIC" };
    juce::Slider frequency, gain, q, output;
    GraphValue threshold, range, attack, release;
    juce::ComboBox type, channel, slope;
    std::unique_ptr<SliderAttachment> frequencyAttachment, gainAttachment, qAttachment, outputAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment, rangeAttachment, attackAttachment, releaseAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment, channelAttachment, slopeAttachment;
    std::unique_ptr<ButtonAttachment> enabledAttachment, deltaAttachment, bypassAttachment, dynamicAttachment;
    juce::Rectangle<int> header, controlPanel, footer;
    int selected = 3;
    bool abIsB = false;
    gill::QualitySelector quality{p.apvts, p};
    explicit Impl (GilleqAudioProcessorEditor& editor, GilleqAudioProcessor& processor)
        : owner (editor), p (processor), graph (processor), dynamicsMeter (processor), rangeHandle (processor), tips (&editor, 600)
    {
        owner.setLookAndFeel (&look);owner.addAndMakeVisible(quality);
        wood = juce::ImageCache::getFromMemory (BinaryData::oak_sage_png, BinaryData::oak_sage_pngSize);
        owner.addAndMakeVisible (graph); owner.addAndMakeVisible (meter);
        owner.addChildComponent (dynamicsMeter);
        for (int i = 0; i < 8; ++i)
        {
            auto& b = bandButtons[(size_t) i];
            b.setButtonText (juce::String (i + 1)); b.setRadioGroupId (700); b.setClickingTogglesState (true);
            b.onClick = [this, i] { if (bandButtons[(size_t) i].getToggleState()) selectBand (i); };
            b.setTooltip ("SELECT BAND " + juce::String (i + 1)); owner.addAndMakeVisible (b);
        }
        for (auto* b : { &copy, &swap, &reset, &enabled, &analyzer, &freeze, &delta, &bypass, &dynamic }) owner.addAndMakeVisible (b);
        for (auto* b : { &enabled, &analyzer, &freeze, &delta, &bypass, &dynamic }) b->setClickingTogglesState (true);
        dynamic.setName ("DYNAMICS");
        dynamic.setTooltip ("STATIC / DYNAMIC | BELL AND SHELVES ONLY | INPUT RMS DETECTOR | CHANGE: HALF THE THRESHOLD EXCESS, LIMITED BY RANGE");
        dynamic.onClick = [this] { updateEnabled(); };
        copy.onClick = [this] { p.copyAtoB(); abIsB = false; swap.setButtonText ("A / B"); };
        swap.onClick = [this] { p.swapAB(); abIsB = ! abIsB; swap.setButtonText (abIsB ? "B / A" : "A / B"); };
        reset.onClick = [this] { p.resetAllBands(); };
        copy.setTooltip ("COPY CURRENT SETTINGS TO THE COMPARISON SLOT");
        swap.setTooltip ("SWAP CURRENT AND STORED COMPARISON SETTINGS");
        reset.setTooltip ("RESET ALL EIGHT EQ BANDS");
        enabled.setTooltip ("ENABLE THE SELECTED BAND");
        analyzer.setToggleState (true, juce::dontSendNotification);
        analyzer.onClick = [this] { graph.analyzing = analyzer.getToggleState(); p.setAnalyzerEnabled (graph.analyzing); };
        freeze.onClick = [this] { graph.frozen = freeze.getToggleState(); };
        analyzer.setTooltip ("LIVE LEFT CHANNEL PRE AND POST SPECTRUM | -96 TO 0 DBFS");
        freeze.setTooltip ("HOLD THE CURRENT SPECTRUM");
        delta.setTooltip ("LISTEN TO THE DIFFERENCE BETWEEN THE INPUT AND EQ OUTPUT");
        bypass.setTooltip ("BYPASS ALL PROCESSING");
        setupKnob (frequency); setupKnob (gain); setupKnob (q); setupKnob (output);
        for (auto* s : { &threshold, &range, &attack, &release })
        {
            setupKnob (*s); s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 23); s->setVisible (false);
        }
        threshold.setName ("THRESHOLD"); range.setName ("RANGE"); attack.setName ("ATTACK"); release.setName ("RELEASE");
        threshold.setTooltip ("BAND-LIMITED INPUT RMS THRESHOLD IN DBFS | CHANGE: HALF THE THRESHOLD EXCESS, LIMITED BY RANGE");
        range.setTooltip ("MAXIMUM DYNAMIC CHANGE | NEGATIVE: CUT | POSITIVE: BOOST | TOTAL BAND GAIN LIMITED TO +/-24 DB");
        attack.setTooltip ("GAIN CHANGE ATTACK TIME IN MILLISECONDS"); release.setTooltip ("GAIN RECOVERY TIME IN MILLISECONDS");
        threshold.setDoubleClickReturnValue (true, -24); range.setDoubleClickReturnValue (true, -6);
        attack.setDoubleClickReturnValue (true, 10); release.setDoubleClickReturnValue (true, 150);
        frequency.setName ("FREQUENCY"); gain.setName ("GAIN"); q.setName ("Q"); output.setName ("OUTPUT");
        frequency.textFromValueFunction = [] (double v) { return frequencyText (v); };
        frequency.valueFromTextFunction = [] (const juce::String& s) { auto v = s.getDoubleValue(); return s.containsIgnoreCase ("K") ? v * 1000.0 : v; };
        gain.textFromValueFunction = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 2) + " DB"; };
        q.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
        output.textFromValueFunction = gain.textFromValueFunction;
        frequency.setTooltip ("FREQUENCY | CLICK THE VALUE TO TYPE A PRECISE NUMBER");
        gain.setTooltip ("GAIN | DOUBLE CLICK THE KNOB TO RESET TO 0 DB");
        q.setTooltip ("Q | SHIFT DRAG OR SCROLL A GRAPH NODE TO CHANGE WIDTH");
        output.setTooltip ("OUTPUT TRIM | DOUBLE CLICK THE KNOB TO RESET TO 0 DB");
        gain.setDoubleClickReturnValue (true, 0); output.setDoubleClickReturnValue (true, 0); q.setDoubleClickReturnValue (true, 0.70710678);
        type.addItemList ({ "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" }, 1);
        channel.addItemList ({ "STEREO", "MID", "SIDE", "LEFT", "RIGHT" }, 1);
        slope.addItemList ({ "12 DB/OCT", "24 DB/OCT", "48 DB/OCT" }, 1);
        for (auto* c : { &type, &channel, &slope }) owner.addAndMakeVisible (c);
        type.setName ("TYPE"); channel.setName ("CHANNEL"); slope.setName ("SLOPE");
        type.setTooltip ("FILTER TYPE"); channel.setTooltip ("SELECT THE CHANNELS THIS BAND PROCESSES"); slope.setTooltip ("LOW CUT AND HIGH CUT SLOPE");
        type.onChange = [this] { updateEnabled(); };
        outputAttachment = std::make_unique<SliderAttachment> (p.apvts, "output", output);
        deltaAttachment = std::make_unique<ButtonAttachment> (p.apvts, "delta", delta);
        bypassAttachment = std::make_unique<ButtonAttachment> (p.apvts, "bypass", bypass);
        dynamicsMeter.configure (dynamic, threshold, range, attack, release);
        graph.attachOverlay (dynamicsMeter, rangeHandle);
        frequency.onValueChange = gain.onValueChange = range.onValueChange = [this] { graph.updateOverlayLayout(); graph.repaint(); };
        threshold.onValueChange = [this] { dynamicsMeter.repaint(); dynamicsMeter.detector.repaint(); };
        dynamicsMeter.detector.onEdit = [this] { dynamicsMeter.repaint(); dynamicsMeter.detector.repaint(); };
        graph.onSelection = [this] (int i) { selectBand (i); };
        selectBand (selected);
        p.setAnalyzerEnabled (true);
        startTimerHz (15);
    }
    ~Impl() override { stopTimer(); owner.setLookAndFeel (nullptr); }
    void setupKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 116, 23);
        s.setPopupDisplayEnabled (false, false, &owner);
        s.setScrollWheelEnabled (false);
        owner.addAndMakeVisible (s);
        // Slider text boxes may be created before inheriting the editor's look
        // and feel. Explicit colours also keep the editable value high contrast.
        s.setColour (juce::Slider::textBoxTextColourId, ink);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xfff4f2e9));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xffadb2a5));
        s.setColour (juce::Slider::textBoxHighlightColourId, sage.withAlpha (0.2f));
    }
    void selectBand (int i)
    {
        rangeHandle.finishGesture(); dynamicsMeter.detector.finishGesture();
        selected = juce::jlimit (0, 7, i); graph.selected = selected;
        frequencyAttachment.reset(); gainAttachment.reset(); qAttachment.reset(); typeAttachment.reset(); channelAttachment.reset(); slopeAttachment.reset(); enabledAttachment.reset();
        thresholdAttachment.reset(); rangeAttachment.reset(); attackAttachment.reset(); releaseAttachment.reset(); dynamicAttachment.reset();
        frequencyAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "freq"), frequency);
        gainAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "gain"), gain);
        qAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "q"), q);
        typeAttachment = std::make_unique<ComboAttachment> (p.apvts, bandId (selected, "type"), type);
        channelAttachment = std::make_unique<ComboAttachment> (p.apvts, bandId (selected, "channel"), channel);
        slopeAttachment = std::make_unique<ComboAttachment> (p.apvts, bandId (selected, "slope"), slope);
        enabledAttachment = std::make_unique<ButtonAttachment> (p.apvts, bandId (selected, "enabled"), enabled);
        thresholdAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "threshold"), threshold);
        rangeAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "range"), range);
        attackAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "attack"), attack);
        releaseAttachment = std::make_unique<SliderAttachment> (p.apvts, bandId (selected, "release"), release);
        dynamicAttachment = std::make_unique<ButtonAttachment> (p.apvts, bandId (selected, "dynamic"), dynamic);
        // Attachments supply generic formatting; restore clear unit-aware readouts.
        frequency.textFromValueFunction = [] (double v) { return frequencyText (v); };
        frequency.valueFromTextFunction = [] (const juce::String& s) { const auto v = s.getDoubleValue(); return s.containsIgnoreCase ("K") ? v * 1000.0 : v; };
        gain.textFromValueFunction = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 2) + " DB"; };
        q.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
        output.textFromValueFunction = gain.textFromValueFunction;
        frequency.updateText(); gain.updateText(); q.updateText(); output.updateText();
        threshold.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " DBFS"; };
        range.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " DB"; };
        attack.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " MS"; };
        release.textFromValueFunction = [] (double v) { return juce::String (v, 0) + " MS"; };
        for (auto* s : { &threshold, &range, &attack, &release })
        { s->valueFromTextFunction = [] (const juce::String& text) { return text.replaceCharacter (',', '.').getDoubleValue(); }; s->updateText(); }
        bandButtons[(size_t) selected].setToggleState (true, juce::dontSendNotification);
        updateEnabled(); owner.repaint();
    }
    void updateEnabled()
    {
        const auto t = type.getSelectedItemIndex();
        gain.setEnabled (t < 3); slope.setEnabled (t == 3 || t == 4);
        enabled.setButtonText (enabled.getToggleState() ? "ON" : "OFF");
        const bool supported = gill::supportsDynamics (t);
        dynamic.setEnabled (supported);
        dynamic.setButtonText (supported ? (dynamic.getToggleState() ? "DYNAMIC" : "STATIC") : "STATIC ONLY");
        const bool show = supported && dynamic.getToggleState();
        if (! show) rangeHandle.finishGesture();
        dynamicsMeter.refresh (selected, show); graph.showDynamics (show);
    }
    void timerCallback() override
    {
        meter.db = p.getOutputDb(); meter.repaint(); dynamicsMeter.repaint(); updateEnabled();
        for (int i = 0; i < 8; ++i) bandButtons[(size_t) i].setAlpha (value (p, i, "enabled") > 0.5f ? 1.0f : 0.45f);
    }
    void resized()
    {
        quality.setBounds(365,25,110,26);
        const int w = owner.getWidth(), h = owner.getHeight();
        if (w < 760 || h < 500) return;
        header = { 12, 8, w - 24, 60 };
        footer = { 22, h - 61, w - 44, 49 };
        controlPanel = { 22, footer.getY() - 148, w - 44, 138 };
        auto graphRegion = juce::Rectangle<int> (22, header.getBottom()+10, w-44, controlPanel.getY()-header.getBottom()-20);
        meter.setBounds (graphRegion.removeFromRight (34)); graph.setBounds (graphRegion);
        auto actions = juce::Rectangle<int> (w-260, 23, 238, 30);
        copy.setBounds(actions.removeFromLeft(76).reduced(2,0));swap.setBounds(actions.removeFromLeft(76).reduced(2,0));reset.setBounds(actions.reduced(2,0));
        auto panel = controlPanel.reduced(12,10);
        auto left = panel.removeFromLeft(190);
        auto bands = left.removeFromTop(28);for(auto& b:bandButtons)b.setBounds(bands.removeFromLeft(23).reduced(1,0));
        left.removeFromTop(11);enabled.setBounds(left.removeFromLeft(44).removeFromTop(31));left.removeFromLeft(7);type.setBounds(left.removeFromTop(31));
        auto right=panel.removeFromRight(118);channel.setBounds(right.withTrimmedTop(18).removeFromTop(29));slope.setBounds(right.withTrimmedTop(84).removeFromTop(29));
        panel.reduce(8,0);const int knobWidth=panel.getWidth()/3;
        frequency.setBounds(panel.removeFromLeft(knobWidth).withTrimmedTop(20));gain.setBounds(panel.removeFromLeft(knobWidth).withTrimmedTop(20));q.setBounds(panel.withTrimmedTop(20));
        auto foot=footer.reduced(0,8);analyzer.setBounds(foot.removeFromLeft(105).reduced(2,0));freeze.setBounds(foot.removeFromLeft(78).reduced(2,0));
        bypass.setBounds(foot.removeFromRight(89).reduced(2,0));foot.removeFromRight(7);delta.setBounds(foot.removeFromRight(99).reduced(2,0));foot.removeFromRight(7);output.setBounds(foot.removeFromRight(118).withTrimmedTop(-3).withTrimmedBottom(-3));
    }
    void labelAbove (juce::Graphics& g, juce::Component& c, const juce::String& text)
    {
        g.setFont (font (14, true)); g.setColour (muted);
        g.drawText (text, c.getBounds().withY (c.getY() - 20).withHeight (17), juce::Justification::centred);
    }
    void paint (juce::Graphics& g)
    {
        auto all = owner.getLocalBounds().toFloat();
        g.fillAll (juce::Colour (0xffbba583));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9bd92), 0, 0, juce::Colour (0xffb8956c), all.getRight(), all.getBottom(), false));
        g.fillRoundedRectangle (all.reduced (1), 9);
        if (wood.isValid())
        {
            // The user's original oak and GP emblem are embedded unchanged.
            for (int y = 8; y < header.getBottom(); y += 22)
                g.drawImage (wood, 10, y, owner.getWidth() - 20, 22, 52, 51, 1430, 28);
            g.drawImage (wood, 3, 7, 12, owner.getHeight() - 14, 34, 175, 16, 760);
            g.drawImage (wood, owner.getWidth() - 15, 7, 12, owner.getHeight() - 14, 1489, 175, 17, 760);
            g.drawImage (wood, 15, owner.getHeight() - 19, owner.getWidth() - 30, 16, 54, 945, 1430, 23);
        }
        g.setColour (juce::Colour (0xff745a3c).withAlpha (0.6f)); g.drawRoundedRectangle (all.reduced (1.5f), 8, 1.0f);
        auto face = all.reduced (15).withTop ((float) header.getBottom());
        gill::material::panel(g,face,juce::Colour(0xffeeeade),9);
        if (wood.isValid()) g.drawImage (wood, 28, 16, 61, 41, 95, 80, 105, 70);
        else { g.setFont (font (28, true)); g.setColour (juce::Colour (0xff60482d)); g.drawText ("GP", 28, 16, 61, 41, juce::Justification::centred); }
        g.setColour (juce::Colour (0xff80633e)); g.drawVerticalLine (105, 17, 57);
        g.setFont (font (26.0f)); g.setColour (juce::Colour (0xff332e23)); g.drawText ("GILLEQ", 123, 15, 160, 31, juce::Justification::centredLeft);
        g.setFont (font (12.0f, true)); g.setColour (juce::Colour (0xff67563d)); g.drawText ("GILLPRODUCTION", 124, 46, 240, 15, juce::Justification::centredLeft);
        gill::material::panel(g,controlPanel.toFloat(),juce::Colour(0xfff5f1e6),8,true);
        labelAbove (g, frequency, "FREQUENCY"); labelAbove (g, gain, "GAIN"); labelAbove (g, q, "Q");
        labelAbove (g, channel, "CHANNEL"); labelAbove (g, slope, "SLOPE");
        g.setFont (font (13, true)); g.setColour (muted);
        g.drawText ("BAND " + juce::String (selected + 1), type.getX(), type.getBottom() + 9, type.getWidth(), 17, juce::Justification::centredLeft);
        g.setColour (line); g.drawHorizontalLine (footer.getY() - 3, (float) footer.getX(), (float) footer.getRight());
        g.setColour (muted); g.setFont (font (13, true));
        g.drawText ("OUTPUT", output.getX() - 62, footer.getY() + 14, 62, 19, juce::Justification::centredRight);
    }
};

GilleqAudioProcessorEditor::GilleqAudioProcessorEditor (GilleqAudioProcessor& processor)
    : juce::AudioProcessorEditor (&processor)
{
    impl = std::make_unique<Impl> (*this, processor);
    setResizable (true, true);
    setResizeLimits (760, 500, 1720, 1160);
    setSize (860, 580);
}
GilleqAudioProcessorEditor::~GilleqAudioProcessorEditor() = default;
void GilleqAudioProcessorEditor::paint (juce::Graphics& g) { impl->paint (g); }
void GilleqAudioProcessorEditor::resized() { if (impl) impl->resized(); }
