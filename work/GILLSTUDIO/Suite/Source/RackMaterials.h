#pragma once
#include "../../../GILLCommon/PrismUi.h"
#include "RackLibrary.h"
#include <cmath>
namespace studio::rackskin {
enum class Surface {
  Matte,
  Brushed,
  Chrome,
  Walnut,
  Oak,
  Marble,
  Porcelain,
  Enamel,
  Copper
};
struct Finish {
  Surface surface;
  juce::uint32 body, accent;
  int knob;
  const char *name;
};
inline Finish finish(int m) {
  using S = Surface;
  switch (m) {
  case 9:
    return {S::Walnut, 0xff795239, 0xffe6be76, 2, "OILED WALNUT"};
  case 10:
    return {S::Brushed, 0xff627d7b, 0xff9ee5d1, 0, "SAGE ALUMINIUM"};
  case 11:
    return {S::Enamel, 0xff3a607f, 0xffa2d9fa, 1, "BLUE ENAMEL"};
  case 12:
    return {S::Matte, 0xff303744, 0xff96d7ee, 0, "GRAPHITE"};
  case 13:
    return {S::Marble, 0xffdbd5c9, 0xffecc987, 2, "CALACATTA GOLD"};
  case 14:
    return {S::Oak, 0xffa47140, 0xfff4cd8d, 0, "SMOKED OAK"};
  case 15:
    return {S::Chrome, 0xffa6b4c1, 0xffb9e8fc, 1, "PLATINUM"};
  case 16:
    return {S::Enamel, 0xff496453, 0xffc6e5ad, 2, "OLIVE GLOSS"};
  case 17:
    return {S::Matte, 0xff24282b, 0xffedbb69, 0, "BLACK FET"};
  case 20:
    return {S::Walnut, 0xff553929, 0xfff3d3a3, 2, "DARK WALNUT"};
  case 23:
    return {S::Brushed, 0xff595f70, 0xffbbd6f6, 1, "SLATE TITANIUM"};
  case 25:
    return {S::Enamel, 0xff762f3d, 0xffffa6ad, 0, "OXBLOOD LACQUER"};
  case 26:
    return {S::Copper, 0xffa15b35, 0xffffc481, 0, "HAMMERED COPPER"};
  case 27:
    return {S::Marble, 0xff253b35, 0xffd4c88e, 2, "VERDE MARBLE"};
  case 24:
    return {S::Matte, 0xff454237, 0xffdfd79b, 0, "KHAKI CERAMIC"};
  case 4:
    return {S::Matte, 0xff363e44, 0xffb4d6e0, 1, "STORM POWDER"};
  case 28:
    return {S::Oak, 0xff70483a, 0xffffbb91, 0, "RED OAK"};
  case 5:
    return {S::Porcelain, 0xffcadfd8, 0xff86d5be, 0, "CELADON"};
  case 8:
    return {S::Copper, 0xff8c6254, 0xffeed1aa, 1, "ROSE BRONZE"};
  case 29:
    return {S::Enamel, 0xff594272, 0xffd3b5f5, 2, "VIOLET GLASS"};
  case 30:
    return {S::Chrome, 0xff778fa2, 0xff99d2ff, 1, "BLUE CHROME"};
  case 31:
    return {S::Brushed, 0xff7b7669, 0xffebd9ae, 0, "CHAMPAGNE"};
  case 32:
    return {S::Marble, 0xff22252b, 0xffead6a4, 2, "NOIR GOLD"};
  case 33:
    return {S::Chrome, 0xffbbc0ba, 0xfff4e7af, 1, "PALE CHROME"};
  case 34:
    return {S::Porcelain, 0xffd9bdb3, 0xffffd4c5, 0, "ROSE PORCELAIN"};
  case 35:
    return {S::Walnut, 0xff684437, 0xffedc090, 1, "MAHOGANY"};
  case 36:
    return {S::Brushed, 0xff475962, 0xffb2d7e5, 0, "GUNMETAL"};
  case 38:
    return {S::Marble, 0xffd7c9cf, 0xffe8bad9, 1, "ROSE QUARTZ"};
  case 40:
    return {S::Matte, 0xff4c5866, 0xffbfddf1, 0, "FROSTED BLUE"};
  case 41:
    return {S::Marble, 0xffd8dfe0, 0xffa5e6eb, 0, "GLACIER MARBLE"};
  default:
    return {S::Brushed, 0xff4a5668, 0xffa4daf4, 1, "LEGACY STEEL"};
  }
}
inline float hash(int x, int y, int seed) {
  unsigned n = unsigned(x) * 374761393u + unsigned(y) * 668265263u +
               unsigned(seed) * 1274126177u;
  n = (n ^ (n >> 13u)) * 1274126177u;
  return float((n ^ (n >> 16u)) & 65535u) / 65535.f;
}
inline float noise(float x, float y, int seed) {
  int ix = int(std::floor(x)), iy = int(std::floor(y));
  float a = x - ix, b = y - iy;
  a = a * a * (3 - 2 * a);
  b = b * b * (3 - 2 * b);
  const float p = hash(ix, iy, seed), q = hash(ix + 1, iy, seed),
              r = hash(ix, iy + 1, seed), s = hash(ix + 1, iy + 1, seed);
  return p + (q - p) * a + (r - p) * b + (p - q - r + s) * a * b;
}
inline float cloud(float x, float y, int seed) {
  return .56f * noise(x, y, seed) +
         .28f * noise(x * 2.07f, y * 2.07f, seed + 4) +
         .16f * noise(x * 4.17f, y * 4.17f, seed + 9);
}
// CPU material shader, cached on the message thread. Never runs in the audio
// callback.
inline juce::Image renderFace(int module) {
  constexpr int w = 220, h = 674;
  juce::Image image(juce::Image::RGB, w, h, false, juce::SoftwareImageType());
  const auto f = finish(module);
  const juce::Colour base(f.body), accent(f.accent);
  {
    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float u = float(x) / w, v = float(y) / h, n = hash(x, y, module),
              light = .91f + .12f * (1 - v), vein = 0;
        const float edge = std::min(u, 1 - u);
        switch (f.surface) {
        case Surface::Walnut:
        case Surface::Oak: {
          float warp = cloud(u * 3, v * 2, module),
                grain = std::sin(u * 120 + warp * 22 + std::sin(v * 11) * 1.4f);
          float fine = std::sin(u * 430 + warp * 49 + v * 13);
          light *= .77f + .22f * grain + .08f * fine +
                   .14f * cloud(u * 10, v * 2, module + 3);
          light += .1f * std::exp(-std::pow((u - .26f) / .2f, 2));
          break;
        }
        case Surface::Marble: {
          float t = u * 2.1f - v * 3.2f + cloud(u * 5, v * 6, module) * 1.8f;
          vein = std::pow(std::max(0.f, 1 - std::abs(std::sin(t * 4.5f)) * 12),
                          1.6f);
          light *= .91f + .13f * cloud(u * 12, v * 18, module + 7);
          if (base.getBrightness() > .6f)
            light *= 1 - .48f * vein;
          else
            light += .65f * vein;
          light += .08f * std::exp(-std::pow((u + v * .2f - .3f) / .13f, 2));
          break;
        }
        case Surface::Chrome: {
          light *= .48f + .3f * std::sin(u * 9) +
                   .84f * std::exp(-std::pow((u - .26f) / .095f, 2)) +
                   .42f * std::exp(-std::pow((u - .86f) / .035f, 2));
          light += .015f * (hash(0, y, module) - .5f);
          break;
        }
        case Surface::Brushed:
        case Surface::Copper:
          light *= .83f + .25f * std::exp(-std::pow((u - .33f) / .25f, 2));
          light += .08f * (hash(0, y, module) - .5f) + .025f * (n - .5f);
          break;
        case Surface::Enamel:
        case Surface::Porcelain:
          light *= .8f +
                   .38f * std::exp(-std::pow((u - .18f - v * .05f) / .09f, 2)) +
                   .15f * std::exp(-std::pow((v - .15f) / .028f, 2));
          break;
        default:
          light *= .86f + .04f * (n - .5f) + .06f * std::sin(u * 3.14f);
          break;
        }
        light *= .73f + .27f * std::min(1.f, edge * 30);
        auto c = juce::Colour::fromFloatRGBA(
            std::clamp(base.getFloatRed() * light, 0.f, 1.f),
            std::clamp(base.getFloatGreen() * light, 0.f, 1.f),
            std::clamp(base.getFloatBlue() * light, 0.f, 1.f), 1);
        if (f.surface == Surface::Marble && vein > .35f)
          c = c.interpolatedWith(accent, vein * .34f);
        pixels.setPixelColour(x, y, c);
      }
  }
  juce::Graphics g(image);
  g.addTransform(juce::AffineTransform::scale(2.f));
  auto r = juce::Rectangle<float>(0, 0, 110, 337);
  g.setColour(juce::Colours::black.withAlpha(.8f));
  g.drawRoundedRectangle(r.reduced(1), 4, 2);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe4e6df), 0, 0,
                                         juce::Colour(0xff1b2229), 110, 337,
                                         false));
  g.drawRoundedRectangle(r.reduced(2.3f), 3, .8f);
  // Recessed engraving plate and raised machined rails, separate from the
  // textured face.
  gill::material::panel(g, {5, 3, 100, 26}, juce::Colour(0xff202930), 3);
  gill::material::panel(g, {5, 317, 100, 17}, juce::Colour(0xff171d23), 2);
  g.setColour(accent.withAlpha(.8f));
  g.setFont(gill::prism::font(7.1f, true));
  // The surface remains visually distinct without displaying internal theme names.
  for (auto p : std::array<juce::Point<float>, 4>{
           {{5, 5}, {105, 5}, {5, 332}, {105, 332}}}) {
    g.setColour(juce::Colours::black.withAlpha(.8f));
    g.fillEllipse(p.x - 2.2f, p.y - 1.2f, 4.5f, 4.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white, p.x - 2,
                                           p.y - 2, juce::Colour(0xff3c4650),
                                           p.x + 2, p.y + 2, false));
    g.fillEllipse(p.x - 1.8f, p.y - 1.8f, 3.6f, 3.6f);
    g.setColour(juce::Colour(0xff11171e));
    g.drawLine(p.x - 1, p.y + .7f, p.x + 1, p.y - .7f, .65f);
  }
  return image;
}
inline const juce::Image &face(int module) {
  static std::array<juce::Image, rackModelCount> images;
  auto &image = images[size_t(std::clamp(module, 0, rackModelCount - 1))];
  if (!image.isValid())
    image = renderFace(module);
  return image;
}
inline void drawFace(juce::Graphics &g, int module, juce::Rectangle<float> r) {
  juce::Graphics::ScopedSaveState save(g);
  g.setOpacity(1.f);
  g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
  g.drawImage(face(module), r);
}
inline const juce::Image &knobFace(int type) {
  static std::array<juce::Image, 3> cache;
  auto &image = cache[size_t(std::clamp(type, 0, 2))];
  if (image.isValid())
    return image;
  constexpr int size = 240;
  image = juce::Image(juce::Image::ARGB, size, size, true,
                      juce::SoftwareImageType());
  juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      float px = (x + .5f) * 2 / size - 1, py = (y + .5f) * 2 / size - 1,
            rad = std::sqrt(px * px + py * py);
      if (rad >= 1)
        continue;
      float theta = std::atan2(py, px), light;
      auto pigment = juce::Colour(type == 0   ? 0xff39434a
                                  : type == 1 ? 0xffcfdae2
                                              : 0xffecdfc5);
      if (rad > .83f) {
        pigment = juce::Colour(type == 2 ? 0xffb59e71 : 0xff9fafbc);
        light = .68f - .19f * px - .25f * py +
                .38f * std::exp(-std::pow((rad - .86f) / .016f, 2)) -
                .42f * std::exp(-std::pow((rad - .92f) / .018f, 2)) +
                .3f * std::exp(-std::pow((rad - .974f) / .009f, 2));
        light += std::sin(theta * 90) * .06f;
      } else if (type == 1) {
        light = .76f + .23f * std::cos(theta * 2 - .7f) +
                .08f * std::cos(theta * 4) - .04f * py +
                .013f * std::sin(rad * 1600);
      } else {
        light = .89f - .17f * px - .22f * py - .12f * rad * rad;
        light +=
            (type == 2 ? .23f : .09f) *
            std::exp(-((px + .35f) * (px + .35f) + (py + .4f) * (py + .4f)) /
                     .08f);
        light += (hash(x, y, type) - .5f) * (type == 0 ? .04f : .006f);
        light -= .1f * std::exp(-std::pow((rad - .8f) / .018f, 2));
      }
      pixels.setPixelColour(
          x, y,
          juce::Colour::fromFloatRGBA(
              std::clamp(pigment.getFloatRed() * light, 0.f, 1.f),
              std::clamp(pigment.getFloatGreen() * light, 0.f, 1.f),
              std::clamp(pigment.getFloatBlue() * light, 0.f, 1.f),
              std::clamp((1 - rad) * size * .5f, 0.f, 1.f)));
    }
  return image;
}
class Look : public gill::prism::Look {
public:
  void drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h,
                        float value, float start, float end,
                        juce::Slider &s) override {
    const int module = int(s.getProperties().getWithDefault("rackModule", 0));
    if (module <= 0) {
      gill::prism::Look::drawRotarySlider(g, x, y, w, h, value, start, end, s);
      return;
    }
    const auto f = finish(module);
    const juce::Colour accent(f.accent);
    float size = float(std::min(w, h)) * .83f, r = size * .5f, cx = x + w * .5f,
          cy = y + h * .5f;
    juce::Rectangle<float> disc(cx - r, cy - r, size, size);
    juce::Path track;
    track.addCentredArc(cx, cy, r + 2, r + 2, 0, start, end, true);
    g.setColour(juce::Colours::black.withAlpha(.75f));
    g.strokePath(track, juce::PathStrokeType(3));
    juce::Path active;
    active.addCentredArc(cx, cy, r + 2, r + 2, 0, start,
                         start + value * (end - start), true);
    g.setColour(accent.withAlpha(.25f));
    g.strokePath(active, juce::PathStrokeType(4));
    g.setColour(accent);
    g.strokePath(active, juce::PathStrokeType(1.1f));
    g.setColour(juce::Colours::black.withAlpha(.55f));
    g.fillEllipse(disc.translated(1, 2.8f).expanded(1));
    g.setOpacity(1.f);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(knobFace(f.knob), disc);
    float a = start + value * (end - start);
    auto p1 = juce::Point<float>(cx + std::sin(a) * r * .36f,
                                 cy - std::cos(a) * r * .36f),
         p2 = juce::Point<float>(cx + std::sin(a) * r * .78f,
                                 cy - std::cos(a) * r * .78f);
    g.setColour(f.knob == 0 ? accent : juce::Colour(0xff182734));
    g.drawLine({p1, p2}, std::max(1.6f, size * .043f));
    if (s.isMouseOverOrDragging()) {
      g.setColour(accent.withAlpha(.15f));
      g.drawEllipse(disc.expanded(1), 1);
    }
  }
};
} // namespace studio::rackskin
