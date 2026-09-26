#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "AssistDSP.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <memory>
#include <set>

namespace gill::assist {
class Engine final:private juce::Thread{
public:
    enum State{Idle,Armed,Capturing,Analysing,Ready,Error};
    explicit Engine(std::function<Settings()>);
    ~Engine()override;
    void prepare(double rate,int channels);
    void process(juce::AudioBuffer<float>&,bool playing,bool hasPosition,std::int64_t position,bool bypass)noexcept;
    void arm();void stop();void importFile(const juce::File&,double timelineStart);
    void restore(const juce::String& token,double timelineStart,const std::vector<Edit>&);void clearTransfer();
    struct TransferState{juce::String token;double start=0;std::vector<Edit>edits;};
    TransferState transferState()const;
    void edit(Edit);void clearEdits();void undo();void redo();void swapAB();
    bool canUndo()const;bool canRedo()const;
    Plan plan()const;std::vector<Edit> edits()const;
    juce::String status()const;juce::String token()const;juce::File exportFile()const;juce::File claimExportFile();
    static juce::File cacheRoot();static bool validToken(const juce::String&);
    std::atomic<int>state{Idle};std::atomic<double>capturedSeconds{0},rateView{48000},positionView{0};
    std::atomic<float>currentGain{0};std::atomic<bool>timelineMatched{false};
    std::atomic<std::uint64_t>completedRevision{0};
private:
    void run()override;void finishCapture();void loadSource(const juce::File&,bool copy,double start);void build();
    void setStatus(juce::String);void history();void publish(const Plan&);
    static constexpr std::uint32_t capacity=1u<<20,mask=capacity-1;
    struct Frame{float l=0,r=0;};std::unique_ptr<Frame[]>ring;
    std::atomic<std::uint64_t>writeIndex{0},readIndex{0},captureFrames{0};
    std::atomic<bool>startRequested{false},stopRequested{false},captureFinished{false},captureFailed{false},resetRequested{false};
    std::atomic<unsigned>audioUsers{0};
    std::atomic<int>channelView{2};std::atomic<double>startSeconds{0};
    double captureRate=48000;int captureChannels=2;std::int64_t expectedPosition=0;
    struct Slot{std::array<float,maxPoints>values{};int count=0;double duration=0,start=0;std::atomic<unsigned>users{0};};
    std::array<Slot,3>slots;std::atomic<int>published{-1};
    mutable std::mutex mutex;juce::String message="LEARN, dann den Song abspielen (max. 5:00).",cacheToken;
    juce::File source,rendered,pendingFile;bool pendingCopy=false,pendingRestore=false;double pendingStart=0;
    juce::String resetToken;juce::File resetImport;double resetStart=0;std::vector<Edit>resetEdits;
    std::vector<juce::File>unclaimedRenders;std::set<juce::String>claimedRenders;
    Plan visual;std::vector<Feature>features;std::vector<Edit>manual,abManual;
    std::vector<std::vector<Edit>>undoStack,redoStack;std::atomic<std::uint64_t>editRevision{0};
    std::uint64_t builtEditRevision=0,renderRevision=0;Settings lastSettings{};
    std::function<Settings()>readSettings;std::unique_ptr<juce::AudioFormatWriter>writer;
    juce::AudioBuffer<float>scratch{2,4096};double sourceDuration=0,sourceStart=0,sourceRate=48000;int sourceChannels=2;
};
}
