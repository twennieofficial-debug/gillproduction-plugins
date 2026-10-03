#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "NoteDSP.h"
#include <atomic>
#include <mutex>
#include <memory>

namespace gill::note {
class Engine final:private juce::Thread {
public:
 enum State{Idle,Armed,Capturing,Analysing,Ready,Rendering,Error};
 Engine();~Engine()override;
 void prepare(double,int);void process(juce::AudioBuffer<float>&,bool playing,bool positionValid,std::int64_t position,bool live,bool bypass)noexcept;
 void arm();void stop();void importFile(const juce::File&);void restore(const juce::String&,double,const std::vector<Note>&);void clear();
 void edit(Note);void snapNotes(int key,int scale,bool all,int selected);void undo();void redo();void render();void audition(double start=0);void stopAudition();void setFormants(bool enabled){if(preserveFormants.exchange(enabled)!=enabled){++revision;previewing=false;}}
 struct Saved {juce::String token;double start=0;std::vector<Note>notes;};Saved saved()const;
 Plan plan()const;juce::String status()const;juce::File exportFile()const;bool canUndo()const;bool canRedo()const;
 static juce::File cacheRoot();static bool validToken(const juce::String&);
 std::atomic<int>state{Idle};std::atomic<double>rateView{48000},capturedSeconds{0},previewSeconds{0};std::atomic<bool>previewing{false};
 std::atomic<std::uint64_t>completedRevision{0};std::atomic<bool>preserveFormants{true};
private:
 struct Frame{float l=0,r=0;};static constexpr std::uint64_t capacity=1u<<20,previewCapacity=1u<<17;
 std::unique_ptr<Frame[]>captureRing,previewRing;
 std::atomic<std::uint64_t>captureWrite{0},captureRead{0},captureFrames{0},previewWrite{0},previewRead{0};
 std::atomic<unsigned>audioUsers{0};std::atomic<bool>resetRequested{false},stopRequested{false},captureFinished{false},captureFailed{false},renderRequested{false};
 std::atomic<int>previewCommand{0},channelsView{2};std::atomic<double>previewStart{0},startSeconds{0};std::atomic<bool>previewEnd{false};
 double captureRate=48000;int captureChannels=2;std::int64_t expectedPosition=0;
 mutable std::mutex mutex;juce::String message="LEARN oder WAV importieren. Maximal 5:00.",token;
 Plan visual;juce::File source,result;
 std::vector<std::vector<Note>>undoStack,redoStack;
 juce::String resetToken;juce::File resetFile;double resetStart=0;std::vector<Note>resetNotes;bool resetArm=false;
 std::atomic<std::uint64_t>revision{0};std::uint64_t renderedRevision=0;
 std::unique_ptr<juce::AudioFormatWriter>writer;std::unique_ptr<juce::AudioFormatReader>previewReader;
 juce::AudioBuffer<float>scratch{2,4096},previewScratch;double sourceRate=48000;int sourceChannels=2;juce::int64 sourceFrames=0;double previewCursor=0,previewOutputRate=48000;
 void run()override;void reset();void finishCapture();void analyseFile(const juce::File&,bool copy,double,const std::vector<Note>&);void renderFile();void fillPreview();void history();void setStatus(juce::String);
};
}
