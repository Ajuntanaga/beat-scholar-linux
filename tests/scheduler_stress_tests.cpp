#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    BeatScholarLinuxProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    std::atomic<bool> start { false };
    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(
            std::max(2, processor.getTotalNumOutputChannels()), 64);
        juce::MidiBuffer midi;
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (std::size_t index = 0; index < 20000; ++index)
        {
            buffer.clear();
            midi.clear();
            processor.triggerPad(0, 1.0f);
            processor.processBlock(buffer, midi);
        }
    });
    std::thread uiThread([&] {
        start.store(true, std::memory_order_release);
        for (std::size_t index = 0; index < 20000; ++index)
        {
            processor.stopStandalone();
            processor.playStandalone();
            if (index % 32 == 0)
            {
                auto& state = processor.projectState();
                state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(
                    1 + index % 7, static_cast<std::uint8_t>(index % 16), 0.8f);
                processor.markProjectDirty();
                processor.publishProjectStateIfNeeded();
            }
        }
    });

    uiThread.join();
    audioThread.join();

    juce::AudioBuffer<float> buffer(
        std::max(2, processor.getTotalNumOutputChannels()), 64);
    juce::MidiBuffer midi;
    processor.playStandalone();
    processor.triggerPad(0, 1.0f);
    processor.processBlock(buffer, midi);
    float activeEnergy = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        activeEnergy += std::abs(buffer.getSample(0, sample));
    if (activeEnergy <= 0.0f)
        return EXIT_FAILURE;

    processor.stopStandalone();
    buffer.clear();
    midi.clear();
    processor.processBlock(buffer, midi);
    float stoppedEnergy = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        stoppedEnergy += std::abs(buffer.getSample(0, sample));
    if (stoppedEnergy != 0.0f)
        return EXIT_FAILURE;

    std::cout << "Processor stop/reset and snapshot publication TSAN stress passed\n";
    return EXIT_SUCCESS;
}
