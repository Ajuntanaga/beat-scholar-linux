#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <new>
#include <string_view>

namespace {
bool countAudioAllocations = false;
std::size_t audioAllocations = 0;
}

void* operator new(std::size_t size)
{
    if (countAudioAllocations) ++audioAllocations;
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
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bsl::AudioBlock makeBlock(std::size_t channels, std::size_t samples)
{
    return bsl::AudioBlock(channels, std::vector<float>(samples));
}

std::vector<float> renderMasterImpulse(double sampleRate, std::size_t blockSize,
                                       bsl::MasterPlaybackSettings settings)
{
    bsl::AudioEngine engine;
    engine.prepare(sampleRate);
    engine.setMasterSettings(settings);
    engine.setPadSample(0, { sampleRate, { { 1.0f } } });
    bsl::PadPlaybackSettings pad;
    pad.delay = settings.delay > 0.0f ? 1.0f : 0.0f;
    engine.setPadSettings(0, pad);
    engine.noteOn(36, 1.0f);
    const auto totalFrames = static_cast<std::size_t>(sampleRate * 0.45);
    std::vector<float> rendered;
    rendered.reserve(totalFrames);
    while (rendered.size() < totalFrames)
    {
        const auto count = std::min(blockSize, totalFrames - rendered.size());
        auto block = makeBlock(34, count);
        engine.process(block, count);
        rendered.insert(rendered.end(), block[0].begin(), block[0].end());
    }
    return rendered;
}

void testPadMetersTrackRenderedAudio()
{
    bsl::AudioEngine engine;
    engine.prepare(48000, 4);
    engine.setPadSample(0, {48000, {{1.0f, -0.5f}}});
    engine.setPadSample(1, {48000, {{0.2f}}});
    bsl::PadPlaybackSettings pad;
    pad.pan = -1.0f;
    engine.setPadSettings(0, pad);
    auto output = makeBlock(34, 8);
    const std::array<bsl::TimedNoteEvent, 2> events {{{1, 36, 1.0f, true, 0}, {5, 37, 1.0f, true, 0}}};
    engine.process(output, 8, events);
    check(std::abs(engine.padPeaks()[0] - 1.0f) < 1e-6f,
          "pad meter retains early peak across event segments and prepared chunks");
    check(std::abs(engine.padPeaks()[1] - 0.2f * 0.70710678f) < 1e-6f,
          "pad meters remain independent");
    check(engine.padPeaks()[2] == 0.0f, "unplayed pad meter is silent");
    engine.process(output, 8);
    check(engine.padPeaks()[0] == 0.0f && engine.padPeaks()[1] == 0.0f,
          "pad meter clears when sample ends");
    pad.muted = true;
    engine.setPadSettings(0, pad);
    engine.noteOn(36, 1.0f);
    engine.process(output, 8);
    check(engine.padPeaks()[0] == 0.0f, "muted pad reports actual silence");
    pad.muted = false;
    engine.setPadSettings(0, pad);
    engine.setRoutingMode(bsl::RoutingMode::multi);
    engine.noteOn(36, 1.0f);
    engine.process(output, 8);
    check(engine.padPeaks()[0] == 1.0f && output[0][0] == 0.0f,
          "multi-output pad metering does not invent master output");
}

void testStereoPlaybackIsSampleExactAtUnity()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 0.5f, -0.25f } } });
    engine.noteOn(36, 1.0f);

    auto output = makeBlock(34, 4);
    engine.process(output, 4);
    constexpr float centreGain = 0.7071067811865475f;
    check(std::abs(output[0][0] - centreGain) < 1.0e-6f, "main left receives pad");
    check(std::abs(output[1][1] - 0.5f * centreGain) < 1.0e-6f, "main right is sample accurate");
    check(output[2][0] == 0.0f, "stereo mode leaves aux buses silent");
}

