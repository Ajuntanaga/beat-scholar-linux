# Native reference host diagnostic

This isolated console host resolves the open native-versus-bridged Swing timing question. It sends the same raw VST3 parameter points already used by the Linux diagnostic, directly to `ProcessData.inputParameterChanges`. It does not contain or download the original plugin, license, samples, or project states.

The source is copied from the existing recovery diagnostic. The only additional source change selects the Windows x64 VST3 payload for hash verification. The two patched JUCE headers match upstream revision `ea77444750de45e036e0279bb0291144bf8e3688` byte for byte before patching; this is not a claim about every vendored JUCE file.

## Build

Stage this directory as `tools/native-reference-host` and the workflow recipe as `.github/workflows/native-reference-host.yml` in an isolated branch. `upstream/` is local comparison evidence and is not needed in the package. Build uses the existing approved public/free-tier convention, a Windows x64 runner and a one-day artifact. No workflow has been dispatched yet.

For a local Windows compiler, clone the pinned `8.0.16-security` JUCE branch with `core.autocrlf=false`, run `python prepare.py PATH_TO_JUCE`, then configure this directory with `-DJUCE_SOURCE_DIR=PATH_TO_JUCE` and build target `actual_vst3_host` in Release. The patch script rejects any revision or header mismatch.

## Reference run

Run only inside the existing offline reference VM after confirming an unlocked desktop and exclusive input ownership. Use the original bundle already installed there, its native binary SHA-256, its known CID/name/vendor, and the same local raw component state as `raw-swing-aligned3/original-command.json`. Adapt only filesystem paths and the expected binary hash. Retain all sample coordinates, block size, tempo, rate, transport, pacing and output options. Run the unchanged baseline and transition once each; compare the report's accepted raw-event coordinates before comparing MIDI/audio. Read-only media transfer and output recovery still need arranging. Do not upload original reference assets to Actions.

## Validation status

Linux diagnostic behavior is covered by existing baseline/transition evidence. This package has not yet compiled or run on Windows. A successful native build alone will not establish reference timing parity.
