#pragma once

#include "core/Model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bsl {

enum class HistoryParameters { none, masterGain, kit, padGain, padPan, padPitch, padFine, padHighPass, padCutoff, padResonance, padCompression, padCrush, padDrive, padAuxGain, padRoom, padReverb, padAux3, masterCrush, masterCompression, masterCutoff, masterResonance, masterDistortionDrive, padLength, padAttack, padDecay, padSustain, padRelease, swingReference, padOneShot };

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
    // Mutates a detached beat; the stroke caller owns its undo checkpoint.
    void randomizeBeatData(BeatState& beat, std::uint32_t seed);
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
    void checkpoint(HistoryParameters = HistoryParameters::none, std::size_t pad = 0);
    [[nodiscard]] std::size_t restoredPad() const noexcept { return restoredPad_; }
    [[nodiscard]] HistoryParameters restoredParameters() const noexcept { return restoredParameters_; }
    bool undo();
    bool redo();

private:
    void beginEdit(HistoryParameters = HistoryParameters::none);
    static std::size_t firstBeatOfBar(const PatternState&, std::size_t barIndex);
    static BeatState emptyBeat(std::uint8_t padIndex);

    ProjectState state_;
    // Original settings retain this across actions; it is not musical/undo state.
    bool previousRandomizerTrigger_ = false;
    struct HistoryEntry { ProjectState state; HistoryParameters parameters; std::size_t pad = 0; };
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    HistoryParameters restoredParameters_ = HistoryParameters::none;
    std::size_t restoredPad_ = 0;
    std::uint64_t revision_ = 0;
};

} // namespace bsl
