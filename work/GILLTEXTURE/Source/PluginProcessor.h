#pragma once
#include "TextureInfo.h"
#include "../../GILLCommon/QualityBus.h"
#include <array>

class GillTextureProcessor final : public juce::AudioProcessor {
public:
    explicit GillTextureProcessor(TextureKind);
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    const juce::String getName() const override { return textureInfo(kind).name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Conservative bounds include the slowest vocoder envelope and repeated
    // granular feedback; hosts may use this to decide offline render length.
    double getTailLengthSeconds() const override { return kind==TextureKind::Grain ? 60.0 : kind==TextureKind::Vocode ? 3.0 : 0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 6; }
    int getCurrentProgram() override { return program.load(); }
    void setCurrentProgram(int i) override { selectPreset(i, false); }
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void selectPreset(int, bool gesture=true);
    void setValue(const juce::String&, float, bool gesture=true);
    float value(const juce::String&) const;
    bool presetMatches() const;
    void applyPattern(int);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(TextureKind);
    const TextureKind kind;
    const std::vector<TextureParam> specs;
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient quality{*this, apvts};
    std::atomic<float> inputPeak{0}, outputPeak{0};
    std::atomic<double> rateView{48000}, tempo{120};
    std::atomic<bool> hostTempo{false}, carrierConnected{false};
    std::atomic<int> currentStep{0};
    std::array<std::atomic<float>,128> scope{};
    std::atomic<unsigned> scopePosition{0};
private:
    static BusesProperties buses(TextureKind);
    void process(juce::AudioBuffer<float>&, bool);
    gill::texture::Settings settings() const;
    float param(std::size_t) const;
    std::array<std::atomic<float>*,32> raw{};
    std::array<std::atomic<float>,32> presetValues{};
    std::atomic<int> program{0};
    gill::texture::Engine engine;
    bool supported=true, hadPosition=false, wasPlaying=false;
    std::int64_t nextPosition=0;
    double rate=48000, mixSmooth=1, gainSmooth=1, bypassSmooth=0, smoothing=0, inputHold=0, outputHold=0, decay=1;
    unsigned scopeWrite=0, scopeClock=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillTextureProcessor)
};
