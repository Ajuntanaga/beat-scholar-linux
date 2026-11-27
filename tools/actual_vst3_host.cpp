#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr auto expectedDefaultCid = "ABCDEF019182FAEB416A756E42736C78";
constexpr auto expectedDefaultName = "Beat Scholar Linux";
constexpr auto expectedDefaultVendor = "Ajuntanaga";

struct MidiEvent
{
    std::int64_t sample = 0;
    int note = 36;
    int velocity = 127;
    bool controller = false;
    int channel = 1;
};

struct ParameterEvent
{
    std::int64_t sample;
    int index;
    float value;
};

struct Options
{
    juce::File bundle;
    juce::File state;
    juce::File output;
    juce::File restoredStateOutput;
    juce::String expectedSha256;
    juce::String expectedCid { expectedDefaultCid };
    juce::String expectedName { expectedDefaultName };
    juce::String expectedVendor { expectedDefaultVendor };
    double sampleRate = 48000.0;
    double tempo = 120.0;
    int blockSize = 64;
    std::int64_t activeFrames = 0;
    std::int64_t preRollFrames = 0;
    std::int64_t startupWaitMs = 0;
    std::int64_t parameterSettleMs = 0;
    std::int64_t postStopFrames = 0;
    std::int64_t restartFrames = 0;
    bool realtimePacing = false;
    bool playSequencer = false;
    bool floatOutput = false;
    bool allOutputs = false;
    bool rawComponentState = false;
    std::vector<MidiEvent> midi;
    std::vector<ParameterEvent> parameters;
};

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

std::int64_t parseInt64(const std::string& value, const char* option)
{
    std::size_t consumed = 0;
    const auto result = std::stoll(value, &consumed);
    if (consumed != value.size())
        fail(std::string(option) + ": invalid integer");
    return result;
}

double parseDouble(const std::string& value, const char* option)
{
    std::size_t consumed = 0;
    const auto result = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(result))
        fail(std::string(option) + ": invalid number");
    return result;
}

MidiEvent parseMidi(const std::string& value)
{
    juce::StringArray fields;
    fields.addTokens(value, ":", {});
    fields.trim();
    if (fields.size() != 3 && fields.size() != 4)
        fail("--midi must be SAMPLE:NOTE:VELOCITY[:CHANNEL]");
    MidiEvent event;
    event.sample = parseInt64(fields[0].toStdString(), "--midi sample");
    event.note = static_cast<int>(parseInt64(fields[1].toStdString(), "--midi note"));
    event.velocity = static_cast<int>(parseInt64(fields[2].toStdString(), "--midi velocity"));
    if (fields.size() == 4)
        event.channel = static_cast<int>(parseInt64(fields[3].toStdString(), "--midi channel"));
    if (event.sample < 0 || event.note < 0 || event.note > 127
        || event.velocity < 0 || event.velocity > 127 || event.channel < 1 || event.channel > 16)
        fail("--midi values are outside SAMPLE>=0, NOTE=0..127, VELOCITY=0..127, CHANNEL=1..16");
    return event;
}

ParameterEvent parseParameter(const std::string& value)
{
    juce::StringArray fields;
    fields.addTokens(value, ":", {});
    if (fields.size() != 3)
        fail("--parameter must be SAMPLE:INDEX:NORMALIZED_VALUE");
    const auto sample = parseInt64(fields[0].toStdString(), "--parameter sample");
    const auto index = parseInt64(fields[1].toStdString(), "--parameter index");
    const auto normalized = parseDouble(fields[2].toStdString(), "--parameter value");
    if (sample < 0 || index < 0 || index > std::numeric_limits<int>::max()
        || normalized < 0.0 || normalized > 1.0)
        fail("--parameter requires nonnegative sample/index and value in 0..1");
    return { sample, static_cast<int>(index), static_cast<float>(normalized) };
}

