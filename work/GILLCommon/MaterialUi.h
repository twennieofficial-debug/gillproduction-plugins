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

inline const juce::Image& brushedMetal()
{
    static const juce::Image grain = [] {
        juce::Image result (juce::Image::ARGB, 512, 128, true, juce::SoftwareImageType());
        juce::Image::BitmapData pixels (result, juce::Image::BitmapData::writeOnly);
        unsigned seed = 0x53d128u;
        for (int y = 0; y < 128; ++y)
        {
            seed = seed * 1664525u + 1013904223u;
            const float line = float ((seed >> 18) & 255) / 255.f - .5f;
            for (int x = 0; x < 512; ++x)
            {
                const float brushed = line * .018f + std::sin (x * .071f + y * .91f) * .003f;
                pixels.setPixelColour (x, y, (brushed > 0 ? juce::Colours::white : juce::Colours::black)
                    .withAlpha (std::abs (brushed)));
            }
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
    g.setColour(juce::Colour(0xff10233a));
    g.fillRoundedRectangle(r.translated(0, 1.5f), radius);
    const bool metal = base.getPerceivedBrightness() > .48f;
    juce::ColourGradient face(base.interpolatedWith(juce::Colours::white, inset ? 0.04f : metal ? 0.52f : 0.10f),
                              r.getX(), r.getY(), base.darker(inset ? 0.10f : 0.16f),
                              r.getX() + r.getWidth() * 0.20f, r.getBottom(), false);
    face.addColour(0.045, base.brighter(metal ? .30f : .07f));
    face.addColour(0.25, base.brighter(metal ? .02f : .035f));
    face.addColour(0.58, metal ? base.darker(.09f) : base);
    face.addColour(0.84, base.brighter(metal ? .08f : .02f));
    face.addColour(0.975, base.darker(0.11f));
    g.setGradientFill(face);
    g.fillRoundedRectangle(r, radius);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip; clip.addRoundedRectangle(r.reduced(1), std::max(1.0f, radius - 1));
        g.reduceClipRegion(clip);
        g.setTiledImageFill(brushedMetal(), juce::roundToInt(r.getX()), juce::roundToInt(r.getY()), metal ? .85f : .45f);
        g.fillRect(r);
        // Broad softbox reflection, with no distracting hard shine across text.
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(metal ? .19f : .04f),
            r.getX() + r.getWidth() * 0.20f, r.getY() - r.getHeight() * 0.15f,
            juce::Colours::white.withAlpha(0.0f), r.getRight(), r.getBottom(), true));
        g.fillRect(r);
    }
    g.setColour(juce::Colour(0xff010713).withAlpha(0.9f));
    g.drawRoundedRectangle(r.reduced(0.45f), radius, edge);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(inset ? 0.25f : 0.92f),
                                          r.getX(), r.getY(), juce::Colours::black.withAlpha(0.15f),
                                          r.getX(), r.getBottom(), false));
    g.drawRoundedRectangle(r.reduced(1.6f), std::max(1.0f, radius - 1.4f), edge * 1.15f);
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
        juce::Colour pigment = juce::Colour(0xffcbd7e7);
        (void) base;
        if (rad > 0.84f)
        {
            // Machined nickel rim: ridged side wall and a polished top chamfer.
            const float directional = -.17f * px - .26f * py;
            light = .77f + directional + std::sin(theta * 160) * .017f;
            light += .43f * std::exp(-std::pow((rad - .891f) / .015f, 2.f));
            light -= .48f * std::exp(-std::pow((rad - .944f) / .020f, 2.f));
            light += .27f * std::exp(-std::pow((rad - .977f) / .010f, 2.f));
            light -= .42f * std::exp(-std::pow((rad - .998f) / .008f, 2.f));
            pigment = juce::Colour(0xffbbd8ef).interpolatedWith(juce::Colour(0xffffd1ed),std::max(0.f,px+py)*.14f);
        }
        else
        {
            // Flat, machine-turned aluminium: conical light bands, not a sphere.
            const unsigned grain = static_cast<unsigned>(x * 1973 + y * 9277) * 26699u;
            light = .81f + .22f * std::cos(theta * 2.f - .75f)
                         + .09f * std::cos(theta * 4.f + .32f) - .035f * py;
            pigment = juce::Colour(0xffdce5f0).interpolatedWith(juce::Colour(0xffe8c8e6),std::max(0.f,px+py)*.24f)
                .interpolatedWith(juce::Colour(0xff94cbea),std::max(0.f,-px-py)*.11f);
            light += (float((grain >> 15) & 255) / 255.f - .5f) * .008f;
            light += std::sin(rad * 2800.f + theta*.12f) * .013f;
            light -= .15f * std::exp(-std::pow((rad - .83f) / .008f,2.f));
            light += .10f * std::exp(-rad * rad / .0012f);
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
                 juce::Colour base = juce::Colour(0xffc5d2e2))
{
    if (r.isEmpty()) return;
    const float s = juce::jlimit(0.35f, 3.0f, r.getWidth() / 110.0f);
    for (int i = 6; i >= 1; --i)
    {
        g.setColour(juce::Colours::black.withAlpha(0.026f + (6-i)*0.012f));
        g.fillEllipse(r.expanded(i * 0.6f * s).translated(0.8f * s, (i * 0.55f + 2.0f) * s));
    }
    g.setColour(juce::Colour(0xff071727));
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
    g.setColour(juce::Colour(0xff112b46).withAlpha(0.52f));
    g.strokePath(rail, stroke(thickness + 1.2f));
    g.setColour(juce::Colour(0xff487491).withAlpha(0.72f));
    g.strokePath(rail, stroke(std::max(1.0f, thickness - 1.4f)), juce::AffineTransform::translation(0, 0.4f));
    if (value <= 0.0f) return;
    g.setColour(accent.withAlpha(.14f));g.strokePath(active,stroke(thickness+5.0f));
    g.setColour(accent.withAlpha(.19f));g.strokePath(active,stroke(thickness+2.4f));
    g.setColour(juce::Colour(0xff06101e));
    g.strokePath(active, stroke(thickness));
    juce::ColourGradient enamel(accent.brighter(0.42f), centre.x, centre.y - radius,
                                accent.darker(0.10f), centre.x, centre.y + radius, false);
    enamel.addColour(0.45, juce::Colour(0xffdbedff));
    enamel.addColour(0.83, juce::Colour(0xffd8b7ed));
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
            g.setColour(juce::Colour(0xff132d44).withAlpha(i%5==0 ? 0.43f : 0.2f));
            g.drawLine(centre.x+std::sin(a)*outer, centre.y-std::cos(a)*outer,
                       centre.x+std::sin(a)*(outer+length), centre.y-std::cos(a)*(outer+length),
                       juce::jlimit(0.7f, 1.4f, size/180.0f));
        }
    }
    disc(g, juce::Rectangle<float>(size*0.79f, size*0.79f).withCentre(centre));
    const float angle = startAngle + juce::jlimit(0.0f, 1.0f, value)*(endAngle-startAngle);
    const auto point = [&](float r) { return juce::Point<float>(centre.x+std::sin(angle)*r,
                                                              centre.y-std::cos(angle)*r); };
    const auto line = juce::Line<float>(point(radius*0.44f), point(radius*0.74f));
    const float width = juce::jlimit(2.3f, 7.0f, size*0.027f);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    auto edge = line;
    edge.applyTransform(juce::AffineTransform::translation(0, 0.8f));
    g.drawLine(edge, width+1.0f);
    g.setColour(juce::Colour(0xff081323));
    g.drawLine(line, width);
    g.setColour(juce::Colour(0xff203a51));
    g.drawLine(line, std::max(1.0f, width-1.6f));
}
}
