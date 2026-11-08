#pragma once
#include <cstddef>

namespace bsl {
// Prepared outside the callback. Each stereo bus is replaced by its wet return.
class AmbienceProcessor
{
public:
    virtual ~AmbienceProcessor() = default;
    virtual void reset() noexcept = 0;
    virtual void process(float* roomLeft, float* roomRight, float* plateLeft,
                         float* plateRight, std::size_t frames) noexcept = 0;
};
} // namespace bsl