Options parseOptions(int argc, char** argv)
{
    Options options;
    const auto next = [&argc, &argv](int& index, const char* name) -> std::string {
        if (++index >= argc)
            fail(std::string(name) + ": missing value");
        return argv[index];
    };

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--plugin") options.bundle = juce::File(next(index, "--plugin"));
        else if (argument == "--state") options.state = juce::File(next(index, "--state"));
        else if (argument == "--output") options.output = juce::File(next(index, "--output"));
        else if (argument == "--restored-state-output") options.restoredStateOutput = juce::File(next(index, "--restored-state-output"));
        else if (argument == "--expect-sha256") options.expectedSha256 = next(index, "--expect-sha256");
        else if (argument == "--expect-cid") options.expectedCid = next(index, "--expect-cid");
        else if (argument == "--expect-name") options.expectedName = next(index, "--expect-name");
        else if (argument == "--expect-vendor") options.expectedVendor = next(index, "--expect-vendor");
        else if (argument == "--sample-rate") options.sampleRate = parseDouble(next(index, "--sample-rate"), "--sample-rate");
        else if (argument == "--tempo") options.tempo = parseDouble(next(index, "--tempo"), "--tempo");
        else if (argument == "--block-size") options.blockSize = static_cast<int>(parseInt64(next(index, "--block-size"), "--block-size"));
        else if (argument == "--active-frames") options.activeFrames = parseInt64(next(index, "--active-frames"), "--active-frames");
        else if (argument == "--parameter-settle-ms") options.parameterSettleMs = parseInt64(next(index, "--parameter-settle-ms"), "--parameter-settle-ms");
        else if (argument == "--startup-wait-ms") options.startupWaitMs = parseInt64(next(index, "--startup-wait-ms"), "--startup-wait-ms");
        else if (argument == "--pre-roll-frames") options.preRollFrames = parseInt64(next(index, "--pre-roll-frames"), "--pre-roll-frames");
        else if (argument == "--post-stop-frames") options.postStopFrames = parseInt64(next(index, "--post-stop-frames"), "--post-stop-frames");
        else if (argument == "--restart-frames") options.restartFrames = parseInt64(next(index, "--restart-frames"), "--restart-frames");
        else if (argument == "--parameter") options.parameters.push_back(parseParameter(next(index, "--parameter")));
        else if (argument == "--midi") options.midi.push_back(parseMidi(next(index, "--midi")));
        else if (argument == "--cc")
        {
            auto event = parseMidi(next(index, "--cc"));
            event.controller = true;
            options.midi.push_back(event);
        }
        else if (argument == "--realtime-pacing") options.realtimePacing = true;
        else if (argument == "--play-sequencer") options.playSequencer = true;
        else if (argument == "--float-output") options.floatOutput = true;
        else if (argument == "--all-outputs") options.allOutputs = true;
        else if (argument == "--raw-component-state") options.rawComponentState = true;
        else if (argument == "--help")
        {
            std::cout
                << "Usage: actual_vst3_host --plugin BUNDLE --state PRESET_OR_BSLX --output WAV\\n"
                << "       --expect-sha256 HEX --sample-rate HZ --block-size N --active-frames N\\n"
                << "       [--parameter SAMPLE:INDEX:NORMALIZED_VALUE] [--midi SAMPLE:NOTE:VELOCITY[:CHANNEL]] [--cc SAMPLE:CONTROLLER:VALUE] [--expect-cid CID] [--play-sequencer] [--tempo BPM] [--float-output] [--all-outputs] [--realtime-pacing] [--startup-wait-ms N] [--parameter-settle-ms N] [--pre-roll-frames N] [--post-stop-frames N] [--restart-frames N] [--restored-state-output NEW_BSLX] [--raw-component-state]\\n";
            std::exit(0);
        }
        else fail("unknown option: " + argument);
    }

    if (!options.bundle.isDirectory()) fail("--plugin must name an existing VST3 bundle");
    if (!options.state.existsAsFile()) fail("--state must name an existing native preset or BSLX state");
    if (options.output == juce::File {}) fail("--output is required");
    if (options.restoredStateOutput != juce::File {} && options.restoredStateOutput.exists())
        fail("--restored-state-output must name a new file");
    if (options.expectedSha256.length() != 64) fail("--expect-sha256 must be a 64-character digest");
    if (options.sampleRate < 8000.0 || options.sampleRate > 384000.0) fail("--sample-rate is outside 8000..384000");
    if (options.tempo <= 0.0 || options.tempo > 1000.0) fail("--tempo is outside (0,1000]");
    if (options.parameterSettleMs < 0 || options.parameterSettleMs > 1000)
        fail("--parameter-settle-ms is outside 0..1000");
    if (options.blockSize < 1 || options.blockSize > 65536) fail("--block-size is outside 1..65536");
    if (options.startupWaitMs < 0 || options.startupWaitMs > 10000)
        fail("--startup-wait-ms is outside 0..10000");
    if (options.preRollFrames < 0 || options.preRollFrames > static_cast<std::int64_t>(options.sampleRate * 60.0))
        fail("--pre-roll-frames is outside 0..60 seconds");
    if (options.postStopFrames < 0 || options.postStopFrames > static_cast<std::int64_t>(options.sampleRate * 60.0))
        fail("--post-stop-frames is outside 0..60 seconds");
    if (options.restartFrames < 0 || options.restartFrames > static_cast<std::int64_t>(options.sampleRate * 60.0))
        fail("--restart-frames is outside 0..60 seconds");
    if (options.restartFrames > 0 && (!options.playSequencer || options.postStopFrames == 0))
        fail("--restart-frames requires --play-sequencer and a positive --post-stop-frames gap");
    if (options.activeFrames < 1) fail("--active-frames must be positive");
    if (options.midi.empty() && !options.playSequencer)
        fail("at least one deterministic --midi event or --play-sequencer is required");
    std::stable_sort(options.midi.begin(), options.midi.end(), [](const auto& left, const auto& right) {
        return left.sample < right.sample;
    });
    for (const auto& event : options.parameters)
        if (event.sample >= options.activeFrames || event.sample % options.blockSize != 0)
            fail("--parameter sample must be block-aligned and inside active frames");
    std::stable_sort(options.parameters.begin(), options.parameters.end(), [](const auto& a, const auto& b) {
        return a.sample < b.sample;
    });
    return options;
}

