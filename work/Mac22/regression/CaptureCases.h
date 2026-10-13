#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <vector>

namespace capture_audit {
inline juce::var obj() { return juce::var(new juce::DynamicObject); }
inline void prop(juce::var& v, const char* key, juce::var value) { v.getDynamicObject()->setProperty(key, value); }
struct Transport final : juce::AudioPlayHead {
    int64_t frame = 0; bool playing = true; double bpm = 120.;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setTimeInSamples(frame); p.setTimeInSeconds(double(frame) / 48000.);
        p.setBpm(bpm); p.setPpqPosition(double(frame) / 48000. * bpm / 60.);
        p.setIsPlaying(playing); p.setIsRecording(false); p.setIsLooping(false);
        p.setTimeSignature(TimeSignature{4, 4}); return p;
    }
};
inline juce::AudioProcessorParameter* parameter(juce::AudioPluginInstance& p, const juce::String& name) {
    for (auto* x : p.getParameters()) if (x->getName(256).equalsIgnoreCase(name)) return x;
    return nullptr;
}
inline juce::var stateDescription(const juce::MemoryBlock& incoming) {
    auto r = obj(); prop(r, "bytes", double(incoming.getSize()));
    juce::MemoryBlock state(incoming);
    if(auto wrapper=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()))) {
        if(auto*component=wrapper->getChildByName("IComponent")) {
            juce::MemoryBlock decoded;
            if(!decoded.fromBase64Encoding(component->getAllSubText())){prop(r,"xmlObservable",false);return r;}
            state.swapWith(decoded);
        }
    }
    // VST3 host state is normally a container. Read the embedded JUCE XML header
    // without assuming it starts at offset zero; never rewrite container offsets.
    auto* data = static_cast<const unsigned char*>(state.getData());

    for (size_t i = 0; i + 8 < state.getSize(); ++i) {
        if (data[i] != 0x56 || data[i+1] != 0x43 || data[i+2] != 0x32 || data[i+3] != 0x21) continue;
        const uint32_t n = uint32_t(data[i+4]) | (uint32_t(data[i+5])<<8) | (uint32_t(data[i+6])<<16) | (uint32_t(data[i+7])<<24);
        if (n == 0 || n > state.getSize() - i - 8) continue;
        auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(data+i+8), int(n)));
        if (!xml || xml->getTagName()=="VST3PluginState") continue;
        prop(r, "xmlRoot", xml->getTagName());
        prop(r, "hasModel", xml->getChildByName("MODEL") != nullptr);
        prop(r, "alignedAudio", xml->getStringAttribute("alignedAudio"));
        prop(r, "schema", xml->getStringAttribute("schema"));
        auto savedParameters=obj();
        for (auto* child : xml->getChildIterator()) {
            const auto id=child->getStringAttribute("id");
            if(id.isNotEmpty())savedParameters.getDynamicObject()->setProperty(id,child->getDoubleAttribute("value",-1.));
            if (id == "preview") prop(r, "savedPreview", child->getDoubleAttribute("value", -1.));
        }
        prop(r,"savedParameters",savedParameters);
        prop(r, "xmlObservable", true); return r;
    }
    prop(r, "xmlObservable", false); return r;
}
struct Render { std::vector<float> output; bool finite = true; double peak = 0, energy = 0; };
inline Render render(juce::AudioPluginInstance& p, Transport& t, const juce::MemoryBlock& state,
                     int keyMode, int transportMode, bool preview) {
    p.releaseResources(); p.setStateInformation(state.getData(), int(state.getSize()));
    if (auto* x = parameter(p, "PREVIEW")) x->setValueNotifyingHost(preview ? 1.f : 0.f);
    p.setRateAndBufferSizeDetails(48000., 128); p.prepareToPlay(48000., 128);
    t.frame = 0; t.playing = transportMode != 1;
    Render r;
    for (int block = 0; block < 192; ++block) {
        if (transportMode == 2 && block == 96) t.frame += 48000;
        if (transportMode == 3 && block == 96) t.playing = false;
        juce::AudioBuffer<float> buffer(juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()),128);
        buffer.clear();
        auto main = p.getBusBuffer(buffer, true, 0);
        for (int c=0;c<main.getNumChannels();++c) for (int i=0;i<128;++i) {
            const double time = double(block*128+i)/48000.;
            main.setSample(c,i,float(.13*std::sin(juce::MathConstants<double>::twoPi*(c==0?337.:503.)*time)));
        }
        if (p.getBusCount(true)>1 && p.getBus(true,1)->isEnabled()) {
            auto key = p.getBusBuffer(buffer,true,1);
            for (int c=0;c<key.getNumChannels();++c) for(int i=0;i<128;++i) {
                const double time=double(block*128+i)/48000.;
                // Physically distinct from main and from the other key channel.
                const float value=keyMode==0?0.f:float((c==0?.61:.37)*std::sin(juce::MathConstants<double>::twoPi*(c==0?83.:127.)*time));
                key.setSample(c,i,value);
            }
        }
        juce::MidiBuffer midi; p.processBlock(buffer,midi);
        auto out=p.getBusBuffer(buffer,false,0);
        for(int c=0;c<out.getNumChannels();++c) for(int i=0;i<128;++i) {
            const float v=out.getSample(c,i); r.output.push_back(v); r.finite = r.finite && std::isfinite(v);
            if(std::isfinite(v)){r.peak=juce::jmax(r.peak,std::abs(double(v)));r.energy+=double(v)*v;}
        }
        if(t.playing)t.frame+=128;
    }
    p.releaseResources(); return r;
}
inline double difference(const Render&a,const Render&b) {
    if(!a.finite||!b.finite||a.output.size()!=b.output.size()||a.output.empty())return -1.;
    double e=0;for(size_t i=0;i<a.output.size();++i){const double d=double(a.output[i])-b.output[i];e+=d*d;}
    return std::sqrt(e/double(a.output.size()));
}
}

