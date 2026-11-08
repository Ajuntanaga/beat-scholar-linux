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
The batch-55 audit records timestamped comparisons and validation separately.
