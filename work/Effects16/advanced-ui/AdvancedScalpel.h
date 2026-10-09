#pragma once
#include "ScalpelUi.h"
namespace gill::advanced {
class ScalpelLook : public scalpel::Look {
public:
 juce::Slider::SliderLayout getSliderLayout(juce::Slider&s)override{juce::Slider::SliderLayout l;auto r=s.getLocalBounds();l.textBoxBounds=r.removeFromBottom(25);l.sliderBounds=s.isVertical()?r.reduced(10,30):r.reduced(12,9);return l;}
};
}
