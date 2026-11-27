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
                     std::mt19937& engine, bool& previousTriggered)
{
    if (!settings.isValid())
        return;

    if (settings.slicingEnabled && !settings.selectedSubdivisions.empty())
        // Original resize routine default-constructs appended slices; it does not
        // clone the first hit as the ordinary editing subdivision helper does.
        beat.slices.resize(choose(settings.selectedSubdivisions, engine));

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> velocity(settings.minimumVelocity,
                                                   settings.maximumVelocity);
    for (auto& slice : beat.slices)
    {
        if (settings.fillEmptyOnly && slice.enabled)
            continue;

        bool triggered = slice.enabled;
        if (settings.triggeringEnabled)
        {
            // Original 1.6.2: neutral Spacing retains Chance; lower values
            // interpolate toward 1 after a hit, higher values toward 0.
            float chance = settings.triggerChance;
            if (previousTriggered)
            {
                if (settings.spacing < 0.5f)
                    chance += (1.0f - chance) * (1.0f - 2.0f * settings.spacing);
                else if (settings.spacing > 0.5f)
                    chance *= 2.0f * (1.0f - settings.spacing);
            }
            triggered = unit(engine) < chance;
            previousTriggered = triggered;
        }
        slice.enabled = triggered;
        // Original randomizes stored note/velocity even when the slice is silent.
        // Original skips note assignment when the selected-drum list is empty.
        if (settings.noteSelectionEnabled && !settings.selectedPads.empty())
            slice.padIndex = choose(settings.selectedPads, engine);
        // Disabled modules leave stored data untouched (original 1.6.2).
        if (settings.velocityEnabled)
        {
            slice.velocity = velocity(engine);
            slice.velocityAssigned = true;
        }
    }
}

} // namespace

void Randomizer::apply(BeatState& beat, const RandomizerSettings& settings,
                       std::uint32_t seed, float /*defaultVelocity*/, bool* triggerHistory)
{
    std::mt19937 engine(seed);
    bool initialHistory = false;
    bool& previousTriggered = triggerHistory ? *triggerHistory : initialHistory;
    applyWithEngine(beat, settings, engine, previousTriggered);
}

void Randomizer::apply(LaneState& lane, const RandomizerSettings& settings,
                       std::uint32_t seed, float /*defaultVelocity*/, bool* triggerHistory)
{
    std::mt19937 engine(seed);
    bool initialHistory = false;
    bool& previousTriggered = triggerHistory ? *triggerHistory : initialHistory;
    for (auto& beat : lane.beats)
        applyWithEngine(beat, settings, engine, previousTriggered);
}

} // namespace bsl
