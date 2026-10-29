#include "content/FactoryResourceScanner.h"
#include "content/VerifiedFactoryOffsets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>

namespace {

std::size_t findBytes(std::span<const std::uint8_t> bytes, std::string_view needle,
                      std::size_t start)
{
    const auto found = std::search(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end(),
                                   needle.begin(), needle.end());
    return found == bytes.end() ? bytes.size()
                                : static_cast<std::size_t>(std::distance(bytes.begin(), found));
}

juce::var manifestEntry(const juce::String& name, std::size_t offset,
                        const juce::File& file, const juce::AudioFormatReader& reader)
{
    auto result = juce::var(new juce::DynamicObject());
    auto* object = result.getDynamicObject();
    object->setProperty("name", name);
    object->setProperty("sourceOffset", static_cast<juce::int64>(offset));
    object->setProperty("sha256", juce::SHA256(file).toHexString());
    object->setProperty("sampleRate", reader.sampleRate);
    object->setProperty("channels", static_cast<int>(reader.numChannels));
    object->setProperty("samples", reader.lengthInSamples);
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: extract_factory_samples <Beat Scholar.dll> <Factory directory>\n";
        return 2;
    }
    const juce::File source(juce::String::fromUTF8(argv[1]));
    const juce::File factory(juce::String::fromUTF8(argv[2]));
    if (!source.existsAsFile())
    {
        std::cerr << "source DLL does not exist\n";
        return 2;
    }
    juce::MemoryBlock binary;
    if (!source.loadFileAsData(binary))
    {
        std::cerr << "could not read source DLL\n";
        return 2;
    }
    const auto* begin = static_cast<const std::uint8_t*>(binary.getData());
    const std::span<const std::uint8_t> bytes(begin, binary.getSize());
    const auto sourceHash = juce::SHA256(binary).toHexString();
    // For this build, all 277 getNamedResource branches were resolved to exact
    // offsets indexed by originalFilenames. Other builds need that check
    // before their layout is accepted; matching counts alone are insufficient.
    if (sourceHash != "e5618ec7cf1da65a21d17fbe1054792db6a2d8c99f6c1e0e08aeaa0df59ef533")
    {
        std::cerr << "unverified DLL resource layout; refusing to guess sample identities\n";
        return 3;
    }
    const auto offsets = bsl::content::findFlacStreams(bytes);
    if (offsets.empty())
    {
        std::cerr << "no valid embedded FLAC streams found\n";
        return 3;
    }
    const auto names = bsl::content::findPeFlacResourceNames(bytes, offsets.back() + 8);
    if (names.size() != offsets.size())
    {
        std::cerr << "refusing ambiguous mapping: " << offsets.size() << " streams but "
                  << names.size() << " resource names\n";
        return 3;
    }
    auto mappedOffsets = bsl::content::verifiedFactoryOffsets;
    std::sort(mappedOffsets.begin(), mappedOffsets.end());
    if (offsets.size() != mappedOffsets.size()
        || !std::equal(offsets.begin(), offsets.end(), mappedOffsets.begin()))
    {
        std::cerr << "verified dispatch mapping does not cover every FLAC exactly once\n";
        return 3;
    }
    const auto physicalNames = bsl::content::findFlacResourceNames(bytes, offsets.back() + 8);
    const auto nameTableOffset = findBytes(bytes, physicalNames.front(), offsets.back() + 8);
    if (nameTableOffset == bytes.size())
    {
        std::cerr << "could not locate resource-name table\n";
        return 3;
    }

    const auto samplesDirectory = factory.getChildFile("Samples");
    if (samplesDirectory.exists() && samplesDirectory.getNumberOfChildFiles(juce::File::findFilesAndDirectories) != 0)
    {
        std::cerr << "sample directory is not empty; extract to a fresh staging directory\n";
        return 4;
    }
    if (samplesDirectory.createDirectory().failed())
    {
        std::cerr << "could not create sample directory\n";
        return 4;
    }

    juce::FlacAudioFormat flac;
    juce::Array<juce::var> entries;
    for (std::size_t index = 0; index < offsets.size(); ++index)
    {
        const auto start = bsl::content::verifiedFactoryOffsets[index];
        const auto next = std::upper_bound(offsets.begin(), offsets.end(), start);
        const auto end = next != offsets.end() ? *next : nameTableOffset;
        auto* input = new juce::MemoryInputStream(bytes.data() + start, end - start, false);
        std::unique_ptr<juce::AudioFormatReader> reader(flac.createReaderFor(input, true));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        {
            std::cerr << "could not decode stream " << index << " at " << start << '\n';
            return 5;
        }
        auto safeName = juce::String(names[index]).replaceCharacter('/', '_').replaceCharacter('\\', '_');
        const auto destination = samplesDirectory.getChildFile(safeName);
        if (!destination.existsAsFile())
        {
            std::unique_ptr<juce::AudioFormatWriter> writer(flac.createWriterFor(
                destination.createOutputStream().release(), reader->sampleRate, reader->numChannels,
                static_cast<int>(std::max(16u, reader->bitsPerSample)), {}, 5));
            if (writer == nullptr || !writer->writeFromAudioReader(*reader, 0, reader->lengthInSamples))
            {
                destination.deleteFile();
                std::cerr << "could not write " << destination.getFullPathName() << '\n';
                return 6;
            }
        }
        entries.add(manifestEntry(safeName, start, destination, *reader));
    }

    auto manifest = juce::var(new juce::DynamicObject());
    manifest.getDynamicObject()->setProperty("schemaVersion", 2);
    manifest.getDynamicObject()->setProperty("mapping", "verified-getNamedResource-dispatch");
    manifest.getDynamicObject()->setProperty("sourceSha256", sourceHash);
    manifest.getDynamicObject()->setProperty("sampleCount", entries.size());
    manifest.getDynamicObject()->setProperty("samples", entries);
    if (!factory.getChildFile("sample-manifest.json").replaceWithText(juce::JSON::toString(manifest, true)))
    {
        std::cerr << "could not write sample manifest\n";
        return 7;
    }
    std::cout << "Extracted " << entries.size() << " lossless factory samples to "
              << samplesDirectory.getFullPathName() << '\n';
    return 0;
}
