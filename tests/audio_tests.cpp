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

bsl::PadPlaybackSettings dryPadSettings()
{
    bsl::PadPlaybackSettings settings;
    settings.releaseSeconds = 0.0f; // Immediate gates isolate ownership and other DSP.
    settings.lengthEnvelopeEnabled = false; // These fixtures isolate individual DSP stages.
    settings.filterEnabled = false;
    settings.highPassEnabled = false;
    return settings;
}

// These fixtures isolate steady-state DSP; startup has dedicated raw-prepare tests.
void settleMainGain(bsl::AudioEngine& engine, double sampleRate)
{
    const auto frames = static_cast<std::size_t>(sampleRate * 0.1);
    auto silence = makeBlock(34, frames);
    engine.process(silence, frames);
}

void prepareDryEngine(bsl::AudioEngine& engine, double sampleRate,
                      std::size_t maximumBlockSize = 1024)
{
    engine.prepare(sampleRate, maximumBlockSize);
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
        engine.setPadSettings(pad, dryPadSettings());
    settleMainGain(engine, sampleRate);
}

// Feed exact fixture values into downstream DSP despite normalized sampler phase.
// Append a silent endpoint and compensate its metadata rate so lookup lands on
// each original integer index without adding a nonzero endpoint to the fixture.
void setDspInput(bsl::AudioEngine& engine, std::size_t pad, bsl::SampleData sample)
{
    const auto frames = sample.length();
    for (auto& channel : sample.channels)
        channel.push_back(0.0f);
    sample.sampleRate *= static_cast<double>(frames + 1) / static_cast<double>(frames);
    engine.setPadSample(pad, std::move(sample));
}

void testNativeNormalizedCubicPlayback()
{
    for (const double rate : { 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1u, 7u })
            for (const bool stereo : { false, true })
            {
                bsl::AudioEngine engine;
                prepareDryEngine(engine, rate, blockSize);
                bsl::SampleData sample { 96000.0, { { 0.0f, 1.0f, -1.0f, 0.5f, 0.0f } } };
                if (stereo) sample.channels.push_back({ 0.0f, -1.0f, 1.0f, -0.5f, 0.0f });
                engine.setPadSample(0, sample);
                engine.noteOn(36, 1.0f);
                // Analytic four-point cubic at normalized source positions.
                const std::vector<float> expected = rate == 48000.0
                    ? std::vector<float> { 0.0f, -0.308f, 0.52f, 0.0f, 0.0f, 0.0f, 0.0f }
                    : std::vector<float> { 0.0f, 0.976f, -0.308f, -0.556f, 0.52f, 0.0f, 0.0f };
                for (std::size_t offset = 0; offset < expected.size(); offset += blockSize)
                {
                    const auto count = std::min(blockSize, expected.size() - offset);
                    auto block = makeBlock(34, count);
                    engine.process(block, count);
                    for (std::size_t frame = 0; frame < count; ++frame)
                    {
                        const float value = expected[offset + frame] * 0.7071067811865475f;
                        check(std::abs(block[0][frame] - value) < 3e-7f
                                  && std::abs(block[1][frame] - (stereo ? -value : value)) < 3e-7f,
                              "normalized cubic lookup preserves stereo, boundaries and duration across blocks");
                    }
                }
            }
}

void testNativeNonzeroSampleEndpoint()
{
    // Original FileBuffer WAV capture: endpoint-five-native/factory-{0,1}.f32.
    // Normalize the captured gain by the known final source value (0.125).
    // Native double phase accumulation reaches just below 1, then clamps to 1;
    // both frames play the nonzero endpoint before the silent sentinel.
    constexpr float expected[] { 0.0f, 0.236f, -0.017f, 0.101f,
                                 0.483f, 0.125f, 0.125f, 0.0f, 0.0f };
    for (const std::size_t blockSize : { 1u, 7u })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, blockSize);
        auto settings = dryPadSettings();
        settings.oneShot = false;
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { 48000.0, { { 0.0f, 0.25f, -0.125f, 0.5f, 0.125f } } });
        engine.noteOn(36, 1.0f);
        for (std::size_t offset = 0; offset < std::size(expected); offset += blockSize)
        {
            const auto count = std::min(blockSize, std::size(expected) - offset);
            auto block = makeBlock(34, count);
            engine.process(block, count);
            for (std::size_t frame = 0; frame < count; ++frame)
                check(std::abs(block[0][frame] - expected[offset + frame] * 0.7071067811865475f) < 3e-7f,
                      "nonzero sample endpoint matches original held-note capture across blocks");
        }
    }
}

void testNativeLayerGainPrecedesHighPass()
{
    // Cold original five-frame sample capture, HPF at its 10 Hz floor.
    // The normalized layer Volume endpoint is +6 dB before the filter.
    // Native cubic operation order and the exact gain float both matter: the
    // previous nested polynomial / rounded gain pair differs by several ULPs.
    constexpr std::array<std::pair<std::size_t, float>, 9> reference {{
        { 1, 0.09975851327180862f }, { 4, 0.2038138061761856f },
        { 5, 0.051951758563518524f }, { 6, 0.05181523412466049f },
        { 7, -0.0011592195369303226f }, { 31, -0.0011062086559832096f },
        { 63, -0.0010388141963630915f }, { 95, -0.0009750195313245058f },
        { 127, -0.0009146385709755123f }
    }};
    for (const std::size_t blockSize : { 64u, 128u })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, blockSize);
        bsl::PadState pad;
        pad.oneShot = false;
        pad.filterEnabled = false;
        pad.gain = 1.0f;
        engine.setPadSettings(0, bsl::makePadPlaybackSettings(pad));
        engine.setPadSample(0, { 48000.0, { { 0.0f, 0.25f, -0.125f, 0.5f, 0.125f } } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, blockSize);
        engine.process(output, blockSize);
        for (const auto [frame, expected] : reference)
            if (frame < blockSize)
                check(std::abs(output[0][frame] - expected) < 1e-9f
                          && std::abs(output[1][frame] - expected) < 1e-9f,
                      "native layer gain precedes HPF with the recorded cold-start tail");
        // The reference tail reaches the current render-span boundary, then the
        // exhausted voice retires. Use the captured gain path above: scaling a
        // filtered reference afterward does not preserve recursive float rounding.
        output = makeBlock(34, blockSize);
        engine.process(output, blockSize);
        for (const auto channel : { 0u, 1u })
            check(std::all_of(output[channel].begin(), output[channel].end(),
                              [](float x) { return x == 0.0f; }),
                  "finished sample voice retires after its final render span");
    }
}

void testNativeHighPassSurvivesVoiceReuseAcrossPads()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0, 64);
    bsl::PadState pad;
    pad.oneShot = false;
    pad.filterEnabled = false;
    pad.gain = 1.0f;
    for (const auto index : { 0u, 1u })
        engine.setPadSettings(index, bsl::makePadPlaybackSettings(pad));
    engine.setPadSample(0, { 48000.0, { { 0.0f, 0.25f, -0.125f, 0.5f, 0.125f } } });
    engine.setPadSample(1, { 48000.0, { std::vector<float>(4096, 0.0f) } });
    auto output = makeBlock(34, 64);
    engine.noteOn(36, 1.0f);
    engine.process(output, 64);
    // A retired voice freezes its HPF state even during idle blocks. The next
    // pad inherits it: confirmed by three native cross-pad lifecycle captures.
    engine.process(output, 64);
    check(output[0][0] == 0.0f, "retired HPF voice does not emit an idle tail");
    engine.noteOn(37, 1.0f);
    engine.process(output, 64);
    // Frame64 in the original endpoint-hpf-native-256 capture is the next
    // zero-input filter sample after the first 64 frames above.
    constexpr float expected = -0.001036766916513443f;
    check(std::abs(output[0][0] - expected) < 1e-9f
              && std::abs(output[1][0] - expected) < 1e-9f,
          "reused global voice retains both HPF channels across pads and idle time");
    engine.reset();
    settleMainGain(engine, 48000.0);
    engine.noteOn(37, 1.0f);
    engine.process(output, 64);
    check(std::all_of(output[0].begin(), output[0].end(), [](float x) { return x == 0.0f; }),
          "explicit engine reset clears retained HPF history");
}

void testNativeSilentReleaseAdvancesHighPass()
{
    // Native release-lifetime capture: output ends after 30 ms, but the
    // sampler fades for 100 ms and its HPF completes the final 64-frame span.
    for (const float releaseSeconds : { 0.0f, 0.03f })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, 64);
        auto settings = dryPadSettings();
        settings.oneShot = false;
        settings.pan = -1.0f;
        settings.releaseSeconds = releaseSeconds;
        settings.highPassEnabled = true;
        settings.highPass = 0.5f;
        for (const auto pad : { 0u, 1u })
            engine.setPadSettings(pad, settings);
        std::vector<float> source(8192, 0.0f);
        source[4863] = 1.0f; // Last positive input-release sample after note-off at 64.
        setDspInput(engine, 0, { 48000.0, { source } });
        setDspInput(engine, 1, { 48000.0, { std::vector<float>(4096, 0.0f) } });
        auto block = makeBlock(34, 64);
        engine.noteOn(36, 1.0f);
        engine.process(block, 64);
        engine.noteOff(36);
        auto tail = makeBlock(34, 4864);
        engine.process(tail, tail[0].size());
        check(std::all_of(tail[0].begin(), tail[0].end(), [](float x) { return x == 0.0f; }),
              "silent input release does not extend the audible pad release");
        engine.process(block, 64); // Idle time must not advance a retired filter.
        engine.noteOn(37, 1.0f);
        engine.process(block, 64);
        // Native float release leaves 1.8830731e-5 before its last decrement.
        // A unit impulse there, followed by 64 zero-input HPF frames, leaves
        // this next output. Premature retirement loses the impulse entirely.
        constexpr float expected = 4.6051514601685994e-8f;
        check(std::abs(block[0][0] - expected) < 1e-10f,
              "silent release advances HPF through its final span before voice reuse");
    }
}

void testNativeInputReleaseIndependentOfPadRelease()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0, 64);
    auto settings = dryPadSettings();
    settings.oneShot = false;
    settings.pan = -1.0f;
    settings.releaseSeconds = 0.2f;
    engine.setPadSettings(0, settings);
    engine.setPadSample(0, { 48000.0, { std::vector<float>(24000, 1.0f) } });
    auto block = makeBlock(34, 64);
    engine.noteOn(36, 1.0f);
    engine.process(block, 64);
    engine.noteOff(36);
    auto tail = makeBlock(34, 7200);
    engine.process(tail, tail[0].size());
    // Independent 100 ms input and 200 ms output fades at 25/50/75/100 ms.
    constexpr std::array<float, 4> expected { 0.65625f, 0.375f, 0.15625f, 0.0f };
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(std::abs(tail[0][1200 * (i + 1) - 1] - expected[i]) < 3e-5f,
              "input release stays 100 ms when pad output release changes");
    check(tail[0][4800] == 0.0f && tail[0].back() == 0.0f,
          "finished input release cannot restart during a longer output release");
}

void testNativeGlobalVoicePoolAndMonoOwnership()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0, 64);
    engine.setRoutingMode(bsl::RoutingMode::multi);
    auto pad = dryPadSettings();
    pad.pan = -1.0f;
    for (std::size_t index = 0; index < 4; ++index)
    {
        engine.setPadSettings(index, pad);
        engine.setPadSample(index, { 48000.0, { std::vector<float>(4096, 0.015625f) } });
    }
    auto output = makeBlock(34, 64);
    const auto expectCounts = [&](std::array<int, 4> counts, std::string_view message) {
        engine.process(output, 64);
        bool matches = true;
        for (std::size_t index = 0; index < counts.size(); ++index)
            matches = matches && std::abs(output[2 + 2 * index][0]
                                          - 0.015625f * counts[index]) < 1e-7f;
        check(matches, message);
    };
    for (std::size_t index = 0; index < 3; ++index)
        for (int note = 0; note < 9 - static_cast<int>(index); ++note)
            engine.noteOn(static_cast<std::uint8_t>(36 + index), 1.0f);
    expectCounts({ 9, 8, 7, 0 }, "24 global voices allow more than four simultaneous hits per pad");
    audioAllocations = 0;
    countAudioAllocations = true;
    engine.noteOn(37, 1.0f);
    countAudioAllocations = false;
    check(audioAllocations == 0, "voice stealing does not allocate on the audio thread");
    expectCounts({ 9, 8, 7, 0 }, "full global pool steals a voice of the incoming pitch first");
    engine.noteOn(39, 1.0f);
    expectCounts({ 8, 8, 7, 1 }, "overflow protects the first held extrema and steals the oldest remaining voice");
    pad.mono = true;
    engine.setPadSettings(1, pad);
    engine.noteOn(37, 1.0f);
    expectCounts({ 8, 1, 7, 1 }, "Mono retires only voices belonging to its pad");
    for (int note = 0; note < 6; ++note)
        engine.noteOn(39, 1.0f);
    expectCounts({ 8, 1, 7, 7 }, "voices freed by Mono are available to other pads");

    engine.reset();
    pad.mono = false;
    engine.setPadSettings(1, pad);
    engine.noteOn(36, 1.0f);
    for (int note = 0; note < 22; ++note)
        engine.noteOn(37, 1.0f);
    engine.noteOn(38, 1.0f);
    engine.noteOff(38);
    engine.noteOn(39, 1.0f);
    expectCounts({ 1, 22, 0, 1 },
                 "released One Shot key loses extrema protection before its sample ends");
}

std::vector<float> renderMasterImpulse(double sampleRate, std::size_t blockSize,
                                       bsl::MasterPlaybackSettings settings)
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, sampleRate);
    engine.setMasterSettings(settings);
    setDspInput(engine, 0, { sampleRate, { { 1.0f } } });
    auto pad = dryPadSettings();
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


void testNativeVelocityAmplitude()
{
    // Native isolated captures, normalized to the velocity-127 render.
    // Supply the normalized event values inferred from the measured curve, not a REAPER MIDI conversion
    // rule: host event conversion is independent of the voice amplitude law.
    struct Point { float velocity; float amplitude; };
    constexpr Point points[] {
        { 128.0f / 16383.0f, 0.0574223287f },
        { 4096.0f / 16383.0f, 0.2875145074f },
        { 8192.0f / 16383.0f, 0.5250290031f },
        { 8322.0f / 16383.0f, 0.5325672615f },
        { 12872.0f / 16383.0f, 0.7964078778f },
        { 16252.0f / 16383.0f, 0.9924037453f },
        { 1.0f, 1.0f }, { 0.0f, 0.0f }, { -1.0f, 0.0f }
    };
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto point : points)
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, rate, 17);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            auto settings = dryPadSettings();
            settings.pan = -1.0f;
            engine.setPadSettings(0, settings);
            engine.setPadSample(0, { rate, { std::vector<float>(17, 1.0f) } });
            engine.noteOn(36, point.velocity);
            auto block = makeBlock(34, 17);
            engine.process(block, 17);
            check(std::all_of(block[2].begin(), block[2].end(), [point](float sample) {
                      return std::abs(sample - point.amplitude) < 2e-7f;
                  }), "native velocity amplitude floor preserves zero silence across sample rates");
        }
}

void testMonoRetriggerAndOverlappingNoteOffs()
{
    bsl::PadState state;
    state.mono = true;
    check(bsl::makePadPlaybackSettings(state).mono, "Mono reaches the audio settings");
    for (const bool mono : { false, true })
        for (const bool oneShot : { false, true })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, 48000, 16);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            for (std::size_t pad = 0; pad < 2; ++pad)
                engine.setPadSample(pad, { 48000, { std::vector<float>(128, 0.5f) } });
            auto settings = dryPadSettings();
            settings.mono = mono;
            settings.oneShot = oneShot;
            engine.setPadSettings(0, settings);
            engine.noteOn(36, 0.25f);
            engine.noteOn(37, 0.5f);
            auto block = makeBlock(34, 1);
            engine.process(block, 1);
            engine.noteOn(36, 0.75f);
            engine.process(block, 1);
            const auto expected = 0.5f * 0.70710678f * (mono || !oneShot ? 0.7625f : 1.05f);
            check(std::abs(block[2][0] - expected) < 1.0e-6f,
                  "Mono replaces the old voice; gated MIDI releases it; poly One Shot retains it");
            check(std::abs(block[4][0] - 0.5f * 0.525f * 0.70710678f) < 1.0e-6f,
                  "Mono retrigger does not stop a different pad");
            engine.noteOff(36);
            engine.process(block, 1);
            const auto afterOldRelease = oneShot ? expected : 0.0f;
            check(std::abs(block[2][0] - afterOldRelease) < 1.0e-6f,
                  "host note-off releases the current gated note while One Shot keeps playing");
            engine.noteOff(36);
            engine.process(block, 1);
            check(std::abs(block[2][0] - (oneShot ? expected : 0.0f)) < 1.0e-6f,
                  "latest Mono note respects One Shot versus gated release");
        }

    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000, 4);
    engine.setPadSample(0, { 48000, { std::vector<float>(128, 0.5f) } });
    auto settings = dryPadSettings();
    settings.mono = true;
    settings.oneShot = false;
    engine.setPadSettings(0, settings);
    auto block = makeBlock(34, 4);
    engine.noteOn(36, 0.25f, 12);
    engine.process(block, 4);
    engine.noteOn(36, 0.75f, 8);
    engine.process(block, 4);
    check(std::abs(block[0][0] - 0.5f * 0.7625f * 0.70710678f) < 1.0e-6f,
          "Mono sequenced retrigger replaces the prior gate across callbacks");
    engine.process(block, 4);
    check(block[0][3] > 0.0f, "replacement Mono gate retains its full duration");
    engine.process(block, 4);
    check(block[0][0] == 0.0f, "replacement Mono gate ends on its own boundary");
}

