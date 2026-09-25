#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MixDSP.h"

class GillLinkGainParameter final:public juce::RangedAudioParameter {
public:
 GillLinkGainParameter():RangedAudioParameter(juce::ParameterID{"trackLevel",1},"TRACK LEVEL",juce::AudioProcessorParameterWithIDAttributes().withLabel("dB")){}
 float getValue()const override{return convertTo0to1(value.db());}
 void setValue(float v)override{if(std::isfinite(v))value.set(convertFrom0to1(juce::jlimit(0.f,1.f,v)));}
 float getDefaultValue()const override{return convertTo0to1(0);}
 juce::String getText(float v,int length)const override{return (juce::String(convertFrom0to1(v),2)+" dB").substring(0,length>0?length:128);}
 float getValueForText(const juce::String&t)const override{return convertTo0to1(t.getFloatValue());}
 const juce::NormalisableRange<float>&getNormalisableRange()const override{return range;}
 // Called on the message thread after a successful conditional value change.
 // This publishes the CURRENT canonical value and never mutates it again.
 void publish(){if(value.takeDirty())sendValueChangedMessageToListeners(getValue());}
 gill::mix07::GainValue value;
private:
 juce::NormalisableRange<float>range{-24,6,.01f};
};