juce::File findSingleFile(const juce::File& directory, const juce::String& wildcard)
{
    juce::Array<juce::File> matches;
    directory.findChildFiles(matches, juce::File::findFiles, true, wildcard);
    if (matches.size() != 1)
        fail("expected exactly one " + wildcard.toStdString() + " below "
             + directory.getFullPathName().toStdString());
    return matches.getReference(0);
}

void verifyModuleInfo(const Options& options)
{
    const auto moduleInfo = options.bundle.getChildFile("Contents/Resources/moduleinfo.json");
    if (!moduleInfo.existsAsFile())
        fail("VST3 moduleinfo.json is missing");
    const auto text = moduleInfo.loadFileAsString();
    const auto cidNeedle = "\"CID\": \"" + options.expectedCid + "\"";
    const auto nameNeedle = "\"Name\": \"" + options.expectedName + "\"";
    const auto vendorNeedle = "\"Vendor\": \"" + options.expectedVendor + "\"";
    const auto categoryNeedle = juce::String("\"Category\": \"Audio Module Class\"");
    const auto cidPosition = text.indexOf(cidNeedle);
    const auto classEnd = cidPosition >= 0 ? text.indexOf(cidPosition, "},") : -1;
    const auto componentClass = cidPosition >= 0 && classEnd > cidPosition
        ? text.substring(cidPosition, classEnd) : juce::String {};
    if (cidPosition < 0 || !componentClass.contains(categoryNeedle))
        fail("moduleinfo Audio Module Class CID mismatch");
    if (!componentClass.contains(nameNeedle) || !componentClass.contains(vendorNeedle))
        fail("moduleinfo name/vendor mismatch");
}

juce::var jsonFromState(const juce::MemoryBlock& state)
{
    if (state.getSize() < 8 || std::memcmp(state.getData(), "BSLX", 4) != 0)
        fail("state is not a BSLX envelope");
    juce::MemoryInputStream input(state, false);
    char magic[4] {};
    input.read(magic, 4);
    const auto version = input.readInt();
    if (version != 2)
        fail("state envelope is not native schema v2");
    juce::GZIPDecompressorInputStream gzip(&input, false);
    const auto json = gzip.readEntireStreamAsString();
    const auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject())
        fail("state contains invalid JSON");
    return parsed;
}

