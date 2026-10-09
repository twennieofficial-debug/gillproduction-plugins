#pragma once

namespace gill {
// Non-realtime state recall only. APVTS can skip its native parameter update
// when a fractional boolean already maps to the accepted snapped atomic value.
// Reconcile only booleans: normalising continuous values again can introduce
// float round-trip drift after APVTS has already restored their native value.
inline void replaceStateAndReconcile(juce::AudioProcessorValueTreeState& state,
                                     const juce::ValueTree& accepted)
{
    state.replaceState(accepted);
    for (auto* base : state.processor.getParameters())
        if (auto* parameter = dynamic_cast<juce::AudioParameterBool*>(base))
            if (auto* raw = state.getRawParameterValue(parameter->paramID))
            {
                const auto normalized = parameter->convertTo0to1(raw->load());
                if (base->getValue() != normalized)
                    parameter->setValueNotifyingHost(normalized);
            }
}
}
