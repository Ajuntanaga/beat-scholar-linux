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
Current validation and remaining limitations are recorded in [STATUS.md](STATUS.md).

## Randomizer behavior in desktop 1.6.2

The manual (page 22) describes 100% Trigger Chance as triggering all slices.
Desktop 1.6.2 applies Spacing after a generated hit even at 100% Chance.
Above 50% Spacing, the next eligible slice's probability is
`Chance * 2 * (1 - Spacing)`. At 100% for both controls, hits and misses
alternate, starting with a hit when the previous decision is false.
The Linux implementation follows this behavior.

Repeated three-slice actions with both controls at 100% produced
`010 -> 101 -> 010`, with other modules disabled. Trigger history persists
between actions, including selecting Init and then restoring the previous
randomizer preset. The implementation shares transient history between lane
and beat actions; it is neither serialized nor included in undo snapshots.

With slicing disabled and Drum Selection empty, existing assignments remain
unchanged. Disabled Velocity similarly preserves existing slice velocity.
With other modules disabled, increasing subdivision from three to four retains
`101` and appends a silent slice, producing `1010`. These observations are
covered by distinguishing regression tests. They do not establish every
combination of newly created slices and enabled randomizer modules.

## Bar note-value dragging

Desktop 1.6.2 uses a 250-pixel normalized denominator drag range and retains
fractional movement between gestures. It maps `floor(3 * normalized)` to
`16, 8, 4, 2`. Starting at 4, successive gestures up 12, up 48, down 48,
up 100, down 100 and down 12 pixels produced `4, 4, 4, 2, 8, 8`.
The editor regression reproduces this sequence and checks undo.

Switching patterns resets the fractional position: starting at 4, dragging
up 50 pixels retains 4, switching A -> B -> A and dragging down 25 produces 8.
Adding a bar also resets it: starting at 4/8, dragging up 50 retains 8,
adding a bar retains 8, and dragging down 25 changes the first bar to 16.
Both sequences have regression coverage. Other reconstruction paths are not
established by these observations.

## MIDI display and saved-state compatibility

Desktop 1.6.2 offers direct toggles for `Middle C = C4`, `Note Names`, and
`Use Sharps`. Disabling the first changes MIDI note 60 from C4 to C3;
disabling names displays 60. MIDI Channel offers Any and channels 1–16.
Fresh defaults are C4, note names enabled, and flats.

The Linux implementation uses those fresh defaults and explicitly saves the
display preferences. Older native saves that omitted them retain their
historical C3/sharps labels. State tests cover fresh defaults, all four explicit
C3/C4 and flats/sharps combinations, and older omitted-key recall.

## Custom sample dropdown

The manual describes a working dropdown when clicking a custom sample name.
The observed desktop 1.6.2 checks did not open it. The Linux implementation
retains the manual's working dropdown by user decision.

## User Data compatibility remains unresolved

Desktop 1.6.2 User Data > Export opens a directory chooser titled
“Save Beat Scholar Data”. Controlled checks left a newly created destination
empty, including a check with a verified saved user preset in the source.
This establishes failure for the tested arrangements, not every arrangement
or a compatible backup format. A working Linux export would be an intentional
difference from those observations; that choice remains pending. Original
import behavior has not been verified.
