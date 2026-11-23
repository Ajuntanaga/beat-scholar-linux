#include "AuxDistortion.h"
#include "chowdsp_fft.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace bsl {

struct AuxDistortion::Convolution
{
    static constexpr std::size_t block = 512, size = 1024;
    struct alignas(64) Buffer { std::array<float, size> samples {}; };
    std::array<Buffer, 2> input, spectrum, overlap;
    Buffer frequency, product, output, scratch;
    void* setup = chowdsp::fft::fft_new_setup(size, chowdsp::fft::FFT_REAL, true);
    std::size_t offset = 0;
    ~Convolution() { if (setup) chowdsp::fft::fft_destroy_setup(setup); }
    void reset() noexcept { input = {}; overlap = {}; offset = 0; }
    void setResponse(const std::array<std::vector<float>, 2>& response)
    {
        spectrum = {};
        if (!setup || response[0].empty()) return;
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            output.samples.fill(0);
            std::copy(response[ch].begin(), response[ch].end(), output.samples.begin());
            chowdsp::fft::fft_transform_unordered(setup, output.samples.data(),
                spectrum[ch].samples.data(), scratch.samples.data(), chowdsp::fft::FFT_FORWARD);
        }
    }
    void process(float* left, float* right, std::size_t frames) noexcept
    {
        using namespace chowdsp::fft;
        std::array<float*, 2> channels {left, right};
        for (std::size_t start = 0; start < frames;)
        {
            const auto count = std::min(block - offset, frames - start);
            for (std::size_t ch = 0; ch < 2; ++ch)
            {
                std::copy_n(channels[ch] + start, count, input[ch].samples.data() + offset);
                fft_transform_unordered(setup, input[ch].samples.data(), frequency.samples.data(),
                                        scratch.samples.data(), FFT_FORWARD);
                product.samples.fill(0);
                fft_convolve_unordered(setup, frequency.samples.data(), spectrum[ch].samples.data(),
                                       product.samples.data(), 1.0f / static_cast<float>(size));
                fft_transform_unordered(setup, product.samples.data(), output.samples.data(),
                                        scratch.samples.data(), FFT_BACKWARD);
                for (std::size_t i = 0; i < count; ++i)
                    channels[ch][start + i] = output.samples[offset + i] + overlap[ch].samples[offset + i];
                if (offset + count == block)
                {
                    std::copy_n(output.samples.data() + block, block, overlap[ch].samples.data());
                    input[ch].samples.fill(0);
                }
            }
            start += count;
            offset = (offset + count) % block;
        }
    }
};

AuxDistortion::AuxDistortion() = default;
AuxDistortion::~AuxDistortion() = default;

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
    convolution_ = std::make_unique<Convolution>();
    reset();
}

void AuxDistortion::setResponse(std::array<std::vector<float>, 2> response)
{
    const bool valid = !response[0].empty() && response[0].size() <= 256
        && response[0].size() == response[1].size()
        && std::all_of(response[0].begin(), response[0].end(), [](float x) { return std::isfinite(x); })
        && std::all_of(response[1].begin(), response[1].end(), [](float x) { return std::isfinite(x); });
    response_ = valid ? std::move(response) : std::array<std::vector<float>, 2> {};
    if (!convolution_) convolution_ = std::make_unique<Convolution>();
    convolution_->setResponse(response_);
    reset();
}

void AuxDistortion::reset() noexcept
{
    inputHighPass_.state = dcHighPass_.state = lowPass_.state = {};
    if (convolution_) convolution_->reset();
    inputGain_ = 0;
    gainFrames_ = rampLength_;
    gainStep_ = 5.75f / static_cast<float>(rampLength_);
}

void AuxDistortion::snapFilterStateToZero() noexcept
{
    for (auto* filter : { &inputHighPass_, &dcHighPass_, &lowPass_ })
        for (auto& channel : filter->state)
            for (auto& value : channel)
                if (std::abs(value) <= 1.0e-8f)
                    value = 0.0f;
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
            channels[channel][frame] = value;
        }
    }
    if (length != 0 && convolution_ && convolution_->setup)
        convolution_->process(left, right, frames);
    else
        for (auto* channel : channels) std::fill_n(channel, frames, 0.0f);
}
} // namespace bsl
