#include "content/FactoryResourceScanner.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace bsl::content {

std::vector<std::string> findPeFlacResourceNames(std::span<const std::uint8_t> bytes,
                                                std::size_t startOffset)
{
    const auto read = [&](std::size_t offset, std::size_t length) -> std::uint64_t {
        if (offset > bytes.size() || length > bytes.size() - offset) return 0;
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < length; ++i)
            value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
        return value;
    };
    if (bytes.size() < 64 || read(0, 2) != 0x5a4d) return {};
    const auto pe = static_cast<std::size_t>(read(0x3c, 4));
    if (pe > bytes.size() - 24 || read(pe, 4) != 0x4550) return {};
    const auto sectionCount = static_cast<std::size_t>(read(pe + 6, 2));
    const auto optionalSize = static_cast<std::size_t>(read(pe + 20, 2));
    if (optionalSize < 32 || optionalSize > bytes.size() - pe - 24
        || read(pe + 24, 2) != 0x20b) return {};
    const auto imageBase = read(pe + 48, 8);
    const auto sectionTable = pe + 24 + optionalSize;
    if (sectionCount > (bytes.size() - sectionTable) / 40) return {};
    struct Section { std::size_t raw, size; std::uint64_t rva; };
    std::vector<Section> sections;
    for (std::size_t i = 0; i < sectionCount; ++i)
    {
        const auto entry = sectionTable + 40 * i;
        Section section { static_cast<std::size_t>(read(entry + 20, 4)),
                          static_cast<std::size_t>(read(entry + 16, 4)), read(entry + 12, 4) };
        if (section.raw > bytes.size() || section.size > bytes.size() - section.raw
            || imageBase > std::numeric_limits<std::uint64_t>::max() - section.rva - section.size)
            return {};
        sections.push_back(section);
    }
    const auto names = findFlacResourceNames(bytes, startOffset);
    if (names.empty()) return {};
    std::unordered_map<std::uint64_t, std::string> namesByAddress;
    auto cursor = std::min(startOffset, bytes.size());
    for (const auto& name : names)
    {
        const auto found = std::search(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end(),
                                       name.begin(), name.end());
        if (found == bytes.end()) return {};
        const auto offset = static_cast<std::size_t>(found - bytes.begin());
        cursor = offset + name.size();
        for (const auto& section : sections)
            if (offset >= section.raw && offset - section.raw < section.size)
                namesByAddress.emplace(imageBase + section.rva + offset - section.raw, name);
    }
    if (namesByAddress.size() != names.size()) return {};
    std::vector<std::string> result;
    for (const auto& section : sections)
    {
        std::vector<std::string> run;
        const auto finishRun = [&] {
            if (run.size() != names.size()) return true;
            const std::unordered_set<std::string> unique(run.begin(), run.end());
            if (unique.size() != names.size()) return true;
            if (!result.empty()) return false; // more than one complete array
            result = run;
            return true;
        };
        for (auto position = section.raw; position + 8 <= section.raw + section.size; position += 8)
        {
            if (const auto name = namesByAddress.find(read(position, 8)); name != namesByAddress.end())
                run.push_back(name->second);
            else
            {
                if (!finishRun()) return {};
                run.clear();
            }
        }
        if (!finishRun()) return {};
    }
    return result;
}

std::vector<std::size_t> findFlacStreams(std::span<const std::uint8_t> bytes,
                                         std::size_t startOffset)
{
    std::vector<std::size_t> result;
    if (startOffset >= bytes.size())
        return result;
    for (std::size_t index = startOffset; index + 8 <= bytes.size(); ++index)
    {
        if (bytes[index] != 'f' || bytes[index + 1] != 'L'
            || bytes[index + 2] != 'a' || bytes[index + 3] != 'C')
            continue;
        const auto metadataType = static_cast<std::uint8_t>(bytes[index + 4] & 0x7fu);
        const auto metadataLength = (static_cast<std::size_t>(bytes[index + 5]) << 16u)
                                    | (static_cast<std::size_t>(bytes[index + 6]) << 8u)
                                    | static_cast<std::size_t>(bytes[index + 7]);
        if (metadataType == 0 && metadataLength == 34)
        {
            result.push_back(index);
            index += 7;
        }
    }
    return result;
}

std::vector<std::string> findFlacResourceNames(std::span<const std::uint8_t> bytes,
                                               std::size_t startOffset)
{
    std::vector<std::string> result;
    std::string current;
    const auto flush = [&] {
        constexpr std::string_view suffix = ".flac";
        if (current.size() > suffix.size()
            && current.ends_with(suffix)
            && current.find('*') == std::string::npos
            && current.find(';') == std::string::npos)
            result.push_back(current);
        current.clear();
    };
    for (std::size_t index = std::min(startOffset, bytes.size()); index < bytes.size(); ++index)
    {
        const auto value = bytes[index];
        if (value >= 32 && value <= 126)
            current.push_back(static_cast<char>(value));
        else
            flush();
    }
    flush();
    return result;
}

} // namespace bsl::content