bool equivalentState(const juce::var& left, const juce::var& right)
{
    const auto numeric = [](const juce::var& value) {
        return value.isInt() || value.isInt64() || value.isDouble();
    };
    if (numeric(left) && numeric(right))
    {
        const auto a = static_cast<double>(left);
        const auto b = static_cast<double>(right);
        return std::abs(a - b) <= 1.0e-6 * std::max({ 1.0, std::abs(a), std::abs(b) });
    }
    if (left.isBool() || right.isBool())
        return left.isBool() && right.isBool() && static_cast<bool>(left) == static_cast<bool>(right);
    if (left.isString() || right.isString())
        return left.isString() && right.isString() && left.toString() == right.toString();
    if (const auto* leftArray = left.getArray())
    {
        const auto* rightArray = right.getArray();
        if (rightArray == nullptr || leftArray->size() != rightArray->size())
            return false;
        for (int index = 0; index < leftArray->size(); ++index)
            if (!equivalentState(leftArray->getReference(index), rightArray->getReference(index)))
                return false;
        return true;
    }
    if (const auto* leftObject = left.getDynamicObject())
    {
        const auto* rightObject = right.getDynamicObject();
        if (rightObject == nullptr
            || leftObject->getProperties().size() != rightObject->getProperties().size())
            return false;
        for (int index = 0; index < leftObject->getProperties().size(); ++index)
        {
            const auto name = leftObject->getProperties().getName(index);
            if (!rightObject->hasProperty(name)
                || !equivalentState(leftObject->getProperty(name), rightObject->getProperty(name)))
                return false;
        }
        return true;
    }
    return left.isVoid() == right.isVoid() && left.isUndefined() == right.isUndefined();
}

juce::MemoryBlock loadState(const juce::File& file)
{
    juce::MemoryBlock source;
    if (!file.loadFileAsData(source))
        fail("failed to read state file");
    if (source.getSize() >= 4 && std::memcmp(source.getData(), "BSLX", 4) == 0)
    {
        (void) jsonFromState(source);
        return source;
    }

    const auto json = file.loadFileAsString();
    const auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject())
        fail("native preset is not valid JSON");
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr || static_cast<int>(object->getProperty("schemaVersion")) != 2)
        fail("native preset is not schema v2");

    juce::MemoryOutputStream output;
    output.write("BSLX", 4);
    output.writeInt(2);
    {
        juce::GZIPCompressorOutputStream gzip(&output, 9, false);
        gzip.write(json.toRawUTF8(), static_cast<std::size_t>(json.getNumBytesAsUTF8()));
    }
    return output.getMemoryBlock();
}

juce::MemoryBlock wrapVst3State(const juce::MemoryBlock& componentState)
{
    juce::XmlElement state("VST3PluginState");
    state.createNewChildElement("IComponent")
        ->addTextElement(componentState.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary(state, wrapped);
    return wrapped;
}

juce::MemoryBlock extractVst3ComponentState(const juce::MemoryBlock& wrapped)
{
    const auto state = juce::AudioProcessor::getXmlFromBinary(
        wrapped.getData(), static_cast<int>(wrapped.getSize()));
    if (state == nullptr || !state->hasTagName("VST3PluginState"))
        fail("hosted VST3 returned an invalid state container");
    const auto* component = state->getChildByName("IComponent");
    juce::MemoryBlock result;
    if (component == nullptr || !result.fromBase64Encoding(component->getAllSubText()))
        fail("hosted VST3 returned no component state");
    return result;
}

void writeWave(const juce::File& output, double sampleRate,
               const juce::AudioBuffer<float>& audio, bool floatOutput)
{
    if (!output.getParentDirectory().createDirectory())
        fail("failed to create output directory");
    if (output.existsAsFile() && !output.deleteFile())
        fail("failed to replace existing output WAV");
    std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
    if (stream == nullptr)
        fail("failed to open output WAV");
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriter::Options {}
            .withSampleRate(sampleRate)
            .withNumChannels(audio.getNumChannels())
            .withBitsPerSample(floatOutput ? 32 : 24)
            .withSampleFormat(floatOutput
                ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                : juce::AudioFormatWriterOptions::SampleFormat::integral));
    if (writer == nullptr)
        fail("failed to create WAV writer");
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        fail("failed while writing WAV samples");
}

