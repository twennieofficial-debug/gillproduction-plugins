#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>

// Shared rendering only. Controls, gestures and audio parameters stay in their editors.
namespace gill::material
{
inline void panel(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base,
                  float radius = 12.0f, bool inset = false)
{
    if (r.isEmpty()) return;
    const float edge = juce::jlimit(0.7f, 1.4f, r.getHeight() / 100.0f);
    g.setColour(juce::Colours::black.withAlpha(inset ? 0.18f : 0.07f));
    g.fillRoundedRectangle(r.translated(0, inset ? -1.0f : 3.0f).expanded(inset ? 0.0f : 1.0f), radius);
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.fillRoundedRectangle(r.translated(0, 1.0f), radius);
    juce::ColourGradient face(base.interpolatedWith(juce::Colours::white, inset ? 0.05f : 0.46f),
                              r.getX(), r.getY(), base.darker(inset ? 0.08f : 0.055f),
                              r.getX() + r.getWidth() * 0.22f, r.getBottom(), false);
    face.addColour(0.28, base.interpolatedWith(juce::Colours::white, 0.14f));
    face.addColour(0.68, base);
    g.setGradientFill(face);
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colour(0xff75644c).withAlpha(0.23f));
    g.drawRoundedRectangle(r.reduced(0.45f), radius, edge);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(inset ? 0.3f : 0.85f),
                                          r.getX(), r.getY(), juce::Colours::white.withAlpha(0.12f),
                                          r.getX(), r.getBottom(), false));
    g.drawRoundedRectangle(r.reduced(1.5f), std::max(1.0f, radius - 1.0f), edge);
}

inline void disc(juce::Graphics& g, juce::Rectangle<float> r,
                 juce::Colour base = juce::Colour(0xfff4f0e5))
{
    if (r.isEmpty()) return;
    const float s = juce::jlimit(0.35f, 3.0f, r.getWidth() / 110.0f);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour(juce::Colours::black.withAlpha(0.035f + (3-i)*0.022f));
        g.fillEllipse(r.expanded(i * 1.1f * s).translated(0, (i + 1.0f) * s));
    }
    g.setColour(juce::Colour(0xff756e5e));
    g.fillEllipse(r);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white, r.getX(), r.getY(),
                                          juce::Colour(0xffb2aa97), r.getRight(), r.getBottom(), false));
    g.fillEllipse(r.reduced(0.7f * s));
    const auto cap = r.reduced(3.0f * s);
    juce::ColourGradient face(juce::Colours::white, cap.getX() + cap.getWidth() * 0.25f,
                              cap.getY(), base.darker(0.095f), cap.getRight(), cap.getBottom(), false);
    face.addColour(0.32, base.brighter(0.08f));
    face.addColour(0.70, base);
    g.setGradientFill(face);
    g.fillEllipse(cap);
    g.setColour(juce::Colours::white.withAlpha(0.82f));
    g.drawEllipse(cap.reduced(0.6f * s), 0.8f * s);
    g.setColour(juce::Colour(0xff807562).withAlpha(0.25f));
    g.drawEllipse(r.reduced(1.1f * s), 0.7f * s);
}

inline void grooveArc(juce::Graphics& g, juce::Point<float> centre, float radius,
                      float start, float end, float value, juce::Colour accent,
                      float thickness = 4.0f)
{
    const float angle = start + juce::jlimit(0.0f, 1.0f, value) * (end - start);
    juce::Path rail, active;
    rail.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
    active.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
    const auto stroke = [](float w) { return juce::PathStrokeType(w, juce::PathStrokeType::curved,
                                                                juce::PathStrokeType::rounded); };
    g.setColour(juce::Colours::white.withAlpha(0.58f));
    g.strokePath(rail, stroke(thickness + 2.0f), juce::AffineTransform::translation(0, 0.9f));
    g.setColour(juce::Colour(0xff635c4d).withAlpha(0.52f));
    g.strokePath(rail, stroke(thickness + 1.2f));
    g.setColour(juce::Colour(0xffd9d0bd).withAlpha(0.72f));
    g.strokePath(rail, stroke(std::max(1.0f, thickness - 1.4f)), juce::AffineTransform::translation(0, 0.4f));
    if (value <= 0.0f) return;
    g.setColour(accent.darker(0.48f));
    g.strokePath(active, stroke(thickness));
    juce::ColourGradient enamel(accent.brighter(0.42f), centre.x, centre.y - radius,
                                accent.darker(0.10f), centre.x, centre.y + radius, false);
    enamel.addColour(0.45, accent.brighter(0.12f));
    g.setGradientFill(enamel);
    g.strokePath(active, stroke(std::max(1.0f, thickness - 1.3f)));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.strokePath(active, stroke(std::max(0.65f, thickness * 0.17f)),
                 juce::AffineTransform::translation(0, -0.45f));
}

inline void rotary(juce::Graphics& g, juce::Rectangle<float> bounds, float value,
                   float startAngle, float endAngle, juce::Colour accent)
{
    const float size = std::min(bounds.getWidth(), bounds.getHeight());
    if (size <= 8.0f) return;
    const auto centre = bounds.getCentre();
    const float radius = size * 0.43f;
    const float thickness = juce::jlimit(2.5f, 9.0f, size * 0.039f);
    grooveArc(g, centre, radius, startAngle, endAngle, value, accent, thickness);
    if (size > 76.0f)
    {
        for (int i=0; i<=20; ++i)
        {
            const float a = startAngle + (endAngle-startAngle)*i/20.0f;
            const float outer = radius + thickness * 0.70f;
            const float length = i%5==0 ? size*0.022f : size*0.012f;
            g.setColour(juce::Colour(0xff493f31).withAlpha(i%5==0 ? 0.43f : 0.2f));
            g.drawLine(centre.x+std::sin(a)*outer, centre.y-std::cos(a)*outer,
                       centre.x+std::sin(a)*(outer+length), centre.y-std::cos(a)*(outer+length),
                       juce::jlimit(0.7f, 1.4f, size/180.0f));
        }
    }
    disc(g, juce::Rectangle<float>(size*0.73f, size*0.73f).withCentre(centre));
    const float angle = startAngle + juce::jlimit(0.0f, 1.0f, value)*(endAngle-startAngle);
    const auto point = [&](float r) { return juce::Point<float>(centre.x+std::sin(angle)*r,
                                                              centre.y-std::cos(angle)*r); };
    const auto line = juce::Line<float>(point(radius*0.44f), point(radius*0.74f));
    const float width = juce::jlimit(2.3f, 7.0f, size*0.027f);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    auto edge = line;
    edge.applyTransform(juce::AffineTransform::translation(0, 0.8f));
    g.drawLine(edge, width+1.0f);
    g.setColour(accent.darker(0.44f));
    g.drawLine(line, width);
    g.setColour(accent.brighter(0.12f));
    g.drawLine(line, std::max(1.0f, width-1.6f));
}
}
