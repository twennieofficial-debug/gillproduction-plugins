#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <array>

// Shared rendering only. Controls, gestures and audio parameters stay in their editors.
namespace gill::material
{
// Small, deterministic textures are generated once on the drawing thread.
// No texture generation, image lookup or allocation occurs in audio processing.
inline const juce::Image& enamelGrain()
{
    static const juce::Image grain = [] {
        juce::Image result(juce::Image::ARGB, 128, 128, true, juce::SoftwareImageType());
        juce::Image::BitmapData pixels(result, juce::Image::BitmapData::writeOnly);
        unsigned seed = 0x6a17c2u;
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
        {
            seed = seed * 1664525u + 1013904223u;
            pixels.setPixelColour(x, y, (seed & 0x10000u ? juce::Colours::white : juce::Colours::black)
                .withAlpha(0.025f + static_cast<float>((seed >> 24) & 31) * 0.001f));
        }
        return result;
    }();
    return grain;
}

inline void panel(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base,
                  float radius = 12.0f, bool inset = false)
{
    if (r.isEmpty()) return;
    const float edge = juce::jlimit(0.8f, 1.5f, r.getHeight() / 100.0f);
    for (int i = 5; i >= 1; --i)
    {
        g.setColour(juce::Colours::black.withAlpha(inset ? 0.03f : 0.036f));
        g.fillRoundedRectangle(r.expanded(i * 0.6f).translated(0, inset ? -0.6f : i * 0.8f), radius + i * 0.4f);
    }
    g.setColour(juce::Colour(0xff514b40));
    g.fillRoundedRectangle(r.translated(0, 1.5f), radius);
    juce::ColourGradient face(base.interpolatedWith(juce::Colours::white, inset ? 0.06f : 0.52f),
                              r.getX(), r.getY(), base.darker(inset ? 0.10f : 0.16f),
                              r.getX() + r.getWidth() * 0.20f, r.getBottom(), false);
    face.addColour(0.045, base.brighter(0.14f));
    face.addColour(0.25, base.brighter(0.04f));
    face.addColour(0.70, base);
    face.addColour(0.975, base.darker(0.11f));
    g.setGradientFill(face);
    g.fillRoundedRectangle(r, radius);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip; clip.addRoundedRectangle(r.reduced(1), std::max(1.0f, radius - 1));
        g.reduceClipRegion(clip);
        g.setTiledImageFill(enamelGrain(), juce::roundToInt(r.getX()), juce::roundToInt(r.getY()), 0.40f);
        g.fillRect(r);
        // Broad softbox reflection, with no distracting hard shine across text.
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.16f),
            r.getX() + r.getWidth() * 0.20f, r.getY() - r.getHeight() * 0.15f,
            juce::Colours::white.withAlpha(0.0f), r.getRight(), r.getBottom(), true));
        g.fillRect(r);
    }
    g.setColour(juce::Colour(0xff625440).withAlpha(0.46f));
    g.drawRoundedRectangle(r.reduced(0.45f), radius, edge);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(inset ? 0.25f : 0.92f),
                                          r.getX(), r.getY(), juce::Colours::black.withAlpha(0.15f),
                                          r.getX(), r.getBottom(), false));
    g.drawRoundedRectangle(r.reduced(1.6f), std::max(1.0f, radius - 1.4f), edge);
    g.setColour(juce::Colours::white.withAlpha(0.19f));
    g.drawRoundedRectangle(r.reduced(2.8f), std::max(1.0f, radius - 2.4f), 0.7f);
}

inline void fader(juce::Graphics& g, juce::Rectangle<float> r, bool vertical,
                  juce::Colour base, juce::Colour accent)
{
    panel(g, r, base, 4.0f);
    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;clip.addRoundedRectangle(r.reduced(1.2f),3.0f);g.reduceClipRegion(clip);
    juce::ColourGradient crown(base.brighter(0.2f),r.getX(),r.getY(),base.darker(0.34f),r.getX(),r.getBottom(),false);
    crown.addColour(0.15,base.brighter(0.16f));crown.addColour(0.42,base.darker(0.06f));
    crown.addColour(0.55,base);crown.addColour(0.82,base.darker(0.09f));
    g.setGradientFill(crown);g.fillRect(r.reduced(2));
    const auto mark=vertical?r.reduced(std::max(3.0f,r.getWidth()*.17f),r.getHeight()*.5f-1.0f)
                            :r.reduced(r.getWidth()*.5f-1.0f,std::max(3.0f,r.getHeight()*.17f));
    g.setColour(juce::Colours::white.withAlpha(0.8f));g.fillRoundedRectangle(mark.translated(0,1),0.7f);
    g.setColour(accent.darker(0.4f));g.fillRoundedRectangle(mark,0.7f);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawHorizontalLine(juce::roundToInt(r.getY()+2),r.getX()+3,r.getRight()-3);
}