int run(const Options& options)
{
    const auto module = findSingleFile(options.bundle.getChildFile("Contents"), "*.so");
    const auto actualSha256 = juce::SHA256(module).toHexString();
    if (!actualSha256.equalsIgnoreCase(options.expectedSha256))
        fail("VST3 module SHA-256 mismatch: expected " + options.expectedSha256.toStdString()
             + ", got " + actualSha256.toStdString());
    verifyModuleInfo(options);

    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, options.bundle.getFullPathName());
    if (descriptions.size() != 1)
        fail("expected exactly one PluginDescription, found " + std::to_string(descriptions.size()));
    const auto& description = *descriptions.getFirst();
    if (description.name != options.expectedName
        || (description.descriptiveName.isNotEmpty()
            && description.descriptiveName != options.expectedName)
        || description.manufacturerName != options.expectedVendor
        || description.pluginFormatName != "VST3"
        || !description.isInstrument
        || juce::File(description.fileOrIdentifier).getFullPathName()
               != options.bundle.getFullPathName())
        fail("PluginDescription identity mismatch: name=" + description.name.toStdString()
             + ", descriptiveName=" + description.descriptiveName.toStdString()
             + ", vendor=" + description.manufacturerName.toStdString()
             + ", format=" + description.pluginFormatName.toStdString()
             + ", instrument=" + std::to_string(description.isInstrument)
             + ", outputs=" + std::to_string(description.numOutputChannels)
             + ", file=" + description.fileOrIdentifier.toStdString());

    struct Transport : juce::AudioPlayHead
    {
        PositionInfo position;
        juce::Optional<PositionInfo> getPosition() const override { return position; }
    } transport;
    juce::String creationError;
    auto plugin = format.createInstanceFromDescription(
        description, options.sampleRate, options.blockSize, creationError);
    if (plugin == nullptr)
        fail("VST3 instantiation failed: " + creationError.toStdString());

    juce::MemoryBlock state;
    if (options.rawComponentState)
    {
        if (!options.state.loadFileAsData(state) || state.getSize() == 0)
            fail("failed to read raw component state");
        if (options.restoredStateOutput == juce::File {})
            fail("--raw-component-state requires --restored-state-output for inspection");
    }
    else
        state = loadState(options.state);
    const auto expectedStateJson = options.rawComponentState ? juce::var {} : jsonFromState(state);
    // Older v2 states omit this optional field. Compare their restored value
    // against the documented default without changing what the plugin loads.
    if (auto* global = expectedStateJson.getProperty("global", {}).getDynamicObject();
        global != nullptr && !global->hasProperty("midiChannel"))
        global->setProperty("midiChannel", 1);
    if (auto* master = expectedStateJson.getProperty("master", {}).getDynamicObject();
        master != nullptr && !master->hasProperty("roomReturn"))
        master->setProperty("roomReturn", 1.0);
    if (auto* master = expectedStateJson.getProperty("master", {}).getDynamicObject();
        master != nullptr && !master->hasProperty("reverbReturn"))
        master->setProperty("reverbReturn", 1.0);
    if (auto* ui = expectedStateJson.getProperty("ui", {}).getDynamicObject())
    {
        // Older native saves omitted sharps/C3. The codec now writes these
        // preferences explicitly; supplied values must still match exactly.
        if (!ui->hasProperty("midiFlats"))
            ui->setProperty("midiFlats", false);
        if (!ui->hasProperty("middleCOctave"))
            ui->setProperty("middleCOctave", 3);
    }
    if (auto* pads = expectedStateJson.getProperty("pads", {}).getArray())
        for (auto& value : *pads)
            if (auto* pad = value.getDynamicObject())
            {
                // Native v2 predates independent sampler routing and ADSR
                // decay/sustain. Check their migration defaults explicitly;
                // supplied values must still survive the strict comparison.
                if (!pad->hasProperty("samplerNote"))
                    pad->setProperty("samplerNote", pad->getProperty("midiNote"));
                if (!pad->hasProperty("decaySeconds"))
                    pad->setProperty("decaySeconds", 1.0);
                if (!pad->hasProperty("sustainLevel"))
                    pad->setProperty("sustainLevel", 1.0);
            }
    const auto wrappedState = wrapVst3State(state);
    plugin->setStateInformation(wrappedState.getData(), static_cast<int>(wrappedState.getSize()));
    juce::MemoryBlock restored;
    plugin->getStateInformation(restored);
    const auto restoredComponent = extractVst3ComponentState(restored);
    // Preserve the actual restored bytes for cross-version diagnostics while
    // retaining the strict round-trip failure below.
    if (options.restoredStateOutput != juce::File {}
        && !options.restoredStateOutput.replaceWithData(restoredComponent.getData(), restoredComponent.getSize()))
        fail("could not save restored component state");
    if (!options.rawComponentState && !equivalentState(jsonFromState(restoredComponent), expectedStateJson))
        fail("plugin did not restore the supplied native v2 state exactly");

    int reportedPadNames = 0;
    std::array<bool, 128> checkedNotes {};
    const auto padsValue = expectedStateJson.getProperty("pads", {});
    if (const auto* pads = padsValue.getArray())
        for (const auto& pad : *pads)
        {
            const auto expectedName = pad.getProperty("name", {}).toString();
            const auto note = static_cast<int>(pad.getProperty("midiNote", -1));
            if (note < 0 || note > 127 || checkedNotes[static_cast<std::size_t>(note)]) continue;
            // A shared MIDI note exposes one name: the first mapped pad wins.
            checkedNotes[static_cast<std::size_t>(note)] = true;
            if (expectedName.isEmpty()) continue;
            const auto reported = plugin->getNameForMidiNoteNumber(note, 1);
            if (!reported || *reported != expectedName)
                fail("VST3 host note-name query did not report the restored pad map");
            ++reportedPadNames;
        }

    if (options.allOutputs)
    {
        auto layout = plugin->getBusesLayout();
        if (layout.outputBuses.size() != 17)
            fail("expected Main plus 16 pad output buses");
        for (auto& bus : layout.outputBuses)
            bus = juce::AudioChannelSet::stereo();
        if (!plugin->setBusesLayout(layout))
            fail("host could not activate all stereo output buses");
        if (plugin->getTotalNumOutputChannels() != 34)
            fail("activated output buses did not expose 34 channels");
    }
    const int renderChannels = options.allOutputs ? 34 : 2;
    plugin->setRateAndBufferSizeDetails(options.sampleRate, options.blockSize);
    if (options.playSequencer) plugin->setPlayHead(&transport);
    plugin->prepareToPlay(options.sampleRate, options.blockSize);
    // Optional diagnostic for background sample-loading readiness. This waits
    // in wall time without advancing DSP or transport; it does not pump GUI events.
    // Keep the default zero to preserve existing cold-start validation.
    if (options.startupWaitMs > 0)
        juce::Thread::sleep(static_cast<int>(options.startupWaitMs));
    if (plugin->getTotalNumOutputChannels() < 2)
        fail("plugin did not expose a main stereo output");
    // The process buffer must cover every active bus, even when the capture
    // retains only Main. Some hosted plugins enable auxiliary buses by default.
    const int processChannels = std::max(plugin->getTotalNumInputChannels(),
                                         plugin->getTotalNumOutputChannels());

    // Settle control ramps before replaying previously rendered references.
    // Default remains zero so cold-start checks retain their original behavior.
    if (options.playSequencer)
    {
        transport.position.setIsPlaying(false);
        transport.position.setBpm(options.tempo);
        transport.position.setTimeInSamples(0);
        transport.position.setPpqPosition(0.0);
    }
    // Diagnostic wall-clock pacing only: preserve sample positions and plugin
    // processing mode while allowing background work between audio callbacks.
    auto pacingStart = std::chrono::steady_clock::now();
    std::int64_t pacedFrames = 0;
    const auto pace = [&](int frames) {
        if (!options.realtimePacing) return;
        pacedFrames += frames;
        std::this_thread::sleep_until(pacingStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(pacedFrames) / options.sampleRate)));
    };
    for (std::int64_t remaining = options.preRollFrames; remaining > 0;)
    {
        const auto frames = static_cast<int>(std::min<std::int64_t>(options.blockSize, remaining));
        juce::AudioBuffer<float> silence(processChannels, frames);
        silence.clear();
        juce::MidiBuffer noMidi;
        plugin->processBlock(silence, noMidi);
        pace(frames);
        remaining -= frames;
    }

    const auto tailSeconds = plugin->getTailLengthSeconds();
    if (!std::isfinite(tailSeconds) || tailSeconds < 0.0 || tailSeconds > 60.0)
        fail("plugin declared an invalid tail length");
    const auto tailFrames = std::max(options.postStopFrames,
        static_cast<std::int64_t>(std::ceil(tailSeconds * options.sampleRate)));
    const auto restartGap = options.restartFrames > 0 ? options.postStopFrames : 0;
    const auto extraFrames = restartGap + options.restartFrames + tailFrames;
    if (options.activeFrames > std::numeric_limits<int>::max() - extraFrames)
        fail("requested render is too large");
    const auto restartStart = options.activeFrames + restartGap;
    const auto restartEnd = restartStart + options.restartFrames;
    const auto totalFrames = options.activeFrames + extraFrames;
    juce::AudioBuffer<float> rendered(renderChannels, static_cast<int>(totalFrames));
    rendered.clear();
    juce::Array<juce::var> outputMidi;
    juce::Array<juce::var> appliedParameters;
    for (const auto& event : options.parameters)
        if (event.index >= plugin->getParameters().size())
            fail("--parameter index outside plugin parameter list");
    std::size_t parameterIndex = 0;
    std::size_t midiIndex = 0;
    std::int64_t cursor = 0;
    while (cursor < totalFrames)
    {
        const auto previousParameterIndex = parameterIndex;
        while (parameterIndex < options.parameters.size()
               && options.parameters[parameterIndex].sample == cursor)
        {
            const auto& event = options.parameters[parameterIndex++];
            auto* parameter = plugin->getParameters()[event.index];
            parameter->setValueNotifyingHost(event.value);
            juce::DynamicObject::Ptr encoded = new juce::DynamicObject();
            encoded->setProperty("sample", static_cast<juce::int64>(cursor));
            encoded->setProperty("index", event.index);
            encoded->setProperty("name", parameter->getName(256));
            encoded->setProperty("requested", event.value);
            encoded->setProperty("readback", parameter->getValue());
            encoded->setProperty("injected_on_message_thread",
                juce::MessageManager::getInstance()->isThisTheMessageThread());
            appliedParameters.add(juce::var(encoded.get()));
        }
        // Diagnostic only: allow asynchronous controller/bridge delivery without
        // advancing musical time. Exclude this pause from realtime catch-up.
        if (parameterIndex != previousParameterIndex && options.parameterSettleMs > 0)
        {
            const auto pauseStart = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(options.parameterSettleMs));
            pacingStart += std::chrono::steady_clock::now() - pauseStart;
        }
        // Stop at the requested sample even when it falls inside a host block.
        const bool restarted = options.restartFrames > 0 && cursor >= restartStart;
        const bool playing = cursor < options.activeFrames || (restarted && cursor < restartEnd);
        const auto boundary = cursor < options.activeFrames ? options.activeFrames
            : options.restartFrames > 0 && cursor < restartStart ? restartStart
            : restarted && cursor < restartEnd ? restartEnd : totalFrames;
        const auto currentBlock = static_cast<int>(std::min<std::int64_t>(options.blockSize,
                                                                          boundary - cursor));
        juce::AudioBuffer<float> block(processChannels, currentBlock);
        block.clear();
        juce::MidiBuffer midi;
        while (midiIndex < options.midi.size()
               && options.midi[midiIndex].sample < cursor + currentBlock)
        {
            const auto& event = options.midi[midiIndex++];
            if (event.sample >= cursor)
            {
                const auto offset = static_cast<int>(event.sample - cursor);
                const auto message = event.controller
                    ? juce::MidiMessage::controllerEvent(event.channel, event.note, event.velocity)
                    : event.velocity == 0
                    ? juce::MidiMessage::noteOff(event.channel, event.note)
                    : juce::MidiMessage::noteOn(event.channel, event.note,
                                                static_cast<juce::uint8>(event.velocity));
                midi.addEvent(message, offset);
            }
        }
        if (options.playSequencer)
        {
            transport.position.setIsPlaying(playing);
            transport.position.setBpm(options.tempo);
            // Restart from beat one; capture timestamps remain monotonic.
            const auto transportSample = restarted ? cursor - restartStart : cursor;
            transport.position.setTimeInSamples(transportSample);
            // Match the isolated REAPER reference: samples -> seconds -> beats.
            // Reassociation changes boundary placement by one sample.
            transport.position.setPpqPosition(static_cast<double>(transportSample) / options.sampleRate * options.tempo / 60.0);
        }
        plugin->processBlock(block, midi);
        for (const auto metadata : midi)
        {
            if (metadata.samplePosition < 0 || metadata.samplePosition >= currentBlock)
                fail("VST3 returned a MIDI event outside the processed block");
            const auto message = metadata.getMessage();
            juce::DynamicObject::Ptr event = new juce::DynamicObject();
            event->setProperty("sample", static_cast<juce::int64>(cursor + metadata.samplePosition));
            event->setProperty("channel", message.getChannel());
            juce::Array<juce::var> bytes;
            for (int index = 0; index < message.getRawDataSize(); ++index)
                bytes.add(static_cast<int>(message.getRawData()[index]));
            event->setProperty("bytes", bytes);
            event->setProperty("type", message.isNoteOn() ? "note_on"
                                       : message.isNoteOff() ? "note_off" : "other");
            if (message.isNoteOnOrOff())
            {
                event->setProperty("note", message.getNoteNumber());
                event->setProperty("velocity", static_cast<int>(message.getVelocity()));
            }
            outputMidi.add(juce::var(event.get()));
        }
        for (int channel = 0; channel < renderChannels; ++channel)
            rendered.copyFrom(channel, static_cast<int>(cursor), block, channel, 0, currentBlock);
        cursor += currentBlock;
        pace(currentBlock);
    }
    plugin->releaseResources();
    writeWave(options.output, options.sampleRate, rendered, options.floatOutput);

    const auto outputSha256 = juce::SHA256(options.output).toHexString();
    juce::DynamicObject::Ptr reportObject = new juce::DynamicObject();
    reportObject->setProperty("status", "pass");
    reportObject->setProperty("state_validation", options.rawComponentState
        ? "raw-component-capture-only" : "native-v2-semantic-roundtrip");
    reportObject->setProperty("restored_component_bytes_equal", restoredComponent == state);
    reportObject->setProperty("reported_pad_names", reportedPadNames);
    reportObject->setProperty("output_channels", renderChannels);
    reportObject->setProperty("process_channels", processChannels);
    reportObject->setProperty("plugin_bundle", options.bundle.getFullPathName());
    reportObject->setProperty("module_path", module.getFullPathName());
    reportObject->setProperty("module_sha256", actualSha256);
    reportObject->setProperty("moduleinfo_cid", options.expectedCid);
    reportObject->setProperty("plugin_name", description.name);
    reportObject->setProperty("plugin_vendor", description.manufacturerName);
    reportObject->setProperty("plugin_format", description.pluginFormatName);
    reportObject->setProperty("plugin_unique_id", description.uniqueId);
    reportObject->setProperty("sample_rate", options.sampleRate);
    reportObject->setProperty("block_size", options.blockSize);
    reportObject->setProperty("tempo", options.tempo);
    reportObject->setProperty("output_midi_events", outputMidi);
    reportObject->setProperty("realtime_pacing", options.realtimePacing);
    reportObject->setProperty("parameter_settle_ms", static_cast<juce::int64>(options.parameterSettleMs));
    reportObject->setProperty("startup_wait_ms", static_cast<juce::int64>(options.startupWaitMs));
    reportObject->setProperty("pre_roll_frames", static_cast<juce::int64>(options.preRollFrames));
    reportObject->setProperty("requested_post_stop_frames", static_cast<juce::int64>(options.postStopFrames));
    reportObject->setProperty("restart_frames", static_cast<juce::int64>(options.restartFrames));
    reportObject->setProperty("restart_capture_sample", static_cast<juce::int64>(options.restartFrames > 0 ? restartStart : -1));
    reportObject->setProperty("active_frames", static_cast<juce::int64>(options.activeFrames));
    reportObject->setProperty("play_sequencer", options.playSequencer);
    reportObject->setProperty("declared_tail_seconds", tailSeconds);
    reportObject->setProperty("tail_frames", static_cast<juce::int64>(tailFrames));
    reportObject->setProperty("total_frames", static_cast<juce::int64>(totalFrames));
    reportObject->setProperty("parameter_events", appliedParameters);
    reportObject->setProperty("midi_event_count", static_cast<int>(options.midi.size()));
    juce::Array<juce::var> midiEvents;
    for (const auto& event : options.midi)
    {
        juce::DynamicObject::Ptr encoded = new juce::DynamicObject();
        encoded->setProperty("sample", static_cast<juce::int64>(event.sample));
        encoded->setProperty("channel", event.channel);
        encoded->setProperty(event.controller ? "controller" : "note", event.note);
        encoded->setProperty(event.controller ? "value" : "velocity", event.velocity);
        midiEvents.add(juce::var(encoded.get()));
    }
    reportObject->setProperty("midi_events", midiEvents);
    reportObject->setProperty("state_path", options.state.getFullPathName());
    reportObject->setProperty("state_component_sha256", juce::SHA256(state).toHexString());
    reportObject->setProperty("output_sha256", outputSha256);
    reportObject->setProperty("output_sample_format", options.floatOutput ? "float32" : "pcm24");
    std::cout << juce::JSON::toString(juce::var(reportObject.get()), true) << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        return run(parseOptions(argc, argv));
    }
    catch (const std::exception& exception)
    {
        std::cerr << "actual_vst3_host: " << exception.what() << '\n';
        return 1;
    }
}
