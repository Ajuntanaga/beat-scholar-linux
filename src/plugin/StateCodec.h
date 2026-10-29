#pragma once

#include "core/Model.h"

#include <juce_core/juce_core.h>

namespace bsl::statecodec {

inline constexpr std::size_t maximumEmbeddedSampleBytes = 128u * 1024u * 1024u;
[[nodiscard]] constexpr bool fitsEmbeddedSampleBudget(std::size_t retained, std::size_t incoming) noexcept
{
    return retained <= maximumEmbeddedSampleBytes
        && incoming <= maximumEmbeddedSampleBytes - retained;
}
[[nodiscard]] bool canReplaceEmbeddedSample(const KitState&, std::size_t pad,
                                            std::size_t incomingBytes) noexcept;

juce::MemoryBlock encode(const ProjectState& state);
bool decode(const void* data, std::size_t size, ProjectState& destination);
juce::String encodePresetJson(const ProjectState& state);
bool decodePresetJson(const juce::String& json, ProjectState& destination);
juce::String encodeKitJson(const KitState& kit, const MasterState& master);
bool decodeKitJson(const juce::String& json, KitState& kit, MasterState& master);
juce::String encodeRandomizerJson(const RandomizerSettings& settings);
bool decodeRandomizerJson(const juce::String& json, RandomizerSettings& destination);
bool importLegacyPresetJson(const juce::String& json, ProjectState& destination);
bool importLegacyKitJson(const juce::String& json, KitState& kit, MasterState& master);
bool importLegacyRandomizerJson(const juce::String& json, RandomizerSettings& destination);

} // namespace bsl::statecodec
