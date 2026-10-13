#pragma once
#include <fstream>
#include <functional>
inline juce::var runRenderCases(std::function<std::unique_ptr<juce::AudioPluginInstance>()> create,const juce::var& config,const juce::File& output){
 using namespace juce;Array<var> rows;
 for(auto c:*config.getArray()){
  var row(new DynamicObject);auto set=[&](const char*k,var v){row.getDynamicObject()->setProperty(k,v);};set("config",c);
  auto p=create();if(!p){set("error","instance failed");rows.add(row);continue;}
  const int main=int(c["main"]),key=int(c["key"]),block=int(c["block"]);const double rate=double(c["rate"]);auto layout=p->getBusesLayout();layout.inputBuses.set(0,AudioChannelSet::canonicalChannelSet(main));layout.outputBuses.set(0,AudioChannelSet::canonicalChannelSet(main));
  if(layout.inputBuses.size()>1)layout.inputBuses.set(1,AudioChannelSet::canonicalChannelSet(key));else if(key>0){set("error","missing physical aux");rows.add(row);continue;}
  const bool supported=p->setBusesLayout(layout);set("layoutApplied",supported);if(!supported){rows.add(row);continue;}
  Array<var> configured;
  for(auto setting:*c["settings"].getArray()) {const String wanted=setting["name"].toString();bool found=false;for(auto*param:p->getParameters())if(param->getName(256)==wanted){found=true;const float value=setting.hasProperty("text")?param->getValueForText(setting["text"].toString()):float(setting["value"]);param->setValueNotifyingHost(value);var r(new DynamicObject);r.getDynamicObject()->setProperty("name",wanted);r.getDynamicObject()->setProperty("value",param->getValue());r.getDynamicObject()->setProperty("text",param->getText(param->getValue(),256));configured.add(r);}if(!found){var r(new DynamicObject);r.getDynamicObject()->setProperty("missing",wanted);configured.add(r);}}
  set("configured",configured);p->setRateAndBufferSizeDetails(rate,block);p->prepareToPlay(rate,block);p->reset();set("latency",p->getLatencySamples());
  const int frames=int(rate*1.25),total=((frames+block-1)/block)*block;AudioBuffer<float> buffer(main+key,block);MidiBuffer midi;std::vector<float> samples; samples.reserve(total*main);double power=0,dryPower=0,auxDiff=0,peak=0,dryError=0;int count=0,nonfinite=0;
  auto mainSample=[&](int t,int ch){return float(double(c["mainAmp"])*(std::sin(MathConstants<double>::twoPi*997.*t/rate)+.4*std::sin(MathConstants<double>::twoPi*6001.*t/rate+ch*.1)));};
  const double keyFrequency1=c.hasProperty("keyFrequency1")?double(c["keyFrequency1"]):1603.;
  const double keyFrequency2=c.hasProperty("keyFrequency2")?double(c["keyFrequency2"]):7003.;
  auto keySample=[&](int t,int ch){if ((c["keySide"].toString()=="left"&&ch==1)||(c["keySide"].toString()=="right"&&ch==0))return 0.f;const double sign=c["anti"].operator bool()&&ch==1?-1.:1.;return float(sign*double(c["keyAmp"])*(std::sin(MathConstants<double>::twoPi*keyFrequency1*t/rate)+.5*std::sin(MathConstants<double>::twoPi*keyFrequency2*t/rate)));};
  for(int t=0;t<total;t+=block){buffer.clear();for(int ch=0;ch<main;++ch)for(int n=0;n<block;++n)buffer.setSample(ch,n,mainSample(t+n,ch));for(int ch=0;ch<key;++ch)for(int n=0;n<block;++n)buffer.setSample(main+ch,n,keySample(t+n,ch));midi.clear();p->processBlock(buffer,midi);
   for(int n=0;n<block;++n)for(int ch=0;ch<main;++ch){const float y=buffer.getSample(ch,n);samples.push_back(y);if(!std::isfinite(y))++nonfinite;if(t+n>total/2){const double dry=mainSample(t+n,ch);power+=double(y)*y;dryPower+=dry*dry;dryError=std::max(dryError,std::abs(double(y)-dry));peak=std::max(peak,std::abs(double(y)));++count;}}
   for(int ch=0;ch<key;++ch)for(int n=0;n<block;++n)auxDiff=std::max(auxDiff,std::abs(double(buffer.getSample(main+ch,n))-keySample(t+n,ch)));
  }
  const auto file=output.getSiblingFile(c["id"].toString()+".f32");std::ofstream stream(file.getFullPathName().toStdString(),std::ios::binary);stream.write(reinterpret_cast<const char*>(samples.data()),samples.size()*sizeof(float));stream.close();set("audio",file.getFullPathName());set("frames",total);set("rms",std::sqrt(power/count));set("dryRms",std::sqrt(dryPower/count));set("peak",peak);set("maxDryError",dryError);set("auxMaxMutation",auxDiff);set("nonfinite",nonfinite);
  MemoryBlock saved;p->getStateInformation(saved);Array<float> values;for(auto*param:p->getParameters())values.add(param->getValue());for(auto*param:p->getParameters())if(param->getName(256)!="Program")param->setValueNotifyingHost(.229f);p->setStateInformation(saved.getData(),int(saved.getSize()));double recallError=0;Array<var> recallErrors;for(int i=0;i<values.size();++i){auto*param=p->getParameters()[i];const double e=std::abs(double(values[i])-param->getValue());recallError=std::max(recallError,e);if(e>1e-5){var error(new DynamicObject);error.getDynamicObject()->setProperty("name",param->getName(256));error.getDynamicObject()->setProperty("expected",values[i]);error.getDynamicObject()->setProperty("actual",param->getValue());recallErrors.add(error);}}set("recallErrors",recallErrors);set("stateBytes",int(saved.getSize()));set("parameterRecallMaxError",recallError);p->releaseResources();rows.add(row);
 }
 return rows;
}
