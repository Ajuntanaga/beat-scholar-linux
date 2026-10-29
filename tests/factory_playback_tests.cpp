#include "plugin/PluginProcessor.h"
#include "plugin/DataPaths.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    const auto factory = bsl::dataRoot().getChildFile("Factory");
    if (!factory.getChildFile("sample-manifest.json").existsAsFile())
    {
        std::cout << "Private factory library unavailable; skipped\n";
        return 77;
    }
    const auto manifest = juce::JSON::parse(factory.getChildFile("sample-manifest.json"));
    if (int(manifest["schemaVersion"]) < 2)
    {
        std::cerr << "Factory library predates the resource identity repair\n";
        return 1;
    }
    const juce::File output(argc > 1 ? argv[1] : "/tmp/beat-scholar-factory-playback");
    if (output.createDirectory().failed()) return 1;
    const bool allPresets = argc > 2 && juce::String(argv[2]) == "--all";
    const auto presetRoot = factory.getChildFile("Presets");
    juce::Array<juce::File> files;
    if (allPresets)
        files = presetRoot.findChildFiles(juce::File::findFiles, true, "*.json");
    else
        for (const auto* name : { "Rock/Drummer Soundcheck", "Electronic/SikBit", "TRAP/Trap Hats 3" })
            files.add(presetRoot.getChildFile(juce::String(name) + ".json"));
    const std::vector<int> rates = allPresets ? std::vector<int>{48000} : std::vector<int>{44100, 48000, 96000};
    if (files.isEmpty()) { std::cerr << "No factory presets found\n"; return 1; }
    int failures = 0;
    for (const auto& file : files)
    {
        if (file.getFileNameWithoutExtension().equalsIgnoreCase("Version")) continue;
        const auto name = file.getRelativePathFrom(presetRoot).dropLastCharacters(5);
        for (const int rate : rates)
        {
            const auto reportFailure = [&](const juce::String& reason) {
                ++failures;
                auto* row = new juce::DynamicObject();
                row->setProperty("preset", name);
                row->setProperty("sample_rate", rate);
                row->setProperty("result", "fail");
                row->setProperty("error", reason);
                std::cout << juce::JSON::toString(juce::var(row), true) << std::endl;
            };
            auto processor = std::make_unique<BeatScholarLinuxProcessor>();
            processor->setRateAndBufferSizeDetails(rate, 64);
            processor->prepareToPlay(rate, 64);
            if (!processor->loadPresetFile(file))
            {
                reportFailure("Cannot import preset");
                continue;
            }
            bool resourcesPresent = true;
            for (const auto& pad : processor->projectState().kit.pads)
                for (const auto& id : pad.factorySampleIds)
                    if (!id.empty() && !factory.getChildFile("Samples/" + juce::String(id)).existsAsFile())
                    {
                        resourcesPresent = false;
                    }
            if (!resourcesPresent) { reportFailure("Missing factory resource"); continue; }
            juce::MemoryBlock state;
            processor->getStateInformation(state);
            const auto stem = juce::String(name).replaceCharacter('/', '_');
            if (!allPresets && !output.getChildFile(stem + ".bslx").replaceWithData(state.getData(), state.getSize())) return 1;
            processor->playStandalone();
            double sumSquares = 0.0, peak = 0.0;
            bool finiteAudio = true;
            std::size_t midiNotes = 0, clipped = 0;
            juce::AudioBuffer<float> buffer(processor->getTotalNumOutputChannels(), 64);
            juce::MidiBuffer midi;
            for (int frame = 0; frame < rate * 4; frame += 64)
            {
                const int frames = std::min(64, rate * 4 - frame);
                buffer.setSize(processor->getTotalNumOutputChannels(), frames, false, false, true);
                midi.clear();
                processor->processBlock(buffer, midi);
                for (const auto event : midi)
                    midiNotes += event.getMessage().isNoteOn();
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < frames; ++sample)
                    {
                        const double value = buffer.getSample(channel, sample);
                        if (!std::isfinite(value)) { finiteAudio = false; continue; }
                        peak = std::max(peak, std::abs(value));
                        sumSquares += value * value;
                        clipped += std::abs(value) >= 1.0;
                    }
            }
            if (!finiteAudio) { reportFailure("Non-finite audio"); continue; }
            const double rms = std::sqrt(sumSquares / (rate * 8));
            const bool expectedAudio = processor->projectState().global.samplerEnabled && midiNotes > 0;
            const bool failed = allPresets ? (expectedAudio && rms < 1.0e-5)
                                          : (midiNotes == 0 || rms < 1.0e-5);
            failures += failed;
            if (allPresets)
            {
                auto* row = new juce::DynamicObject();
                row->setProperty("preset", name);
                row->setProperty("sample_rate", rate);
                row->setProperty("note_ons", static_cast<juce::int64>(midiNotes));
                row->setProperty("rms", rms);
                row->setProperty("peak", peak);
                row->setProperty("clipped_samples", static_cast<juce::int64>(clipped));
                row->setProperty("result", failed ? "fail" : "pass");
                std::cout << juce::JSON::toString(juce::var(row), true) << std::endl;
            }
            else
                std::cout << name << " rate=" << rate << " notes=" << midiNotes
                          << " rms=" << rms << " peak=" << peak << " clipped=" << clipped << '\n';
            processor->releaseResources();
        }
    }
    return failures == 0 ? 0 : 1;
}
