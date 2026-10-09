#pragma once
#include "Processor.h"
namespace preview {
// A marker only represents dimensions that the selected formula consumes.
struct GraphModel {
 static bool frequency(int shape){return shape!=int(sp::Shape::Gain);}
 static bool gain(int shape){return shape<5||shape==int(sp::Shape::Gain);}
 static float effectiveFrequency(float hz,double rate){return juce::jlimit(20.f,float(std::max(8000.,rate)*.49),hz);}
 static float effectiveGain(int shape,float db){return shape==int(sp::Shape::Notch)?-std::abs(db):(gain(shape)?db:0.f);}
 static juce::Point<float> marker(int shape,float hz,float db,double rate){return {frequency(shape)?effectiveFrequency(hz,rate):1000.f,effectiveGain(shape,db)};}
};
}