void testMultiModeRoutesEachPadToItsOwnAux()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setRoutingMode(bsl::RoutingMode::multi);
    engine.setPadSample(3, { 48000.0, { { 0.75f } } });
    engine.noteOn(39, 1.0f);

    auto output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] == 0.0f && output[1][0] == 0.0f, "multi mode bypasses main bus");
    check(std::abs(output[8][0] - 0.75f * 0.7071067811865475f) < 1.0e-6f,
          "pad four routes to auxiliary bus four");
    check(output[6][0] == 0.0f && output[10][0] == 0.0f, "neighbouring aux buses remain silent");
}

void testAllSixteenPadsKeepTheirThirtyFourChannelRouting()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setRoutingMode(bsl::RoutingMode::multi);
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        engine.setPadSample(pad, { 48000.0, { { static_cast<float>(pad + 1) / 16.0f } } });
        engine.noteOn(static_cast<std::uint8_t>(36 + pad), 1.0f);
    }
    auto output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] == 0.0f && output[1][0] == 0.0f,
          "all multi-output pads leave the main bus silent");
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        const auto channel = 2 + pad * 2;
        const float expected = static_cast<float>(pad + 1) / 16.0f * 0.7071067811865475f;
        check(std::abs(output[channel][0] - expected) < 1.0e-6f
                  && std::abs(output[channel + 1][0] - expected) < 1.0e-6f,
              "each pad reaches only its assigned stereo auxiliary pair");
    }
}

void testRetriggerRestartsOneShot()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 0.25f } } });
    auto output = makeBlock(34, 1);
    engine.noteOn(36, 1.0f);
    engine.process(output, 1);
    output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] < 0.2f, "voice advances through its sample");
    engine.noteOn(36, 1.0f);
    output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] > 0.7f, "retrigger restarts the sample");
}

void testScheduledTriggerStartsAtExactSampleOffset()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 0.5f, 0.25f } } });
    const std::array events { bsl::TimedNoteEvent { 3, 36, 1.0f, true } };
    auto output = makeBlock(34, 7);
    engine.process(output, 7, events);
    check(output[0][0] == 0.0f && output[0][2] == 0.0f,
          "scheduled note leaves preceding samples silent");
    check(output[0][3] > 0.7f, "scheduled note starts at its exact sample offset");
    check(output[0][4] > 0.3f, "scheduled note advances after its start offset");
}

void testGatedPadStopsOnScheduledNoteOff()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } } });
    bsl::PadPlaybackSettings settings;
    settings.oneShot = false;
    engine.setPadSettings(0, settings);
    const std::array events {
        bsl::TimedNoteEvent { 0, 36, 1.0f, true },
        bsl::TimedNoteEvent { 2, 36, 0.0f, false },
    };
    auto output = makeBlock(34, 5);
    engine.process(output, 5, events);
    check(output[0][0] > 0.7f && output[0][1] > 0.7f, "gated pad plays before note-off");
    check(output[0][2] == 0.0f && output[0][4] == 0.0f,
          "gated pad stops exactly at note-off");
}

void testSequencedGatesArePerVoiceAcrossCallbacks()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0, 4);
    engine.setPadSample(0, { 48000.0, { std::vector<float>(20, 0.5f) } });
    bsl::PadPlaybackSettings settings;
    settings.oneShot = false;
    engine.setPadSettings(0, settings);
    const std::array events {
        bsl::TimedNoteEvent { 0, 36, 1.0f, true, 4 },
        bsl::TimedNoteEvent { 2, 36, 1.0f, true, 6 },
    };
    auto first = makeBlock(34, 3);
    engine.process(first, 3, events);
    auto second = makeBlock(34, 7);
    engine.process(second, 7);
    check(second[0][0] > 0.7f, "overlapping gated voices both render across callbacks");
    check(second[0][1] > 0.35f && second[0][1] < 0.36f,
          "an earlier gate does not terminate a later voice on the same pad");
    check(second[0][4] > 0.35f && second[0][5] == 0.0f,
          "each voice stops on its own gate across prepared scratch chunks");
}

