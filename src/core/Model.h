#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace bsl {

inline constexpr std::size_t padCount = 16;
inline constexpr std::size_t maximumLaneCount = 16;
inline constexpr std::size_t maximumSubdivision = 45;

enum class SwingReference
{
    eighth,
    quarter,
    half
};

enum class PatternQuantization
{
    off,
    eighth,
    quarter,
    half,
    whole,
    sixteenth,
    thirtySecond
};

struct GlobalState
{
    double swingPercent = 0.0;
    SwingReference swingReference = SwingReference::eighth;
    double beatsPerMinute = 120.0;
    float defaultVelocity = 0.8f;
    bool sequencerEnabled = true;
    bool samplerEnabled = true;
    bool auditionDuringPlayback = true;
    bool kitLocked = false;
    bool multiOutput = false;
};

struct SliceState
{
    std::uint8_t padIndex = 0;
    float velocity = 0.8f;
    bool enabled = false;
    // Empty cells use the current default; previously placed/edited notes retain theirs.
    bool velocityAssigned = false;

    auto operator<=>(const SliceState&) const = default;
};

struct BeatState
{
    std::vector<SliceState> slices;

    auto operator<=>(const BeatState&) const = default;

    static BeatState withSubdivision(std::size_t subdivision,
                                     std::uint8_t padIndex,
                                     float velocity);
    void setSubdivision(std::size_t subdivision);
    void rotate(int amount);
    void mirror();
    void reset() noexcept;
};

struct BarState
{
    int numerator = 4;
    int denominator = 4;

    static BarState make(int numerator, int denominator, std::size_t unusedBeatCount = 0);
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] double durationInQuarterNotes() const;
};

struct LaneState
{
    std::string name = "Lane";
    bool muted = false;
    bool solo = false;
    std::vector<BeatState> beats;

    static LaneState make(std::size_t beatCount);
};

struct PatternState
{
    std::string name = "Pattern 1";
    std::uint8_t triggerNote = 12;
    PatternQuantization quantization = PatternQuantization::whole;
    std::vector<BarState> bars;
    std::vector<LaneState> lanes;

    [[nodiscard]] double durationInQuarterNotes() const;
};

// Embedded audio is immutable and shared by project snapshots/undo entries.
// Replacing one pad's payload never mutates earlier saved states.
class SamplePayload
{
public:
    SamplePayload() = default;
    SamplePayload(std::string value) { *this = std::move(value); }
    SamplePayload& operator=(std::string value)
    {
        bytes_ = value.empty() ? nullptr : std::make_shared<const std::string>(std::move(value));
        return *this;
    }
    [[nodiscard]] bool empty() const noexcept { return !bytes_; }
    [[nodiscard]] const std::string& value() const noexcept
    {
        static const std::string empty;
        return bytes_ ? *bytes_ : empty;
    }
    void clear() noexcept { bytes_.reset(); }

private:
    std::shared_ptr<const std::string> bytes_;
};

struct PadState
{
    std::string name;
    std::array<std::string, 2> factorySampleIds;
    std::string customSampleName;
    std::string customSampleSha256;
    SamplePayload customSampleBase64;
    std::uint8_t midiNote = 36;
    std::uint8_t rootNote = 60;
    float layerMix = 0.0f;
    float gain = 1.0f;
    float pan = 0.0f;
    float attackSeconds = 0.0f;
    float attackNormalized = 0.0f;
    float damp = 0.0f;
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;
    float highPass = 0.0f;
    bool filterEnabled = true;
    float cutoff = 1.0f;
    float resonance = 0.0f;
    bool driveEnabled = false;
    float driveGainDb = 0.0f;
    float distortionMix = 0.0f;
    bool independentDistortion = false;
    float distortionDrive = 0.5f;
    float compression = 0.0f;
    float crush = 0.0f;
    float reverb = 0.0f;
    float room = 0.0f;
    float delay = 0.0f;
    bool oneShot = true;
    bool muted = false;
    bool solo = false;
    bool lpfBypass = false;
    bool hpfBypass = false;
    bool driveBypass = false;
    bool crushBypass = false;
};

struct KitState
{
    std::array<PadState, padCount> pads;
};

struct MasterState
{
    bool filterEnabled = true;
    float cutoff = 1.0f;
    float resonance = 0.0f;
    float delay = 0.0f;
    float distortion = 0.0f;
    float reverb = 0.0f;
    float room = 0.0f;
    float crush = 0.0f;
    float compression = 0.0f;
    float gain = 1.0f;
};

struct RandomizerSettings
{
    std::vector<std::uint8_t> selectedPads;
    std::vector<std::size_t> selectedSubdivisions;
    bool noteSelectionEnabled = true;
    bool slicingEnabled = true;
    bool triggeringEnabled = true;
    bool velocityEnabled = true;
    bool fillEmptyOnly = false;
    float triggerChance = 0.5f;
    float spacing = 0.5f;
    float minimumVelocity = 0.4f;
    float maximumVelocity = 1.0f;

    [[nodiscard]] bool isValid() const noexcept;
};

struct UiState
{
    std::size_t selectedPad = 0;
    std::size_t selectedLane = 0;
    std::size_t selectedBar = 0;
    bool patternMode = false;
    bool randomizerVisible = false;
    bool darkMode = false;
    bool midiNoteNames = true;
    bool midiFlats = false;
    int middleCOctave = 3;
    // Content identity belongs to the saved project, not an editor instance.
    std::string presetFile;
    std::string kitFile;
    std::string randomizerFile;
};

struct ProjectState
{
    int schemaVersion = 2;
    GlobalState global;
    KitState kit;
    MasterState master;
    RandomizerSettings randomizer;
    UiState ui;
    std::vector<PatternState> patterns;
    std::size_t activePattern = 0;

    static ProjectState makeDefault();
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::optional<std::size_t> padIndexForMidiNote(std::uint8_t note) const noexcept;
    bool selectPatternForTrigger(std::uint8_t note) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> unusedPatternTriggerNote(int preferred) const noexcept;
    bool normalizePatternTriggerNotes() noexcept;
};

} // namespace bsl
