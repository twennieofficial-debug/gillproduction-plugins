#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../Source/Spectral/Spectral.h"
#include <mutex>
namespace preview {
namespace sp=gillstudio::spectral;
class Processor final : public juce::AudioProcessor, private juce::Timer {
public:
 Processor(); ~Processor() override {stopTimer();}
 juce::AudioProcessorValueTreeState state;
 float value(const juce::String& id) const {return state.getRawParameterValue(id)->load();}
 void set(const juce::String& id,float v){auto*p=state.getParameter(id);p->setValueNotifyingHost(p->convertTo0to1(v));}
 static juce::AudioProcessorValueTreeState::ParameterLayout layout();
 void prepareToPlay(double,int) override;
 void releaseResources() override {}
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
 void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
 bool isBusesLayoutSupported(const BusesLayout& l) const override;
 const juce::String getName() const override {return "GILLSPECTRA PREVIEW";}
 bool acceptsMidi()const override{return false;} bool producesMidi()const override{return false;} double getTailLengthSeconds()const override{return getLatencySamples()/std::max(8000.,getSampleRate());}
 int getNumPrograms() override{return 9;} int getCurrentProgram()override{return currentProgram.load();} void setCurrentProgram(int)override; const juce::String getProgramName(int)override; void changeProgramName(int,const juce::String&)override{}
 bool hasEditor()const override{return true;} juce::AudioProcessorEditor*createEditor()override;
 void getStateInformation(juce::MemoryBlock&)override; void setStateInformation(const void*,int)override;
 double displaySampleRate()const{return displayRate.load();}
 int effectiveMode()const noexcept{return appliedMode.load(std::memory_order_acquire);}
 bool modePending()const noexcept{return int(controls[0]->load())!=effectiveMode();}
 bool hasTransportInformation()const noexcept{return transportKnown.load(std::memory_order_acquire);}
 std::atomic<bool> clearRequested{false},profilePresent{false}; std::atomic<float> reduction{0};
private:
 std::atomic<double> displayRate{48000};
 std::atomic<int> currentProgram{0};
 struct Slot { std::atomic<int> ownership{0}; sp::Profile profile; std::atomic<uint64_t> serial{0};uint64_t generation=0; Slot(){for(auto& d:profile.density)d.resize(1025);} };
 std::array<Slot,3> snapshots; std::array<Slot,2> incoming;
 std::array<std::atomic<float>*,25> controls{}; std::array<std::array<std::atomic<float>*,6>,12> nodes{},detectorNodes{}; sp::Processor engine; std::mutex stateMutex; uint64_t serial=0; int fade=0,lastMode=-1,lastRouting=-1;float bypassMix=0;std::atomic<uint64_t> stateGeneration{0};uint64_t audioGeneration=0; bool prepared=false;
 std::array<std::array<float,2048>,2> bypassRing{};size_t bypassPos=0;void process(juce::AudioBuffer<float>&,bool);
 std::atomic<int> appliedMode{0};std::atomic<bool> transportKnown{false},transportPlaying{true};
 void timerCallback()override;sp::Parameters parameters()const;void publish();
};
}



