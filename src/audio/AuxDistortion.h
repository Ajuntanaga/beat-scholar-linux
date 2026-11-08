#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace bsl {
// Shared Aux3 bus. Response preparation and replacement require stopped audio.
class AuxDistortion
{
public:
    void prepare(double sampleRate);
    void setResponse(std::array<std::vector<float>, 2> response);
    void reset() noexcept;
    void process(float* left, float* right, std::size_t frames, float drive) noexcept;
private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<std::array<float, 2>, 2> state {};
        float tick(float input, std::size_t channel) noexcept;
    };
    Biquad inputHighPass_, dcHighPass_, lowPass_;
    std::array<std::vector<float>, 2> response_, history_;
    std::size_t position_ = 0, gainFrames_ = 0, rampLength_ = 1;
    float inputGain_ = 0, gainStep_ = 0;
};
} // namespace bsl
