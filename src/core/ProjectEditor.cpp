#include "core/ProjectEditor.h"
#include "core/Randomizer.h"

#include <algorithm>
#include <string>

namespace bsl {

ProjectEditor::ProjectEditor(ProjectState initialState)
    : state_(initialState.isValid() ? std::move(initialState) : ProjectState::makeDefault())
{
}

void ProjectEditor::beginEdit(HistoryParameters parameters)
{
    undo_.push_back({ state_, parameters });
    if (undo_.size() > 128)
        undo_.erase(undo_.begin());
    redo_.clear();
    ++revision_;
}

void ProjectEditor::checkpoint(HistoryParameters parameters)
{
    beginEdit(parameters);
}

std::size_t ProjectEditor::firstBeatOfBar(const PatternState& pattern, std::size_t barIndex)
{
    std::size_t result = 0;
    for (std::size_t index = 0; index < barIndex; ++index)
        result += static_cast<std::size_t>(pattern.bars[index].numerator);
    return result;
}

BeatState ProjectEditor::emptyBeat(std::uint8_t padIndex)
{
    auto beat = BeatState::withSubdivision(1, padIndex, 0.8f);
    beat.slices.front().enabled = false;
    return beat;
}

bool ProjectEditor::addLane(std::size_t patternIndex)
{
    if (patternIndex >= state_.patterns.size())
        return false;
    auto& pattern = state_.patterns[patternIndex];
    if (pattern.lanes.size() >= maximumLaneCount)
        return false;
    beginEdit();
    auto lane = LaneState::make(pattern.lanes.front().beats.size());
    const auto pad = std::min(pattern.lanes.size(), padCount - 1);
    lane.name = state_.kit.pads[pad].name;
    for (auto& beat : lane.beats)
        beat.slices.front().padIndex = static_cast<std::uint8_t>(pad);
    pattern.lanes.push_back(std::move(lane));
    return true;
}

bool ProjectEditor::removeLane(std::size_t patternIndex, std::size_t laneIndex)
{
    if (patternIndex >= state_.patterns.size())
        return false;
    auto& lanes = state_.patterns[patternIndex].lanes;
    if (lanes.size() <= 1 || laneIndex >= lanes.size())
        return false;
    beginEdit();
    lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(laneIndex));
    return true;
}

bool ProjectEditor::resetLane(std::size_t patternIndex, std::size_t laneIndex)
{
    if (patternIndex >= state_.patterns.size()
        || laneIndex >= state_.patterns[patternIndex].lanes.size())
        return false;
    beginEdit();
    for (auto& beat : state_.patterns[patternIndex].lanes[laneIndex].beats)
        beat.reset();
    return true;
}

bool ProjectEditor::randomizeLane(std::size_t patternIndex, std::size_t laneIndex,
                                  std::uint32_t seed)
{
    if (patternIndex >= state_.patterns.size()
        || laneIndex >= state_.patterns[patternIndex].lanes.size()
        || !state_.randomizer.isValid())
        return false;
    beginEdit();
    auto& beats = state_.patterns[patternIndex].lanes[laneIndex].beats;
    for (std::size_t beatIndex = 0; beatIndex < beats.size(); ++beatIndex)
        Randomizer::apply(beats[beatIndex], state_.randomizer,
                          seed + static_cast<std::uint32_t>(beatIndex),
                          state_.global.defaultVelocity);
    return true;
}

bool ProjectEditor::addBar(std::size_t patternIndex, BarState bar)
{
    if (patternIndex >= state_.patterns.size() || bar.numerator < 1 || bar.numerator > 64
        || (bar.denominator != 2 && bar.denominator != 4
            && bar.denominator != 8 && bar.denominator != 16))
        return false;
    auto& pattern = state_.patterns[patternIndex];
    beginEdit();
    pattern.bars.push_back(bar);
    for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
    {
        auto& lane = pattern.lanes[laneIndex];
        const auto padIndex = static_cast<std::uint8_t>(std::min(laneIndex, padCount - 1));
        lane.beats.insert(lane.beats.end(), static_cast<std::size_t>(bar.numerator), emptyBeat(padIndex));
    }
    return true;
}

bool ProjectEditor::duplicateBar(std::size_t patternIndex, std::size_t barIndex)
{
    if (patternIndex >= state_.patterns.size())
        return false;
    auto& pattern = state_.patterns[patternIndex];
    if (barIndex >= pattern.bars.size())
        return false;
    const auto start = firstBeatOfBar(pattern, barIndex);
    const auto count = static_cast<std::size_t>(pattern.bars[barIndex].numerator);
    beginEdit();
    pattern.bars.insert(pattern.bars.begin() + static_cast<std::ptrdiff_t>(barIndex + 1),
                        pattern.bars[barIndex]);
    for (auto& lane : pattern.lanes)
    {
        std::vector<BeatState> copy(lane.beats.begin() + static_cast<std::ptrdiff_t>(start),
                                    lane.beats.begin() + static_cast<std::ptrdiff_t>(start + count));
        lane.beats.insert(lane.beats.begin() + static_cast<std::ptrdiff_t>(start + count),
                          copy.begin(), copy.end());
    }
    return true;
}