void testPitchAndDampAlterPlaybackWithoutAllocationContractChanges()
{
    bsl::AudioEngine engine;
    engine.prepare(1000.0);
    std::vector<float> source(600);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = 1.0f - static_cast<float>(index) / 1000.0f;
    engine.setPadSample(0, { 1000.0, { source } });
    bsl::PadPlaybackSettings settings;
    settings.pitchSemitones = 12.0f;
    settings.damp = 0.5f;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, 300);
    engine.process(output, 300);
    check(output[0][0] > output[0][1], "one octave pitch-up advances through source frames twice as fast");
    check(output[0][100] > 0.0f && output[0][100] < output[0][0] * 0.75f,
          "damp applies the measured per-voice length envelope without truncating early");
}

void testPadLowPassAndCompressionChangeDrySignal()
{
    bsl::AudioEngine dry;
    bsl::AudioEngine processed;
    dry.prepare(48000.0);
    processed.prepare(48000.0);
    const bsl::SampleData source { 48000.0, { { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f } } };
    dry.setPadSample(0, source);
    processed.setPadSample(0, source);
    bsl::PadPlaybackSettings settings;
    settings.cutoff = 0.08f;
    settings.compression = 0.9f;
    processed.setPadSettings(0, settings);
    dry.noteOn(36, 1.0f);
    processed.noteOn(36, 1.0f);
    auto dryOut = makeBlock(34, 6);
    auto processedOut = makeBlock(34, 6);
    dry.process(dryOut, 6);
    processed.process(processedOut, 6);
    check(std::abs(processedOut[0][1]) < std::abs(dryOut[0][1]),
          "pad low-pass and compression attenuate a full-scale alternating signal");
}

void testRoomAndReverbProduceTailAfterSampleEnds()
{
    bsl::AudioEngine engine;
    engine.prepare(1000.0);
    engine.setPadSample(0, { 1000.0, { { 1.0f } } });
    bsl::PadPlaybackSettings settings;
    settings.room = 1.0f;
    settings.reverb = 1.0f;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, 400);
    engine.process(output, 400);
    check(output[0][0] > 0.7f, "impulse begins dry");
    check(std::abs(output[0][80]) > 0.01f, "room send produces a short tail");
    check(std::abs(output[0][330]) > 0.01f, "reverb send produces a long tail");
}

void testMasterProcessingAffectsStereoButIsBypassedInMultiMode()
{
    bsl::MasterPlaybackSettings master;
    master.gain = 0.25f;
    master.crush = 0.8f;

    bsl::AudioEngine stereo;
    stereo.prepare(48000.0);
    stereo.setMasterSettings(master);
    stereo.setPadSample(0, { 48000.0, { { 0.83f } } });
    stereo.noteOn(36, 1.0f);
    auto stereoOut = makeBlock(34, 1);
    stereo.process(stereoOut, 1);

    bsl::AudioEngine multi;
    multi.prepare(48000.0);
    multi.setRoutingMode(bsl::RoutingMode::multi);
    multi.setMasterSettings(master);
    multi.setPadSample(0, { 48000.0, { { 0.83f } } });
    multi.noteOn(36, 1.0f);
    auto multiOut = makeBlock(34, 1);
    multi.process(multiOut, 1);
    check(stereoOut[0][0] < 0.25f, "master gain affects stereo output");
    check(multiOut[2][0] > 0.5f, "multi output bypasses master processing");
}

void testMeasuredMasterCompressionCalibration()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    bsl::MasterPlaybackSettings master;
    master.compression = 0.8f;
    engine.setMasterSettings(master);
    engine.setPadSample(0, { 48000.0, { { 1.0f } } });
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, 1);
    engine.process(output, 1);
    const float expected = 0.7071067811865475f * 1.3925f;
    check(std::abs(output[0][0] - expected) < 1.0e-5f,
          "measured compressor amount maps to calibrated makeup gain");
}

