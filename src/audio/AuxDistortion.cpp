#include "AuxDistortion.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace bsl {
float AuxDistortion::Biquad::tick(float input, std::size_t channel) noexcept
{
    auto& s = state[channel];
    const float result = input * b0 + s[0];
    s[0] = (input * b1 - result * a1) + s[1];
    s[1] = input * b2 - result * a2;
    return result;
}

void AuxDistortion::prepare(double sampleRate)
{
    const float rate = static_cast<float>(std::isfinite(sampleRate) && sampleRate > 0
                                               ? sampleRate : 48000.0);
    const auto filter = [rate](float frequency, float q, bool highPass) {
        Biquad result;
        float t = std::tan(3.14159265358979323846f * std::min(frequency, rate * 0.499f) / rate);
        if (!highPass) t = 1.0f / t;
        const float term = (1.0f / q) * t;
        const float square = t * t;
        result.b0 = 1.0f / ((term + 1.0f) + square);
        result.b1 = (highPass ? -2.0f : 2.0f) * result.b0;
        result.b2 = result.b0;
        result.a1 = (result.b0 + result.b0) * (highPass ? square - 1.0f : 1.0f - square);
        result.a2 = ((1.0f - term) + square) * result.b0;
        return result;
    };
    inputHighPass_ = filter(380.0f, 0.5f, true);
    dcHighPass_ = filter(8.0f, 1.0f, true);
    lowPass_ = filter(6000.0f, 0.3f, false);
    rampLength_ = std::max<std::size_t>(1, static_cast<std::size_t>(rate * 0.1f));
    // A response resampled for the previous rate must not survive re-prepare.
    response_ = {};
    history_ = {};
    reset();
}

void AuxDistortion::setResponse(std::array<std::vector<float>, 2> response)
{
    const bool valid = !response[0].empty() && response[0].size() <= 256
        && response[0].size() == response[1].size()
        && std::all_of(response[0].begin(), response[0].end(), [](float x) { return std::isfinite(x); })
        && std::all_of(response[1].begin(), response[1].end(), [](float x) { return std::isfinite(x); });
    response_ = valid ? std::move(response) : std::array<std::vector<float>, 2> {};
    for (auto& channel : history_) channel.assign(response_[0].size(), 0.0f);
    reset();
}

void AuxDistortion::reset() noexcept
{
    inputHighPass_.state = dcHighPass_.state = lowPass_.state = {};
    for (auto& channel : history_) std::fill(channel.begin(), channel.end(), 0.0f);
    position_ = 0;
    inputGain_ = 0;
    gainFrames_ = rampLength_;
    gainStep_ = 5.75f / static_cast<float>(rampLength_);
}

void AuxDistortion::process(float* left, float* right, std::size_t frames, float drive) noexcept
{
    constexpr float halfPi = 1.57079632679489661923f;
    drive = std::isfinite(drive) ? std::clamp(drive, 0.0f, 1.0f) : 0.5f;
    const float threshold = std::pow(10.0f, -3.0f * drive);
    const std::size_t length = response_[0].size();
    std::array<float*, 2> channels { left, right };
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        if (gainFrames_ > 0)
        {
            inputGain_ += gainStep_;
            if (--gainFrames_ == 0) inputGain_ = 5.75f;
        }
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            float value = inputHighPass_.tick(channels[channel][frame], channel) * inputGain_;
            // Preserve the two native float multiplies: folding the constants
            // changes the DC-filter state enough to alter high-drive clipping.
            value = std::atan((value * 0.51f) * halfPi) / halfPi;
            value = dcHighPass_.tick(value, channel);
            value = std::clamp(value, -threshold, threshold) / threshold;
            value = 1.5f * value - 0.5f * value * value * value;
            value = lowPass_.tick(value, channel);
            float output = 0.0f;
            if (length != 0)
            {
                history_[channel][position_] = value;
                auto tap = position_;
                for (std::size_t i = 0; i < length; ++i)
                {
                    output += response_[channel][i] * history_[channel][tap];
                    tap = tap == 0 ? length - 1 : tap - 1;
                }
            }
            channels[channel][frame] = output;
        }
        if (length != 0 && ++position_ == length) position_ = 0;
    }
}
} // namespace bsl
