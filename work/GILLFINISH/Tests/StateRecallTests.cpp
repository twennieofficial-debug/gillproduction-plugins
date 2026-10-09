#include "PluginProcessor.h"
#include <iostream>
#include <vector>
#include <cmath>

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    int checks=0, failures=0, booleans=0;
    for (auto kind : {FinishKind::Silk, FinishKind::Spark, FinishKind::Strip, FinishKind::Gold, FinishKind::Dive})
    {
        auto instance=std::make_unique<GillFinishProcessor>(kind);
        auto& processor=*instance;
        for (auto* base : processor.getParameters())
        {
            auto* parameter=dynamic_cast<juce::AudioParameterBool*>(base);
            if (!parameter) continue;
            ++booleans;
            for (float saved : {0.f,1.f})
            {
                parameter->setValueNotifyingHost(saved);
                juce::MemoryBlock state;
                processor.getStateInformation(state);
                for (float changed : {.229f,.45f,.55f,.771f})
                    for (int repeat=0;repeat<3;++repeat)
                    {
                        parameter->setValueNotifyingHost(changed);
                        // Verify the host-visible fractional precondition really occurred.
                        if (base->getValue()!=changed) return 2;
                        processor.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
                        ++checks;
                        if (base->getValue()!=saved || processor.value(parameter->paramID)!=saved)
                        {
                            ++failures;
                            std::cerr << "STATE_RECALL_MISMATCH " << processor.getName() << " " << parameter->paramID
                                      << " saved=" << saved << " changed=" << changed << " actual=" << base->getValue() << '\n';
                        }
                        juce::MemoryBlock again;
                        processor.getStateInformation(again);
                        auto roundtrip=std::make_unique<GillFinishProcessor>(kind);
                        auto& restored=*roundtrip;
                        restored.setStateInformation(again.getData(),static_cast<int>(again.getSize()));
                        ++checks;
                        if (restored.apvts.getParameter(parameter->paramID)->getValue()!=saved) ++failures;
                    }
            }
        }
    }
    if (booleans!=8 || checks!=384) { std::cerr<<"COVERAGE_MISMATCH "<<booleans<<" "<<checks<<'\n';return 2; }
    std::cout << "STATE_RECALL_CHECKS " << checks << " FAILURES " << failures << '\n';
    if (failures) return 1;
    std::cout << "FINISH_STATE_RECALL_PASS\n";
    return 0;
}