void testMeasuredMasterCrushUsesSeventeenFrameHoldAtPointEight()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    bsl::MasterPlaybackSettings master;
    master.crush = 0.8f;
    engine.setMasterSettings(master);
    std::vector<float> ramp(40);
    for (std::size_t index = 0; index < ramp.size(); ++index)
        ramp[index] = static_cast<float>(index) / 40.0f;
    engine.setPadSample(0, { 48000.0, { ramp } });
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, ramp.size());
    engine.process(output, ramp.size());
    check(output[0][0] == output[0][16], "measured crush holds one sample for seventeen frames");
    check(output[0][17] > output[0][16], "measured crush refreshes on frame seventeen");
    check(std::abs(output[0][17] - 0.25f) < 1.0e-6f,
          "measured crush point eight rounds the held signal to three-bit depth");
}

void testTwoFactoryLayersMixContinuously()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadLayerSample(0, 0, { 48000.0, { { 1.0f } } });
    engine.setPadLayerSample(0, 1, { 48000.0, { { -1.0f } } });

    bsl::PadPlaybackSettings settings;
    settings.layerMix = 0.25f;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, 1);
    engine.process(output, 1);
    const float expected = 0.5f * 0.7071067811865475f;
    check(std::abs(output[0][0] - expected) < 1.0e-6f,
          "layer mix blends both factory samples continuously");

    engine.reset();
    settings.layerMix = 1.0f;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] < -0.7f, "full layer mix selects the second layer");
}

void testPadBypassesAreDryInvariantAndDriveControlsStayDistinct()
{
    const bsl::SampleData source { 48000.0, { { 0.2f, -0.4f, 0.7f, -0.8f } } };
    const auto render = [&source](bsl::PadPlaybackSettings settings) {
        bsl::AudioEngine engine;
        engine.prepare(48000.0);
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, source);
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, source.length());
        engine.process(output, source.length());
        return output[0];
    };

    const auto dry = render({});
    bsl::PadPlaybackSettings bypassed;
    bypassed.filterEnabled = false;
    bypassed.cutoff = 0.0f;
    bypassed.resonance = 1.0f;
    bypassed.driveEnabled = false;
    bypassed.driveGainDb = 60.0f;
    bypassed.distortionMix = 1.0f;
    check(render(bypassed) == dry, "disabled pad filter and drive are sample-exact bypasses");

    bypassed.driveEnabled = true;
    bypassed.distortionMix = 0.0f;
    check(render(bypassed) == dry, "zero parallel distortion mix preserves the dry signal");

    bsl::PadPlaybackSettings lowDrive;
    lowDrive.driveEnabled = true;
    lowDrive.driveGainDb = -24.0f;
    lowDrive.distortionMix = 1.0f;
    bsl::PadPlaybackSettings highDrive = lowDrive;
    highDrive.driveGainDb = 24.0f;
    const auto low = render(lowDrive);
    const auto high = render(highDrive);
    check(std::abs(low[2] - high[2]) > 0.05f,
          "drive gain changes saturation independently of parallel mix");

    bsl::PadPlaybackSettings halfWet = highDrive;
    halfWet.distortionMix = 0.5f;
    const auto half = render(halfWet);
    check(std::abs(half[2] - (dry[2] + high[2]) * 0.5f) < 1.0e-6f,
          "parallel distortion mix interpolates dry and driven signals");
}

