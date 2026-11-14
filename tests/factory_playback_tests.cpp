#include "plugin/PluginProcessor.h"
#include "plugin/DataPaths.h"

#include <algorithm>
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
    const juce::String mode = argc > 2 ? argv[2] : "";
    bool requireAmbience = false, exportStates = false;
    for (int index = 3; index < argc; ++index)
    {
        const juce::String option(argv[index]);
        if (option == "--require-ambience") requireAmbience = true;
        else if (option == "--export-states") exportStates = true;
        else { std::cerr << "Unknown option: " << option << '\n'; return 1; }
    }
    const bool allPatterns = mode == "--all-patterns";
    const bool allPresets = allPatterns || mode == "--all";
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
            int reportPattern = -1;
            const auto reportFailure = [&](const juce::String& reason) {
                ++failures;
                auto* row = new juce::DynamicObject();
                row->setProperty("preset", name);
                row->setProperty("sample_rate", rate);
                row->setProperty("result", "fail");
                row->setProperty("error", reason);
                if (reportPattern >= 0) row->setProperty("pattern_index", reportPattern);
                std::cout << juce::JSON::toString(juce::var(row), true) << std::endl;
            };
            auto processor = std::make_unique<BeatScholarLinuxProcessor>();
            processor->setRateAndBufferSizeDetails(rate, 64);
            processor->prepareToPlay(rate, 64);
            if (requireAmbience && !processor->hasFactoryAmbience())
            {
                reportFailure("Required Room/Plate responses are unavailable");
                continue;
            }
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
            const auto firstPattern = allPatterns ? std::size_t { 0 } : processor->projectState().activePattern;
            const auto endPattern = allPatterns ? processor->projectState().patterns.size() : firstPattern + 1;
            for (auto patternIndex = firstPattern; patternIndex < endPattern; ++patternIndex)
            {
                reportPattern = static_cast<int>(patternIndex);
                processor->stopStandalone();
                processor->selectPatternFromUi(patternIndex);
                if (exportStates)
                {
                    processor->getStateInformation(state);
                    const auto stateFile = output.getChildFile(stem + "-pattern-"
                        + juce::String(static_cast<int>(patternIndex)) + ".bslx");
                    if (!stateFile.replaceWithData(state.getData(), state.getSize())) return 1;
                }
                const auto& project = processor->projectState();
                const auto& pattern = project.patterns[patternIndex];
                const double seconds = allPatterns
                    ? std::max(4.0, pattern.durationInQuarterNotes() * 60.0 / project.global.beatsPerMinute)
                    : 4.0;
                const auto totalFrames = static_cast<std::int64_t>(std::ceil(seconds * rate));
                const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
                    [](const auto& lane) { return lane.solo; });
                bool expectedNotes = false;
                if (project.global.sequencerEnabled)
                    for (const auto& lane : pattern.lanes)
                        if (!lane.muted && (!hasSolo || lane.solo))
                            for (const auto& beat : lane.beats)
                                for (const auto& slice : beat.slices)
                                    expectedNotes |= slice.enabled;
                processor->playStandalone();
                double sumSquares = 0.0, peak = 0.0;
                bool finiteAudio = true;
                std::size_t midiNotes = 0, clipped = 0;
                juce::AudioBuffer<float> buffer(processor->getTotalNumOutputChannels(), 64);
                juce::MidiBuffer midi;
                for (std::int64_t frame = 0; frame < totalFrames; frame += 64)
                {
                    const int frames = static_cast<int>(std::min<std::int64_t>(64, totalFrames - frame));
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
                const double rms = std::sqrt(sumSquares / (static_cast<double>(totalFrames) * 2.0));
                const bool expectedAudio = processor->projectState().global.samplerEnabled && midiNotes > 0;
                const bool missingNotes = allPatterns && expectedNotes && midiNotes == 0;
                const bool failed = missingNotes || (allPresets ? (expectedAudio && rms < 1.0e-5)
                                                               : (midiNotes == 0 || rms < 1.0e-5));
                failures += failed;
                if (allPresets)
                {
                    auto* row = new juce::DynamicObject();
                    row->setProperty("preset", name);
                    row->setProperty("sample_rate", rate);
                    row->setProperty("pattern_index", static_cast<int>(patternIndex));
                    row->setProperty("pattern_name", juce::String(pattern.name));
                    row->setProperty("duration_seconds", seconds);
                    row->setProperty("expected_notes", expectedNotes);
                    if (missingNotes) row->setProperty("error", "Enabled pattern emitted no notes");
                    row->setProperty("note_ons", static_cast<juce::int64>(midiNotes));
                    row->setProperty("factory_ambience", processor->hasFactoryAmbience());
                    row->setProperty("rms", rms);
                    row->setProperty("peak", peak);
                    row->setProperty("clipped_samples", static_cast<juce::int64>(clipped));
                    row->setProperty("result", failed ? "fail" : "pass");
                    std::cout << juce::JSON::toString(juce::var(row), true) << std::endl;
                }
                else
                    std::cout << name << " rate=" << rate << " notes=" << midiNotes
                              << " rms=" << rms << " peak=" << peak << " clipped=" << clipped << '\n';
            }
            processor->releaseResources();
        }
    }
    return failures == 0 ? 0 : 1;
}
