# Native reference host diagnostic

This isolated console host resolves the open native-versus-bridged Swing timing question. It sends the same raw VST3 parameter points already used by the Linux diagnostic, directly to `ProcessData.inputParameterChanges`. It does not contain or download the original plugin, license, samples, or project states.

The source is copied from the existing recovery diagnostic. The only additional source change selects the Windows x64 VST3 payload for hash verification. The two patched JUCE headers match upstream revision `ea77444750de45e036e0279bb0291144bf8e3688` byte for byte before patching; this is not a claim about every vendored JUCE file.

## Build

The source is staged as `tools/native-reference-host` on diagnostic branch `ci/native-reference-host-20261125`. `upstream/` is local comparison evidence and is not included. [Actions run 45743323672](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45743323672) built the Windows x64 executable successfully at commit `cf86e50834b1a21fda5ca7817c344ec1ea161cfb`. The downloaded executable matches its artifact SHA-256 (`fdb9f9307bf04973731921576276ce5f63ecd3369cf5e06df0e5e9fe0e978325`).

For a local Windows compiler, clone the pinned `8.0.16-security` JUCE branch with `core.autocrlf=false`, run `python prepare.py PATH_TO_JUCE`, then configure this directory with `-DJUCE_SOURCE_DIR=PATH_TO_JUCE` and build target `actual_vst3_host` in Release. The patch script rejects any revision or header mismatch.

## Reference run

Run only inside the existing offline reference VM after confirming an unlocked desktop and exclusive input ownership. Use the original bundle already installed there, its native binary SHA-256, its known CID/name/vendor, and the same local raw component state as `raw-swing-aligned3/original-command.json`. Adapt only filesystem paths and the expected binary hash. Retain all sample coordinates, block size, tempo, rate, transport, pacing and output options. Run the unchanged baseline and transition once each; compare the report's accepted raw-event coordinates before comparing MIDI/audio. `../stage-native-vm-diagnostic.py` stages read-only input ISO and separate writable FAT results media locally. Extract results only after a clean guest shutdown. Do not upload original reference assets to Actions.

## Validation status

Linux diagnostic behavior is covered by existing baseline/transition evidence. The Windows build passed; execution against the original remains pending. Build success alone does not establish reference timing parity or prove the factory samples loaded.

## Controller message-dispatch probe (2026-11-26)
Native baseline matches 106 MIDI events and all 16 drum outputs, but both raw and ordinary controller transition tests have no MIDI effect. `--pump-messages` opts into a one-millisecond JUCE message dispatch before each preroll/render block on the message thread. Its call count and elapsed wall time are reported; dispatch time is excluded from realtime catch-up. This is a responsiveness probe, not evidence of exact transition latency. Without the flag the prior render behavior is unchanged. No production plugin changes are included.