void testPadFilterEnableAndNormalizedAttack()
{
    const bsl::SampleData alternating {
        48000.0, { { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f } }
    };
    const auto renderFilter = [&alternating](bool enabled) {
        bsl::AudioEngine engine;
        engine.prepare(48000.0);
        bsl::PadPlaybackSettings settings;
        settings.filterEnabled = enabled;
        settings.cutoff = 0.0f;
        settings.resonance = 1.0f;
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, alternating);
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, alternating.length());
        engine.process(output, alternating.length());
        return output[0];
    };
    const auto bypassed = renderFilter(false);
    const auto filtered = renderFilter(true);
    check(std::abs(bypassed[1]) > 0.7f && std::abs(filtered[1]) < 0.01f,
          "pad filter enable gates cutoff and resonance processing");

    for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        bsl::AudioEngine engine;
        engine.prepare(sampleRate);
        bsl::PadPlaybackSettings settings;
        settings.attackNormalized = 0.01f; // Ten milliseconds.
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { sampleRate, { std::vector<float>(1024, 1.0f) } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 1);
        engine.process(output, 1);
        const auto attackFrames = static_cast<std::size_t>(
            static_cast<double>(settings.attackNormalized) * sampleRate);
        const float expected = 0.7071067811865475f / static_cast<float>(attackFrames);
        check(std::abs(output[0][0] - expected) < 1.0e-7f,
              "normalized attack maps consistently at each supported sample rate");
    }


    bsl::AudioEngine secondsEngine;
    secondsEngine.prepare(48000.0);
    bsl::PadPlaybackSettings secondsSettings;
    secondsSettings.attackSeconds = 0.001f;
    secondsSettings.attackNormalized = 1.0f;
    secondsEngine.setPadSettings(0, secondsSettings);
    secondsEngine.setPadSample(0, { 48000.0, { std::vector<float>(64, 1.0f) } });
    secondsEngine.noteOn(36, 1.0f);
    auto secondsOutput = makeBlock(34, 1);
    secondsEngine.process(secondsOutput, 1);
    const auto secondsFrames = static_cast<std::size_t>(secondsSettings.attackSeconds * 48000.0);
    check(std::abs(secondsOutput[0][0] - 0.7071067811865475f
                                           / static_cast<float>(secondsFrames)) < 1.0e-7f,
          "explicit attack seconds take precedence over the normalized compatibility value");
}

void testRemainingPadEffectsReachTheirEndpointsIndependently()
{
    const std::vector<float> ramp = [] {
        std::vector<float> result(48);
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = static_cast<float>(index + 1) / 64.0f;
        return result;
    }();
    const auto render = [&ramp](bsl::PadPlaybackSettings settings) {
        bsl::AudioEngine engine;
        engine.prepare(48000.0);
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { 48000.0, { ramp } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, ramp.size());
        engine.process(output, ramp.size());
        return output[0];
    };
    const auto dry = render({});

    bsl::PadPlaybackSettings fine;
    fine.fineCents = 100.0f;
    const auto fineOut = render(fine);
    check(std::abs(fineOut[17] - dry[17]) > 0.005f,
          "fine pitch reaches its positive endpoint independently");

    bsl::PadPlaybackSettings highPass;
    highPass.highPass = 1.0f;
    const auto highPassed = render(highPass);
    check(std::abs(highPassed[20]) < std::abs(dry[20]) * 0.2f,
          "pad high-pass reaches its maximum cutoff endpoint");

    bsl::PadPlaybackSettings compression;
    compression.compression = 1.0f;
    const auto compressed = render(compression);
    check(compressed[10] > dry[10], "pad compression endpoint changes gain independently");

    bsl::PadPlaybackSettings crush;
    crush.crush = 1.0f;
    const auto crushed = render(crush);
    check(crushed[0] == 0.0f && crushed[0] == crushed[20],
          "pad crush endpoint applies one-bit rounding and holds twenty-one frames");
}

