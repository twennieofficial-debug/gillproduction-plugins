#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstring>
#include <optional>
#include <cstdlib>

namespace gill::qualityTest {
// Read the processor's serialized APVTS, never the host's parameter cache.
inline juce::String binaryStateRoot(const juce::String& product) {
    static constexpr const char* studio[] = { "GILLTRANSIENT", "GILLROOM", "GILLMULTI", "GILLTAPE",
        "GILLVITAL", "GILL825", "GILLALPHA", "GILLMAX", "GILLDEESSPRO", "GILLHARMONIC",
        "GILLPRECISION", "GILLRACK" };
    for (auto name : studio) if (product == name) return product + "State";
    if (product == "MOBINA") return "MobinaState";
    if (product == "GILLSPECTRA PREVIEW") return "SpectraPreview";
    return {};
}
inline std::optional<float> parameterInTree(const juce::ValueTree& tree, const juce::String& id, bool xml) {
    std::optional<float> result;
    for (auto child : tree) {
        if (!child.hasProperty("id") || child["id"].toString() != id) continue;
        if (result || !child.hasType("PARAM") || !child.hasProperty("value")) return std::nullopt;
        const auto value = child["value"];
        double number = 0;
        if (value.isInt() || value.isInt64() || value.isDouble()) number = double(value);
        else if (xml && value.isString()) {
            const auto text = value.toString();
            const auto* start = text.toRawUTF8(); char* end = nullptr;
            number = std::strtod(start, &end);
            if (end == start || *end != '\0') return std::nullopt;
        } else return std::nullopt;
        if (!std::isfinite(number) || !std::isfinite(static_cast<float>(number))) return std::nullopt;
        // Both parameters used by this host are native two-state parameters.
        if ((id == "gillQuality" || id == "bypass") && number != 0.0 && number != 1.0) return std::nullopt;
        result = static_cast<float>(number);
    }
    return result;
}
inline std::optional<float> savedProcessorParameter(juce::MemoryBlock state, const juce::String& product,
                                                   const juce::String& id) {
    if (state.getSize() == 0 || state.getSize() > 64 * 1024 * 1024) return std::nullopt;
    if (auto wrapper = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()))) {
        if (auto* component = wrapper->getChildByName("IComponent")) {
            juce::MemoryBlock decoded;
            if (!decoded.fromBase64Encoding(component->getAllSubText())) return std::nullopt;
            state.swapWith(decoded);
        }
    }
    const auto* bytes = static_cast<const std::uint8_t*>(state.getData());
    const auto root = binaryStateRoot(product);
    if (root.isNotEmpty()) {
        const auto length = static_cast<std::size_t>(root.getNumBytesAsUTF8() + 1);
        for (std::size_t offset = 0; offset + length <= state.getSize(); ++offset) {
            if (std::memcmp(bytes + offset, root.toRawUTF8(), length) != 0) continue;
            auto tree = juce::ValueTree::readFromData(bytes + offset, state.getSize() - offset);
            const auto schema = tree.getProperty(product == "GILLSPECTRA PREVIEW" ? "stateSchema" : "schema");
            if (!tree.hasType(juce::Identifier(root)) || !schema.isInt()
                || int(schema) != (product == "GILLSPECTRA PREVIEW" ? 2 : 1)) return std::nullopt;
            // JUCE may return a partial tree for truncated input. A canonical
            // serialization must match the whole consumed tree, including its end.
            juce::MemoryOutputStream canonical; tree.writeToStream(canonical);
            if (canonical.getDataSize() > state.getSize() - offset
                || std::memcmp(canonical.getData(), bytes + offset, canonical.getDataSize()) != 0) return std::nullopt;
            return parameterInTree(tree, id, false);
        }
        return std::nullopt;
    }
    // Existing XML/APVTS processors are carried in JUCE VST3 component chunks.
    for (std::size_t offset = 0; offset + 9 <= state.getSize(); ++offset) {
        if (juce::ByteOrder::littleEndianInt(bytes + offset) != 0x21324356u) continue;
        const auto length = juce::ByteOrder::littleEndianInt(bytes + offset + 4);
        if (length == 0 || length > state.getSize() - offset - 9) continue;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(bytes + offset, static_cast<int>(length + 9)))
            return parameterInTree(juce::ValueTree::fromXml(*xml), id, true);
    }
    return std::nullopt;
}
}