void testPadMetersTrackRenderedAudio()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000, 4);
    engine.setPadSample(0, {48000, {{1.0f, -0.5f}}});
    engine.setPadSample(1, {48000, {{0.2f}}});
    auto pad = dryPadSettings();
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

void testStereoPlaybackUsesNormalizedPhaseAtUnity()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 0.5f, -0.25f } } });
    engine.noteOn(36, 1.0f);

    auto output = makeBlock(34, 4);
    engine.process(output, 4);
    constexpr float centreGain = 0.7071067811865475f;
    check(std::abs(output[0][0] - centreGain) < 1.0e-6f, "main left receives pad");
    check(std::abs(output[1][1] - (19.0f / 27.0f) * centreGain) < 1.0e-6f, "main right follows normalized cubic source phase");
    check(output[2][0] == 0.0f, "stereo mode leaves aux buses silent");
}

void testMixedSampleRatesPreserveDurationAndStereoContent()
{
    constexpr float centreGain = 0.7071067811865475f;
    constexpr std::array<double, 3> rates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<std::size_t, 3> blockSizes { 17, 64, 511 };
    for (const double sourceRate : rates)
    {
        const auto sourceFrames = static_cast<std::size_t>(sourceRate);
        auto samples = makeBlock(2, sourceFrames);
        for (std::size_t i = 0; i < sourceFrames; ++i)
        {
            const auto seconds = static_cast<float>(static_cast<double>(i) / sourceRate);
            samples[0][i] = 0.1f + 0.2f * seconds;
            samples[1][i] = -0.2f + 0.1f * seconds;
        }
        for (std::size_t rateIndex = 0; rateIndex < rates.size(); ++rateIndex)
        {
            const double hostRate = rates[rateIndex];
            const auto hostFrames = static_cast<std::size_t>(hostRate);
            const auto blockSize = blockSizes[rateIndex];
            bsl::AudioEngine engine;
            prepareDryEngine(engine, hostRate, 31);
            engine.setPadSample(0, { sourceRate, samples });
            engine.noteOn(36, 1.0f);
            auto output = makeBlock(34, blockSize);
            float maximumError = 0.0f;
            std::size_t audibleFrames = 0;
            for (std::size_t start = 0; start < hostFrames + blockSize; start += blockSize)
            {
                const auto count = std::min(blockSize, hostFrames + blockSize - start);
                engine.process(output, count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (output[0][i] != 0.0f || output[1][i] != 0.0f)
                        audibleFrames = start + i + 1;
                    if (start + i + 2 < hostFrames)
                    {
                        const auto seconds = static_cast<float>(static_cast<double>(start + i) / hostRate
                            * static_cast<double>(sourceFrames - 1) / static_cast<double>(sourceFrames));
                        maximumError = std::max(maximumError,
                            std::abs(output[0][i] - (0.1f + 0.2f * seconds) * centreGain));
                        maximumError = std::max(maximumError,
                            std::abs(output[1][i] - (-0.2f + 0.1f * seconds) * centreGain));
                    }
                }
            }
            check(maximumError < 1.0e-6f,
                  "mixed-rate stereo playback follows source time and preserves separate channels");
            check(audibleFrames >= hostFrames - 1 && audibleFrames <= hostFrames + 1,
                  "one-second samples retain duration across source/host rates and callback boundaries");
        }
    }
}

void testMultiModeRoutesEachPadToItsOwnAux()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
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
    prepareDryEngine(engine, 48000.0);
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
    prepareDryEngine(engine, 48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 0.25f } } });
    auto output = makeBlock(34, 1);
    engine.noteOn(36, 1.0f);
    engine.process(output, 1);
    output = makeBlock(34, 1);
    engine.process(output, 1);
    check(std::abs(output[0][0] - 0.625f * 0.7071067811865475f) < 1e-6f,
          "voice advances through normalized source phase");
    engine.noteOn(36, 1.0f);
    output = makeBlock(34, 1);
    engine.process(output, 1);
    check(output[0][0] > 0.7f, "retrigger restarts the sample");
}

void testScheduledTriggerStartsAtExactSampleOffset()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 0.5f, 0.25f } } });
    const std::array events { bsl::TimedNoteEvent { 3, 36, 1.0f, true } };
    auto output = makeBlock(34, 7);
    engine.process(output, 7, events);
    check(output[0][0] == 0.0f && output[0][2] == 0.0f,
          "scheduled note leaves preceding samples silent");
    check(output[0][3] > 0.7f, "scheduled note starts at its exact sample offset");
    check(output[0][4] > 0.3f, "scheduled note advances after its start offset");
}

void testNativeGatedRelease()
{
    // Original dry captures: gain at frame 0, 360, 720 and 1080 of a 30 ms release.
    const std::array<float, 4> measured { 0.9990975f, 0.6929526f, 0.4243055f, 0.1931562f };
    for (const bool scheduled : { false, true })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, 64);
        auto settings = dryPadSettings();
        settings.oneShot = false;
        settings.releaseSeconds = 0.03f;
        settings.pan = -1.0f;
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { 48000.0, { std::vector<float>(4800, 1.0f) } });
        engine.noteOn(36, 1.0f, scheduled ? 64 : 0);
        auto before = makeBlock(34, 64);
        engine.process(before, 64);
        if (!scheduled)
            engine.noteOff(36);
        auto tail = makeBlock(34, 1600);
        engine.process(tail, 1600);
        for (std::size_t i = 0; i < measured.size(); ++i)
            check(std::abs(tail[0][i * 360] - measured[i]) < 1.0e-5f,
                  "host and sequenced gated release follow the measured native curve");
        check(tail[0][1438] > 0.0f && tail[0][1440] == 0.0f,
              "gated release lasts 30 ms without restarting at each scheduled frame");
        engine.noteOn(36, 1.0f);
        engine.noteOff(36);
        engine.stopMidiChannel(1, true);
        engine.process(tail, 1600);
        check(std::all_of(tail[0].begin(), tail[0].end(), [](float x) { return x == 0.0f; }),
              "all sound off immediately clears an active release");
    }
}

void testNativeGatedRetriggerRelease()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1u, 64u, 257u })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, rate, blockSize);
            auto settings = dryPadSettings();
            settings.oneShot = false;
            settings.releaseSeconds = 0.03f;
            settings.pan = -1.0f;
            engine.setPadSettings(0, settings);
            engine.setPadSample(0, { rate, { std::vector<float>(static_cast<std::size_t>(rate), 1.0f) } });
            engine.noteOn(36, 1.0f);
            auto first = makeBlock(34, 1);
            engine.process(first, 1);
            engine.noteOn(36, 1.0f); // Starts the preceding host voice's measured release.
            auto tail = makeBlock(34, static_cast<std::size_t>(rate * 0.04));
            engine.process(tail, tail[0].size());
            for (const std::size_t frame : { 0u, 360u, 720u })
            {
                const float r = std::max(0.0f, 1.0f - static_cast<float>(frame + 1) / static_cast<float>(rate * 0.03));
                check(std::abs(tail[0][frame] - (1.0f + r * (0.7f + 0.3f * r))) < 3e-5f,
                      "gated host retrigger fades its predecessor with the native 30 ms release");
            }
            engine.noteOn(36, 1.0f, 0, true, 2);
            engine.noteOff(36, 1);
            engine.process(tail, tail[0].size());
            check(std::abs(tail[0].back() - 1.0f) < 1e-6f,
                  "gated note-off preserves another MIDI channel");
        }
}

void testGatedPadStopsOnScheduledNoteOff()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } } });
    auto settings = dryPadSettings();
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
    prepareDryEngine(engine, 48000.0, 4);
    engine.setPadSample(0, { 48000.0, { std::vector<float>(20, 0.5f) } });
    auto settings = dryPadSettings();
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


void testExplicitSequencerReleaseOwnership()
{
    constexpr float unit = 0.5f * 0.70710678118f;
    for (const bool oneShot : { false, true })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, 8);
        engine.setPadSample(0, { 48000.0, { std::vector<float>(128, 0.5f) } });
        auto settings = dryPadSettings();
        settings.oneShot = oneShot;
        engine.setPadSettings(0, settings);
        auto output = makeBlock(34, 1);
        engine.noteOn(36, 0.25f, 0, true, 2);
        engine.noteOn(36, 0.5f, 0, false); // Preview.
        engine.sequencerNoteOn(36, 0.75f);
        engine.sequencerNoteOff(36);
        engine.process(output, 1);
        check(std::abs(output[0][0] - unit * (0.2875f + 0.525f
                    + (oneShot ? 0.7625f : 0.0f))) < 1e-6f,
              "sequencer release preserves host and preview voices and respects One Shot");
        engine.reset();
        engine.sequencerNoteOn(36, 0.75f);
        engine.noteOn(36, 0.25f);
        engine.noteOff(36);
        engine.process(output, 1);
        check(std::abs(output[0][0] - unit * (0.7625f
                    + (oneShot ? 0.2875f : 0.0f))) < 1e-6f,
              "host release cannot consume a sequencer voice");
        engine.reset();
        engine.sequencerNoteOn(36, 0.25f);
        engine.sequencerNoteOn(36, 0.75f);
        engine.process(output, 1);
        check(std::abs(output[0][0] - unit * (0.7625f
                    + (oneShot ? 0.2875f : 0.0f))) < 1e-6f,
              "explicit sequencer retrigger releases gated voices but preserves One Shot polyphony");
        for (const std::size_t shortLane : { 0u, 1u })
        {
            engine.reset();
            engine.sequencerNoteOn(36, 0.75f, shortLane);
            engine.sequencerNoteOn(36, 0.75f, 1 - shortLane);
            engine.process(output, 1);
            check(std::abs(output[0][0] - unit * 1.525f) < 1e-6f,
                  "same-pad sequencer onsets on separate lanes remain polyphonic");
            engine.sequencerNoteOff(36, shortLane);
            engine.process(output, 1);
            check(std::abs(output[0][0] - unit * 0.7625f * (oneShot ? 2.0f : 1.0f)) < 1e-6f,
                  "short lane release preserves the longer lane voice");
        }
    }
}

void testHostNoteOffReleasesMatchingGatedVoices()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0, 4);
    engine.setPadSample(0, { 48000.0, { std::vector<float>(32, 0.5f) } });
    auto settings = dryPadSettings();
    settings.oneShot = false;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 0.25f);
    engine.noteOn(36, 0.75f);
    engine.noteOn(36, 0.5f, 8); // Sequenced voice owns its gate.
    engine.noteOff(36);
    auto output = makeBlock(34, 1);
    engine.process(output, 1);
    constexpr float unit = 0.5f * 0.70710678118f;
    check(std::abs(output[0][0] - unit * 0.525f) < 1e-6f,
          "host note-off releases matching gated MIDI voices and preserves sequenced gates");
    engine.noteOff(36);
    engine.process(output, 1);
    check(std::abs(output[0][0] - unit * 0.525f) < 1e-6f,
          "second host note-off releases the second host voice without touching sequenced audio");
    engine.noteOff(36);
    engine.process(output, 1);
    check(std::abs(output[0][0] - unit * 0.525f) < 1e-6f,
          "unmatched host note-off leaves a sequenced voice running");

    engine.reset();
    for (int hit = 0; hit < 5; ++hit) engine.noteOn(36, 0.25f);
    engine.noteOff(36); // Native note-off releases the currently held note after repeated retriggers.
    engine.process(output, 1);
    check(std::abs(output[0][0]) < 1e-6f,
          "note-off stops the held gated note after repeated retriggers");
    engine.noteOff(36);
    engine.process(output, 1);
    check(std::abs(output[0][0]) < 1e-6f,
          "further note-offs do not restart a released voice");

    engine.reset();
    engine.noteOn(36, 0.25f);
    auto finished = makeBlock(34, 40);
    engine.process(finished, 40);
    engine.noteOn(36, 0.75f);
    engine.noteOff(36);
    engine.process(output, 1);
    check(std::abs(output[0][0]) < 1e-6f,
          "note-off releases the current gated note even if an earlier hit finished");

    engine.reset();
    engine.noteOn(36, 0.25f, 0, false); // Pad preview has no matching MIDI release.
    engine.noteOn(36, 0.75f);
    engine.noteOff(36);
    engine.process(output, 1);
    check(std::abs(output[0][0] - unit * 0.2875f) < 1e-6f,
          "a pad preview does not consume a held MIDI note release");

    engine.reset();
    settings.oneShot = true;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 0.5f);
    engine.noteOff(36);
    engine.process(output, 1);
    check(std::abs(output[0][0] - unit * 0.525f) < 1e-6f,
          "One Shot still ignores host note-off");
}

class DelayedAmbience final : public bsl::AmbienceProcessor
{
public:
    void reset() noexcept override { history = {}; position = 0; }
    void process(float* roomLeft, float* roomRight, float* plateLeft,
                 float* plateRight, std::size_t frames) noexcept override
    {
        const std::array<float*, 4> channels { roomLeft, roomRight, plateLeft, plateRight };
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::size_t ch = 0; ch < channels.size(); ++ch)
            {
                const auto delay = ch < 2 ? 7u : 13u;
                const float value = history[ch][(position + 16 - delay) % 16];
                history[ch][position] = channels[ch][frame];
                channels[ch][frame] = value;
            }
            position = (position + 1) % 16;
        }
    }
private:
    std::array<std::array<float, 16>, 4> history {};
    std::size_t position = 0;
};

void testMuteAndSoloStopNewSendsWithoutClearingSharedTails()
{
    for (const auto routing : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
    for (const bool soloAnotherPad : { false, true })
    {
        DelayedAmbience ambience;
        bsl::AudioEngine engine;
        engine.setAmbienceProcessor(&ambience);
        prepareDryEngine(engine, 48000.0, 17);
        engine.setRoutingMode(routing);
        engine.setPadSample(0, { 48000.0, { { 0.5f } } });
        auto pad = dryPadSettings();
        pad.room = pad.reverb = 1.0f;
        engine.setPadSettings(0, pad);
        engine.noteOn(36, 1.0f);
        auto impulse = makeBlock(34, 1);
        engine.process(impulse, 1);
        const auto channel = routing == bsl::RoutingMode::stereo ? 0 : 2;
        check(impulse[channel][0] > 0.3f, "dry pad reaches its selected routing output");
        auto other = dryPadSettings();
        other.solo = soloAnotherPad;
        pad.muted = !soloAnotherPad;
        engine.setPadSettings(0, pad);
        engine.setPadSettings(1, other);
        engine.noteOn(36, 1.0f);
        auto hidden = makeBlock(34, 32);
        engine.process(hidden, 32);
        const float input = 0.5f * std::sqrt(0.5f);
        bool matches = true;
        for (std::size_t ch = 0; ch < hidden.size(); ++ch)
            for (std::size_t frame = 0; frame < hidden[ch].size(); ++frame)
            {
                const float expected = ch < 2 ? input * (frame == 6 ? 1.4125375f
                    : frame == 12 ? 1.9952623f : 0.0f) : 0.0f;
                matches = matches && std::abs(hidden[ch][frame] - expected) < 1.0e-6f;
            }
        check(matches, "mute/solo stop new sends while shared tails finish on the main stereo bus");
        check(engine.padPeaks()[0] == 0.0f, "shared returns do not light a muted pad meter");
    }
}

void testPitchAndDampAlterPlaybackWithoutAllocationContractChanges()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 1000.0);
    std::vector<float> source(600);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = 1.0f - static_cast<float>(index) / 1000.0f;
    engine.setPadSample(0, { 1000.0, { source } });
    auto settings = dryPadSettings();
    settings.pitchSemitones = 12.0f;
    settings.damp = 0.5f;
    settings.lengthEnvelopeEnabled = true;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, 300);
    engine.process(output, 300);
    check(output[0][0] > output[0][1], "one octave pitch-up advances through source frames twice as fast");
    check(output[0][100] > 0.0f && output[0][100] < output[0][0] * 0.75f,
          "damp applies the measured per-voice length envelope without truncating early");
}

void testNativeLengthUsesLoadedSampleDuration()
{
    // Native captures at 48 kHz, neutral DSP, two source durations and four
    // Length positions. These durations come from affine fits to the renders.
    struct Capture { std::size_t sourceFrames; std::array<double, 4> fadeSeconds; };
    const std::array<Capture, 2> captures {{
        { 96692, { 1.00727537, 0.75817153, 0.50874761, 0.00997915 } },
        { 62469, { 0.65087811, 0.49027432, 0.33034554, 0.00997922 } }
    }};
    constexpr std::array<float, 4> lengths { 0.0f, 0.25f, 0.5f, 1.0f };
    check(bsl::PadPlaybackSettings{}.lengthEnvelopeEnabled,
          "production playback enables the Length envelope by default");
    for (const auto& capture : captures)
        for (std::size_t position = 0; position < lengths.size(); ++position)
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
                for (const std::size_t blockSize : { 17u, 128u })
                {
                    bsl::AudioEngine engine;
                    prepareDryEngine(engine, rate);
                    // The inaudible longer layer must still set the duration.
                    const auto audibleFrames = std::min(capture.sourceFrames, std::size_t{62469});
                    engine.setPadSample(0, { 96000.0, { std::vector<float>(audibleFrames, 0.25f) } });
                    engine.setPadLayerSample(0, 1, { 96000.0,
                        { std::vector<float>(capture.sourceFrames, 0.0f) } });
                    auto pad = dryPadSettings();
                    pad.lengthEnvelopeEnabled = true;
                    pad.layerMix = 1.0f;
                    pad.damp = lengths[position];
                    engine.setPadSettings(0, pad);
                    engine.noteOn(36, 1.0f);
                    const auto frames = static_cast<std::size_t>(rate * std::min(
                        audibleFrames / 96000.0, capture.fadeSeconds[position]) * 0.9);
                    bool matches = true;
                    for (std::size_t offset = 0; offset < frames; offset += blockSize)
                    {
                        const auto count = std::min(blockSize, frames - offset);
                        auto output = makeBlock(34, count);
                        engine.process(output, count);
                        for (std::size_t frame = 0; frame < count; ++frame)
                        {
                            const double measured = 1.0 - (offset + frame)
                                / (rate * capture.fadeSeconds[position]);
                            const double actual = output[0][frame] / (0.25 * std::sqrt(0.5));
                            matches = matches && std::abs(actual - measured) < 0.0025;
                        }
                    }
                    check(matches, "Length matches native linear fades across sample durations, rates and blocks");
                }
}

