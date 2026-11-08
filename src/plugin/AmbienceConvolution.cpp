#include "AmbienceConvolution.h"

#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace bsl {
namespace {
juce::AudioBuffer<float> readResponse(const juce::File& file, const char* hash, int frames, double sourceRate = 48000.0)
{
    juce::MemoryBlock bytes;
    if (file.getSize() > 1024 * 1024 || !file.loadFileAsData(bytes)
        || juce::SHA256(bytes).toHexString() != hash)
        return {};
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::MemoryInputStream(bytes, true), true));
    if (!reader || reader->numChannels != 2 || reader->sampleRate != sourceRate
        || reader->lengthInSamples != frames)
        return {};
    juce::AudioBuffer<float> result(2, frames);
    if (!reader->read(&result, 0, frames, 0, true, true))
        return {};
    return result;
}

bool validResponse(const juce::AudioBuffer<float>& response)
{
    if (response.getNumChannels() != 2 || response.getNumSamples() <= 0)
        return false;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < response.getNumSamples(); ++i)
            if (!std::isfinite(response.getSample(ch, i)))
                return false;
    return true;
}
} // namespace

bool AmbienceConvolution::prepareFromFactory(const juce::File& directory, double sampleRate,
                                             std::size_t maximumBlockSize)
{
    auto room = readResponse(directory.getChildFile("Room.wav"),
        "f32926b50f9f140ee66ad5ba6002c3d2cf545a52d675234db05f744065407dcc", 18892);
    auto plate = readResponse(directory.getChildFile("Plate.wav"),
        "207fe7ae2e07ae3905c1d1c6accf46dfb4f10efee31d431073c7afd5a7b37872", 101808);
    prepare(std::move(room), std::move(plate), 48000.0, sampleRate, maximumBlockSize);
    return isAvailable();
}

juce::AudioBuffer<float> AmbienceConvolution::loadDriveResponse(const juce::File& directory,
                                                              double sampleRate)
{
    const auto response = readResponse(directory.getChildFile("Drive.wav"),
        "34f70f831ddef41e7d287beaa80adbae97d07df963e9fc55567f7f51aa340339", 32, 96000.0);
    return prepareResponse(response, 96000.0, sampleRate);
}

juce::AudioBuffer<float> AmbienceConvolution::prepareResponse(
    const juce::AudioBuffer<float>& source, double sourceRate, double sampleRate)
{
    if (!validResponse(source) || !std::isfinite(sourceRate) || sourceRate <= 0.0
        || !std::isfinite(sampleRate) || sampleRate <= 0.0)
        return {};
    const double requestedLength = std::max(1.0, std::nearbyint(
        static_cast<double>(source.getNumSamples()) * sampleRate / sourceRate));
    if (!std::isfinite(requestedLength)
        || requestedLength > static_cast<double>(std::numeric_limits<int>::max() - 200))
        return {};
    const auto length = static_cast<int>(requestedLength);
    juce::AudioBuffer<float> result(2, length);
    if (sourceRate == sampleRate)
    {
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, 0, source, ch, 0, length);
    }
    else
    {
        result.clear();
        const double ratio = sampleRate / sourceRate;
        // Native d427e0: unwindowed sinc, offsets -200..200, truncation
        // toward zero for source indices. Keep accumulation in float.
        // The native downsample filter touches a deep-copied temporary, while
        // sinc reads the owned source (d43f5e -> d41bb0 -> d427e0/cef3c0).
        // Reproduce that source path without filtering the input here.
        for (int i = 0; i < length; ++i)
        {
            const double position = static_cast<double>(i) / ratio;
            for (int offset = -200; offset <= 200; ++offset)
            {
                const auto index = static_cast<int>(position + static_cast<double>(offset));
                if (index < 0 || index >= source.getNumSamples())
                    continue;
                const double distance = position - static_cast<double>(index);
                const double angle = juce::MathConstants<double>::pi * distance;
                const float weight = static_cast<float>(distance == 0.0 ? 1.0 : std::sin(angle) / angle);
                for (int ch = 0; ch < 2; ++ch)
                    result.addSample(ch, i, weight * source.getSample(ch, index));
            }
        }
    }
    float energy = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        float sum = 0.0f;
        for (int i = 0; i < length; ++i)
        {
            const float value = result.getSample(ch, i);
            sum += value * value;
        }
        energy = std::max(energy, sum);
    }
    result.applyGain(energy < 1.0e-8f ? 1.0f : 0.125f / std::sqrt(energy));
    return result;
}

void AmbienceConvolution::prepare(juce::AudioBuffer<float> room, juce::AudioBuffer<float> plate,
                                  double sourceRate, double sampleRate,
                                  std::size_t maximumBlockSize)
{
    room = prepareResponse(room, sourceRate, sampleRate);
    plate = prepareResponse(plate, sourceRate, sampleRate);
    roomAvailable_ = validResponse(room);
    plateAvailable_ = validResponse(plate);
    const juce::dsp::ProcessSpec spec { sampleRate,
        static_cast<juce::uint32>(std::max<std::size_t>(1, maximumBlockSize)), 2 };
    const auto load = [&](auto& convolver, auto buffer, bool available) {
        if (available)
        {
            // Loading before prepare makes the response ready for the first
            // callback; no asynchronous crossfade or asset IO occurs there.
            convolver.loadImpulseResponse(std::move(buffer), sampleRate,
                juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
            convolver.prepare(spec);
            convolver.reset();
        }
    };
    load(room_, std::move(room), roomAvailable_);
    load(plate_, std::move(plate), plateAvailable_);
}

void AmbienceConvolution::reset() noexcept
{
    if (roomAvailable_) room_.reset();
    if (plateAvailable_) plate_.reset();
}

void AmbienceConvolution::process(float* roomLeft, float* roomRight, float* plateLeft,
                                  float* plateRight, std::size_t frames) noexcept
{
    const auto run = [frames](auto& convolver, bool available, float* left, float* right) {
        if (!available)
        {
            std::fill_n(left, frames, 0.0f);
            std::fill_n(right, frames, 0.0f);
            return;
        }
        float* channels[] { left, right };
        juce::dsp::AudioBlock<float> block(channels, 2, frames);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolver.process(context);
    };
    run(room_, roomAvailable_, roomLeft, roomRight);
    run(plate_, plateAvailable_, plateLeft, plateRight);
}
} // namespace bsl
