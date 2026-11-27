# Reference notes

The main behavior reference is the
[Beat Scholar manual](https://downloads.modalics.com/BeatScholar/Beat%20Scholar%20Manual.pdf).
The [official product page](https://www.modalics.com/beatscholar) provides an
overview and visuals. The
[1.2.6 release notes](https://www.modalics.com/post/beat-scholar-version-1-2-6)
describe fixes and additions such as effect bypass automation, MIDI-export
corrections, and finer pattern-switch quantization.

The sources sometimes describe different versions. For example, the manual
describes up to 45 slices while some product copy says 42. The implementation
keeps the manual's 45-slice limit. A screenshot from a later version can also
show options absent from the earlier manual.

Community reports and video demonstrations are useful for finding things to
check. A feature request or a proposed future feature is not evidence that a
released version implements it. When references disagree, record the version
and the observed behavior before changing the plug-in.

This repository links to the references rather than redistributing the manual,
official images, commercial presets, or original audio. JUCE is fetched from
[its upstream repository](https://github.com/juce-framework/JUCE) and retains its
own licensing terms.

## SoundForMore Getting Started walkthrough

[Modalics Beat Scholar - Demo / Tutorial 1: Getting Started](https://www.youtube.com/watch?v=qOY2oMsUEMs)
shows the iPad AUv3 version in Logic Pro. The context menu at 8:35 supplies a
useful shared-workflow reference: Copy, Paste, Clear, Fill, Mirror, Shift Right,
Shift Left and grouped subdivisions. Desktop keeps the manual's 45-slice limit;
the video shows 42. Its touch-only toolbar and host chrome are platform-specific.
The timestamped comparisons and validation are recorded in [STATUS.md](STATUS.md).

## Randomizer Chance and Spacing

The manual (page 22) describes 100% Trigger Chance as triggering all slices.
The original desktop 1.6.2 routine instead applies Spacing after a generated
hit even at 100% Chance. Above 50% Spacing, the next eligible slice's probability
is `Chance * 2 * (1 - Spacing)`. At 100% for both controls, generated hits and
misses alternate, starting with a hit when the previous-decision state is false.
The Linux implementation follows this original binary behavior.

This decision is supported by the traced Chance/Spacing field bindings and
the branch at `0x180d392ab` through `0x180d39321`: there is no 100% Chance bypass;
the cap applies to the random draw, not to the adjusted probability. Core,
editor, and actual VST3 smoke checks passed for the repair in `d8398fe`.
The prior-decision lifetime across separate randomizer invocations remains
unverified. This finding does not change the user-selected manual behavior
for the custom-sample dropdown.

## User Data export reference limitation

The original 1.6.2 User Data > Export action opens a directory chooser titled
“Save Beat Scholar Data”. A controlled export into a newly created empty guest
folder returned without a visible error and left the destination empty.
The source root contained Kits, Presets, and Step Randomizer directories;
Step Randomizer contained a Parity Check subdirectory. These directories were
not reproduced in the destination. A subsequent inspection confirmed Parity
Check contains zero items. Thus this probe does not demonstrate omission of
actual saved preset files; export with a nonempty source remains unverified.

This observation agrees with the bounded callback trace reaching CopyFileW
rather than recursive directory copying. It does not establish a working
backup format or prove behavior for every source arrangement. Linux User Data
import/export remains unresolved; do not describe it as verified compatibility.
Captures and trace notes are retained in the local recovery record under
main-kit-menu-reference-20261127/RESULT.md.

A follow-up with a verified saved user preset (`exportprobe2719.json`, 74.9 KB)
also produced an empty newly created destination. This confirms failure for
that nonempty source arrangement. Working Linux import/export would therefore
be an intentional departure from the observed original behavior; that decision
is pending. Import behavior has not been verified.

## Bar note-value dragging

The original's denominator slider uses a 250-pixel normalized drag range and
retains fractional movement between gestures. Its model maps
`floor(3 * normalized)` to `16, 8, 4, 2`; it does not advance one denominator
for every 12 pixels. The original slider drag at `0x180ba10b8` and denominator
setter at `0x180d35410` support this mapping. Six chronological VM probes
(up 12, up 48, down 48, up 100, down 100, down 12 pixels), starting at 4,
produced `4, 4, 4, 2, 8, 8`. Retaining the fractional slider position reproduces
all six results. The editor regression replays this sequence and checks undo.
A separate original probe (meter-switch-up50.png and meter-switch-down25.png)
confirms that switching A -> B -> A resets the fractional position: up 50 pixels
leaves 4, then switching and dragging down 25 produces 8. The editor regression
replays this sequence and caught missing synchronization on ordinary selection;
the shared editor now resets on direct and display-observed pattern changes.
Control reconstruction without a pattern switch remains
unverified; the candidate also refreshes when the displayed model value or bar
count changes.


For existing slices with slicing disabled, original 1.6.2 skips note assignment when Drum Selection is empty (0x180d39331–0x180d39342). The candidate preserves the existing drum accordingly; this does not establish how the original initializes newly created slices.


### Repeated-action randomizer trigger history
Original 1.6.2 captures in randomizer-probability-20261127/repeated-action-result.md show three odd-slice actions alternating 010 -> 101 -> 010 with Chance/Spacing 100%, other modules disabled. ProjectEditor now owns transient trigger history shared by lane actions and beat strokes; Randomizer accepts an optional history pointer while isolated deterministic calls retain their initial false state. Core regression failed before repair (three assertions), then passed after repair, including interleaved lane/beat/lane actions. History is not serialized or included in undo snapshots. Reset behavior on settings/preset replacement remains unverified against original. Both format builds are running serially; no installation or REAPER changes.


### Disabled randomizer velocity preservation
Original live comparison and positive control: randomizer-probability-20261127/velocity-disabled-result.md. Disabled Velocity preserves existing slice velocity, rather than replacing it with brush velocity. Repaired shared Randomizer implementation; regression failed before repair (two assertions). Serial standalone/VST3/core build and targeted checks in progress. No installation or live REAPER changes.


### Randomizer slicing growth discrepancy
Original live slicing-only change 3 -> 4 retains enabled pattern 101 and appends a silent fourth slice (1010), screenshot randomizer-probability-20261127/subdivision-four-only.png; all other modules disabled. Bounded original routine180d31610 confirms appended records zero-enabled at+0x28, MIDI36 at+0x1c, velocity0.8 at+0x20. Candidate cloned the first active slice through BeatState::setSubdivision. Randomizer now resizes with default SliceState while ordinary editing helper remains unchanged. Distinguishing core regression failed before repair. Validation pending; editor run87860 covers preceding velocity repair only.

### Original MIDI Display menu (2026-11-27)
Controlled original 1.6.2 captures show direct toggles `Middle C = C4`, `Note Names`, and `Use Sharps`, rather than nested value menus. Disabling the first changes MIDI60 from C4 to C3; disabling names shows60. Candidate menu now follows those controls using its existing persisted settings. MIDI Channel shows Any and1–16. Evidence: recovery `main-kit-menu-reference-20261127/midi-{display,c4-off,names-off,sharps-on,channel}.png`.

## 2026-11-27: MIDI Display fresh defaults repaired
Accepted Claude's pre-toggle original captures: Middle C C4 checked, Note Names checked, Use Sharps unchecked. UiState now defaults to C4/flats. Native writer explicitly stores both display preferences; decoder deliberately retains C3/sharps for missing keys in older native saves, preserving their labels without a schema bump. Added state tests for fresh defaults, all four explicit combinations through host/preset recall, and old omitted-key recall. State tests passed; standalone/VST3/editor builds passed serially. Full editor regression passed 73.15 s; inspected editor-regression/midi-display-menu.png, showing C4 and Note Names checked and Use Sharps unchecked. git diff --check clean. Logs: midi-defaults-build-20261127.log and midi-defaults-editor-20261127.log. No installed binary, REAPER, or VM changes. Full parity remains open.