void testNativeLengthFloatAccumulation()
{
    // Values from the float pre-decrement model distinguished by native Body
    // and Hit captures at both rates (including the float bin boundary at .5).
    struct Probe { double rate; std::array<float, 3> envelope; };
    const std::array<Probe, 2> probes {{
        { 44100.0, { 0.9999774694f, 0.6169580221f, 0.3692950308f } },
        { 48000.0, { 0.9999793172f, 0.6483715177f, 0.4208605886f } }
    }};
    constexpr std::array<std::size_t, 3> frames { 0, 17000, 28000 };
    for (const auto& probe : probes)
        for (const std::size_t blockSize : { 17u, 128u })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, probe.rate);
            engine.setPadSample(0, { 96000.0, { std::vector<float>(96692, 0.25f) } });
            auto pad = dryPadSettings();
            pad.lengthEnvelopeEnabled = true;
            engine.setPadSettings(0, pad);
            // Reset/retrigger must restore the per-voice accumulator.
            for (int repetition = 0; repetition < 2; ++repetition)
            {
                engine.stopMidiChannel(1, true);
                engine.noteOn(36, 1.0f);
                for (std::size_t offset = 0; offset <= frames.back(); offset += blockSize)
                {
                    const auto count = std::min(blockSize, frames.back() + 1 - offset);
                    auto output = makeBlock(34, count);
                    engine.process(output, count);
                    for (std::size_t i = 0; i < frames.size(); ++i)
                        if (frames[i] >= offset && frames[i] < offset + count)
                            check(std::abs(output[0][frames[i] - offset]
                                / (0.25f * std::sqrt(0.5f)) - probe.envelope[i]) < 2.0e-7f,
                                "native Length pre-decrement survives blocks and resets on retrigger");
                }
            }
        }
}

void testGatedPlaybackDoesNotUseOneShotLength()
{
    // Native gated factory captures retain sample amplitude until note-off,
    // unlike the linear Length fade measured in One Shot mode.
    for (const float damp : { 0.0f, 0.5f, 1.0f })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        engine.setPadSample(0, { 48000.0, { std::vector<float>(48000, 0.25f) } });
        auto pad = dryPadSettings();
        pad.oneShot = false;
        pad.lengthEnvelopeEnabled = true;
        pad.damp = damp;
        engine.setPadSettings(0, pad);
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 24000);
        engine.process(output, 24000);
        for (const auto frame : { 0, 1000, 23999 })
            check(std::abs(output[0][frame] - 0.25f * std::sqrt(0.5f)) < 1.0e-7f,
                  "gated playback preserves sustain independent of One Shot Length");
    }
}

void testNativePadPitchUsesEqualTemperament()
{
    struct Tuning { float semitones, cents; double ratio; };
    // Fixed musical intervals, independent of the playback implementation.
    const std::array<Tuning, 15> tunings {{
        { 0.0f, 0.0f, 1.0 }, { 12.0f, 0.0f, 2.0 },
        { -12.0f, 0.0f, 0.5 }, { 2.0f, 0.0f, 1.122462048309373 },
        { 0.0f, 100.0f, 1.0594630943592953 },
        { 0.0f, -100.0f, 0.9438743126816935 },
        { 2.0f, -50.0f, 1.0905077326652577 },
        // The native integer Tune parameter rounds to nearest, ties to even.
        { 0.49f, 0.0f, 1.0 }, { 0.51f, 0.0f, 1.0594630943592953 },
        { 0.5f, 0.0f, 1.0 }, { -0.5f, 0.0f, 1.0 },
        { 1.5f, 0.0f, 1.122462048309373 }, { 2.5f, 0.0f, 1.122462048309373 },
        { -1.5f, 0.0f, 0.8908987181403393 }, { -2.5f, 0.0f, 0.8908987181403393 }
    }};
    std::vector<float> source(2048);
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<float>(i) / 2048.0f;
    for (const double sourceRate : { 44100.0, 48000.0, 96000.0 })
        for (const double hostRate : { 44100.0, 48000.0, 96000.0 })
            for (const auto tuning : tunings)
                for (const std::size_t blockSize : { 1u, 17u, 129u })
                {
                    bsl::AudioEngine engine;
                    prepareDryEngine(engine, hostRate);
                    engine.setPadSample(0, { sourceRate, { source } });
                    auto settings = dryPadSettings();
                    settings.pitchSemitones = tuning.semitones;
                    settings.fineCents = tuning.cents;
                    engine.setPadSettings(0, settings);
                    engine.noteOn(36, 1.0f);
                    bool matches = true;
                    for (std::size_t offset = 0; offset < 129; offset += blockSize)
                    {
                        const auto count = std::min(blockSize, std::size_t(129) - offset);
                        auto output = makeBlock(34, count);
                        engine.process(output, count);
                        for (std::size_t frame = 0; frame < count; ++frame)
                        {
                            const double position = static_cast<double>(offset + frame)
                                * sourceRate / hostRate * tuning.ratio * 2047.0 / 2048.0;
                            // Linear ramp is reproduced by cubic interpolation away from its clamped start.
                            const double ramp = position < 1.0
                                ? 0.5 * position * (1.0 + position * (2.0 - position)) : position;
                            const double expected = ramp / 2048.0 * std::sqrt(0.5);
                            matches = matches && std::abs(output[0][frame] - expected) < 1.0e-7;
                        }
                    }
                    check(matches, "native pad pitch preserves exact musical intervals across rates and blocks");
                }
}

void testPadLowPassAndCompressionChangeDrySignal()
{
    bsl::AudioEngine dry;
    bsl::AudioEngine processed;
    prepareDryEngine(dry, 48000.0);
    prepareDryEngine(processed, 48000.0);
    const bsl::SampleData source { 48000.0, { { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f } } };
    dry.setPadSample(0, source);
    processed.setPadSample(0, source);
    auto settings = dryPadSettings();
    settings.filterEnabled = true;
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

void testUnavailableAmbienceDoesNotInventEchoes()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 1000.0);
    setDspInput(engine, 0, { 1000.0, { { 1.0f } } });
    auto settings = dryPadSettings();
    settings.room = settings.reverb = 1.0f;
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    auto output = makeBlock(34, 500);
    engine.process(output, 500);
    check(output[0][0] > 0.7f, "dry audio remains available without owned ambience assets");
    check(std::all_of(output[0].begin() + 1, output[0].end(),
                     [](float value) { return value == 0.0f; }),
          "missing responses never substitute feedback echoes for native convolution");
}

void testMasterProcessingAffectsStereoButIsBypassedInMultiMode()
{
    bsl::MasterPlaybackSettings master;
    master.gain = 0.25f;
    master.crush = 0.8f;

    bsl::AudioEngine stereo;
    prepareDryEngine(stereo, 48000.0);
    stereo.setMasterSettings(master);
    settleMainGain(stereo, 48000.0);
    stereo.setPadSample(0, { 48000.0, { { 0.83f } } });
    stereo.noteOn(36, 1.0f);
    auto stereoOut = makeBlock(34, 1);
    stereo.process(stereoOut, 1);

    bsl::AudioEngine multi;
    prepareDryEngine(multi, 48000.0);
    multi.setRoutingMode(bsl::RoutingMode::multi);
    multi.setMasterSettings(master);
    multi.setPadSample(0, { 48000.0, { { 0.83f } } });
    multi.noteOn(36, 1.0f);
    auto multiOut = makeBlock(34, 1);
    multi.process(multiOut, 1);
    check(stereoOut[0][0] < 0.25f, "master gain affects stereo output");
    check(multiOut[2][0] > 0.5f, "multi output bypasses master processing");
}


void testModuleBypassesPreserveVolumeAndRouting()
{
    const auto render = [](bool aux, bool master, bsl::RoutingMode routing) {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000, 64);
        engine.setRoutingMode(routing);
        engine.setModuleEnables(aux, master);
        auto pad = dryPadSettings();
        pad.independentDistortion = true;
        pad.distortionMix = 0.7f;
        pad.distortionDrive = 0.8f;
        engine.setDistortionResponse({ std::vector<float>{1.0f}, std::vector<float>{1.0f} });
        engine.setPadSettings(0, pad);
        bsl::MasterPlaybackSettings settings;
        settings.distortionDrive = 0.8f;
        settings.gain = 0.25f;
        settings.cutoff = 0.15f;
        settings.crush = settings.compression = 1.0f;
        engine.setMasterSettings(settings);
        settleMainGain(engine, 48000.0);
        engine.setPadSample(0, { 48000, { std::vector<float>(64, 0.3f) } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 64);
        engine.process(output, 64);
        return output;
    };
    for (const auto routing : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
    {
        const auto channel = routing == bsl::RoutingMode::stereo ? 0u : 2u;
        const float expected = 0.3f * std::sqrt(0.5f)
                             * (routing == bsl::RoutingMode::stereo ? 0.25f : 1.0f);
        const auto dry = render(false, false, routing);
        const auto master = render(false, true, routing);
        const auto aux = render(true, false, routing);
        const auto both = render(true, true, routing);
        for (const auto& block : { dry, master, aux, both })
            check(block[4][0] == 0.0f,
                  "module switches do not send audio to an unrelated pad output");
        for (std::size_t frame = 0; frame < 64; ++frame)
            check(std::abs(dry[channel][frame] - expected) < 2.0e-6f,
                  "Master bypass retains stereo volume and Multi pad gain");
        check(std::abs(aux[0][0] - dry[0][0]) > 0.01f,
              "AUX independently gates the main parallel distortion return");
        if (routing == bsl::RoutingMode::multi)
            for (const auto& block : { master, aux, both })
                check(block[2] == dry[2] && block[3] == dry[3],
                      "AUX and Master leave the separate Multi pad output unchanged");
        if (routing == bsl::RoutingMode::stereo)
            check(std::abs(master[0][0] - dry[0][0]) > 0.01f,
                  "enabled Master processes audio with AUX disabled");
        else
            check(master == dry && both == aux,
                  "Multi pad outputs bypass Master regardless of its switch");
    }
}


void testAuxiliaryReturnsPreserveDryAndAmbienceSends()
{
    struct ObservedAmbience final : bsl::AmbienceProcessor
    {
        std::array<std::array<float, 64>, 4> inputs {};
        void reset() noexcept override { inputs = {}; }
        void process(float* roomLeft, float* roomRight, float* plateLeft,
                     float* plateRight, std::size_t frames) noexcept override
        {
            std::array<float*, 4> buses { roomLeft, roomRight, plateLeft, plateRight };
            for (std::size_t bus = 0; bus < buses.size(); ++bus)
            {
                std::copy_n(buses[bus], frames, inputs[bus].begin());
                std::fill_n(buses[bus], frames, 0.0f);
            }
        }
    };
    struct Capture
    {
        bsl::AudioBlock output;
        std::array<std::array<float, 64>, 4> sends;
        float peak;
    };
    const auto render = [](bsl::RoutingMode routing, bool enabled, float level, float auxGain) {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000, 64);
        engine.setRoutingMode(routing);
        engine.setModuleEnables(enabled, false);
        ObservedAmbience ambience;
        engine.setAmbienceProcessor(&ambience);
        auto pad = dryPadSettings();
        pad.independentDistortion = true;
        pad.distortionMix = 0.8f;
        pad.distortionDrive = 0.7f;
        pad.auxGain = auxGain;
        pad.room = 0.3f;
        pad.reverb = 0.4f;
        engine.setPadSettings(0, pad);
        bsl::MasterPlaybackSettings master;
        master.distortionReturn = level;
        master.distortionDrive = 0.7f;
        engine.setDistortionResponse({ std::vector<float>{1.0f}, std::vector<float>{1.0f} });
        engine.setMasterSettings(master);
        settleMainGain(engine, 48000.0);
        engine.setPadSample(0, { 48000, { std::vector<float>(64, 0.2f) } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 64);
        engine.process(output, 64);
        return Capture { std::move(output), ambience.inputs, engine.padPeaks()[0] };
    };
    const auto dry = render(bsl::RoutingMode::multi, false, 1.0f, 0.0f);
    const auto full = render(bsl::RoutingMode::multi, true, 1.0f, 0.0f);
    check(full.output[0][0] > 0.01f, "Aux3 return fixture has a nonzero wet signal");
    // Native first-attack captures: halving the global Aux3 return is -18 dB.
    constexpr std::array levels { 0.0f, 0.25f, 0.5f, 1.0f };
    constexpr std::array ratios { 0.0f, 0.0158489319f, 0.1258925412f, 1.0f };
    for (std::size_t index = 0; index < levels.size(); ++index)
    {
        const auto multi = render(bsl::RoutingMode::multi, true, levels[index], 0.0f);
        const auto stereo = render(bsl::RoutingMode::stereo, true, levels[index], 0.0f);
        bool matches = true;
        for (std::size_t channel = 0; channel < 2; ++channel)
            for (std::size_t frame = 0; frame < 64; ++frame)
                matches = matches
                    && std::abs(multi.output[channel][frame]
                                - full.output[channel][frame] * ratios[index]) < 2.0e-6f
                    && std::abs(stereo.output[channel][frame] - dry.output[channel + 2][frame]
                                - multi.output[channel][frame]) < 2.0e-6f;
        check(matches, "Aux3 uses the measured return taper and the same main return in either routing mode");
        check(multi.output[2] == dry.output[2] && multi.output[3] == dry.output[3]
                  && multi.sends == dry.sends && stereo.sends == dry.sends
                  && multi.peak == dry.peak && stereo.peak == dry.peak,
              "Aux3 never alters separate dry pad audio, its meter, or Room/Reverb sends");
    }
    const auto both = render(bsl::RoutingMode::multi, true, 1.0f, 1.0f);
    check(both.output[0][63] > full.output[0][63]
              && both.output[2] == dry.output[2] && both.output[3] == dry.output[3]
              && both.sends == dry.sends && both.peak == dry.peak,
          "Aux4 adds only to the main return and leaves other sends and dry meters unchanged");
}

void testAuxGainTaperStartupAndRetrigger()
{
    bsl::PadState state;
    state.auxGain = 0.5f;
    bsl::MasterState masterState;
    masterState.distortionReturn = 0.25f;
    check(bsl::makePadPlaybackSettings(state).auxGain == 0.5f
              && bsl::makeMasterPlaybackSettings(masterState, 0.0f).distortionReturn == 0.25f,
          "native auxiliary state reaches the shared playback engine");
    constexpr std::array amounts { 0.25f, 0.5f, 1.0f };
    constexpr std::array gains { 0.0630957344f, 0.2511886432f, 1.0f };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1U, 127U, 513U })
            for (std::size_t index = 0; index < amounts.size(); ++index)
                for (const bool silenceFirst : { false, true })
                {
                    const auto rampFrames = static_cast<std::size_t>(rate * 0.1);
                    const auto lead = silenceFirst ? rampFrames / 2 : 0;
                    const auto retrigger = rampFrames * 3 / 4;
                    const auto total = rampFrames + 19;
                    bsl::AudioEngine engine;
                    prepareDryEngine(engine, rate, 127);
                    engine.setRoutingMode(bsl::RoutingMode::multi);
                    engine.setModuleEnables(true, false);
                    auto pad = dryPadSettings();
                    pad.auxGain = amounts[index];
                    pad.pan = -1.0f;
                    pad.mono = true;
                    engine.setPadSettings(0, pad);
                    for (std::size_t offset = 0; offset < lead;)
                    {
                        const auto frames = std::min(blockSize, lead - offset);
                        auto block = makeBlock(34, frames);
                        engine.process(block, frames); // No sample loaded yet.
                        offset += frames;
                    }
                    engine.setPadSample(0, { rate, { std::vector<float>(total, 0.4f) } });
                    engine.noteOn(36, 1.0f);
                    bool matches = true;
                    for (std::size_t offset = lead; offset < total;)
                    {
                        if (offset == retrigger) engine.noteOn(36, 1.0f);
                        auto frames = std::min(blockSize, total - offset);
                        if (offset < retrigger) frames = std::min(frames, retrigger - offset);
                        auto block = makeBlock(34, frames);
                        engine.process(block, frames);
                        for (std::size_t frame = 0; frame < frames; ++frame)
                        {
                            const float progress = std::min(1.0f,
                                static_cast<float>(offset + frame + 1) / static_cast<float>(rampFrames));
                            matches = matches && std::abs(block[2][frame] - 0.4f) < 1.0e-6f
                                && std::abs(block[0][frame] / block[2][frame]
                                            - gains[index] * progress) < 1.0e-4f;
                        }
                        offset += frames;
                    }
                    check(matches, "Aux4 follows its measured -12 dB taper and 100 ms startup across rates, blocks, silence and retrigger");
                    engine.setModuleEnables(false, false);
                    auto muted = makeBlock(34, 1);
                    engine.process(muted, 1);
                    check(muted[0][0] == 0.0f && std::abs(muted[2][0] - 0.4f) < 1.0e-6f,
                          "AUX bypass mutes Aux4 without changing the separate dry pad");
                }
}

