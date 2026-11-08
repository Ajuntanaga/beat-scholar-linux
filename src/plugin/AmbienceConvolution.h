#pragma once

#include "audio/AmbienceProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace bsl {
// Independent stereo Room and Plate convolvers. Assets are resolved only on the
// user's machine; tests can supply small synthetic responses explicitly.
class AmbienceConvolution final : public AmbienceProcessor
{
public:
    bool prepareFromFactory(const juce::File& directory, double sampleRate,
                            std::size_t maximumBlockSize);
    void prepare(juce::AudioBuffer<float> room, juce::AudioBuffer<float> plate,
                 double sourceRate, double sampleRate, std::size_t maximumBlockSize);
    void reset() noexcept override;
    void process(float* roomLeft, float* roomRight, float* plateLeft,
                 float* plateRight, std::size_t frames) noexcept override;
    [[nodiscard]] bool isAvailable() const noexcept { return roomAvailable_ && plateAvailable_; }
    // The native loader uses a 401-tap sinc resampler and energy normalisation.
    static juce::AudioBuffer<float> loadDriveResponse(const juce::File& directory, double sampleRate);
    static juce::AudioBuffer<float> prepareResponse(const juce::AudioBuffer<float>& source,
                                                    double sourceRate, double sampleRate);
private:
    juce::dsp::ConvolutionMessageQueue queue_;
    juce::dsp::Convolution room_ { juce::dsp::Convolution::NonUniform { 256 }, queue_ };
    juce::dsp::Convolution plate_ { juce::dsp::Convolution::NonUniform { 256 }, queue_ };
    bool roomAvailable_ = false;
    bool plateAvailable_ = false;
};
} // namespace bsl
