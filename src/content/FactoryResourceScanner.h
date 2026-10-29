#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bsl::content {

std::vector<std::size_t> findFlacStreams(std::span<const std::uint8_t> bytes,
                                         std::size_t startOffset = 0);
std::vector<std::string> findFlacResourceNames(std::span<const std::uint8_t> bytes,
                                               std::size_t startOffset = 0);
// Read the PE64 originalFilenames pointer array, preserving its declared order.
// Physical string placement is not resource order. Ambiguous arrays are rejected.
std::vector<std::string> findPeFlacResourceNames(std::span<const std::uint8_t> bytes,
                                                std::size_t startOffset = 0);

} // namespace bsl::content
