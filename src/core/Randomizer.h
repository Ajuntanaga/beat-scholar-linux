#pragma once

#include "core/Model.h"

#include <cstdint>
#include <vector>

namespace bsl {

class Randomizer
{
public:
    static void apply(BeatState& beat, const RandomizerSettings& settings,
                      std::uint32_t seed, float defaultVelocity = GlobalState{}.defaultVelocity);
    static void apply(LaneState& lane, const RandomizerSettings& settings,
                      std::uint32_t seed, float defaultVelocity = GlobalState{}.defaultVelocity);
};

} // namespace bsl