void testMainGainStartupAndRouting()
{
    // Native fresh-start main/dry ratios rise for 100 ms. Multi Aux4 has
    // its own ramp, so its main output has the product of both envelopes.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1U, 127U, 513U })
            for (const auto routing : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
                for (const bool masterEnabled : { false, true })
                    for (const bool silenceFirst : { false, true })
                    {
                        const auto ramp = static_cast<std::size_t>(rate * 0.1);
                        const auto lead = silenceFirst ? ramp / 2 : 0;
                        const auto retrigger = ramp * 3 / 4;
                        const auto total = ramp + 19;
                        bsl::AudioEngine engine;
                        engine.prepare(rate, 127); // Deliberately retain fresh startup.
                        engine.setRoutingMode(routing);
                        engine.setModuleEnables(true, masterEnabled);
                        bsl::MasterPlaybackSettings master;
                        master.filterEnabled = false;
                        master.gain = 0.25f;
                        engine.setMasterSettings(master);
                        auto pad = dryPadSettings();
                        pad.gain = 1.0f / std::sqrt(0.5f);
                        pad.auxGain = routing == bsl::RoutingMode::multi ? 1.0f : 0.0f;
                        pad.mono = true;
                        engine.setPadSettings(0, pad);
                        auto block = makeBlock(34, blockSize);
                        for (std::size_t offset = 0; offset < lead;)
                        {
                            const auto frames = std::min(blockSize, lead - offset);
                            engine.process(block, frames); // No sample loaded yet.
                            offset += frames;
                        }
                        engine.setPadSample(0, { rate, { std::vector<float>(total, 0.4f),
                                                        std::vector<float>(total, -0.2f) } });
                        engine.noteOn(36, 1.0f);
                        float error = 0.0f;
                        bool finite = true;
                        audioAllocations = 0;
                        for (std::size_t offset = lead; offset < total;)
                        {
                            if (offset == retrigger) engine.noteOn(36, 1.0f);
                            auto frames = std::min(blockSize, total - offset);
                            if (offset < retrigger) frames = std::min(frames, retrigger - offset);
                            countAudioAllocations = true;
                            engine.process(block, frames);
                            countAudioAllocations = false;
                            for (std::size_t frame = 0; frame < frames; ++frame)
                            {
                                const float progress = std::min(1.0f,
                                    static_cast<float>(offset + frame + 1) / static_cast<float>(ramp));
                                const float gain = master.gain * progress
                                    * (routing == bsl::RoutingMode::multi ? progress : 1.0f);
                                finite = finite && std::isfinite(block[0][frame]) && std::isfinite(block[1][frame]);
                                error = std::max({ error, std::abs(block[0][frame] - 0.4f * gain),
                                                        std::abs(block[1][frame] + 0.2f * gain) });
                                if (routing == bsl::RoutingMode::multi)
                                    error = std::max({ error, std::abs(block[2][frame] - 0.4f),
                                                            std::abs(block[3][frame] + 0.2f) });
                            }
                            offset += frames;
                        }
                        check(finite && error < 3.0e-5f,
                              "main 100 ms startup survives blocks, silence, retrigger and Master bypass; Multi dry is unchanged");
                        check(audioAllocations == 0, "main and auxiliary gain ramps allocate no callback memory");
                    }
}

void testMainGainRetargeting()
{
    // Native cedd40 starts a full-duration slope from the current gain only
    // when the target changes. Repeating a target does not restart it.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1U, 127U, 513U })
        {
            const auto ramp = static_cast<std::size_t>(rate * 0.1);
            bsl::AudioEngine engine;
            engine.prepare(rate, 127);
            auto pad = dryPadSettings();
            pad.pan = -1.0f;
            engine.setPadSettings(0, pad);
            engine.setPadSample(0, { rate, { std::vector<float>(4 * ramp, 0.4f) } });
            engine.noteOn(36, 1.0f);
            auto block = makeBlock(34, blockSize);
            bsl::MasterPlaybackSettings master;
            master.filterEnabled = false;
            struct Phase { float target; std::size_t frames; };
            const std::array phases { Phase { 1.0f, ramp / 2 }, Phase { 0.25f, ramp / 4 },
                                     Phase { 0.8f, ramp + 19 }, Phase { 0.0f, ramp + 19 } };
            float from = 0.0f, error = 0.0f;
            float nativeCurrent = 0.0f, nativeError = 0.0f;
            bool finite = true;
            for (const auto phase : phases)
            {
                master.gain = phase.target;
                const float nativeStep = (phase.target - nativeCurrent) / static_cast<float>(ramp);
                for (std::size_t offset = 0; offset < phase.frames;)
                {
                    engine.setMasterSettings(master);
                    engine.setModuleEnables(true, offset % 2 == 0);
                    const auto frames = std::min(blockSize, phase.frames - offset);
                    engine.process(block, frames);
                    for (std::size_t frame = 0; frame < frames; ++frame)
                    {
                        const float progress = std::min(1.0f,
                            static_cast<float>(offset + frame + 1) / static_cast<float>(ramp));
                        const float expected = from + (phase.target - from) * progress;
                        // The native smoother accumulates float steps and snaps on its last frame.
                        nativeCurrent = offset + frame + 1 >= ramp
                            ? phase.target : nativeCurrent + nativeStep;
                        nativeError = std::max(nativeError,
                            std::abs(block[0][frame] / 0.4f - nativeCurrent));
                        finite = finite && std::isfinite(block[0][frame]);
                        error = std::max(error, std::abs(block[0][frame] / 0.4f - expected));
                    }
                    offset += frames;
                }
                from += (phase.target - from) * std::min(1.0f,
                    static_cast<float>(phase.frames) / static_cast<float>(ramp));
            }
            if (!finite || error >= 3.0e-4f || nativeError >= 1.0e-6f)
                std::cerr << "Main retarget rate=" << rate << " block=" << blockSize << " error=" << error << " native error=" << nativeError << '\n';
            // At 96 kHz float accumulation differs from the ideal slope by 2.06e-4.
            // Keep a tight independent check against the native arithmetic as well.
            check(finite && error < 3.0e-4f && nativeError < 1.0e-6f,
                  "main retarget starts a 100 ms slope from current gain across blocks and Master toggles");
        }
}

void testMasterCompressorNeutralRangePreservesSignal()
{
    // Native c79ae0 skips the insert at smoothed amounts <= .001, without
    // clipping or changing gain. Exercise signed, above-full-scale samples.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const float amount : { 0.0f, 0.0005f, 0.001f })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, rate, 17);
            auto pad = dryPadSettings();
            pad.gain = 1.0f / std::sqrt(0.5f);
            engine.setPadSettings(0, pad);
            bsl::MasterPlaybackSettings master;
            master.filterEnabled = false;
            master.compression = amount;
            engine.setMasterSettings(master);
            const std::vector<float> left { 1.25f, -1.25f, 0.3f, -0.7f };
            const std::vector<float> right { -1.4f, 1.4f, -0.2f, 0.4f };
            setDspInput(engine, 0, { rate, { left, right } });
            engine.noteOn(36, 1.0f);
            auto output = makeBlock(34, left.size());
            engine.process(output, left.size());
            for (std::size_t frame = 0; frame < left.size(); ++frame)
            {
                check(std::abs(output[0][frame] - left[frame]) < 2.0e-6f,
                      "master compressor neutral range preserves left gain and headroom");
                check(std::abs(output[1][frame] - right[frame]) < 2.0e-6f,
                      "master compressor neutral range preserves right gain and headroom");
            }
        }
}

void testNativeMasterCompressorDynamicsAndStartup()
{
    const auto render = [](bsl::AudioEngine& engine, std::size_t count, std::size_t blockSize) {
        std::vector<float> result;
        for (std::size_t offset = 0; offset < count; offset += blockSize)
        {
            const auto frames = std::min(blockSize, count - offset);
            auto output = makeBlock(34, frames);
            engine.process(output, frames);
            result.insert(result.end(), output[0].begin(), output[0].end());
        }
        return result;
    };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1U, 127U, 513U })
        {
            const auto amountFrames = static_cast<std::size_t>(rate * static_cast<double>(0.01f));
            const auto gainFrames = static_cast<std::size_t>(rate * 0.1);
            bsl::AudioEngine engine;
            prepareDryEngine(engine, rate, 127);
            auto pad = dryPadSettings();
            pad.pan = -1.0f;
            engine.setPadSettings(0, pad);
            bsl::MasterPlaybackSettings master;
            master.filterEnabled = false;
            master.compression = 1.0f;
            engine.setMasterSettings(master);
            const auto startupFrames = amountFrames + gainFrames + 8;
            setDspInput(engine, 0, { rate, { std::vector<float>(startupFrames, 0.1f) } });
            engine.noteOn(36, 1.0f);
            const auto startup = render(engine, startupFrames, blockSize);
            // Solve the repeated retargeting analytically while amount rises.
            // Once amount settles, the last gain ramp runs linearly to its end.
            const double q = 1.0 - 1.0 / static_cast<double>(gainFrames);
            const auto risingGain = [&](std::size_t frame) {
                const double qn = std::pow(q, static_cast<double>(frame));
                return 1.0 - qn + 0.5499999523162842 / amountFrames
                    * (static_cast<double>(frame) - q * (1.0 - qn) / (1.0 - q));
            };
            bool startupMatches = true;
            for (const auto frame : { std::size_t { 1 }, amountFrames / 2, amountFrames,
                                      amountFrames + gainFrames / 2, amountFrames + gainFrames })
            {
                const double beforeFinal = risingGain(amountFrames - 1);
                const double expectedGain = frame < amountFrames ? risingGain(frame)
                    : beforeFinal + (1.5499999523162842 - beforeFinal)
                        * std::min(1.0, static_cast<double>(frame - amountFrames + 1) / gainFrames);
                startupMatches = startupMatches
                    && std::abs(startup[frame - 1] - 0.1 * expectedGain) < 8e-6;
            }
            check(startupMatches, "master compressor startup follows native 10 ms amount and 100 ms gain ramps");

            const auto highFrames = static_cast<std::size_t>(rate * 0.03);
            const auto lowFrames = static_cast<std::size_t>(rate * 0.02);
            std::vector<float> source(highFrames + lowFrames, 0.1f);
            std::fill_n(source.begin(), highFrames, 1.25f);
            engine.reset();
            // Settle controls on silence, retaining a zero detector envelope.
            render(engine, static_cast<std::size_t>(rate), blockSize);
            setDspInput(engine, 0, { rate, { source } });
            engine.noteOn(36, 1.0f);
            const auto dynamics = render(engine, source.size(), blockSize);
            const double attack = static_cast<float>(std::exp(-6283.185307179586 / (rate * 70.0)));
            const double release = static_cast<float>(std::exp(-6283.185307179586 / (rate * 10.0)));
            const double highEnvelope = 1.25 * (1.0 - std::pow(attack, static_cast<double>(highFrames)));
            bool dynamicsMatch = true;
            for (const auto frame : { std::size_t { 0 }, highFrames / 4, highFrames - 1,
                                      highFrames, highFrames + lowFrames / 10, source.size() - 1 })
            {
                const double envelope = frame < highFrames
                    ? 1.25 * (1.0 - std::pow(attack, static_cast<double>(frame + 1)))
                    : 0.1 + (highEnvelope - 0.1)
                        * std::pow(release, static_cast<double>(frame - highFrames + 1));
                const double gain = envelope < 0.2 ? 1.0 : std::pow(envelope / 0.2, -0.9);
                const double expected = source[frame] * gain * 1.5499999523162842;
                dynamicsMatch = dynamicsMatch && std::abs(dynamics[frame] - expected) < 3e-5;
            }
            check(dynamicsMatch, "master compressor follows native attack and release envelopes without hard clipping");
            check(dynamics.front() > 1.9f,
                  "master compressor retains transient headroom while the detector rises");

            engine.reset();
            setDspInput(engine, 0, { rate, { std::vector<float>(startupFrames, 0.1f) } });
            engine.noteOn(36, 1.0f);
            check(render(engine, startupFrames, blockSize) == startup,
                  "reset clears the master compressor envelope and both control ramps");
            engine.setRoutingMode(bsl::RoutingMode::multi);
            setDspInput(engine, 0, { rate, { { 1.25f, -1.4f } } });
            engine.noteOn(36, 1.0f);
            auto multi = makeBlock(34, 2);
            engine.process(multi, 2);
            check(multi[2][0] == 1.25f && multi[2][1] == -1.4f,
                  "Multi output remains outside the master compressor");
        }
}

void testNativeMasterCompressorSteadyState()
{
    // c79ae0 sets T=.2+.8*(1-amount), ratio=4+6*amount and
    // postgain=1+.5499999523162842*amount. d45d90 uses separate peak
    // detectors, so a loud left channel must not duck a quiet right channel.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const float amount : { 0.25f, 0.8f, 1.0f })
            for (const std::size_t blockSize : { 1u, 127u, 513u })
            {
                bsl::AudioEngine engine;
                prepareDryEngine(engine, rate, 127);
                auto pad = dryPadSettings();
                pad.gain = 1.0f / std::sqrt(0.5f);
                engine.setPadSettings(0, pad);
                bsl::MasterPlaybackSettings master;
                master.filterEnabled = false;
                master.compression = amount;
                engine.setMasterSettings(master);
                auto block = makeBlock(34, blockSize);
                const auto warmFrames = static_cast<std::size_t>(rate);
                for (std::size_t offset = 0; offset < warmFrames; offset += blockSize)
                    engine.process(block, std::min(blockSize, warmFrames - offset));
                const auto frames = static_cast<std::size_t>(rate * 0.5);
                engine.setPadSample(0, { rate, {
                    std::vector<float>(frames, 1.25f), std::vector<float>(frames, -0.1f) } });
                engine.noteOn(36, 1.0f);
                float lastLeft = 0.0f, lastRight = 0.0f;
                for (std::size_t offset = 0; offset < frames; offset += blockSize)
                {
                    const auto count = std::min(blockSize, frames - offset);
                    engine.process(block, count);
                    lastLeft = block[0][count - 1];
                    lastRight = block[1][count - 1];
                }
                const double threshold = 0.2 + 0.8 * (1.0 - amount);
                const double ratio = 4.0 + 6.0 * amount;
                const double gain = 1.0 + 0.5499999523162842 * amount;
                const double expectedLeft = 1.25 * std::pow(1.25 / threshold, 1.0 / ratio - 1.0) * gain;
                check(std::abs(lastLeft - expectedLeft) < 2.0e-4,
                      "master compressor steady level follows native threshold and ratio");
                check(std::abs(lastRight + 0.1 * gain) < 2.0e-5,
                      "master compressor has independent channel envelopes and native makeup gain");
            }
}

void testMeasuredMasterCrushUsesSeventeenFrameHoldAtPointEight()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
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

void testNativeMasterCrusherMappingAndAutomation()
{
    // The original master process at c5b948 calls d463f0, also used by pads.
    // Golden vectors distinguish its nonlinear ranges, ties and raw hold state.
    struct CrushCase { float amount; std::size_t hold; float first; float second; };
    const std::array cases {
        CrushCase { 0.0f, 1, 0.3f, 0.7f },
        CrushCase { 0.25f, 1, 0.296875f, 0.703125f },
        CrushCase { 0.5f, 2, 0.25f, 0.75f },
        CrushCase { 0.65f, 5, 0.25f, 0.75f },
        CrushCase { 0.8f, 17, 0.25f, 0.75f },
        CrushCase { 1.0f, 64, 0.25f, 0.75f }
    };
    for (const auto& item : cases)
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const std::size_t blockSize : { 1u, 17u, 129u })
            {
                bsl::AudioEngine engine;
                prepareDryEngine(engine, rate, 17);
                auto pad = dryPadSettings();
                pad.gain = 1.0f / 0.7071067811865475f;
                engine.setPadSettings(0, pad);
                bsl::MasterPlaybackSettings master;
                master.crush = item.amount;
                engine.setMasterSettings(master);
                std::vector<float> left(129, 0.7f), right(129, -0.5f);
                left[0] = 0.3f;
                right[0] = -0.25f;
                setDspInput(engine, 0, { rate, { left, right } });
                engine.noteOn(36, 1.0f);
                bool matches = true;
                for (std::size_t offset = 0; offset < left.size(); offset += blockSize)
                {
                    const auto count = std::min(blockSize, left.size() - offset);
                    auto block = makeBlock(34, count);
                    engine.process(block, count);
                    for (std::size_t frame = 0; frame < count; ++frame)
                    {
                        const bool first = offset + frame < item.hold;
                        matches &= std::abs(block[0][frame]
                            - (first ? item.first : item.second)) < 1.0e-6f;
                        matches &= std::abs(block[1][frame]
                            - (first ? -0.25f : -0.5f)) < 1.0e-6f;
                    }
                }
                check(matches, "master crusher matches native stereo hold/depth across rates and blocks");
            }

    for (const auto [input, expected] : std::array {
             std::pair { 0.125f, 0.0f }, std::pair { -0.125f, 0.0f },
             std::pair { 0.375f, 0.5f }, std::pair { -0.375f, -0.5f },
             std::pair { 1.125f, 1.0f }, std::pair { 1.375f, 1.5f } })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        auto pad = dryPadSettings();
        pad.pan = -1.0f;
        engine.setPadSettings(0, pad);
        bsl::MasterPlaybackSettings master;
        master.crush = 1.0f;
        engine.setMasterSettings(master);
        setDspInput(engine, 0, { 48000.0, { { input } } });
        engine.noteOn(36, 1.0f);
        auto block = makeBlock(34, 1);
        engine.process(block, 1);
        check(block[0][0] == expected,
              "master three-bit quantizer rounds ties to even without clipping overloads");
    }

    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0, 17);
    auto pad = dryPadSettings();
    pad.pan = -1.0f;
    engine.setPadSettings(0, pad);
    std::vector<float> source(128, 0.1f);
    source[0] = 0.375f;
    source[1] = source[2] = source[3] = 0.7f;
    setDspInput(engine, 0, { 48000.0, { source } });
    bsl::MasterPlaybackSettings master;
    master.crush = 1.0f;
    engine.setMasterSettings(master);
    engine.noteOn(36, 1.0f);
    auto one = makeBlock(34, 1);
    engine.process(one, 1);
    master.crush = 0.5f;
    engine.setMasterSettings(master);
    engine.process(one, 1);
    check(one[0][0] == 0.375f, "master crush automation requantizes the held raw sample");
    engine.process(one, 1);
    check(one[0][0] == 0.75f, "shortened master hold refreshes at its new boundary");
    master.crush = 0.25f;
    engine.setMasterSettings(master);
    engine.process(one, 1);
    check(one[0][0] == 0.703125f, "one-frame master hold bypass retains quantization");
    master.crush = 0.0f;
    engine.setMasterSettings(master);
    engine.process(one, 1);
    check(one[0][0] == 0.1f, "zero master crush passes the current dry sample");
    master.crush = 1.0f;
    engine.setMasterSettings(master);
    engine.process(one, 1);
    check(one[0][0] == 0.75f, "master crush resumes its frozen raw hold after bypass");
    auto held = makeBlock(34, 62);
    engine.process(held, 62);
    check(std::all_of(held[0].begin(), held[0].end(), [](float value) { return value == 0.75f; }),
          "master crush preserves hold phase across bypass and internal buffer chunks");
    engine.process(one, 1);
    check(one[0][0] == 0.0f, "resumed master hold refreshes exactly at frame sixty-four");
    engine.reset();
    setDspInput(engine, 0, { 48000.0, { { 0.3f } } });
    engine.noteOn(36, 1.0f);
    engine.process(one, 1);
    check(one[0][0] == 0.25f, "engine reset clears the master raw hold and phase");
}