inline juce::var runCaptureCases(juce::AudioPluginInstance& p, juce::String name) {
    using namespace capture_audit;
    auto result=obj();prop(result,"product",name);prop(result,"sampleRate",48000);prop(result,"blockSize",128);
    prop(result,"coverage","Native VST3 idle guide isolation, transport, host parameter/state and empty-result preview; editor-only acquisition not exercised.");
    if(name!="GILLALIGN"&&name!="GILLDIRECTOR"){prop(result,"status","not-applicable");return result;}
    const auto originalLayout=p.getBusesLayout(); juce::MemoryBlock original; p.getStateInformation(original);
    auto* oldHead=p.getPlayHead(); const bool oldNonRealtime=p.isNonRealtime();
    Transport transport; p.setPlayHead(&transport); p.setNonRealtime(false);
    prop(result,"initialState",stateDescription(original));
    juce::Array<juce::var> controls;
    for(auto* x:p.getParameters()) {
        auto row=obj();prop(row,"name",x->getName(256));prop(row,"index",x->getParameterIndex());
        if(auto*h=dynamic_cast<juce::HostedAudioProcessorParameter*>(x))prop(row,"hostId",h->getParameterID());
        controls.add(row);
    } prop(result,"exposedControls",controls);
    auto unavailable=obj();prop(unavailable,"status","unsupported-through-public-host-parameters");
    prop(unavailable,"reason",name=="GILLALIGN"?"CAPTURE GUIDE, CAPTURE DOUBLE, ALIGN and clear are processor editor actions; PREVIEW alone does not create captured audio.":"LEARN, STOP, APPLY, UNDO and PREVIEW are processor editor actions; their states are not exported as host parameters.");
    prop(result,"acquisition",unavailable);
    auto* target=parameter(p,name=="GILLALIGN"?"TIGHTNESS":"INTENSITY");
    if(target) {
        const float before=target->getValue(); target->setValueNotifyingHost(before<.5f?.83f:.17f);
        juce::MemoryBlock changed;p.getStateInformation(changed);
        p.setStateInformation(original.getData(),int(original.getSize()));
        auto row=obj();prop(row,"before",before);prop(row,"restored",target->getValue());
        prop(row,"stateChanged",changed!=original);prop(row,"parameterRestored",std::abs(target->getValue()-before)<1.e-5f);
        prop(result,"stateRoundTrip",row);
    }
    juce::Array<juce::var> cases;
    for(int mainChannels:{1,2})for(int keyChannels:{0,1,2})for(int mode:{0,1,2,3}) {
        auto row=obj();prop(row,"mainChannels",mainChannels);prop(row,"keyChannels",keyChannels);
        prop(row,"transport",mode==0?"playing":mode==1?"stopped":mode==2?"seek-forward":"play-then-stop");
        auto layout=originalLayout;layout.inputBuses.set(0,juce::AudioChannelSet::canonicalChannelSet(mainChannels));
        layout.outputBuses.set(0,juce::AudioChannelSet::canonicalChannelSet(mainChannels));
        if(layout.inputBuses.size()>1)layout.inputBuses.set(1,juce::AudioChannelSet::canonicalChannelSet(keyChannels));
        if(layout.inputBuses.size()<2||!p.setBusesLayout(layout)){prop(row,"status","unsupported-layout");cases.add(row);continue;}
        const auto silent=render(p,transport,original,0,mode,false);
        const auto live=render(p,transport,original,1,mode,false);
        prop(row,"finite",silent.finite&&live.finite);prop(row,"outputPeak",live.peak);prop(row,"mainPositiveControl",live.peak>1.e-4);
        const auto delta=difference(silent,live);prop(row,"silentVsLiveKeyRms",delta);
        prop(row,"idleKeyIsolation",delta>=0&&delta<1.e-6&&silent.finite&&live.finite);
        prop(row,"interpretation","Idle isolation only; this is neither learned-guide consumption evidence nor a ducking test.");
        if(name=="GILLALIGN") {
            const auto preview=render(p,transport,original,1,mode,true);
            prop(row,"emptyPreviewFinite",preview.finite);prop(row,"emptyPreviewVsDryRms",difference(live,preview));
            juce::MemoryBlock previewState;p.getStateInformation(previewState);prop(row,"previewSavedState",stateDescription(previewState));
        }
        prop(row,"status",silent.finite&&live.finite?"observed":"non-finite-output");cases.add(row);
    }
    prop(result,"cases",cases);prop(result,"status","partial-coverage-acquisition-requires-editor");
    p.releaseResources();p.setBusesLayout(originalLayout);p.setStateInformation(original.getData(),int(original.getSize()));
    p.setPlayHead(oldHead);p.setNonRealtime(oldNonRealtime);
    return result;
}

