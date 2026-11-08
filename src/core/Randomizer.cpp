#include "core/Randomizer.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace bsl {
namespace {

template <typename T>
const T& choose(const std::vector<T>& values, std::mt19937& engine)
{
    std::uniform_int_distribution<std::size_t> distribution(0, values.size() - 1);
    return values[distribution(engine)];
}

void applyWithEngine(BeatState& beat, const RandomizerSettings& settings,
                     std::mt19937& engine, float defaultVelocity, bool& previousTriggered)
{
    if (!settings.isValid())
        return;

    if (settings.slicingEnabled && !settings.selectedSubdivisions.empty())
        beat.setSubdivision(choose(settings.selectedSubdivisions, engine));

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> velocity(settings.minimumVelocity,
                                                   settings.maximumVelocity);
    for (auto& slice : beat.slices)
    {
        if (settings.fillEmptyOnly && slice.enabled)
        {
            previousTriggered = true;
            continue;
        }

        bool triggered = slice.enabled;
        if (settings.triggeringEnabled)
        {
            // Low Spacing continues a phrase; high Spacing leaves gaps.
            // Trigger Chance starts phrases, with 100% always enabling all slices.
            const float chance = previousTriggered && settings.triggerChance < 1.0f
                                     ? 1.0f - settings.spacing : settings.triggerChance;
            triggered = unit(engine) < chance;
        }
        slice.enabled = triggered;
        previousTriggered = triggered;
        if (!triggered)
            continue;
        if (settings.noteSelectionEnabled)
        {
            const std::uint8_t pad = settings.selectedPads.empty()
                                         ? std::uint8_t { 0 }
                                         : choose(settings.selectedPads, engine);
            slice.padIndex = std::min<std::uint8_t>(pad, 15);
        }
        if (settings.velocityEnabled)
            slice.velocity = velocity(engine);
        else
            slice.velocity = std::clamp(defaultVelocity, 0.0f, 1.0f);
        slice.velocityAssigned = true;
    }
}

} // namespace

void Randomizer::apply(BeatState& beat, const RandomizerSettings& settings,
                       std::uint32_t seed, float defaultVelocity)
{
    std::mt19937 engine(seed);
    bool previousTriggered = false;
    applyWithEngine(beat, settings, engine, defaultVelocity, previousTriggered);
}

void Randomizer::apply(LaneState& lane, const RandomizerSettings& settings,
                       std::uint32_t seed, float defaultVelocity)
{
    std::mt19937 engine(seed);
    bool previousTriggered = false;
    for (auto& beat : lane.beats)
        applyWithEngine(beat, settings, engine, defaultVelocity, previousTriggered);
}

} // namespace bsl
