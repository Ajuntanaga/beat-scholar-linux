#pragma once

#include "core/Model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bsl {

enum class HistoryParameters { none, masterGain, kit };

class ProjectEditor
{
public:
    explicit ProjectEditor(ProjectState initialState);

    [[nodiscard]] const ProjectState& state() const noexcept { return state_; }
    [[nodiscard]] ProjectState& state() noexcept { return state_; }

    bool addLane(std::size_t patternIndex);
    bool removeLane(std::size_t patternIndex, std::size_t laneIndex);
    bool resetLane(std::size_t patternIndex, std::size_t laneIndex);
    bool randomizeLane(std::size_t patternIndex, std::size_t laneIndex,
                       std::uint32_t seed);
    bool addBar(std::size_t patternIndex, BarState bar);
    bool insertBar(std::size_t patternIndex, std::size_t barIndex, BarState bar);
    bool duplicateBar(std::size_t patternIndex, std::size_t barIndex);
    bool removeBar(std::size_t patternIndex, std::size_t barIndex);
    bool setBarMeter(std::size_t patternIndex, std::size_t barIndex,
                     int numerator, int denominator, bool createUndoStep = true);
    bool addPattern();
    bool insertPattern(std::size_t patternIndex);
    bool duplicatePattern(std::size_t patternIndex);
    bool removePattern(std::size_t patternIndex);
    bool shiftPatternTriggerNote(std::size_t patternIndex, int direction);

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    void checkpoint(HistoryParameters = HistoryParameters::none);
    [[nodiscard]] HistoryParameters restoredParameters() const noexcept { return restoredParameters_; }
    bool undo();
    bool redo();

private:
    void beginEdit(HistoryParameters = HistoryParameters::none);
    static std::size_t firstBeatOfBar(const PatternState&, std::size_t barIndex);
    static BeatState emptyBeat(std::uint8_t padIndex);

    ProjectState state_;
    struct HistoryEntry { ProjectState state; HistoryParameters parameters; };
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    HistoryParameters restoredParameters_ = HistoryParameters::none;
    std::uint64_t revision_ = 0;
};

} // namespace bsl