void testNativePadCrusherMappingAndAutomation()
{
    // Recovered from the original d463f0 process and its normalisable ranges.
    // Endpoint and intermediate vectors distinguish the nonlinear mapping from
    // the previous linear 1..21-frame and 1..16-bit approximation.
    struct CrushCase { float amount; std::size_t hold; float first; float second; };
    const std::array cases {
        CrushCase { 0.25f, 1, 0.296875f, 0.703125f },
        CrushCase { 0.5f, 2, 0.25f, 0.75f },
        CrushCase { 0.65f, 5, 0.25f, 0.75f },
        CrushCase { 0.8f, 17, 0.25f, 0.75f },
        CrushCase { 1.0f, 64, 0.25f, 0.75f }
    };
    constexpr float inputGain = 1.995262315f;
    constexpr float outputGain = 0.5011872336f;
    constexpr float centrePan = 0.7071067811865475f;
    for (const auto& item : cases)
        for (const std::size_t blockSize : { 1u, 17u, 129u })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, 48000.0, 129);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            auto pad = dryPadSettings();
            pad.layerInputGain = inputGain;
            pad.outputTrim = outputGain;
            pad.filterEnabled = false;
            pad.crush = item.amount;
            engine.setPadSettings(0, pad);
            std::vector<float> left(129, 0.7f), right(129, -0.5f);
            left[0] = 0.3f;
            right[0] = -0.25f;
            for (auto& value : left) value /= inputGain;
            for (auto& value : right) value /= inputGain;
            engine.setPadSample(0, { 48000.0, { left, right } });
            engine.noteOn(36, 1.0f);
            bool matches = true;
            for (std::size_t offset = 0; offset < left.size(); offset += blockSize)
            {
                const auto count = std::min(blockSize, left.size() - offset);
                auto block = makeBlock(34, count);
                engine.process(block, count);
                for (std::size_t frame = 0; frame < count; ++frame)
                {
                    const bool first = offset + frame < item.hold;
                    matches = matches
                        && std::abs(block[2][frame] / centrePan
                                    - (first ? item.first : item.second) * outputGain) < 1.0e-6f
                        && std::abs(block[3][frame] / centrePan
                                    - (first ? -0.25f : -0.5f) * outputGain) < 1.0e-6f;
                }
            }
            check(matches, "pad crusher matches native nonlinear hold/depth in both channels across blocks");
        }

    for (const auto [input, expected] : std::array {
             std::pair { 0.125f, 0.0f }, std::pair { -0.125f, 0.0f },
             std::pair { 0.375f, 0.5f }, std::pair { -0.375f, -0.5f },
             std::pair { 1.125f, 1.0f }, std::pair { 1.375f, 1.5f } })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        engine.setRoutingMode(bsl::RoutingMode::multi);
        auto pad = dryPadSettings();
        pad.layerInputGain = inputGain;
        pad.outputTrim = outputGain;
        pad.filterEnabled = false;
        pad.pan = -1.0f;
        pad.crush = 1.0f;
        engine.setPadSettings(0, pad);
        engine.setPadSample(0, { 48000.0, { { input / inputGain } } });
        engine.noteOn(36, 1.0f);
        auto block = makeBlock(34, 1);
        engine.process(block, 1);
        check(std::abs(block[2][0] - expected * outputGain) < 1.0e-6f,
              "native three-bit quantizer rounds ties to even without clipping overloads");
    }

    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0, 64);
    engine.setRoutingMode(bsl::RoutingMode::multi);
    auto pad = dryPadSettings();
    pad.layerInputGain = inputGain;
    pad.outputTrim = outputGain;
    pad.filterEnabled = false;
    pad.pan = -1.0f;
    pad.crush = 1.0f;
    engine.setPadSettings(0, pad);
    std::vector<float> source(128, 0.7f);
    source[0] = 0.375f;
    for (auto& value : source) value /= inputGain;
    engine.setPadSample(0, { 48000.0, { source } });
    engine.noteOn(36, 1.0f);
    auto one = makeBlock(34, 1);
    engine.process(one, 1);
    pad.crush = 0.5f;
    engine.setPadSettings(0, pad);
    engine.process(one, 1);
    check(std::abs(one[2][0] - 0.375f * outputGain) < 1.0e-6f, "shortening the native hold requantizes its raw sample at the new bit depth");
    engine.process(one, 1);
    check(std::abs(one[2][0] - 0.75f * outputGain) < 1.0e-6f, "shortened native hold refreshes at its new two-frame boundary");
    pad.crush = 0.25f;
    engine.setPadSettings(0, pad);
    engine.process(one, 1);
    check(std::abs(one[2][0] - 0.703125f * outputGain) < 1.0e-6f, "one-frame crush bypasses sample holding while retaining quantization");
    pad.crush = 1.0f;
    engine.setPadSettings(0, pad);
    engine.process(one, 1);
    check(std::abs(one[2][0] - 0.75f * outputGain) < 1.0e-6f, "returning to sample holding preserves the original hold counter");
}

void testNativePadCompressorSteadyStateAndSumming()
{
    // Native volume sweeps place pad gain after compression.
    // A settled constant signal independently fixes the expected static curve.
    constexpr float centre = 0.7071067811865475f;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1, 127, 513 })
            for (const auto routing : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
                for (const float amount : { 0.25f, 0.8f, 1.0f })
                {
                    bsl::AudioEngine engine;
                    prepareDryEngine(engine, rate, blockSize);
                    engine.setRoutingMode(routing);
                    auto pad = dryPadSettings();
                    pad.gain = 0.5f;
                    pad.outputTrim = 0.4f;
                    pad.layerInputGain = 1.995262315f;
                    pad.compression = amount;
                    engine.setPadSettings(0, pad);
                    const auto frames = static_cast<std::size_t>(rate * 0.5);
                    engine.setPadSample(0, { rate, {
                        std::vector<float>(frames, 0.75f),
                        std::vector<float>(frames, -0.05f) } });
                    engine.noteOn(36, 1.0f);
                    engine.noteOn(36, 1.0f);
                    auto block = makeBlock(34, blockSize);
                    float lastLeft = 0.0f;
                    float lastRight = 0.0f;
                    const std::size_t channel = routing == bsl::RoutingMode::stereo ? 0 : 2;
                    for (std::size_t start = 0; start < frames; start += blockSize)
                    {
                        const auto count = std::min(blockSize, frames - start);
                        engine.process(block, count);
                        lastLeft = block[channel][count - 1];
                        lastRight = block[channel + 1][count - 1];
                    }
                    const double input = 1.5 * centre * pad.layerInputGain;
                    const double threshold = 1.0 - 0.8 * amount;
                    const double ratio = 4.0 + 6.0 * amount;
                    const double makeup = 1.0 + 0.55 * amount;
                    const double expected = 0.5 * 0.4 * makeup * input
                        * std::min(1.0, std::pow(input / threshold, 1.0 / ratio - 1.0));
                    check(std::abs(lastLeft - expected) < 2.0e-4,
                          "pad volume follows compression of the boosted panned voice sum");
                    check(std::abs(lastRight - (-0.1 * centre * pad.layerInputGain * 0.5 * makeup * 0.4)) < 2.0e-5,
                          "pad compressor preserves an independent quiet channel in Stereo and Multi");
                }
}

void testPadCompressorNeutralRangePreservesHeadroom()
{
    for (const float amount : { 0.0f, 0.0005f, 0.001f })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        auto pad = dryPadSettings();
        pad.pan = -1.0f;
        pad.compression = amount;
        engine.setPadSettings(0, pad);
        engine.setPadSample(0, { 48000.0, { std::vector<float>(24000, 1.25f) } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 24000);
        engine.process(output, 24000);
        check(std::all_of(output[0].begin(), output[0].end(), [](float value) {
                  return value == 1.25f;
              }), "pad compressor neutral range preserves unclipped audio");
    }
}

void testPadCompressorLifecycleAndAutomation()
{
    const auto render = [](bsl::AudioEngine& engine, std::size_t count, std::size_t blockSize) {
        std::vector<float> result;
        for (std::size_t offset = 0; offset < count; offset += blockSize)
        {
            const auto frames = std::min(blockSize, count - offset);
            auto output = makeBlock(34, frames);
            engine.process(output, frames);
            result.insert(result.end(), output[0].begin(), output[0].end());
        }
        return result;
    };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1U, 127U, 513U })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, rate, blockSize);
            auto pad = dryPadSettings();
            pad.pan = -1.0f;
            pad.compression = 1.0f;
            engine.setPadSettings(0, pad);
            const auto sampleFrames = static_cast<std::size_t>(rate * 0.15);
            engine.setPadSample(0, { rate, { std::vector<float>(sampleFrames, 1.25f) } });
            engine.noteOn(36, 1.0f);
            const auto first = render(engine, static_cast<std::size_t>(rate * 0.1), blockSize);
            check(std::abs(first.front() - 1.9375f) < 1e-6f,
                  "first pad hit snaps compressor controls and starts with a clear detector");
            engine.noteOn(36, 0.01f);
            const auto overlap = render(engine, 1, blockSize);
            check(overlap.front() < 0.5f && overlap.front() > first.back(),
                  "overlapping pad hits retain the compressor detector");
            render(engine, sampleFrames + 1, blockSize);
            engine.noteOn(36, 1.0f);
            check(render(engine, 1, blockSize).front() == first.front(),
                  "first hit after a pad goes idle resets its compressor");
            engine.reset();
            engine.noteOn(36, 1.0f);
            check(render(engine, 1, blockSize).front() == first.front(),
                  "engine reset also starts the next pad hit with a clear compressor");

            engine.reset();
            pad.compression = 0.25f;
            engine.setPadSettings(0, pad);
            const auto amountFrames = static_cast<std::size_t>(rate * static_cast<double>(0.01f));
            const auto gainFrames = static_cast<std::size_t>(rate * 0.1);
            const auto rampFrames = amountFrames + gainFrames + 8;
            engine.setPadSample(0, { rate, { std::vector<float>(rampFrames + 1, 0.1f) } });
            engine.noteOn(36, 1.0f);
            const auto initial = render(engine, 1, blockSize);
            const double initialGain = 1.0 + 0.25 * 0.5499999523162842;
            check(std::abs(initial.front() - 0.1 * initialGain) < 1e-6,
                  "pad compressor makeup starts at the current amount without fading in");
            pad.compression = 1.0f;
            engine.setPadSettings(0, pad);
            const auto ramp = render(engine, rampFrames, blockSize);
            const double q = 1.0 - 1.0 / static_cast<double>(gainFrames);
            const auto risingGain = [&](std::size_t frame) {
                return initialGain + 0.75 * 0.5499999523162842 / amountFrames
                    * (static_cast<double>(frame)
                       - q * (1.0 - std::pow(q, static_cast<double>(frame))) / (1.0 - q));
            };
            bool matches = true;
            for (const auto frame : { std::size_t { 1 }, amountFrames / 2, amountFrames,
                                      amountFrames + gainFrames / 2, amountFrames + gainFrames })
            {
                const double beforeFinal = risingGain(amountFrames - 1);
                const double expectedGain = frame < amountFrames ? risingGain(frame)
                    : beforeFinal + (1.5499999523162842 - beforeFinal)
                        * std::min(1.0, static_cast<double>(frame - amountFrames + 1) / gainFrames);
                matches = matches && std::abs(ramp[frame - 1] - 0.1 * expectedGain) < 8e-6;
            }
            check(matches, "active pad compressor automation follows amount and makeup gain ramps");
        }
}

void testNativePadOutputTrim()
{
    for (const auto mode : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
        for (const float gain : { 0.0f, 0.5f, 1.0f, 1.995262315f })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, 48000.0);
            engine.setRoutingMode(mode);
            bsl::PadState state;
            state.filterEnabled = false;
            state.hpfBypass = true;
            state.gain = gain;
            auto settings = bsl::makePadPlaybackSettings(state);
            settings.releaseSeconds = 0.0f; // Immediate gates isolate ownership and other DSP.
    settings.lengthEnvelopeEnabled = false; // Isolate output trim on a one-frame fixture.
            engine.setPadSettings(0, settings);
            engine.setPadSample(0, { 48000.0, { { 0.25f }, { -0.5f } } });
            engine.noteOn(36, 0.5f);
            auto output = makeBlock(34, 1);
            engine.process(output, 1);
            const auto channel = mode == bsl::RoutingMode::stereo ? 0U : 2U;
            const float scale = 0.5985786945f * gain * 0.525f * std::sqrt(0.5f);
            check(std::abs(output[channel][0] - 0.25f * scale) < 1e-7f
                      && std::abs(output[channel + 1][0] + 0.5f * scale) < 1e-7f,
                  "stored pad volume applies native output trim in Stereo and Multi");
            check(state.gain == gain, "native output trim preserves serialized/UI volume");
        }
}

void testPadVolumeSmoothingAndRetrigger()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1U, 127U, 513U })
            for (const auto mode : { bsl::RoutingMode::stereo, bsl::RoutingMode::multi })
            {
                bsl::AudioEngine engine;
                prepareDryEngine(engine, rate, blockSize);
                engine.setRoutingMode(mode);
                const auto target = mode == bsl::RoutingMode::stereo ? 0U : 2U;
                const auto render = [&](std::size_t count) {
                    std::array<std::vector<float>, 2> result;
                    for (std::size_t offset = 0; offset < count; offset += blockSize)
                    {
                        const auto frames = std::min(blockSize, count - offset);
                        auto output = makeBlock(34, frames);
                        engine.process(output, frames);
                        for (std::size_t channel = 0; channel < 2; ++channel)
                            result[channel].insert(result[channel].end(), output[target + channel].begin(),
                                                   output[target + channel].end());
                    }
                    return result;
                };
                auto pad = dryPadSettings();
                pad.gain = 0.25f;
                engine.setPadSettings(0, pad);
                const auto frames = static_cast<std::size_t>(rate * 0.1);
                const auto length = frames * 3;
                engine.setPadSample(0, { rate, { std::vector<float>(length, 0.25f),
                                                 std::vector<float>(length, -0.125f) } });
                engine.noteOn(36, 1.0f);
                const float left = 0.25f * std::sqrt(0.5f);
                const float right = -0.125f * std::sqrt(0.5f);
                const auto initial = render(1);
                check(std::abs(initial[0].front() - left * 0.25f) < 1e-7f,
                      "first pad hit snaps volume without a startup fade");
                engine.setPadGainPan(0, 1.5f, 0.0f);
                const auto firstPart = frames / 3;
                const auto rise = render(firstPart);
                bool rising = true;
                for (std::size_t i = 0; i < firstPart; ++i)
                {
                    const float gain = 0.25f + 1.25f * static_cast<float>(i + 1) / static_cast<float>(frames);
                    rising = rising && std::abs(rise[0][i] - left * gain) < 2e-5f
                                    && std::abs(rise[1][i] - right * gain) < 2e-5f;
                }
                check(rising, "pad volume ramps over 100 ms and advances once per stereo frame");
                engine.noteOn(36, 0.5f);
                const float overlapGain = 0.25f + 1.25f * static_cast<float>(firstPart + 1)
                                                       / static_cast<float>(frames);
                const auto overlap = render(1);
                check(std::abs(overlap[0].front() - left * 1.525f * overlapGain) < 3e-5f,
                      "overlapping hits keep the active pad volume ramp");
                engine.setPadGainPan(0, 0.5f, 0.0f);
                const auto fall = render(frames + 1);
                // The native float ramp accumulates rounding error (up to 6.8e-5
                // in this 96 kHz fixture); the exact endpoint is checked separately.
                bool falling = true;
                for (std::size_t i = 0; i < fall[0].size(); ++i)
                {
                    const float progress = std::min(1.0f, static_cast<float>(i + 1) / static_cast<float>(frames));
                    const float gain = overlapGain + (0.5f - overlapGain) * progress;
                    falling = falling && std::abs(fall[0][i] - left * 1.525f * gain) < 1e-4f
                                      && std::abs(fall[1][i] - right * 1.525f * gain) < 1e-4f;
                }
                check(falling, "retargeted pad volume ramps from its current value");
                check(std::abs(fall[0][frames - 1] - left * 0.7625f) < 1e-7f
                          && std::abs(fall[1][frames - 1] - right * 0.7625f) < 1e-7f
                          && fall[0][frames] == fall[0][frames - 1]
                          && fall[1][frames] == fall[1][frames - 1],
                      "pad volume reaches and holds the exact target at 100 ms");
                render(length + 1);
                engine.setPadGainPan(0, 1.25f, 0.0f);
                engine.noteOn(36, 1.0f);
                check(std::abs(render(1)[0].front() - left * 1.25f) < 1e-7f,
                      "an idle pad snaps its new volume on the next hit");
                engine.reset();
                engine.setPadGainPan(0, 0.75f, 0.0f);
                engine.noteOn(36, 1.0f);
                check(std::abs(render(1)[0].front() - left * 0.75f) < 1e-7f,
                      "reset clears the pad volume ramp before the next hit");
            }
}