bool ProjectEditor::removeBar(std::size_t patternIndex, std::size_t barIndex)
{
    if (patternIndex >= state_.patterns.size())
        return false;
    auto& pattern = state_.patterns[patternIndex];
    if (pattern.bars.size() <= 1 || barIndex >= pattern.bars.size())
        return false;
    const auto start = firstBeatOfBar(pattern, barIndex);
    const auto count = static_cast<std::size_t>(pattern.bars[barIndex].numerator);
    beginEdit();
    pattern.bars.erase(pattern.bars.begin() + static_cast<std::ptrdiff_t>(barIndex));
    for (auto& lane : pattern.lanes)
        lane.beats.erase(lane.beats.begin() + static_cast<std::ptrdiff_t>(start),
                         lane.beats.begin() + static_cast<std::ptrdiff_t>(start + count));
    return true;
}

bool ProjectEditor::setBarMeter(std::size_t patternIndex, std::size_t barIndex,
                                int numerator, int denominator, bool createUndoStep)
{
    if (patternIndex >= state_.patterns.size())
        return false;
    auto& pattern = state_.patterns[patternIndex];
    if (barIndex >= pattern.bars.size())
        return false;
    const auto replacement = BarState::make(numerator, denominator);
    const auto oldCount = static_cast<std::size_t>(pattern.bars[barIndex].numerator);
    const auto newCount = static_cast<std::size_t>(replacement.numerator);
    const auto start = firstBeatOfBar(pattern, barIndex);
    if (createUndoStep)
        beginEdit();
    else
        ++revision_; // Every drag update must still reach the audio snapshot.
    pattern.bars[barIndex] = replacement;
    for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
    {
        auto& beats = pattern.lanes[laneIndex].beats;
        if (newCount < oldCount)
            beats.erase(beats.begin() + static_cast<std::ptrdiff_t>(start + newCount),
                        beats.begin() + static_cast<std::ptrdiff_t>(start + oldCount));
        else if (newCount > oldCount)
        {
            const auto padIndex = static_cast<std::uint8_t>(std::min(laneIndex, padCount - 1));
            beats.insert(beats.begin() + static_cast<std::ptrdiff_t>(start + oldCount),
                         newCount - oldCount, emptyBeat(padIndex));
        }
    }
    return true;
}

bool ProjectEditor::addPattern()
{
    if (state_.patterns.size() >= 116)
        return false;
    const auto trigger = state_.unusedPatternTriggerNote(12 + static_cast<int>(state_.patterns.size()));
    if (!trigger) return false;
    beginEdit();
    auto pattern = ProjectState::makeDefault().patterns.front();
    pattern.name = "Pattern " + std::to_string(state_.patterns.size() + 1);
    pattern.triggerNote = *trigger;
    state_.patterns.push_back(std::move(pattern));
    state_.activePattern = state_.patterns.size() - 1;
    return true;
}

bool ProjectEditor::duplicatePattern(std::size_t patternIndex)
{
    if (patternIndex >= state_.patterns.size() || state_.patterns.size() >= 116)
        return false;
    const auto trigger = state_.unusedPatternTriggerNote(12 + static_cast<int>(state_.patterns.size()));
    if (!trigger) return false;
    beginEdit();
    auto copy = state_.patterns[patternIndex];
    copy.name += " Copy";
    copy.triggerNote = *trigger;
    state_.patterns.insert(state_.patterns.begin() + static_cast<std::ptrdiff_t>(patternIndex + 1),
                           std::move(copy));
    state_.activePattern = patternIndex + 1;
    return true;
}

bool ProjectEditor::shiftPatternTriggerNote(std::size_t patternIndex, int direction)
{
    if (patternIndex >= state_.patterns.size() || direction == 0)
        return false;
    const int step = direction > 0 ? 1 : -1;
    for (int note = static_cast<int>(state_.patterns[patternIndex].triggerNote) + step;
         note >= 0 && note <= 127; note += step)
    {
        if (std::any_of(state_.patterns.begin(), state_.patterns.end(), [note](const auto& pattern) {
                return pattern.triggerNote == note;
            }))
            continue;
        beginEdit();
        state_.patterns[patternIndex].triggerNote = static_cast<std::uint8_t>(note);
        return true;
    }
    return false;
}

bool ProjectEditor::removePattern(std::size_t patternIndex)
{
    if (state_.patterns.size() <= 1 || patternIndex >= state_.patterns.size())
        return false;
    beginEdit();
    state_.patterns.erase(state_.patterns.begin() + static_cast<std::ptrdiff_t>(patternIndex));
    state_.activePattern = std::min(state_.activePattern, state_.patterns.size() - 1);
    return true;
}

bool ProjectEditor::undo()
{
    if (undo_.empty())
        return false;
    restoredParameters_ = undo_.back().parameters;
    redo_.push_back({ state_, restoredParameters_ });
    state_ = std::move(undo_.back().state);
    undo_.pop_back();
    ++revision_;
    return true;
}

bool ProjectEditor::redo()
{
    if (redo_.empty())
        return false;
    restoredParameters_ = redo_.back().parameters;
    undo_.push_back({ state_, restoredParameters_ });
    state_ = std::move(redo_.back().state);
    redo_.pop_back();
    ++revision_;
    return true;
}

} // namespace bsl
