#include "MacTestSupport.h"
#include "../Source/Processor.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <random>
#include <stdexcept>
thread_local bool counting = false;
thread_local size_t allocations = 0;
void *operator new(size_t n) {
  if (counting)
    ++allocations;
  if (auto *p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { std::free(p); }
void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
void run(mobina::Processor &p, juce::AudioBuffer<float> &b) {
  juce::MidiBuffer m;
  allocations = 0;
  counting = true;
  p.processBlock(b, m);
  counting = false;
  check(allocations == 0, "callback allocation");
  for (int c = 0; c < b.getNumChannels(); ++c)
    for (int i = 0; i < b.getNumSamples(); ++i)
      check(std::isfinite(b.getSample(c, i)), "nonfinite");
}
std::vector<float> render(int block, int preset, int quality) {
  mobina::Processor p;
  p.setCurrentProgram(preset);
  p.set("gillQuality", float(quality));
  p.prepareToPlay(48000, block);
  std::vector<float> out;
  for (int s = 0; s < 48000; s += block) {
    int n = std::min(block, 48000 - s);
    juce::AudioBuffer<float> b(2, n);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < n; ++i)
        b.setSample(
            c, i,
            float(.2 * std::sin((s + i) * .057) * (s + i < 36000 ? 1 : 0)));
    run(p, b);
    for (int i = 0; i < n; ++i)
      out.push_back(b.getSample(0, i));
  }
  return out;
}

// This exercises the native APVTS BYPASS through ordinary processBlock, using
// an always-active twin to observe the same running wet engine during fades.
int bypassRegressions() {
  int checks = 0, failures = 0, configurations = 0, transitions = 0;
  auto expect = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 24) std::cerr << "BYPASS_FAIL " << label << '\n'; }
  };
  for (double fs : {44100., 48000., 96000., 192000.})
    for (int channels : {1, 2}) for (int quality : {0, 1}) {
      ++configurations;
      mobina::Processor p, active;
      for (auto* processor : {&p, &active}) {
        auto layout = processor->getBusesLayout();
        layout.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.set(0, layout.inputBuses[0]);
        check(processor->setBusesLayout(layout), "bypass layout");
        processor->set("gillQuality", float(quality));
        processor->prepareToPlay(fs, 128);
      }
      expect(p.getLatencySamples() == 0, "zero PDC");
      juce::AudioBuffer<float> input(channels,128), tested(channels,128), wet(channels,128);
      uint32_t rng = 0x731ab92u;
      auto fill = [&](int n, bool silence = false) {
        input.setSize(channels,n,false,false,true);
        for (int c=0;c<channels;++c) for(int i=0;i<n;++i) {
          rng = rng * 1664525u + 1013904223u;
          input.setSample(c,i,silence ? 0.f : float(int32_t(rng >> 8) - 8388608) / 16777216.f);
        }
      };
      auto process = [&] {
        tested.makeCopyOf(input,true); wet.makeCopyOf(input,true);
        run(p,tested); run(active,wet);
      };
      for(int k=0;k<4;++k) { fill(128); process(); }
      const int ramp = int(std::ceil(fs * .025));
      double maximumFadeError=0, maximumDryError=0, hostDryError=0;
      auto fade = [&](double start, double target, int samples) {
        p.set("bypass",float(target)); ++transitions;
        for(int offset=0;offset<samples;offset+=128) {
          const int n=std::min(128,samples-offset);fill(n);process();
          for(int c=0;c<channels;++c) for(int i=0;i<n;++i) {
            const double blend=start+(target-start)*std::min(1.,double(offset+i+1)/ramp);
            const double expected=double(wet.getSample(c,i))+(double(input.getSample(c,i))-wet.getSample(c,i))*blend;
            maximumFadeError=std::max(maximumFadeError,std::abs(double(tested.getSample(c,i))-expected));
          }
        }
        return start+(target-start)*std::min(1.,double(samples)/ramp);
      };
      auto exactDry = [&](bool hostProbe = false) {
        for(int c=0;c<channels;++c) for(int i=0;i<input.getNumSamples();++i)
          {
            const double error=std::abs(double(tested.getSample(c,i))-input.getSample(c,i));
            maximumDryError=std::max(maximumDryError,error);
            if(hostProbe)hostDryError=std::max(hostDryError,error);
          }
      };
      for(int repeat=0;repeat<3;++repeat) {
        fade(0,1,ramp);
        fill(128);process();exactDry();
        // Exactly the native host's existing flush duration, with no extension.
        for(int k=0;k<std::max(64,int(std::ceil(fs*.25/128)));++k) {fill(128,true);process();}
        for(int position:{0,37,127}) {
          fill(128,true);
          for(int c=0;c<channels;++c)input.setSample(c,position,c==0?.75f:-.625f);
          process();exactDry(true);
        }
        uint32_t nativeRandom=0x137abc29u;
        for(int k=0;k<32;++k) {
          fill(128,true);
          for(int c=0;c<channels;++c)for(int i=0;i<128;++i) {
            nativeRandom^=nativeRandom<<13;nativeRandom^=nativeRandom>>17;nativeRandom^=nativeRandom<<5;
            input.setSample(c,i,float((double(nativeRandom)/4294967295.-.5)*.40));
          }
          process();exactDry(true);
        }
        fade(1,0,ramp);
        fill(128);process();
        bool exact=true;
        for(int c=0;c<channels;++c) exact=exact && std::memcmp(tested.getReadPointer(c),wet.getReadPointer(c),128*sizeof(float))==0;
        expect(exact,"settled ACTIVE equals running wet twin bitwise");
      }
      // Reversing an unfinished fade must retain its current blend smoothly.
      double blend=0;
      for(int k=0;k<8;++k) blend=fade(blend,k%2==0?1:0,ramp/4);
      // Complete the last ACTIVE fade without changing its target.
      fill(ramp-ramp/4);process();
      fade(0,1,ramp);
      juce::MemoryBlock saved; p.getStateInformation(saved);
      fade(1,0,ramp);
      p.setStateInformation(saved.getData(),int(saved.getSize()));
      expect(p.state.getRawParameterValue("bypass")->load()==1.f,"recall native BYPASS");
      // The restored APVTS reaches the same dry endpoint through normal processing.
      for(int k=0;k<ramp;k+=128) {fill(std::min(128,ramp-k));process();}
      fill(128);process();exactDry();
      p.reset();active.reset();fill(128);process();exactDry();
      p.prepareToPlay(fs,128);active.prepareToPlay(fs,128);fill(128);process();exactDry();
      expect(maximumFadeError<=3e-6,"smooth bounded 25ms fades including reversals");
      expect(maximumDryError==0,"exact same-sample native dry; no leakage");
      expect(hostDryError<=1e-7,"unchanged native-host impulse/random threshold");
      std::cout<<"BYPASS_CASE "<<fs<<" "<<channels<<" "<<quality<<" FADE "<<maximumFadeError<<" DRY "<<maximumDryError<<" HOST_DRY "<<hostDryError<<'\n';
    }
  std::cout<<"BYPASS_CHECKS "<<checks<<" FAILURES "<<failures<<" CONFIGURATIONS "<<configurations<<" TRANSITIONS "<<transitions<<'\n';
  return failures;
}