void testNativeMasterFilterResponseAndBypass()
{
    constexpr float defaultResonance = 7.0f / 24.0f;
    check(bsl::MasterState {}.resonance == defaultResonance
              && bsl::MasterPlaybackSettings {}.resonance == defaultResonance,
          "master filter defaults to native Q 0.8");
    // Native master process (0x180c5b94d): two resonant LPF stages, 10..20000 Hz
    // with 2000 Hz at centre. Captures at 48/96 kHz establish a fixed 44.1 kHz
    // coefficient rate; the last two fixtures use their captured cutoff and Q.
    // Expected impulses use independent bilinear biquads, not recorded audio or
    // the implementation's state-variable recurrence.
    struct Fixture
    {
        double rate;
        float cutoff;
        float resonance;
        std::size_t frames;
        std::array<std::size_t, 16> indices;
        std::array<float, 16> expected;
    };
    const std::array<Fixture, 8> fixtures {{
        { 44100.0, 0.5000000000f, 0.2916666567f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 2.9409993405e-04f, 2.1366506613e-03f, 7.5446535888e-03f, 1.7792748064e-02f, 3.2447966935e-02f, 4.9795218988e-02f, 6.7627989863e-02f, 8.3806006035e-02f, 9.6602554256e-02f, 1.0487708081e-01f, 1.0811397289e-01f, 1.0636810582e-01f, 1.0015369456e-01f, 9.0306802119e-02f, 7.7844788663e-02f, 6.3838963352e-02f } },
        { 48000.0, 0.5000000000f, 0.2916666567f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 2.9409993405e-04f, 2.1366506613e-03f, 7.5446535888e-03f, 1.7792748064e-02f, 3.2447966935e-02f, 4.9795218988e-02f, 6.7627989863e-02f, 8.3806006035e-02f, 9.6602554256e-02f, 1.0487708081e-01f, 1.0811397289e-01f, 1.0636810582e-01f, 1.0015369456e-01f, 9.0306802119e-02f, 7.7844788663e-02f, 6.3838963352e-02f } },
        { 96000.0, 0.5000000000f, 0.2916666567f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 2.9409993405e-04f, 2.1366506613e-03f, 7.5446535888e-03f, 1.7792748064e-02f, 3.2447966935e-02f, 4.9795218988e-02f, 6.7627989863e-02f, 8.3806006035e-02f, 9.6602554256e-02f, 1.0487708081e-01f, 1.0811397289e-01f, 1.0636810582e-01f, 1.0015369456e-01f, 9.0306802119e-02f, 7.7844788663e-02f, 6.3838963352e-02f } },
        { 48000.0, 0.5000000000f, 1.0000000000f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 3.6439571605e-04f, 2.7819312225e-03f, 1.0442440356e-02f, 2.6434702414e-02f, 5.2037019803e-02f, 8.6279389869e-02f, 1.2625034694e-01f, 1.6762583607e-01f, 2.0533713886e-01f, 2.3428842564e-01f, 2.5003787686e-01f, 2.4936813296e-01f, 2.3069006652e-01f, 1.9424609015e-01f, 1.4210285263e-01f, 7.7945787316e-02f } },
        { 48000.0, 0.9998000264f, 0.2916666567f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 6.8642869040e-01f, 5.2072697854e-01f, -3.2418640551e-01f, 1.6399821011e-01f, -4.4342651575e-02f, -3.6199742251e-02f, 8.2575078032e-02f, -1.0155865826e-01f, 1.0046114660e-01f, -8.6221152427e-02f, 6.4838312798e-02f, -4.1085698114e-02f, 1.8435497668e-02f, 8.6552118852e-04f, -1.5626978024e-02f, 2.5498574454e-02f } },
        { 48000.0, 0.0000000000f, 0.2916666567f, 8001, { 0, 1, 2, 10, 50, 100, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000 }, { 2.5708212105e-13f, 2.0557410407e-12f, 8.2183859289e-12f, 6.9410836304e-10f, 8.2144310503e-08f, 6.2759463362e-07f, 8.5231483592e-06f, 5.3317641692e-05f, 1.3849261954e-04f, 2.4852964360e-04f, 4.5590489675e-04f, 5.4156399912e-04f, 3.1892758862e-04f, 8.2308246320e-06f, -4.1479411539e-05f, 9.8852983454e-06f } },
        { 48000.0, 0.8835753798f, 0.4874999523f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 2.2782416014e-01f, 7.0455915504e-01f, 4.7314201059e-01f, -3.9914319534e-01f, -2.7693067861e-01f, 3.5533218886e-01f, 5.3671856671e-02f, -2.4828216830e-01f, 7.5939581338e-02f, 1.1247915838e-01f, -9.5393001429e-02f, -1.8936414822e-02f, 6.1476825317e-02f, -1.8544423287e-02f, -2.4214143621e-02f, 2.1202600968e-02f } },
        { 96000.0, 0.7591794729f, 0.4874999523f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 4.5975095189e-02f, 2.4048740347e-01f, 5.1093500076e-01f, 5.2091930099e-01f, 1.3993967241e-01f, -2.7423793272e-01f, -3.3397289738e-01f, -8.2825718870e-02f, 1.4953255128e-01f, 1.6069378331e-01f, 2.6188093723e-02f, -7.5062719972e-02f, -6.6982905542e-02f, -3.7093486267e-03f, 3.4611168976e-02f, 2.5311412631e-02f } }
    }};
    constexpr float centre = 0.7071067811865475f;
    for (const auto& fixture : fixtures)
        for (const std::size_t blockSize : { 1, 17, 129 })
        {
            bsl::AudioEngine engine;
            prepareDryEngine(engine, fixture.rate, blockSize);
            bsl::MasterPlaybackSettings master;
            master.cutoff = fixture.cutoff;
            master.resonance = fixture.resonance;
            engine.setMasterSettings(master);
            setDspInput(engine, 0, { fixture.rate, { { 1.0f }, { -0.25f } } });
            engine.noteOn(36, 1.0f);
            auto output = makeBlock(34, fixture.frames);
            engine.process(output, fixture.frames);
            bool matches = true;
            for (std::size_t i = 0; i < fixture.indices.size(); ++i)
            {
                const auto frame = fixture.indices[i];
                const float expected = fixture.expected[i] * centre;
                matches = matches && std::abs(output[0][frame] - expected) < 2.0e-5f
                                  && std::abs(output[1][frame] + expected * 0.25f) < 5.0e-6f;
            }
            check(matches, "master LPF matches native cutoff, Q and two-stage stereo response across rates and blocks");
        }

    for (const bool cutoffBypass : { false, true })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, 17);
        bsl::MasterPlaybackSettings master;
        master.cutoff = 0.5f;
        engine.setMasterSettings(master);
        setDspInput(engine, 0, { 48000.0, { { 1.0f } } });
        engine.noteOn(36, 1.0f);
        auto first = makeBlock(34, 16);
        engine.process(first, 16);
        auto bypass = master;
        bypass.filterEnabled = cutoffBypass;
        bypass.cutoff = cutoffBypass ? 0.9999f : master.cutoff;
        engine.setMasterSettings(bypass);
        auto silence = makeBlock(34, 64);
        engine.process(silence, 64);
        check(std::all_of(silence[0].begin(), silence[0].end(), [](float v) { return v == 0.0f; }),
              "disabled or near-maximum master cutoff bypasses its stored filter tail");
        engine.setMasterSettings(master);
        auto resumed = makeBlock(34, 16);
        engine.process(resumed, 16);
        const auto continuous = renderMasterImpulse(48000.0, 17, master);
        check(std::equal(resumed[0].begin(), resumed[0].end(), continuous.begin() + 16),
              "master filter bypass freezes both stages until re-enabled");
        engine.reset();
        engine.process(resumed, 16);
        check(std::all_of(resumed[0].begin(), resumed[0].end(), [](float v) { return v == 0.0f; }),
              "reset clears both master filter stages");
        engine.setMasterSettings(bypass);
        engine.noteOn(36, 1.0f);
        engine.process(first, 16);
        check(first[0][0] == centre && first[0][1] == 0.0f,
              "master filter bypass preserves the incoming samples");
    }
}

void testNativePadFilterResponseAndEffectOrder()
{
    const bsl::PadState state;
    const bsl::PadPlaybackSettings defaults;
    constexpr float defaultResonance = 7.0f / 24.0f; // Native Q=0.8 in [0.1, 2.5].
    check(std::abs(state.resonance - defaultResonance) < 1.0e-7f
              && std::abs(defaults.resonance - defaultResonance) < 1.0e-7f
              && defaults.filterEnabled && defaults.cutoff == 1.0f,
          "new pads retain the original enabled 20 kHz filter with Q 0.8");

    // Native PadVoice::process (0x180c7ed70) uses three two-pole LPF stages.
    // Fixed references use a bilinear transfer-function calculation, independent
    // of the engine's state-variable implementation; these are not recorded audio.
    struct Fixture
    {
        double rate;
        float cutoff;
        float resonance;
        std::size_t frames;
        std::array<std::size_t, 16> indices;
        std::array<float, 16> expected;
    };
    const std::array<Fixture, 6> fixtures {{
        { 48000.0, 0.50000000f, 0.291666667f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 3.1538034197e-06f, 3.4655855828e-05f, 1.8720242213e-04f, 6.7165975807e-04f, 1.8248466900e-03f, 4.0518575690e-03f, 7.7278795360e-03f, 1.3100541224e-02f, 2.0220894571e-02f, 2.8914537171e-02f, 3.8792632793e-02f, 4.9295261681e-02f, 5.9755901141e-02f, 6.9475037707e-02f, 7.7792076062e-02f, 8.4147049452e-02f } },
        { 48000.0, 0.50000000f, 1.000000000f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 4.2504150837e-06f, 4.8923636960e-05f, 2.7882566683e-04f, 1.0625071013e-03f, 3.0824350341e-03f, 7.3345903406e-03f, 1.5018026421e-02f, 2.7335904256e-02f, 4.5247058261e-02f, 6.9212668860e-02f, 9.8981857500e-02f, 1.3345360642e-01f, 1.7064152623e-01f, 2.0775422113e-01f, 2.4138909465e-01f, 2.6782315369e-01f } },
        { 44100.0, 1.00000000f, 0.291666667f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 5.7083305247e-01f, 6.4529719158e-01f, -2.8191187050e-01f, 3.8656387990e-02f, 1.0019846508e-01f, -1.5760636086e-01f, 1.5823384648e-01f, -1.2480404514e-01f, 7.6015239818e-02f, -2.5690702275e-02f, -1.7190329265e-02f, 4.7899399772e-02f, -6.5069423020e-02f, 6.9713565964e-02f, -6.4295558301e-02f, 5.1939547846e-02f } },
        { 48000.0, 1.00000000f, 0.291666667f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 3.5922349371e-01f, 7.3318475111e-01f, 4.4440031890e-02f, -2.8956874474e-01f, 2.2811379770e-01f, -6.6488766405e-02f, -6.7571129431e-02f, 1.2616727717e-01f, -1.1559514802e-01f, 6.6270388130e-02f, -1.0044737437e-02f, -3.1683693502e-02f, 5.0694795544e-02f, -4.9109337161e-02f, 3.4511896152e-02f, -1.5477621053e-02f } },
        { 96000.0, 1.00000000f, 0.291666667f, 129, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 1.2339865356e-02f, 8.5988253365e-02f, 2.5535807234e-01f, 4.0976619020e-01f, 3.5359625900e-01f, 9.3717673593e-02f, -1.2128494819e-01f, -1.2801323567e-01f, -1.9846058251e-02f, 4.3890519991e-02f, 2.9639669766e-02f, -2.9674436903e-03f, -1.2200528363e-02f, -4.1775092476e-03f, 2.4685975777e-03f, 2.4070579389e-03f } },
        { 48000.0, 0.00000000f, 0.291666667f, 8001, { 0, 1, 2, 10, 50, 100, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000 }, { 7.8412652096e-20f, 9.4056682995e-19f, 5.6407068708e-18f, 4.5760082236e-15f, 1.2622222019e-11f, 3.8637026967e-10f, 3.3216786361e-08f, 8.5417177655e-07f, 5.1638792557e-06f, 1.7158616462e-05f, 7.8641539668e-05f, 1.9177391622e-04f, 4.2113311544e-04f, 3.9140244640e-04f, -5.3906445340e-05f, -4.2869957721e-05f } }
    }};
    constexpr float centre = 0.7071067811865475f;
    for (const auto& fixture : fixtures)
        for (const std::size_t blockSize : { 1, 17, 129 })
        {
            bsl::AudioEngine engine;
            engine.prepare(fixture.rate, blockSize);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            bsl::PadPlaybackSettings settings;
            settings.releaseSeconds = 0.0f; // Immediate gates isolate ownership and other DSP.
    settings.lengthEnvelopeEnabled = false; // Isolate the filter stage.
            settings.highPassEnabled = false;
            settings.cutoff = fixture.cutoff;
            settings.resonance = fixture.resonance;
            engine.setPadSettings(0, settings);
            std::vector<float> left(fixture.frames), right(fixture.frames);
            left.front() = 1.0f;
            right.front() = -0.25f;
            setDspInput(engine, 0, { fixture.rate, { left, right } });
            engine.noteOn(36, 1.0f);
            auto output = makeBlock(34, fixture.frames);
            engine.process(output, fixture.frames);
            bool matches = true;
            for (std::size_t i = 0; i < fixture.indices.size(); ++i)
            {
                const auto frame = fixture.indices[i];
                const float expected = fixture.expected[i] * centre;
                matches = matches && std::abs(output[2][frame] - expected) < 2.0e-5f
                                  && std::abs(output[3][frame] + expected * 0.25f) < 5.0e-6f;
            }
            check(matches, "pad LPF matches native frequency, resonance, stage count and stereo state across block sizes");
        }

    // Crush holds and quantizes the source before filtering; the amplitude
    // envelope follows both effects in the native voice.
    const std::array<float, 16> crushedFilterReference { 1.5769017098e-06f, 1.8904829624e-05f, 1.1250604069e-04f, 4.4833591972e-04f, 1.3607592647e-03f, 3.3866880492e-03f, 7.2506278172e-03f, 1.3800898429e-02f, 2.3911345715e-02f, 3.8368614301e-02f, 5.7764930697e-02f, 8.2412561538e-02f, 1.1229051211e-01f, 1.4702803096e-01f, 1.8592406899e-01f, 2.2799759372e-01f };
    for (const bool attack : { false, true })
    {
        bsl::AudioEngine engine;
        engine.prepare(48000.0, 17);
        engine.setRoutingMode(bsl::RoutingMode::multi);
        bsl::PadPlaybackSettings settings;
        settings.releaseSeconds = 0.0f; // Immediate gates isolate ownership and other DSP.
    settings.lengthEnvelopeEnabled = false; // Isolate the filter stage.
        settings.pan = -1.0f;
        settings.highPassEnabled = false;
        settings.cutoff = 0.5f;
        settings.resonance = defaultResonance;
        settings.crush = 1.0f;
        settings.layerInputGain = 1.995262315f;
        settings.outputTrim = 0.5011872336f;
        settings.attackSeconds = attack ? 0.01f : 0.0f;
        engine.setPadSettings(0, settings);
        std::vector<float> source(129);
        source.front() = 0.375f / 1.995262315f; // Quantizer rounds to 0.5 before output attenuation.
        setDspInput(engine, 0, { 48000.0, { source } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, source.size());
        engine.process(output, source.size());
        const auto attackFrames = static_cast<std::size_t>(settings.attackSeconds * 48000.0);
        bool matches = true;
        for (std::size_t i = 0; i < crushedFilterReference.size(); ++i)
        {
            const float gain = attack ? static_cast<float>(i + 1) / static_cast<float>(attackFrames) : 1.0f;
            matches = matches && std::abs(output[2][i] - crushedFilterReference[i] * gain * 0.5011872336f) < 2.0e-5f;
        }
        check(matches, "native pad effect order is crusher, low-pass filter, then amplitude envelope");
    }
}

