#pragma once
#include <juce_core/juce_core.h>

namespace bsl {
inline juce::File dataRoot()
{
    const auto configured = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", "");
    const auto base = juce::File::isAbsolutePath(configured)
        ? juce::File(configured)
        : juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/share");
    return base.getChildFile("BeatScholarLinux");
}

inline juce::URL manualUrl()
{
    const auto local = dataRoot().getChildFile("Manual.pdf");
    return local.existsAsFile() ? juce::URL(local)
        : juce::URL("https://downloads.modalics.com/BeatScholar/Beat%20Scholar%20Manual.pdf");
}
} // namespace bsl