// Export actual active processor samples for independent original/fixed binary
// comparison; the verifier rebuilds both processors from pinned source inputs.
void activeFingerprint(const char* filename) {
  std::ofstream out(filename,std::ios::binary);
  check(bool(out),"active output file");
  int configurations=0;
  for(double fs:{44100.,48000.,96000.,192000.}) for(int channels:{1,2})
    for(int q:{0,1}) for(int preset=0;preset<8;++preset) {
      mobina::Processor p;
      auto l=p.getBusesLayout();
      l.inputBuses.set(0,channels==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());l.outputBuses.set(0,l.inputBuses[0]);
      check(p.setBusesLayout(l),"active layout");p.setCurrentProgram(preset);p.set("gillQuality",float(q));p.prepareToPlay(fs,128);
      juce::AudioBuffer<float> b(channels,128);
      uint32_t rng=0x536217ab;
      for(int k=0;k<128;++k) {
        if(k==64) {p.set("dream",float(17+preset*9));p.set("output",-3.f);}
        for(int c=0;c<channels;++c)for(int i=0;i<128;++i) {rng=rng*1664525u+1013904223u;b.setSample(c,i,float(int32_t(rng>>8)-8388608)/16777216.f);}
        run(p,b);
        for(int c=0;c<channels;++c)out.write(reinterpret_cast<const char*>(b.getReadPointer(c)),128*sizeof(float));
      }
      ++configurations;
    }
  check(bool(out),"active output write");std::cout<<"ACTIVE_CONFIGURATIONS "<<configurations<<'\n';
}
int main(int argc, char** argv) {
  gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;
  std::cout << std::unitbuf;
  try {
    if(argc==3 && juce::String(argv[1])=="--active-output") {activeFingerprint(argv[2]);return 0;}
    const int bypassFailures=bypassRegressions();
    if(argc==2 && juce::String(argv[1])=="--bypass-only") return bypassFailures?1:0;
    check(bypassFailures==0,"native bypass regressions");
    for (int preset = 0; preset < 8; ++preset)
      for (int q = 0; q < 2; ++q) {
        auto a = render(31, preset, q), b = render(257, preset, q);
        double error = 0;
        for (size_t i = 0; i < a.size(); ++i)
          error = std::max(error, std::abs(double(a[i] - b[i])));
        check(error < 2e-6, "block partition mismatch");
        std::cout << "PRESET " << preset << " QUALITY " << q << " BLOCK ERROR "
                  << error << "\n";
      }
    for (double fs : {44100., 48000., 96000.})
      for (int channels : {1, 2})
        for (int q : {0, 1}) {
          mobina::Processor p;
          auto l = p.getBusesLayout();
          l.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono()
                                            : juce::AudioChannelSet::stereo());
          l.outputBuses.set(0, l.inputBuses[0]);
          check(p.setBusesLayout(l), "layout");
          p.set("gillQuality", float(q));
          p.set("mix", 0);
          p.prepareToPlay(fs, 128);
          check(p.getLatencySamples() == 0, "latency");
          juce::AudioBuffer<float> b(channels, 128);
          b.clear();
          for (int c = 0; c < channels; ++c)
            b.setSample(c, 0, .5f);
          run(p, b);
          for (int c = 0; c < channels; ++c) {
            check(b.getSample(c, 0) == .5f, "dry onset");
            for (int i = 1; i < 128; ++i)
              check(b.getSample(c, i) == 0, "dry leakage");
          }
        }
    mobina::Processor p;
    for (int preset = 0; preset < 8; ++preset) {
      p.setCurrentProgram(preset);
      juce::MemoryBlock bytes;
      p.getStateInformation(bytes);
      mobina::Processor restored;
      restored.setStateInformation(bytes.getData(), int(bytes.getSize()));
      for (auto &s : mobina::specs)
        check(std::abs(p.state.getRawParameterValue(s.id)->load() -
                       restored.state.getRawParameterValue(s.id)->load()) <
                  1e-6f,
              "state mismatch");
    }
    p.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> b(2, 128);
    for (auto &s : mobina::specs)
      for (float value : {s.lo, s.hi}) {
        p.set(s.id, value);
        for (int z = 0; z < 12; ++z) {
          for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 128; ++i)
              b.setSample(c, i, float(.2 * std::sin((i + z * 128) * .07)));
          run(p, b);
        }
      }
    b.clear();
    b.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
    b.setSample(1, 0, std::numeric_limits<float>::infinity());
    run(p, b);
    p.setCurrentProgram(0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    check(editor->getWidth() == 660 && editor->getHeight() == 490,
          "editor size");
    check(!editor->getWantsKeyboardFocus(), "editor steals keyboard");
    auto image =
        editor->createComponentSnapshot(editor->getLocalBounds());
    juce::File target =
        juce::File::getCurrentWorkingDirectory().getChildFile("MOBINA-UI-default.png");
    auto out = target.createOutputStream();
    check(out != nullptr && out->openedOk(), "editor image stream");
    check(image.getWidth()==660 && image.getHeight()==490, "compact editor dimensions");
    out->setPosition(0);
    out->truncate();
    juce::PNGImageFormat png;
    check(png.writeImageToStream(image, *out), "editor image write");
    std::cout << "MOBINA_TESTS_PASS\n";
  } catch (const std::exception &e) {
    counting = false;
    std::cerr << e.what() << "\n";
    return 1;
  }
}
