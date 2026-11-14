#include "plugin/AmbienceConvolution.h"
#include "audio/AudioEngine.h"
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <new>
#include <string_view>
#include <vector>

namespace {
thread_local bool countAllocations = false;
thread_local std::size_t allocations = 0;
}
void* operator new(std::size_t size)
{
    if (countAllocations) ++allocations;
    if (auto* memory = std::malloc(std::max<std::size_t>(size, 1))) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
int failures = 0;
void check(bool condition, std::string_view message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
using Stream = std::array<std::vector<float>, 4>;
Stream zeros(std::size_t length)
{
    Stream result;
    for (auto& channel : result) channel.resize(length);
    return result;
}
juce::AudioBuffer<float> response(bool plate)
{
    juce::AudioBuffer<float> buffer(2, plate ? 10001 : 5001);
    buffer.clear();
    if (plate)
    {
        buffer.setSample(0, 0, -0.5f);
        buffer.setSample(0, 1025, 0.25f);
        buffer.setSample(0, 10000, 0.125f);
        buffer.setSample(1, 7, 1.0f);
        buffer.setSample(1, 5000, -0.5f);
    }
    else
    {
        buffer.setSample(0, 0, 1.0f);
        buffer.setSample(0, 31, -0.5f);
        buffer.setSample(0, 256, 0.25f);
        buffer.setSample(0, 4999, 0.125f);
        buffer.setSample(1, 0, 0.5f);
        buffer.setSample(1, 127, 0.25f);
        buffer.setSample(1, 4095, -0.75f);
    }
    return buffer;
}
void run(bsl::AmbienceConvolution& processor, Stream& stream, std::size_t blockSize)
{
    for (std::size_t start = 0; start < stream[0].size(); start += blockSize)
    {
        const auto frames = std::min(blockSize, stream[0].size() - start);
        countAllocations = true;
        processor.process(stream[0].data() + start, stream[1].data() + start,
                          stream[2].data() + start, stream[3].data() + start, frames);
        countAllocations = false;
    }
}
float error(const Stream& actual, const Stream& expected)
{
    float maximum = 0.0f;
    for (std::size_t ch = 0; ch < actual.size(); ++ch)
        for (std::size_t i = 0; i < actual[ch].size(); ++i)
        {
            if (!std::isfinite(actual[ch][i])) return 1.0f;
            maximum = std::max(maximum, std::abs(actual[ch][i] - expected[ch][i]));
        }
    return maximum;
}
void testSyntheticConvolution()
{
    const auto room = response(false), plate = response(true);
    const std::array<float, 2> gains { 0.125f / std::sqrt(1.328125f),
                                       0.125f / std::sqrt(1.25f) };
    for (const std::size_t blockSize : { 1u, 17u, 64u, 511u, 1024u })
    {
        bsl::AmbienceConvolution processor;
        processor.prepare(room, plate, 48000, 48000, blockSize);
        check(processor.isAvailable(), "synthetic responses load before the first callback");
        auto actual = zeros(18000), expected = zeros(18000);
        for (std::size_t ch = 0; ch < 4; ++ch)
            for (const std::size_t offset : { 0u, 13u, 1023u, 5000u })
            {
                const float input = (ch % 2 == 0 ? 0.8f : -0.3f)
                    * (offset == 1023 ? -0.5f : 1.0f);
                actual[ch][offset] = input;
                const auto& ir = ch < 2 ? room : plate;
                for (int tap = 0; tap < ir.getNumSamples(); ++tap)
                    expected[ch][offset + static_cast<std::size_t>(tap)] += input
                        * ir.getSample(static_cast<int>(ch % 2), tap) * gains[ch / 2];
            }
        allocations = 0;
        run(processor, actual, blockSize);
        check(allocations == 0, "processing does not allocate through operator new");
        check(error(actual, expected) < 2.0e-6f,
              "long convolution matches direct FIR, including first sample and stereo separation");
        processor.reset();
        auto silent = zeros(15000);
        run(processor, silent, blockSize);
        check(error(silent, zeros(silent[0].size())) < 1.0e-7f,
              "reset clears all partition tails without a delayed return");
    }
}
void testUnavailableResponses()
{
    bsl::AmbienceConvolution processor;
    processor.prepare(response(false), {}, 48000, 48000, 64);
    check(!processor.isAvailable(), "a missing Plate response is reported");
    auto stream = zeros(64);
    stream[0][0] = stream[2][0] = 1.0f;
    run(processor, stream, 64);
    check(stream[0][0] > 0.0f, "a valid Room remains usable if Plate is missing");
    check(std::all_of(stream[2].begin(), stream[2].end(), [](float x) { return x == 0.0f; }),
          "missing ambience has a silent wet return");
    processor.prepare({}, {}, 48000, 48000, 64);
    stream = zeros(64);
    stream[0][0] = stream[2][0] = 1.0f;
    run(processor, stream, 64);
    check(error(stream, zeros(64)) == 0.0f, "reload with missing assets cannot retain old wet audio");
}
void testNativeRateNormalisation()
{
    auto ir = response(false);
    const auto prepared = bsl::AmbienceConvolution::prepareResponse(ir, 48000, 48000);
    check(prepared.getNumSamples() == ir.getNumSamples(), "native-rate IR length is preserved");
    const auto gain = 0.125f / std::sqrt(1.328125f);
    check(std::abs(prepared.getSample(0, 0) - gain) < 1.0e-7f,
          "normalisation uses the highest channel energy");
    check(std::abs(prepared.getSample(1, 127) - 0.25f * gain) < 1.0e-7f,
          "both channels share one normalisation gain");
    ir.clear();
    ir.setSample(0, 0, 1.0e-6f);
    const auto tiny = bsl::AmbienceConvolution::prepareResponse(ir, 48000, 48000);
    check(tiny.getSample(0, 0) == 1.0e-6f, "near-silent IR energy is not amplified");
}
void testNativeUpsamplingEdges()
{
    juce::AudioBuffer<float> source(2, 2);
    source.clear();
    source.setSample(0, 0, 1.0f);
    source.setSample(1, 0, 0.5f);
    const auto result = bsl::AmbienceConvolution::prepareResponse(source, 48000, 96000);
    check(result.getNumSamples() == 4, "upsampling preserves the native duration");
    // Native truncation toward zero counts tap zero twice at half positions.
    // These four analytic values distinguish it from a floor-index resampler
    // and distinguish normalization before resampling from normalization after it.
    const float firstHalf = 4.0f / juce::MathConstants<float>::pi;
    const float lastHalf = -4.0f / (3.0f * juce::MathConstants<float>::pi);
    const float gain = 0.125f / std::sqrt(1.0f + firstHalf * firstHalf + lastHalf * lastHalf);
    const std::array<float, 4> expected { gain, firstHalf * gain, 0.0f, lastHalf * gain };
    for (int i = 0; i < 4; ++i)
    {
        check(std::abs(result.getSample(0, i) - expected[static_cast<std::size_t>(i)]) < 2.0e-7f,
              "upsampled edge taps and post-resampling energy match the native formula");
        check(std::abs(result.getSample(1, i) - 0.5f * expected[static_cast<std::size_t>(i)]) < 2.0e-7f,
              "resampling preserves stereo levels through shared normalization");
    }
}

void testEngineRoutingWithConvolution()
{
    const auto room = response(false), plate = response(true);
    const std::array<float, 2> irGain { 0.125f / std::sqrt(1.328125f),
                                       0.125f / std::sqrt(1.25f) };
    const std::array<float, 2> roomSend { 0.4f, 0.6f }, plateSend { 0.2f, 0.8f };
    const std::array<std::size_t, 2> noteTimes { 0, 19 };
    constexpr std::size_t total = 12000;
    for (const auto routing : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
    for (const bool aux : { false, true })
    for (const bool masterEnabled : { false, true })
    for (const std::size_t blockSize : { 1u, 17u, 511u, 16384u })
    {
        bsl::AmbienceConvolution ambience;
        ambience.prepare(room, plate, 48000, 48000, 64);
        bsl::AudioEngine engine;
        engine.setAmbienceProcessor(&ambience);
        engine.prepare(48000, 64);
        engine.setRoutingMode(routing);
        engine.setModuleEnables(aux, masterEnabled);
        bsl::MasterPlaybackSettings master;
        master.filterEnabled = false;
        master.gain = 0.25f;
        engine.setMasterSettings(master);
        bsl::AudioBlock silence(34, std::vector<float>(4800));
        engine.process(silence, 4800);
        bsl::AudioBlock expected(34, std::vector<float>(total));
        for (std::size_t pad = 0; pad < 2; ++pad)
        {
            const float sample = pad == 0 ? 1.0f : 0.25f;
            // A silent endpoint at twice the host rate feeds one exact impulse
            // through the native normalized sampler, isolating AUX routing.
            engine.setPadSample(pad, { 96000, { { sample, 0.0f } } });
            bsl::PadPlaybackSettings settings;
            settings.lengthEnvelopeEnabled = false;
            settings.filterEnabled = settings.highPassEnabled = false;
            settings.room = roomSend[pad];
            settings.reverb = plateSend[pad];
            engine.setPadSettings(pad, settings);
            const float input = sample * std::sqrt(0.5f);
            const auto dryChannel = routing == bsl::RoutingMode::stereo ? 0 : 2 + 2 * pad;
            const float dryGain = routing == bsl::RoutingMode::stereo ? master.gain : 1.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                expected[dryChannel + static_cast<std::size_t>(ch)][noteTimes[pad]] += input * dryGain;
                for (int tap = 0; tap < room.getNumSamples(); ++tap)
                    expected[static_cast<std::size_t>(ch)][noteTimes[pad] + static_cast<std::size_t>(tap)]
                        += input * roomSend[pad] * room.getSample(ch, tap) * irGain[0]
                            * 1.4125375f * master.gain * (aux ? 1.0f : 0.0f);
                for (int tap = 0; tap < plate.getNumSamples(); ++tap)
                    expected[static_cast<std::size_t>(ch)][noteTimes[pad] + static_cast<std::size_t>(tap)]
                        += input * plateSend[pad] * plate.getSample(ch, tap) * irGain[1]
                            * 1.9952623f * master.gain * (aux ? 1.0f : 0.0f);
            }
        }
        bsl::AudioBlock block(34, std::vector<float>(std::min(blockSize, total)));
        float maximumError = 0.0f;
        allocations = 0;
        for (std::size_t start = 0; start < total; start += blockSize)
        {
            const auto frames = std::min(blockSize, total - start);
            std::array<bsl::TimedNoteEvent, 2> events;
            std::size_t eventCount = 0;
            for (std::size_t pad = 0; pad < noteTimes.size(); ++pad)
                if (noteTimes[pad] >= start && noteTimes[pad] < start + frames)
                    events[eventCount++] = { noteTimes[pad] - start,
                        static_cast<std::uint8_t>(36 + pad), 1.0f, true, 0 };
            countAllocations = true;
            engine.process(block, frames, { events.data(), eventCount });
            countAllocations = false;
            for (std::size_t ch = 0; ch < block.size(); ++ch)
                for (std::size_t i = 0; i < frames; ++i)
                    maximumError = !std::isfinite(block[ch][i]) ? 1.0f
                        : std::max(maximumError, std::abs(block[ch][i] - expected[ch][start + i]));
        }
        check(maximumError < 2.0e-6f,
              "AUX gates convolution returns independently of Master and Stereo/Multi routing");
        check(allocations == 0, "integrated convolution handles split and oversized callbacks without allocation");
        engine.reset();
        engine.process(block, block[0].size());
        check(std::all_of(block.begin(), block.end(), [](const auto& channel) {
            return std::all_of(channel.begin(), channel.end(), [](float value) { return value == 0.0f; });
        }), "engine reset clears shared convolution tails");
    }
}

void testPrivateFactory(const juce::File& directory)
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Stream reference;
        for (const std::size_t blockSize : { 17u, 511u })
        {
            bsl::AmbienceConvolution processor;
            if (!processor.prepareFromFactory(directory, rate, blockSize))
            {
                check(false, "private Room and Plate hash/format checks pass");
                return;
            }
            auto stream = zeros(static_cast<std::size_t>(rate * 2.3));
            stream[0][0] = stream[1][0] = stream[2][0] = stream[3][0] = 1.0f;
            run(processor, stream, blockSize);
            for (std::size_t ch = 0; ch < 4; ++ch)
            {
                const auto start = static_cast<std::size_t>(rate * (ch < 2 ? 0.1 : 1.0));
                double energy = 0.0;
                for (std::size_t i = start; i < stream[ch].size(); ++i)
                    energy += static_cast<double>(stream[ch][i]) * stream[ch][i];
                check(std::isfinite(energy) && energy > 1.0e-10,
                      "private impulse produces a finite long wet tail in each channel");
            }
            if (reference[0].empty()) reference = std::move(stream);
            else check(error(stream, reference) < 2.0e-6f,
                       "private wet output is invariant to callback size");
        }
    }
}
} // namespace
// Private, local-only parity renderer. Inputs are never part of a cloud build.
int renderDrive(int argc, char** argv)
{
    if (argc != 7) return 2;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::FileInputStream(juce::File(argv[2])), true));
    if (!reader || reader->numChannels != 2 || reader->lengthInSamples > 10000000) return 3;
    const int frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> audio(2, frames);
    if (!reader->read(&audio, 0, frames, 0, true, true)) return 4;
    const auto ir = bsl::AmbienceConvolution::loadDriveResponse(juce::File(argv[3]), reader->sampleRate);
    if (ir.getNumChannels() != 2 || ir.getNumSamples() == 0) return 5;
    bsl::AuxDistortion processor;
    processor.prepare(reader->sampleRate);
    std::array<std::vector<float>, 2> response;
    for (int channel = 0; channel < 2; ++channel)
        response[channel].assign(ir.getReadPointer(channel), ir.getReadPointer(channel) + ir.getNumSamples());
    processor.setResponse(std::move(response));
    const auto drive = std::stof(argv[5]), send = std::stof(argv[6]);
    audio.applyGain(std::pow(send, 12.0f / 6.020599913279624f));
    for (int i = 0; i < frames; i += 128)
        processor.process(audio.getWritePointer(0) + i, audio.getWritePointer(1) + i,
                          static_cast<std::size_t>(std::min(128, frames - i)), drive);
    std::ofstream output(argv[4], std::ios::binary);
    for (int i = 0; i < frames; ++i)
    {
        const float pair[] {audio.getSample(0, i) * 1.995262315f, audio.getSample(1, i) * 1.995262315f};
        output.write(reinterpret_cast<const char*>(pair), sizeof(pair));
    }
    return output ? 0 : 6;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--drive-render") return renderDrive(argc, argv);
    juce::ScopedJuceInitialiser_GUI initialiser;
    testSyntheticConvolution();
    testUnavailableResponses();
    testNativeRateNormalisation();
    testNativeUpsamplingEdges();
    testEngineRoutingWithConvolution();
    if (argc > 1) testPrivateFactory(juce::File(argv[1]));
    std::cout << "Ambience tests: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