void testNativePadHighPassResponse()
{
    // Analytical impulse of H(s)=s^2/(s+w)^2 (Q=.5), bilinear transformed.
    // Native knob endpoints are 1/3000 Hz, centre400; rendering floors at10Hz.
    struct Fixture { double rate; float position; std::array<float,16> expected; };
    const std::array<Fixture,9> fixtures {{
        { 44100.0, 0.0f, { 9.9857676219e-01f, -2.8434364813e-03f, -2.8373639905e-03f, -2.8313030272e-03f, -2.8252535709e-03f, -2.8192156012e-03f, -2.8131890977e-03f, -2.8071740399e-03f, -2.8011704074e-03f, -2.7951781799e-03f, -2.7891973371e-03f, -2.7832278587e-03f, -2.7772697245e-03f, -2.6604584599e-03f, -2.4825886856e-03f, -2.1579708014e-03f } },
        { 44100.0, 0.5f, { 9.4534203697e-01f, -1.0479300375e-01f, -9.6080622512e-02f, -8.8012097547e-02f, -8.0542820903e-02f, -7.3631151558e-02f, -6.7238223563e-02f, -6.1327766344e-02f, -5.5865936397e-02f, -5.0821159669e-02f, -4.6163983951e-02f, -4.1866940664e-02f, -3.7904415451e-02f, -1.6189497192e-03f, 2.4478416683e-03f, 2.0456970295e-04f } },
        { 44100.0, 1.0f, { 6.7514776013e-01f, -4.8158625875e-01f, -2.2394785167e-01f, -8.8825950265e-02f, -2.1600783602e-02f, 8.9710865176e-03f, 2.0483530705e-02f, 2.2642959975e-02f, 2.0656532042e-02f, 1.7206825933e-02f, 1.3590280872e-02f, 1.0364709974e-02f, 7.7112575647e-03f, 4.2158640880e-06f, 6.7802894751e-12f, 7.7504360506e-24f } },
        { 48000.0, 0.0f, { 9.9869228686e-01f, -2.6128605528e-03f, -2.6077335680e-03f, -2.6026155256e-03f, -2.5975064110e-03f, -2.5924062096e-03f, -2.5873149069e-03f, -2.5822324882e-03f, -2.5771589390e-03f, -2.5720942449e-03f, -2.5670383913e-03f, -2.5619913638e-03f, -2.5569531480e-03f, -2.4580172393e-03f, -2.3067578233e-03f, -2.0286356441e-03f } },
        { 48000.0, 0.5f, { 9.4961572058e-01f, -9.6928099508e-02f, -8.9507948174e-02f, -8.2592717692e-02f, -7.6150196992e-02f, -7.0150147695e-02f, -6.4564186649e-02f, -5.9365675329e-02f, -5.4529615686e-02f, -5.0032552087e-02f, -4.5852478987e-02f, -4.1968754012e-02f, -3.8362016136e-02f, -3.1796106764e-03f, 2.4808769798e-03f, 3.0208087694e-04f } },
        { 48000.0, 1.0f, { 6.9570499200e-01f, -4.6169955611e-01f, -2.3189689274e-01f, -1.0376547306e-01f, -3.5134433867e-02f, -6.2474387393e-04f, 1.4851332911e-02f, 2.0125611623e-02f, 2.0264441450e-02f, 1.8095199025e-02f, 1.5134333790e-02f, 1.2146045803e-02f, 9.4745412735e-03f, 1.1532811538e-05f, 6.2838369792e-11f, 8.1378244485e-22f } },
        { 96000.0, 0.0f, { 9.9934582264e-01f, -1.3077127215e-03f, -1.3064292975e-03f, -1.3051469931e-03f, -1.3038658075e-03f, -1.3025857396e-03f, -1.3013067887e-03f, -1.3000289537e-03f, -1.2987522337e-03f, -1.2974766279e-03f, -1.2962021354e-03f, -1.2949287552e-03f, -1.2936564864e-03f, -1.2684431128e-03f, -1.2290077122e-03f, -1.1533780731e-03f } },
        { 96000.0, 0.5f, { 9.7432383775e-01f, -5.0359153406e-02f, -4.8406996254e-02f, -4.6522105567e-02f, -4.4702308375e-02f, -4.2945499098e-02f, -4.1249637507e-02f, -3.9612746761e-02f, -3.8032911487e-02f, -3.6508275929e-02f, -3.5037042138e-02f, -3.3617468233e-02f, -3.2247866696e-02f, -1.3168784747e-02f, -1.5902186142e-03f, 1.2398480268e-03f } },
        { 96000.0, 1.0f, { 8.2871781484e-01f, -2.9721336124e-01f, -2.1726837226e-01f, -1.5643782607e-01f, -1.1043719142e-01f, -7.5903905871e-02f, -5.0204492427e-02f, -3.1281205481e-02f, -1.7530231448e-02f, -7.7050585482e-03f, -8.3990561150e-04f, 3.8108768765e-03f, 6.8206995714e-03f, 1.5497252560e-03f, 6.8480850295e-06f, 4.8173959489e-11f } }
    }};
    constexpr std::array<std::size_t,16> indices { 0,1,2,3,4,5,6,7,8,9,10,11,12,32,64,128 };
    constexpr float centre = 0.7071067811865475f;
    for (const auto& fixture : fixtures)
        for (const std::size_t blockSize : { 1, 17, 129 })
        {
            bsl::AudioEngine engine;
            engine.prepare(fixture.rate, blockSize);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            bsl::PadPlaybackSettings settings;
            settings.releaseSeconds = 0.0f; // Immediate gates isolate ownership and other DSP.
    settings.lengthEnvelopeEnabled = false; // Isolate the filter stage.
            settings.filterEnabled = false;
            settings.highPass = fixture.position;
            engine.setPadSettings(0, settings);
            std::vector<float> left(129), right(129);
            left[0] = 1.0f;
            right[0] = -0.25f;
            setDspInput(engine, 0, { fixture.rate, { left, right } });
            engine.noteOn(36, 1.0f);
            auto output = makeBlock(34,129);
            engine.process(output,129);
            bool matches = true;
            for (std::size_t i=0; i<indices.size(); ++i)
            {
                const float expected=fixture.expected[i]*centre;
                matches = matches && std::abs(output[2][indices[i]]-expected)<2e-5f
                                  && std::abs(output[3][indices[i]]+expected*.25f)<5e-6f;
            }
            check(matches, "pad HPF matches native cutoff range and Q .5 biquad across rates, blocks and stereo");
        }
    bsl::PadState pad;
    pad.highPass=.5f;
    pad.hpfBypass=true;
    const auto settings=bsl::makePadPlaybackSettings(pad);
    check(settings.highPass==.5f && !settings.highPassEnabled,
          "HPF bypass retains its cutoff for subsequent automation");
    bsl::AudioEngine engine;
    engine.prepare(48000.0,17);
    engine.setRoutingMode(bsl::RoutingMode::multi);
    auto bypassed=settings;
    bypassed.lengthEnvelopeEnabled=false;
    bypassed.pan=-1.0f;
    bypassed.gain=1.0f;
    bypassed.outputTrim=1.0f; // Isolate HPF bypass from native output calibration.
    bypassed.layerInputGain=1.0f;
    bypassed.filterEnabled=false;
    engine.setPadSettings(0,bypassed);
    const std::vector<float> source { .25f,-.5f,.75f,-1.0f };
    setDspInput(engine, 0,{48000.0,{source}});
    engine.noteOn(36,1.0f);
    auto output=makeBlock(34,source.size());
    engine.process(output,source.size());
    check(output[2]==source, "disabled HPF is sample-exact even with a nonzero stored cutoff");
}

void testNativeEnvelopePrecedesHighPass()
{
    // A faded impulse must scale the whole filter response by its input level.
    // Fading the response itself instead produces a different tail. Native
    // isolated 400 Hz captures distinguish this order for Length and release.
    constexpr double rate = 48000.0;
    constexpr std::size_t frames = 4800;
    for (const bool gated : { false, true })
    {
        std::array<std::vector<float>, 2> responses;
        for (int enabled = 0; enabled < 2; ++enabled)
        {
            bsl::AudioEngine engine;
            engine.prepare(rate, 64);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            bsl::PadPlaybackSettings settings;
            settings.pan = -1.0f;
            settings.filterEnabled = false;
            settings.highPass = 0.5f;
            settings.oneShot = !gated;
            settings.lengthEnvelopeEnabled = enabled && !gated;
            settings.releaseSeconds = 0.03f;
            engine.setPadSettings(0, settings);
            std::vector<float> source(frames);
            source[gated ? 64 : 0] = 1.0f;
            setDspInput(engine, 0, { rate, { source } });
            engine.noteOn(36, 1.0f);
            if (gated)
            {
                auto lead = makeBlock(34, 64);
                engine.process(lead, 64);
                if (enabled) engine.noteOff(36);
            }
            auto output = makeBlock(34, 129);
            engine.process(output, 129);
            responses[enabled] = output[2];
        }
        const float firstRemaining = 1.0f - static_cast<float>(1.0 / (0.03f * rate));
        const float inputGain = gated ? 0.7f + 0.3f * firstRemaining
                                      : 1.0f - 1.0f / frames;
        bool matches = true;
        for (std::size_t i = 0; i < 129; ++i)
        {
            const float outputGain = gated
                ? 1.0f - static_cast<float>((i + 1) / (0.03f * rate)) : 1.0f;
            matches = matches && std::abs(responses[1][i]
                - responses[0][i] * inputGain * outputGain) < 5e-7f;
        }
        check(matches, gated ? "release input fade precedes HPF; output fade follows it"
                             : "Length scales HPF input, not the filter tail");
    }
}

void testTwoFactoryLayersMixContinuously()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
    engine.setPadLayerSample(0, 0, { 48000.0, { { 1.0f } } });
    engine.setPadLayerSample(0, 1, { 48000.0, { { -0.25f } } });

    // Native isolated two-layer sweep, 48 kHz: endpoints select second/first
    // respectively, and the centre attenuates each layer by exactly 3 dB.
    struct MixFixture { float mix, first, second; };
    const MixFixture fixtures[] {
        { 0.0f, 0.0f, 1.0f },
        { 0.25f, 0.38394324f, 0.92412977f },
        { 0.5f, 0.70794577f, 0.70794577f },
        { 0.75f, 0.92412977f, 0.38394318f },
        { 1.0f, 1.0f, 0.0f }
    };
    for (const auto& fixture : fixtures)
    {
        engine.reset();
        auto settings = dryPadSettings();
        settings.layerMix = fixture.mix;
        engine.setPadSettings(0, settings);
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 1);
        engine.process(output, 1);
        const float expected = (fixture.first - 0.25f * fixture.second) * 0.7071067811865475f;
        check(std::abs(output[0][0] - expected) < 2.0e-6f,
              "factory layer crossfade matches native direction and -3 dB curve");
    }
}


void testNativeLayerMixGainPrecision()
{
    // Original Volume parameters captured at LayerMix 0.81: 0x3ff42424 and
    // 0x3f16d284. The previous fitted pow curve rounds layer 2 up one ULP,
    // which accumulates into a rendered-reference mismatch through the HPF.
    constexpr float gains[] { 1.9073529243469238f, 0.58914971351623535f };
    for (std::size_t layer = 0; layer < 2; ++layer)
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        engine.setModuleEnables(false, false);
        engine.setPadLayerSample(0, 0, { 48000.0, { { layer == 0 ? 1.0f : 0.0f } } });
        engine.setPadLayerSample(0, 1, { 48000.0, { { layer == 1 ? 1.0f : 0.0f } } });
        auto settings = dryPadSettings();
        settings.layerMix = 0.81f;
        settings.layerInputGain = 1.9952623844146729f;
        settings.pan = -1.0f;
        engine.setPadSettings(0, settings);
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, 1);
        engine.process(output, 1);
        check(output[0][0] == gains[layer],
              "layer mix preserves the native gain float before recursive filters");
    }
}

void testNativeDriveInsertCurveAndBypass()
{
    // Golden steady-state values from the original c7ed70 -> cebe80 -> cedd40
    // route, including clipping, threshold normalization and post-gain.
    // Values are normalized to the dry route: the layer's +6 dB precedes
    // the insert chain and the fixed pad output trim follows it.
    struct Fixture { float db; std::array<float, 5> expected; };
    const std::array<Fixture, 4> fixtures {{
        { 0.0f,  { 0.01f, 0.1f, 0.25f, 0.5f, 0.8f } },
        { 6.0f,  { 0.0169424711f, 0.1651270406f, 0.3558409173f, 0.4075502896f, 0.4075502896f } },
        { 12.0f, { 0.0190825342f, 0.1825341842f, 0.3464148106f, 0.3508310635f, 0.3508310635f } },
        { 24.0f, { 0.0592037857f, 0.2004748935f, 0.2004748935f, 0.2004748935f, 0.2004748935f } }
    }};
    const std::vector<float> input { 0.01f, 0.1f, 0.25f, 0.5f, 0.8f,
                                     -0.01f, -0.1f, -0.25f, -0.5f, -0.8f };
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto blockSize : { std::size_t(1), std::size_t(4), input.size() })
            for (const auto& fixture : fixtures)
                for (const bool bypassed : { false, true })
                {
                    bsl::AudioEngine engine;
                    prepareDryEngine(engine, rate);
                    bsl::PadState pad;
                    pad.filterEnabled = false;
                    pad.hpfBypass = true;
                    pad.pan = -1.0f;
                    pad.driveEnabled = true;
                    pad.driveGainDb = fixture.db;
                    pad.driveBypass = bypassed;
                    // A fresh pad has no Aux3 send: Drive must still work.
                    auto playback = bsl::makePadPlaybackSettings(pad);
                    playback.lengthEnvelopeEnabled = false; // Isolate Drive.
                    engine.setPadSettings(0, playback);
                    setDspInput(engine, 0, { rate, { input } });
                    engine.noteOn(36, 1.0f);
                    bool matches = true;
                    for (std::size_t start = 0; start < input.size(); start += blockSize)
                    {
                        const auto count = std::min(blockSize, input.size() - start);
                        auto output = makeBlock(34, count);
                        engine.process(output, count);
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            const auto pos = start + i;
                            const float expected = 0.5985786945f * (bypassed ? input[pos]
                                : fixture.expected[pos % 5] * (pos < 5 ? 1.0f : -1.0f));
                            matches = matches && std::abs(output[0][i] - expected) < 2e-6f;
                        }
                    }
                    check(matches, "native Drive matches signed golden samples independently of Aux3, rate and block size");
                }
}

void testNativeLengthPrecedesDrive()
{
    // Native Drive-only captures of One Shot notes 36/38/49 are a static
    // transfer of their already-faded dry captures (relative error < 8e-8).
    // At 24 dB this input remains clipped even after the measured Length fade.
    for (const std::size_t blockSize : { 17u, 128u })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        engine.setPadSample(0, { 96000.0, { std::vector<float>(96692, 0.25f) } });
        auto pad = dryPadSettings();
        pad.layerInputGain = 1.995262315f;
        pad.outputTrim = 0.5011872336f;
        pad.lengthEnvelopeEnabled = true;
        pad.nativeDriveInsert = true;
        pad.driveEnabled = true;
        pad.driveGainDb = 24.0f;
        engine.setPadSettings(0, pad);
        engine.noteOn(36, 1.0f);
        constexpr std::array<std::size_t, 3> probes { 0, 17000, 28000 };
        for (std::size_t offset = 0; offset <= probes.back(); offset += blockSize)
        {
            const auto count = std::min(blockSize, probes.back() + 1 - offset);
            auto output = makeBlock(34, count);
            engine.process(output, count);
            for (const auto frame : probes)
                if (frame >= offset && frame < offset + count)
                    check(std::abs(output[0][frame - offset]
                        - 0.2004748935f * std::sqrt(0.5f)) < 2e-7f,
                        "One Shot Length fades the input to Drive before saturation");
        }
    }
}

void testNativeDriveGatedRelease()
{
    // Original driven gated captures distinguish a 0.7 + 0.3*r input fade
    // and a linear r output fade from applying their product after Drive.
    for (const bool scheduled : { false, true })
    {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, 64);
        auto settings = dryPadSettings();
        settings.layerInputGain = 1.995262315f;
        settings.outputTrim = 0.5011872336f;
        settings.oneShot = false;
        settings.releaseSeconds = 0.03f;
        settings.pan = -1.0f;
        settings.nativeDriveInsert = true;
        settings.driveEnabled = true;
        settings.driveGainDb = 24.0f;
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { 48000.0, { std::vector<float>(4800, 1.0f) } });
        engine.noteOn(36, 1.0f, scheduled ? 64 : 0);
        auto before = makeBlock(34, 64);
        engine.process(before, 64);
        if (!scheduled)
            engine.noteOff(36);
        auto tail = makeBlock(34, 1600);
        engine.process(tail, 1600);
        for (const std::size_t frame : { 0u, 360u, 720u, 1080u })
        {
            const float remaining = 1.0f - static_cast<float>(frame + 1) / 1440.0f;
            check(std::abs(tail[0][frame] - 0.2004748935f * remaining) < 2e-6f,
                  "saturated native Drive has a linear gated output release");
        }
        check(tail[0][1440] == 0.0f, "driven gated voice finishes its release");
    }
}

