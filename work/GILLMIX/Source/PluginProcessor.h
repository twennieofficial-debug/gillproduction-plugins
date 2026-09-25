#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../GILLCommon/QualityBus.h"
#include "GainParameter.h"
#include "MixProtocol.h"
#include <mutex>
#include <vector>

enum class GillMixKind{master,link};
class GillMixProcessor final:public juce::AudioProcessor,private juce::Timer {
public:
 struct TrackView{gill::mix07::Row row{};gill::mix07::Role role=gill::mix07::Role::automatic;float confidence=0,proposedDb=0;bool selected=false,locked=false,confirmed=false,hasProposal=false;};
 explicit GillMixProcessor(GillMixKind);
 ~GillMixProcessor()override;
 const juce::String getName()const override{return kind==GillMixKind::master?"GILLMIX":"GILLLINK";}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor*createEditor()override;
 void prepareToPlay(double,int)override;void releaseResources()override{}
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&)override;
 void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 void processBlockBypassed(juce::AudioBuffer<double>&,juce::MidiBuffer&)override;
 bool supportsDoublePrecisionProcessing()const override{return true;}double getTailLengthSeconds()const override{return 0;}
 int getNumPrograms()override{return kind==GillMixKind::master?5:1;}int getCurrentProgram()override{return program.load()&7;}void setCurrentProgram(int)override;
 const juce::String getProgramName(int)override;void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 void updateTrackProperties(const TrackProperties&)override;
 void service();
 void setLocalName(const juce::String&);void setLocalRole(gill::mix07::Role);void setLocalLock(bool);void newIdentity();void disconnect();
 gill::mix07::LocalState localSnapshot()const;
 std::vector<TrackView> tracks;
 juce::String status="NOT CONNECTED";
 void chooseTrack(gill::mix07::Id,bool);void chooseRole(gill::mix07::Id,gill::mix07::Role);void lockTrack(gill::mix07::Id,bool);
 void connectSelected();void startLearn();void stopLearn();void apply();void undo();void manualGain(gill::mix07::Id,float);
 bool isLearning()const noexcept{return learning;}bool canApply()const noexcept{return proposalCount>0&&!learning&&txPhase==0;}bool canUndo()const noexcept{return undoCount>0&&txPhase==0;}
 double learnedSeconds()const noexcept{return learnedTime;}
 float meterIn()const noexcept{return audio.inputPeak.load();}float meterOut()const noexcept{return audio.outputPeak.load();}
 GillLinkGainParameter*gainParameter=nullptr;
 const GillMixKind kind;juce::AudioProcessorValueTreeState apvts;gill::QualityClient quality;
 gill::mix07::MixBus bus;
private:
 struct Pick{gill::mix07::Id id{};uint32_t role=0;bool manual=false,selected=false,locked=false;};
 struct Analysis{gill::mix07::Id runtime{};uint64_t cursor=0,baseGain=0,baseMetadata=0;gill::mix07::LearnedTrack learned{};};
 static juce::AudioProcessorValueTreeState::ParameterLayout layout(GillMixKind);
 template<class T>void process(juce::AudioBuffer<T>&,bool);
 void timerCallback()override{service();}
 Pick*pick(gill::mix07::Id,bool create=false);void refreshTracks();void finishProposal();void beginTransaction(const gill::mix07::Change*,int,bool undoOperation=false);void serviceTransaction();
 void handleCommand(const gill::mix07::Command&);
 float parameter(const char*)const noexcept;void setParameter(const char*,float);void applyProgram(int);void consumeProgram();
 mutable std::mutex modelMutex;gill::mix07::LocalState local{};
 std::atomic<bool>restoring{false};std::atomic<int>program{0};
 std::array<Pick,64>picks{},pendingPicks{};std::array<Analysis,64>analysis{};int analysisCount=0;
 std::atomic<float>*bypassRaw=nullptr;
 std::array<gill::mix07::Change,64>proposal{},transaction{},undoRows{};int proposalCount=0,transactionCount=0,undoCount=0,txPhase=0;
 bool learning=false,txIsUndo=false;uint64_t learnId=0,learnStarted=0,transactionId=0,txStarted=0,lastService=0;double learnedTime=0;
 std::array<float,4>proposalSettings{};
 gill::mix07::Ack cachedAck{};std::atomic<uint64_t>restoreSerial{0},seenRestore{0};
 gill::mix07::LinkAudio audio;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillMixProcessor)
};