void testMasterBypassesAndDistinctEffectsAcrossRatesAndBlocks()
{
    const std::array sampleRates { 44100.0, 48000.0, 96000.0 };
    const std::array blockSizes { std::size_t { 17 }, std::size_t { 64 }, std::size_t { 511 } };
    for (std::size_t test = 0; test < sampleRates.size(); ++test)
    {
        const auto sampleRate = sampleRates[test];
        const auto blockSize = blockSizes[test];
        const auto dry = renderMasterImpulse(sampleRate, blockSize, {});

        bsl::MasterPlaybackSettings bypassed;
        bypassed.filterEnabled = false;
        bypassed.cutoff = 0.0f;
        bypassed.resonance = 1.0f;
        check(renderMasterImpulse(sampleRate, blockSize, bypassed) == dry,
              "disabled master filter is a sample-exact bypass");

        bsl::MasterPlaybackSettings room;
        room.room = 1.0f;
        const auto roomOut = renderMasterImpulse(sampleRate, blockSize, room);
        bsl::MasterPlaybackSettings delay;
        delay.delay = 1.0f;
        const auto delayOut = renderMasterImpulse(sampleRate, blockSize, delay);
        bsl::MasterPlaybackSettings reverb;
        reverb.reverb = 1.0f;
        const auto reverbOut = renderMasterImpulse(sampleRate, blockSize, reverb);
        const auto roomFrame = static_cast<std::size_t>(sampleRate * 0.065);
        const auto delayFrame = static_cast<std::size_t>(sampleRate * 0.25);
        const auto reverbFrame = static_cast<std::size_t>(sampleRate * 0.37);
        check(std::abs(roomOut[roomFrame]) > 0.3f && delayOut[roomFrame] == 0.0f
                  && reverbOut[roomFrame] == 0.0f,
              "master room has its own short return");
        check(std::abs(delayOut[delayFrame]) > 0.4f && roomOut[delayFrame] == 0.0f
                  && reverbOut[delayFrame] == 0.0f,
              "master delay has its own quarter-second return");
        check(std::abs(reverbOut[reverbFrame]) > 0.3f && roomOut[reverbFrame] == 0.0f
                  && delayOut[reverbFrame] == 0.0f,
              "master reverb has its own long return");
    }

    bsl::MasterPlaybackSettings distorted;
    distorted.distortion = 1.0f;
    const auto dry = renderMasterImpulse(48000.0, 127, {});
    const auto wet = renderMasterImpulse(48000.0, 127, distorted);
    check(std::abs(dry[0] - wet[0]) > 0.1f,
          "master distortion is routed independently from delay and ambience");
}

void testMuteSoloAndPanGainEndpoints()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f } } });
    engine.setPadSample(1, { 48000.0, { { 0.5f } } });
    bsl::PadPlaybackSettings first;
    first.solo = true;
    first.pan = -1.0f;
    first.gain = 2.0f;
    engine.setPadSettings(0, first);
    engine.noteOn(36, 1.0f);
    engine.noteOn(37, 1.0f);
    auto output = makeBlock(34, 1);
    engine.process(output, 1);
    check(std::abs(output[0][0] - 2.0f) < 1.0e-6f && output[1][0] == 0.0f,
          "solo suppresses other pads while pan and gain reach their endpoints");

    engine.reset();
    first.solo = false;
    first.muted = true;
    engine.setPadSettings(0, first);
    engine.noteOn(36, 1.0f);
    engine.noteOn(37, 1.0f);
    output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] > 0.3f && output[0][0] < 0.4f,
          "mute suppresses only its pad");
}

void testMutedAndNonSoloVoicesDoNotReplayStaleNotes()
{
    bsl::AudioEngine muted;
    muted.prepare(48000.0);
    muted.setPadSample(0, { 48000.0, { { 1.0f, 0.8f, 0.6f } } });
    bsl::PadPlaybackSettings mutedSettings;
    mutedSettings.muted = true;
    muted.setPadSettings(0, mutedSettings);
    muted.noteOn(36, 1.0f);
    auto hidden = makeBlock(34, 3);
    muted.process(hidden, 3);
    mutedSettings.muted = false;
    muted.setPadSettings(0, mutedSettings);
    auto revealed = makeBlock(34, 1);
    muted.process(revealed, 1);
    check(revealed[0][0] == 0.0f,
          "a note that elapsed while muted is not replayed after unmute");

    bsl::AudioEngine soloed;
    soloed.prepare(48000.0);
    soloed.setPadSample(0, { 48000.0, { { 1.0f, 0.8f, 0.6f } } });
    soloed.setPadSample(1, { 48000.0, { { 0.5f, 0.4f, 0.3f } } });
    bsl::PadPlaybackSettings soloSettings;
    soloSettings.solo = true;
    soloed.setPadSettings(1, soloSettings);
    soloed.noteOn(36, 1.0f);
    soloed.noteOn(37, 1.0f);
    hidden = makeBlock(34, 3);
    soloed.process(hidden, 3);
    soloSettings.solo = false;
    soloed.setPadSettings(1, soloSettings);
    revealed = makeBlock(34, 1);
    soloed.process(revealed, 1);
    check(revealed[0][0] == 0.0f,
          "a non-solo note advances and retires instead of replaying after solo clears");
}