inline juce::Image ivoryDisc(juce::Colour base)
{
    // A fixed bounded cache works for every editor size and Retina scaling.
    struct Entry { juce::uint32 colour = 0; juce::Image image; };
    // Software pixels only: native Direct2D images in thread_local storage can
    // wait for the GPU worker during VST DLL unloading under the loader lock.
    static std::array<Entry, 8> cache;
    static unsigned next = 0;
    static juce::CriticalSection cacheLock;
    const juce::ScopedLock guard(cacheLock);
    for (const auto& entry : cache)
        if (entry.colour == base.getARGB() && entry.image.isValid()) return entry.image;
    constexpr int size = 384;
    juce::Image result(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Image::BitmapData pixels(result, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
    {
        const float px = (x + 0.5f) * 2 / size - 1, py = (y + 0.5f) * 2 / size - 1;
        const float rad = std::sqrt(px * px + py * py);
        if (rad >= 1.0f) continue;
        const float theta = std::atan2(py, px);
        float light;
        juce::Colour pigment = base;
        if (rad > 0.88f)
        {
            // Machined nickel rim: ridged side wall and a polished top chamfer.
            const float ridges = std::sin(theta * 128) * 0.05f;
            const float directional = -0.38f * px - 0.50f * py;
            light = 0.64f + directional + ridges;
            light += 0.32f * std::exp(-std::pow((rad - 0.927f) / 0.018f, 2.0f));
            light -= 0.24f * std::exp(-std::pow((rad - 0.985f) / 0.009f, 2.0f));
            pigment = juce::Colour(0xffc1b7a3);
        }
        else
        {
            // Slight convex ceramic face and a rolled edge lit from upper left.
            const float slope = 0.19f + 0.82f * std::pow(rad / 0.88f, 12.0f);
            const float nx = px * slope, ny = py * slope;
            const float nz = std::sqrt(std::max(0.04f, 1 - nx * nx - ny * ny));
            const float diffuse = std::max(0.0f, -0.35f * nx - 0.49f * ny + 0.798f * nz);
            const float reflection = std::pow(std::max(0.0f, -0.18f * nx - 0.27f * ny + 0.946f * nz), 45.0f);
            const unsigned grain = static_cast<unsigned>(x * 1973 + y * 9277) * 26699u;
            light = 0.31f + 0.76f * diffuse + 0.11f * reflection;
            light += (static_cast<float>((grain >> 15) & 255) / 255 - 0.5f) * 0.012f;
            light += std::sin(rad * 2100) * 0.0035f;
            light -= 0.14f * std::exp(-std::pow((rad - 0.875f) / 0.010f, 2.0f));
        }
        const auto channel = [light](float c) { return juce::jlimit(0.0f, 1.0f, c * light); };
        pixels.setPixelColour(x, y, juce::Colour::fromFloatRGBA(channel(pigment.getFloatRed()),
            channel(pigment.getFloatGreen()), channel(pigment.getFloatBlue()),
            juce::jlimit(0.0f, 1.0f, (1.0f - rad) * size * 0.5f)));
    }
    auto& entry = cache[next++ % cache.size()]; entry.colour = base.getARGB(); entry.image = result;
    return result;
}

inline void disc(juce::Graphics& g, juce::Rectangle<float> r,
                 juce::Colour base = juce::Colour(0xfff4f0e5))
{
    if (r.isEmpty()) return;
    const float s = juce::jlimit(0.35f, 3.0f, r.getWidth() / 110.0f);
    for (int i = 6; i >= 1; --i)
    {
        g.setColour(juce::Colours::black.withAlpha(0.026f + (6-i)*0.012f));
        g.fillEllipse(r.expanded(i * 0.6f * s).translated(0.8f * s, (i * 0.55f + 2.0f) * s));
    }
    g.setColour(juce::Colour(0xff3a342b));
    g.fillEllipse(r.translated(0, 1.6f * s));
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(ivoryDisc(base), r);
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