void testNativeDriveAutomationRamp()
{
    // Native PadVoice prepare/start plus cedd40/cedbb0: 100 ms linear
    // post-gain ramp, advanced once per stereo frame, frozen while bypassed.
    constexpr float dryVoiceScale = 0.2875f * 0.7071067811865475f;
    constexpr float voiceScale = dryVoiceScale * 0.5011872336f;
    const auto render = [](bsl::AudioEngine& engine, std::size_t frames, std::size_t blockSize) {
        auto result = makeBlock(2, frames);
        for (std::size_t start = 0; start < frames; start += blockSize)
        {
            const auto count = std::min(blockSize, frames - start);
            auto block = makeBlock(34, count);
            engine.process(block, count);
            for (std::size_t channel = 0; channel < 2; ++channel)
                std::copy_n(block[channel + 2].begin(), count, result[channel].begin() + start);
        }
        return result;
    };
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const std::size_t blockSize : { 1, 17, 257, 16384 })
        {
            const auto rampFrames = static_cast<std::size_t>(rate * 0.1);
            bsl::AudioEngine engine;
            engine.prepare(rate, 257);
            engine.setRoutingMode(bsl::RoutingMode::multi);
            auto settings = dryPadSettings();
            settings.layerInputGain = 1.995262315f;
            settings.outputTrim = 0.5011872336f;
            settings.driveEnabled = true;
            settings.driveGainDb = 0.0f;
            engine.setPadSettings(0, settings);
            engine.setPadSample(0, { rate, { std::vector<float>(rampFrames * 5, 1.0f),
                                           std::vector<float>(rampFrames * 5, -1.0f) } });
            engine.noteOn(36, 0.25f);
            render(engine, 16, blockSize);
            settings.driveGainDb = 24.0f;
            engine.setPadSettings(0, settings);
            const auto first = render(engine, rampFrames / 2, blockSize);
            bool matches = true;
            for (std::size_t i = 0; i < first[0].size(); ++i)
            {
                const float expected = (1.0f - 0.6f * static_cast<float>(i + 1)
                                                  / static_cast<float>(rampFrames)) * voiceScale;
                matches = matches && std::abs(first[0][i] - expected) < 4e-5f
                                  && std::abs(first[1][i] + expected) < 4e-5f;
            }
            check(matches, "Drive post-gain ramps for 100 ms across sample rates, blocks and stereo channels");

            settings.driveEnabled = false;
            engine.setPadSettings(0, settings);
            const auto bypassed = render(engine, 113, blockSize);
            check(std::abs(bypassed[0].back() - dryVoiceScale) < 1e-6f,
                  "bypassing an in-flight Drive ramp is dry");
            settings.driveEnabled = true;
            engine.setPadSettings(0, settings);
            const auto rest = render(engine, rampFrames - first[0].size() + 7, blockSize);
            const float resumed = (1.0f - 0.6f * static_cast<float>(first[0].size() + 1)
                                                      / static_cast<float>(rampFrames)) * voiceScale;
            check(std::abs(rest[0].front() - resumed) < 4e-5f
                    && std::abs(rest[0].back() - 0.4f * voiceScale) < 1e-6f,
                  "Drive bypass freezes ramp progress and resumes to the exact endpoint");

            settings.driveGainDb = 12.0f;
            engine.setPadSettings(0, settings);
            const auto upward = render(engine, rampFrames / 2, blockSize);
            check(std::abs(upward[0].back() - 0.55f * voiceScale) < 4e-5f,
                  "Drive automation ramps upward from its current gain");
            settings.driveGainDb = 24.0f;
            engine.setPadSettings(0, settings);
            const auto retargeted = render(engine, rampFrames + 1, blockSize);
            check(std::abs(retargeted[0].front() - (0.55f - 0.15f / rampFrames) * voiceScale) < 4e-5f
                    && std::abs(retargeted[0].back() - 0.4f * voiceScale) < 1e-6f,
                  "retargeting Drive restarts the ramp from the current value");

            settings.driveGainDb = 12.0f;
            settings.mono = true;
            engine.setPadSettings(0, settings);
            engine.noteOn(36, 0.25f);
            const auto retriggered = render(engine, 1, blockSize);
            check(std::abs(retriggered[0][0] - 0.7f * voiceScale) < 1e-6f,
                  "new Drive voices start at the current target without a stale ramp");
        }
}

void testPadBypassesAreDryInvariantAndDriveControlsStayDistinct()
{
    const bsl::SampleData source { 48000.0, { { 0.2f, -0.4f, 0.7f, -0.8f } } };
    const auto render = [&source](bsl::PadPlaybackSettings settings) {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0);
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, source);
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, source.length());
        engine.process(output, source.length());
        return output[0];
    };

    const auto dry = render(dryPadSettings());
    auto bypassed = dryPadSettings();
    bypassed.filterEnabled = false;
    bypassed.cutoff = 0.0f;
    bypassed.resonance = 1.0f;
    bypassed.driveEnabled = false;
    bypassed.nativeDriveInsert = false;
    bypassed.driveGainDb = 60.0f;
    bypassed.distortionMix = 1.0f;
    check(render(bypassed) == dry, "disabled pad filter and drive are sample-exact bypasses");

    bypassed.driveEnabled = true;
    bypassed.distortionMix = 0.0f;
    check(render(bypassed) == dry, "zero parallel distortion mix preserves the dry signal");

    auto lowDrive = dryPadSettings();
    lowDrive.nativeDriveInsert = false;
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
        prepareDryEngine(engine, 48000.0);
        auto settings = dryPadSettings();
        settings.filterEnabled = enabled;
        settings.cutoff = 0.0f;
        settings.resonance = 1.0f;
        engine.setPadSettings(0, settings);
        setDspInput(engine, 0, alternating);
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
        prepareDryEngine(engine, sampleRate);
        auto settings = dryPadSettings();
        settings.attackNormalized = 0.01f; // Ten milliseconds.
        engine.setPadSettings(0, settings);
        setDspInput(engine, 0, { sampleRate, { std::vector<float>(1024, 1.0f) } });
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
    prepareDryEngine(secondsEngine, 48000.0);
    auto secondsSettings = dryPadSettings();
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
        prepareDryEngine(engine, 48000.0);
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { 48000.0, { ramp } });
        engine.noteOn(36, 1.0f);
        auto output = makeBlock(34, ramp.size());
        engine.process(output, ramp.size());
        return output[0];
    };
    const auto dry = render(dryPadSettings());

    auto fine = dryPadSettings();
    fine.fineCents = 100.0f;
    const auto fineOut = render(fine);
    check(std::abs(fineOut[17] - dry[17]) > 0.005f,
          "fine pitch reaches its positive endpoint independently");

    auto highPass = dryPadSettings();
    highPass.highPass = 1.0f;
    highPass.highPassEnabled = true;
    const auto highPassed = render(highPass);
    check(std::abs(highPassed[20]) < std::abs(dry[20]) * 0.2f,
          "pad high-pass reaches its maximum cutoff endpoint");

    auto compression = dryPadSettings();
    compression.compression = 1.0f;
    const auto compressed = render(compression);
    check(compressed[10] > dry[10], "pad compression endpoint changes gain independently");

    auto crush = dryPadSettings();
    crush.crush = 1.0f;
    const auto crushed = render(crush);
    check(crushed[0] == 0.0f && crushed[0] == crushed[20],
          "pad crush endpoint quantizes the initial sample and retains it across this short buffer");
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
        const auto delayFrame = static_cast<std::size_t>(sampleRate * 0.25);
        check(roomOut == dry && reverbOut == dry,
              "legacy master ambience fields do not invent undocumented echo returns");
        check(std::abs(delayOut[delayFrame]) > 0.4f,
              "legacy delay retains its independent quarter-second return");
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
    prepareDryEngine(engine, 48000.0);
    engine.setPadSample(0, { 48000.0, { { 1.0f } } });
    engine.setPadSample(1, { 48000.0, { { 0.5f } } });
    auto first = dryPadSettings();
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
    prepareDryEngine(muted, 48000.0);
    muted.setPadSample(0, { 48000.0, { { 1.0f, 0.8f, 0.6f } } });
    auto mutedSettings = dryPadSettings();
    mutedSettings.muted = true;
    muted.setPadSettings(0, mutedSettings);
    muted.noteOn(36, 1.0f);
    // Include the normalized endpoint and the finished sentinel while hidden.
    auto hidden = makeBlock(34, 5);
    muted.process(hidden, 5);
    mutedSettings.muted = false;
    muted.setPadSettings(0, mutedSettings);
    auto revealed = makeBlock(34, 1);
    muted.process(revealed, 1);
    check(revealed[0][0] == 0.0f,
          "a note that elapsed while muted is not replayed after unmute");

    bsl::AudioEngine soloed;
    prepareDryEngine(soloed, 48000.0);
    soloed.setPadSample(0, { 48000.0, { { 1.0f, 0.8f, 0.6f } } });
    soloed.setPadSample(1, { 48000.0, { { 0.5f, 0.4f, 0.3f } } });
    auto soloSettings = dryPadSettings();
    soloSettings.solo = true;
    soloed.setPadSettings(1, soloSettings);
    soloed.noteOn(36, 1.0f);
    soloed.noteOn(37, 1.0f);
    hidden = makeBlock(34, 5);
    soloed.process(hidden, 5);
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
    prepareDryEngine(engine, 48000.0);
    bsl::MasterPlaybackSettings master;
    master.delay = 1.0f;
    auto pad = dryPadSettings();
    pad.delay = 1.0f;
    engine.setPadSettings(0, pad);
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

void testSharedDistortionBus()
{
    const auto render = [](bool split, float drive, bool responseAvailable) {
        bsl::AudioEngine engine;
        prepareDryEngine(engine, 48000.0, 128);
        if (responseAvailable)
            engine.setDistortionResponse({std::vector<float>{0.125f, 0.02f},
                                          std::vector<float>{0.1f, -0.01f}});
        bsl::MasterPlaybackSettings master;
        master.distortionReturn = 1.0f;
        master.distortionDrive = drive;
        engine.setMasterSettings(master);
        for (std::size_t pad = 0; pad < (split ? 2u : 1u); ++pad)
        {
            std::vector<float> input(2048);
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = (split ? 0.15f : 0.3f) * std::sin(static_cast<float>(i) * 0.11f);
            engine.setPadSample(pad, {48000.0, {input}});
            auto settings = dryPadSettings();
            settings.driveEnabled = false;
            settings.independentDistortion = true;
            settings.distortionMix = 1.0f;
            engine.setPadSettings(pad, settings);
        }
        settleMainGain(engine, 48000.0);
        engine.noteOn(36, 1.0f);
        if (split) engine.noteOn(37, 1.0f);
        auto output = makeBlock(34, 2048);
        engine.process(output, 2048);
        return output;
    };
    const auto summed = render(false, 0.7f, true), split = render(true, 0.7f, true);
    float error = 0.0f;
    for (std::size_t channel = 0; channel < 2; ++channel)
        for (std::size_t i = 0; i < summed[channel].size(); ++i)
            error = std::max(error, std::abs(summed[channel][i] - split[channel][i]));
    check(error < 2.0e-6f, "Aux3 distorts the combined pad sends rather than each pad separately");
    const auto zero = render(false, 0.0f, true), dry = render(false, 0.0f, false);
    float zeroWet = 0.0f, driveDifference = 0.0f;
    for (std::size_t i = 0; i < zero[0].size(); ++i)
    {
        zeroWet = std::max(zeroWet, std::abs(zero[0][i] - dry[0][i]));
        driveDifference = std::max(driveDifference, std::abs(zero[0][i] - summed[0][i]));
    }
    check(zeroWet > 0.001f, "zero Drive still returns the native filtered distortion signal");
    check(driveDifference > 0.001f, "the global Aux3 Drive changes the shared nonlinear curve");
}

void testDistortionResponseLifecycleAndPartitioning()
{
    using Stereo = std::array<std::vector<float>, 2>;
    const auto signal = [] {
        Stereo result {std::vector<float>(12000), std::vector<float>(12000)};
        for (std::size_t i = 4800; i < 8000; ++i)
        {
            result[0][i] = 0.2f * std::sin(static_cast<float>(i) * 0.13f);
            result[1][i] = -0.12f * std::cos(static_cast<float>(i) * 0.07f);
        }
        return result;
    };
    const auto run = [](bsl::AuxDistortion& processor, Stereo& data, std::size_t block) {
        for (std::size_t i = 0; i < data[0].size(); i += block)
            processor.process(data[0].data() + i, data[1].data() + i,
                              std::min(block, data[0].size() - i), 0.8f);
    };
    const auto isSilent = [](const Stereo& data) {
        return std::all_of(data[0].begin(), data[0].end(), [](float x) { return x == 0; })
            && std::all_of(data[1].begin(), data[1].end(), [](float x) { return x == 0; });
    };
    Stereo reference;
    for (const auto block : {17u, 128u, 511u})
    {
        bsl::AuxDistortion processor;
        processor.prepare(48000);
        processor.setResponse({std::vector<float>{0.1f, 0.02f}, std::vector<float>{0.12f, -0.01f}});
        auto data = signal();
        run(processor, data, block);
        if (reference[0].empty()) reference = data;
        check(data == reference, "Aux3 response and gain ramp are independent of callback partitions");
        processor.reset();
        data = signal();
        run(processor, data, block);
        check(data == reference, "Aux3 reset clears filter, gain and FIR history reproducibly");
        processor.prepare(96000);
        data = signal(); run(processor, data, block);
        check(isSilent(data), "rate changes cannot reuse an IR prepared for the old rate");
        processor.setResponse({std::vector<float>{1}, std::vector<float>{1, 2}});
        data = signal(); run(processor, data, block);
        check(isSilent(data), "malformed stereo responses have silent wet output");
    }
}

void testAudioCallbacksDoNotAllocateOnFirstOrLargerBlock()
{
    bsl::AudioEngine engine;
    prepareDryEngine(engine, 48000.0);
    engine.setPadSample(0, { 48000.0, { std::vector<float>(16384, 0.5f) } });
    auto pad = dryPadSettings();
    pad.auxGain = 0.5f;
    pad.independentDistortion = true;
    pad.distortionMix = 0.7f;
    pad.distortionDrive = 0.8f;
    engine.setPadSettings(0, pad);
    bsl::MasterPlaybackSettings master;
    master.distortionReturn = 0.5f;
    engine.setDistortionResponse({ std::vector<float>{1.0f, 0.1f}, std::vector<float>{1.0f, -0.1f} });
    engine.setMasterSettings(master);
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
    testNativeNormalizedCubicPlayback();
    testNativeNonzeroSampleEndpoint();
    testNativeLayerGainPrecedesHighPass();
    testNativeHighPassSurvivesVoiceReuseAcrossPads();
    testNativeSilentReleaseAdvancesHighPass();
    testNativeInputReleaseIndependentOfPadRelease();
    testNativeGlobalVoicePoolAndMonoOwnership();
    testNativeVelocityAmplitude();
    testMonoRetriggerAndOverlappingNoteOffs();
    testPadMetersTrackRenderedAudio();
    testStereoPlaybackUsesNormalizedPhaseAtUnity();
    testMixedSampleRatesPreserveDurationAndStereoContent();
    testMultiModeRoutesEachPadToItsOwnAux();
    testAllSixteenPadsKeepTheirThirtyFourChannelRouting();
    testRetriggerRestartsOneShot();
    testScheduledTriggerStartsAtExactSampleOffset();
    testNativeGatedRelease();
    testNativeGatedRetriggerRelease();
    testGatedPadStopsOnScheduledNoteOff();
    testSequencedGatesArePerVoiceAcrossCallbacks();
    testHostNoteOffReleasesMatchingGatedVoices();
    testExplicitSequencerReleaseOwnership();
    testMuteAndSoloStopNewSendsWithoutClearingSharedTails();
    testPitchAndDampAlterPlaybackWithoutAllocationContractChanges();
    testNativeLengthUsesLoadedSampleDuration();
    testNativeLengthFloatAccumulation();
    testGatedPlaybackDoesNotUseOneShotLength();
    testNativePadPitchUsesEqualTemperament();
    testPadLowPassAndCompressionChangeDrySignal();
    testUnavailableAmbienceDoesNotInventEchoes();
    testMasterProcessingAffectsStereoButIsBypassedInMultiMode();
    testModuleBypassesPreserveVolumeAndRouting();
    testAuxiliaryReturnsPreserveDryAndAmbienceSends();
    testAuxGainTaperStartupAndRetrigger();
    testMainGainStartupAndRouting();
    testMainGainRetargeting();
    testMasterCompressorNeutralRangePreservesSignal();
    testNativeMasterCompressorSteadyState();
    testNativeMasterCompressorDynamicsAndStartup();
    testMeasuredMasterCrushUsesSeventeenFrameHoldAtPointEight();
    testNativeMasterCrusherMappingAndAutomation();
    testNativePadCrusherMappingAndAutomation();
    testNativePadCompressorSteadyStateAndSumming();
    testPadCompressorNeutralRangePreservesHeadroom();
    testPadCompressorLifecycleAndAutomation();
    testNativePadOutputTrim();
    testPadVolumeSmoothingAndRetrigger();
    testNativeMasterFilterResponseAndBypass();
    testNativePadFilterResponseAndEffectOrder();
    testNativePadHighPassResponse();
    testNativeEnvelopePrecedesHighPass();
    testTwoFactoryLayersMixContinuously();
    testNativeLayerMixGainPrecision();
    testNativeDriveInsertCurveAndBypass();
    testNativeLengthPrecedesDrive();
    testNativeDriveGatedRelease();
    testNativeDriveAutomationRamp();
    testPadBypassesAreDryInvariantAndDriveControlsStayDistinct();
    testPadFilterEnableAndNormalizedAttack();
    testRemainingPadEffectsReachTheirEndpointsIndependently();
    testMasterBypassesAndDistinctEffectsAcrossRatesAndBlocks();
    testMuteSoloAndPanGainEndpoints();
    testMutedAndNonSoloVoicesDoNotReplayStaleNotes();
    testRoutingTransitionsDiscardFrozenMasterTail();
    testSharedDistortionBus();
    testDistortionResponseLifecycleAndPartitioning();
    testAudioCallbacksDoNotAllocateOnFirstOrLargerBlock();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All audio tests passed\n";
    return EXIT_SUCCESS;
}