void testRoutingTransitionsDiscardFrozenMasterTail()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    bsl::MasterPlaybackSettings master;
    master.reverb = 1.0f;
    engine.setMasterSettings(master);
    engine.setPadSample(0, { 48000.0, { { 1.0f } } });
    engine.noteOn(36, 1.0f);
    auto stereo = makeBlock(34, 100);
    engine.process(stereo, 100);

    engine.setRoutingMode(bsl::RoutingMode::multi);
    auto multi = makeBlock(34, 64);
    engine.process(multi, 64);
    engine.setRoutingMode(bsl::RoutingMode::stereo);
    stereo = makeBlock(34, 18000);
    engine.process(stereo, 18000);
    const auto maximum = *std::max_element(stereo[0].begin(), stereo[0].end(),
                                           [](float left, float right) {
                                               return std::abs(left) < std::abs(right);
                                           });
    check(maximum == 0.0f,
          "multi-to-stereo transition cannot resume a master tail frozen in multi mode");
}

void testAudioCallbacksDoNotAllocateOnFirstOrLargerBlock()
{
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { std::vector<float>(16384, 0.5f) } });
    auto output = makeBlock(34, 16384);
    for (const std::size_t count : { 64, 17, 511, 16384 })
    {
        engine.reset();
        engine.noteOn(36, 1.0f);
        audioAllocations = 0;
        countAudioAllocations = true;
        engine.process(output, count);
        countAudioAllocations = false;
        check(audioAllocations == 0, "first and larger audio callbacks allocate no heap memory");
        check(output[0][count - 1] > 0.3f, "allocation-free callbacks retain their complete audio output");
    }
}

} // namespace

int main()
{
    testPadMetersTrackRenderedAudio();
    testStereoPlaybackIsSampleExactAtUnity();
    testMultiModeRoutesEachPadToItsOwnAux();
    testAllSixteenPadsKeepTheirThirtyFourChannelRouting();
    testRetriggerRestartsOneShot();
    testScheduledTriggerStartsAtExactSampleOffset();
    testGatedPadStopsOnScheduledNoteOff();
    testSequencedGatesArePerVoiceAcrossCallbacks();
    testPitchAndDampAlterPlaybackWithoutAllocationContractChanges();
    testPadLowPassAndCompressionChangeDrySignal();
    testRoomAndReverbProduceTailAfterSampleEnds();
    testMasterProcessingAffectsStereoButIsBypassedInMultiMode();
    testMeasuredMasterCompressionCalibration();
    testMeasuredMasterCrushUsesSeventeenFrameHoldAtPointEight();
    testTwoFactoryLayersMixContinuously();
    testPadBypassesAreDryInvariantAndDriveControlsStayDistinct();
    testPadFilterEnableAndNormalizedAttack();
    testRemainingPadEffectsReachTheirEndpointsIndependently();
    testMasterBypassesAndDistinctEffectsAcrossRatesAndBlocks();
    testMuteSoloAndPanGainEndpoints();
    testMutedAndNonSoloVoicesDoNotReplayStaleNotes();
    testRoutingTransitionsDiscardFrozenMasterTail();
    testAudioCallbacksDoNotAllocateOnFirstOrLargerBlock();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All audio tests passed\n";
    return EXIT_SUCCESS;
}
