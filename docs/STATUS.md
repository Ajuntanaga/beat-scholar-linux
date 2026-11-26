## Current checkpoint — 2026-11-26

Original runtime geometry now establishes the subdivision-drag divisor: embedded BeatPizza heights are 54/63/72 at content widths 960/1118/1280 in the measured four-lane layout. The candidate now uses 80% of row height after the scaled top gutter, independently of the painted circle width. Both uninstalled formats build. New three-lane boundary and four-lane 8/16-beat checks cover both wrapper modes at 960/1280; the editor suite passed in 57.90 seconds and actual VST3 smoke passed. An older single-lane playback/undo fixture needed a longer gesture under the corrected height law; its original behavioral assertions are retained and pass. Standalone/VST3 wrapper screenshots were inspected. The preceding full suite passed 13/13 before this editor-only change. Original non-default zoom and a one-pixel inconsistency in an earlier second-threshold pointer capture remain unresolved. Original Kick Note persists through fresh reload, but its playback meaning remains unresolved. These runtime captures had black plugin surfaces and establish geometry/state only. Evidence: `runtime-pie-note-original-20261126/{RESULT.md,OBSERVATIONS.md}` in recovery. Full reference parity remains open.

Raw VST3 Master Gain offset comparison passed: six isolated renders, 48 kHz/512-frame blocks/all 17 stereo buses. Both original and candidate respond to the gain change and produce bit-identical output for offsets 0 and 256 within the same block. Cross-plugin maximum error is 9.78e-9, with all 17 buses within 1e-6 in all three cases. This closes the measured single-point gain-offset gap; no product timing change. It does not establish every parameter or multi-point queue. Evidence: `raw-gain-offset-20261126/{RESULT.md,comparison.json}` in recovery. The auxiliary job peaked at 487.4 MiB with zero swap; all processes exited. Previous 13/13 product regressions remain current. Full reference parity remains open.

The original 1.6.2 Randomizer Default Preset entry is now present with its observed no-op behavior in the tested states. Ctrl-drag subdivision changes now scale with the rendered pie size, use the original directional truncation and stop at 42 for that gesture; the toolbar still supports 45. Both uninstalled formats build. Focused menu tests pass for both wrapper modes, all 13 registered regressions pass in 188.51 seconds, and the actual standalone menu plus subdivision screenshots at 960/1280 widths were inspected. A subsequent original runtime capture resolved the component bounds and replaced the painted-diameter proxy; see the runtime geometry entry above. The old three-lane pointer capture still has a one-pixel second-threshold inconsistency, and non-default zoom remains unmeasured. Evidence: `subdivision-default-candidate-20261126/RESULT.md` and `randomizer-default-original-20261126/OBSERVATIONS.md` in the recovery directory. VM stopped; live REAPER and installed artifacts preserved. Full reference parity remains open.

Repaired the original 1.6.2 Randomizer menu gaps: Load from File, categorized Save To Menu with name/category entry, and the initial Init header. Both uninstalled candidate formats build; asynchronous file-menu checks pass in Standalone and VST3 wrapper modes, including cancel, invalid files, category save and overwrite behavior. Three actual standalone form views were inspected against original captures. Full CTest passed 12/13; the newly exercised built-in sample chooser exposed one stale geometry assertion, corrected to the original custom-sample folder offset. Its editor rerun passed 1/1 in 56.33 seconds, giving all 13 checks passing across the full run and targeted rerun. Evidence: `randomizer-menu-candidate-20261126/RESULT.md` in the recovery directory. Default Preset was subsequently checked in the original; see the follow-up above. Local category-directory storage is not an original filesystem parity claim. VM stopped; live REAPER and installed artifacts preserved. Full reference parity remains open.

Original 1.6.2 follow-up: a bounded 8×8-region capture improved median frame spacing from 148 ms to 30.12 ms. A closed-hi-hat audition retained a visible tail beyond 167 ms (+7 red/+5 green at 235.6 ms after the first observed bright frame, +1/+1 at 471.9 ms). This supports keeping the current traced glow curve; it does not prove exact envelope timing. The same VM loaded an existing guest WAV: single/double clicks on its custom filename opened no menu, whereas the restored factory filename opened its list normally. The manual explicitly promises a custom-name dropdown, so the candidate's working documented behavior is retained and the original 1.6.2 discrepancy is recorded. No product code changed. Evidence: `illumination-roi-original-20261126/OBSERVATIONS.md` and `custom-selector-original-20261126/OBSERVATIONS.md` in the recovery directory. VM shut down cleanly; about 28.9 GiB available RAM and zero memory pressure. Full parity remains open.

Integrated the supplied sweep repairs: default sampler notes now resolve to their own pads, mid-block All Sound Off retains earlier pad meter peaks, and sample arrows suppress inherited button dragging. Actual-pointer captures exposed a further focus-acquisition pressed-state latch; the shared selector now clears that state and restores hover/normal on release. Both uninstalled formats build, all 13 regressions pass in 125.09 seconds, and 10/10 actual-pointer checks pass with the final screenshot inspected. Documentation now reflects the public repository, optional candidate artifact contents and current build prerequisites. Evidence: `sweep-repairs-20261126/{RESULT.md,POINTER_FINDING.md,regression-final.log,editor-final/}` in the recovery directory. Live REAPER and installed artifacts remain unchanged. Full reference parity remains open.

Original 1.6.2 confirms all four disabled Randomizer bodies are half-bright and ignore body controls, while the separate pad bank remains interactive. Repaired the shared editor accordingly, including an Alt-click reset bypass; module re-enabling and the return to sampler restore the controls. Both uninstalled formats build. Focused Standalone/VST3 wrapper tests pass (2/2), the actual headless standalone screenshot was inspected, and all 13 registered regressions pass in 125.08 seconds, including actual VST3 smoke/MIDI/panic/routing. Evidence: `randomizer-disabled-original/OBSERVATIONS.md` and `randomizer-disabled-candidate/RESULT.md` in the recovery directory. The original VM shut down cleanly; live REAPER and installed artifacts are preserved. Full reference parity remains open.

Original 1.6.2 confirms Fill Empty still protects existing hits when Step Triggering is disabled. With protection off, the same brush replaces the Kick with the selected Snare; empty beats remain empty in both cases. The existing implementation matches. Added a focused shared-core regression; serial build and core CTest pass (1/1, 0.63 seconds). Evidence: `randomizer-fill-empty-original/OBSERVATIONS.md` and its captures/logs in the recovery directory. The VM shut down cleanly; no preset or installed artifact changed. The sample-label font trial was reverted after mixed image results; restored Standalone and VST3 builds pass.

The sample-folder drawing now matches the saved original's measured position and 12×9 pixel bounds at the captured size. Both candidate formats rebuild; an isolated standalone screenshot was inspected. Evidence: `folder-proportion-final/{sample-body.png,measurement.json}` and `folder-proportion-{build,capture}.log` in the recovery directory. This is a drawing-only follow-up to the passing illumination regression below; it does not claim complete edge-color or multi-scale identity.

Playback sectors hold at full brightness for their complete swung slice interval, including silent rests. Pads and the selected sample body now follow the rendered audio level instead of a fixed note-event fade. A bounded trace of original 1.6.2 establishes whole-block left-channel RMS before gain/compression, a 10 ms reader with one-step rise/two-step fall, and a full-opacity curve. The candidate implements those mechanics, including incoming MIDI and manual audition while sequencing is stopped. Audio output, MIDI gates and event ordering are unchanged.

Both uninstalled candidates build successfully. Focused lighting checks pass for Standalone and VST3 wrappers; their MIDI-hit screenshots were visually inspected. All 13 registered regression checks pass in 127.87 seconds, including actual VST3 smoke, MIDI, panic and routing. Tests also cover silent prefixes, summed voices, empty/oversized callbacks, and a mid-block All Sound Off retaining earlier activity. Evidence: `.beat-scholar-recovery/pad-glow-reference/{OBSERVATIONS.md,final-build.log,final-focused.log,regression.log,editor-final/}` in the parent checkout. Earlier complete slice-hold evidence remains under `slice-hold-repair/`. Installed artifacts and the user's REAPER session remain untouched.

Fresh original 1.6.2 VM playback captures show constant white sounding sectors and grey rests across long-slice plateaus, supporting the implemented hold behavior on that version. The pad and selected sample body retain similar glow ranges at the visible left, center and right pan positions, supporting the candidate's pre-pan tap; the binary ownership path is still unresolved. Evidence: `illumination-original-20261126/{OBSERVATIONS.md,measurement.json,slice-hold-contact.png}` in the recovery directory. Actual frame spacing was 123–160 ms, so this is not sample-exact timing or short-tail evidence. Full reference parity remains open, including fine illumination timing, icon/font rasterization, live Swing transition equivalence and remaining feature coverage.

Claude's supplied Swing-tutorial measurements show a hi-hat glow tail reaching baseline sooner than the traced 1.6.2 curve predicts. The early fall agrees; simple 8-bit alpha rounding cannot account for the reported 100-count red excursion disappearing by 167 ms. The saved paint/transfer trace has no explicit positive cutoff beyond alpha quantization. Tutorial version, compositing and frame sampling remain unresolved; no speculative fade threshold was added. The earlier full-frame VM cadence could not settle that short tail; the later small-region observation above resolves whether original 1.6.2 cuts off as early as the tutorial measurement. If a prepared host stops audio callbacks entirely, the candidate's last published RMS can remain latched until callbacks or a reset resume; corresponding original behavior is unverified.

Earlier selector arrow fills, folder tab and actual REAPER routing UI checks remain recorded below and in the recovery checkpoint. The routing UI runner exited 137 after saving both passing transitions; only its disposable processes were closed. The native message-pumped Swing positive control passes: the static baseline matches 106/106 MIDI events and 16/16 drum buses. Live transitions vary across original hosts, so no artificial product delay was added. Original sample-arrow wrap is verified and the offline VM is stopped. Earlier credential, wrap and dispatch-pending notes below are historical. Authoritative recovery notes are in the parent checkout's `.beat-scholar-recovery/RESUME.md`.

## Swing host dispatch narrowed — 2026-11-24

Read the existing actual_vst3_host and JUCE VST3 wrapper, then parsed saved original/candidate logs in jungle-extended-a8748ed-swingtransition64corrected. All three Swing injections in both logs report injected_on_message_thread=true. EditControllerParameterDispatcher::push calls setParamNormalized immediately on that thread; its 60 Hz deferred dispatcher is therefore not the cause of this captured discrepancy. VST3Parameter::setValue also marks cachedParamValues, which processAudio sends at sample offset zero on the next block. Bridge/original internal asynchronous handling remains possible, not established. Public VST3Client exposes IComponent and preset operations, but no existing process-only parameter injection API: isolating this would require a separate raw processing path or diagnostic JUCE change, not simply a host option. No speculative production delay or source edit made. Existing combined13/13 regression result stands; no repeat required. RAM28GiB available, swap0. No jobs started; live REAPER and installed artifacts untouched. Full parity remains OPEN, native original access approval pending. This source/log analysis resolves a specific host-side uncertainty; it does not resolve the observed Swing mismatch.

## Native comparison gate revalidated — 2026-11-24

Previous turn was progress: all13 combined regression checks passed. Reviewed current Sounds/manual-key audits rather than repeating those tests: native sample import/persistence and candidate sample-arrow input already verified; original sample ordering/wrap, custom popup contents, focus/gesture semantics and flash timing remain unverified. Current /proc inventory finds no qemu-system process and the Beat Scholar VM root has no top-level .sock/.pid control endpoints. No usable running original GUI session to inspect. Did not retry rejected replacement credential, start another Wine black-window attempt, or modify candidate speculatively. Original credential approval remains pending. This is the first consecutive blocked/no-progress turn after the combined regression progress, not sufficient to mark the entire goal blocked; process-only automation remains a potential independent avenue for the Swing timing discrepancy. Full goal OPEN. No jobs started, no REAPER or installed artifact changes.

## Combined candidate regression passed — 2026-11-24

Prior goal turn made progress repairing the lane-plus stem and verifying captures. Rebuilt all configured targets serially (combined-current-build.log, handle6378 exit0), then ran all13 registered CTests serially with Archive hidden (combined-current-tests.log, handle47865 exit0). All13 passed in118.03seconds: core, audio, scheduler, factory migration, effect parity units, ambience, factory playback, editor, state, actualVST3 smoke/MIDI/panic/routing. This verifies combined candidate regressions after recent changes, not original reference completeness. No new defect reproduced; no duplicate run needed absent a change/failure. RAM28GiB available, swap0. Live REAPER and installed artifacts untouched. Full parity OPEN: original Swing delivery, sample-arrow semantics, flash envelope and remaining native GUI/visual comparisons. Original replacement-credential approval remains pending. Both owned jobs terminal.

## Lane-plus stem mismatch repaired — 2026-11-24

Previous goal turn made progress by saving built changes and the four-pixel residual. Current screenshot measurement identified an overwide vertical stem; narrowed it and offset it left to match saved original reference. Both serial staged builds passed (lane-plus-stem-build.log, job34218 exit0). Archive-hidden isolated capture passed (lane-plus-stem-capture.log, job66572 exit0), screenshot viewed. lane-plus-stem-final/glyph-measurement.json reports header36/36 and lane16/16 bright pixels, zero differing pixels in each narrow region at this capture size. This does not establish whole-UI identity or other-scale equivalence. Hit regions/callbacks unchanged; installed artifacts/live REAPER untouched. RAM27GiB available. Full parity OPEN: native Swing delivery, sample-arrow behavior, exact flash timing and remaining GUI coverage unresolved; original credential approval still pending.

## Add-lane glyph comparison — 2026-11-24

Replaced undersized text plus signs in the header and lane controls with scaled strokes; dense-lane strokes remain bounded by button height. Both staged Standalone and VST3 serial builds passed (lane-plus-build.log). Archive-hidden isolated capture passed (lane-plus-capture.log); screenshot lane-plus-final/linux-tutorial-bars-stopped.png reviewed. Narrow bright-pixel measurement against saved Windows reference: header 36/36 pixels, zero differing; lane reference16/candidate20, four differing. Lane glyph rasterization therefore remains imperfect; this is not full visual parity. Hit regions and callbacks unchanged. Build87828/capture16578 finished successfully. No installed artifacts or live REAPER changes. Full parity remains OPEN; original native GUI credential approval pending and recorded Swing/sample-arrow/flash-timing gaps unresolved. Storage cleanup withdrawn per relayed user instruction.

## Lane-remove glyph repaired — 2026-11-24

Saved Windows reference and current candidate showed lane-minus font hyphen rendered as2x2 bright pixels versus reference8/6 pixels over2 rows. Replaced only control2 text with a scaled rounded bar, bounded by slot dimensions for dense lanes. First screenshot exposed vertical placement error; corrected thickness/offset and rebuilt both staged formats. Final Archive-hidden screenshot viewed: lane-minus-final/linux-tutorial-bars-stopped.png. First-lane mask atx178..191,y233..240 with RGB minimum>120 matches reference exactly (8pixels row237,6row238); this is narrow glyph evidence, not whole-UI pixel parity. Standalone/VST3 serial builds passed lane-minus-build.log; git diff --check passed. Hit regions/callbacks unchanged. Build46146/capture91998 finished0. No live REAPER or installed artifact changes. Full parity remains OPEN, with original native GUI credential approval pending and the previously recorded Swing/sample-arrow/flash-timing gaps unresolved.

## MIDI export lane filtering verified — 2026-11-24

Extended serialized MIDI gate regression from8 to12 cases: muted lane, solo lane, muted solo lane, and multiple solo lanes. Independent expected note counts/identities accompany playback comparison, guarding against shared export/render filtering defects. Serial editor build and Archive-hidden editor suite passed (midi-lane-filter-build.log, midi-lane-filter-tests.log); git diff --check passed. No product defect reproduced. Live REAPER and installed artifacts untouched; available RAM28GiB, swap0. Build51669/test12631 terminal0. Full parity remains OPEN, including original Swing delivery, sample-arrow semantics, flash timing and remaining native GUI/visual coverage. Original Windows credential approval remains pending. Storage cleanup is withdrawn.

## Undo/redo glyph repair — 2026-11-24

Saved Windows/candidate enlarged comparison demonstrated thick filled bend and level arrowhead versus original thin curved stem and angled filled head. Shared LookAndFeel drawUndo now draws separate stroked curved stem and filled angled head, mirrored for redo. Serial Standalone/VST3 builds passed (undo-glyph-build.log); git diff --check passed. Fresh Archive-hidden isolated screenshot and enlarged comparison viewed: undo-glyph-final/{linux-tutorial-bars-stopped,comparison}.png. Shape discrepancy repaired; size/rasterization differences remain, so no pixel-identity claim. No behavioral callbacks changed, no installed artifacts or live REAPER touched. Build8367/capture2703 terminal. Full parity remains OPEN; original native GUI credential approval still pending.

## Pattern Rename dialog verified — 2026-11-24

Manual p14 pattern renaming exercised through actual context menu and modal dialog. Added PatternRenameTests covering initial name, accepted whitespace trimming, blank-name fallback, cancellation without an edit, exactly one accepted edit, and complete project restoration by one undo. No product defect reproduced. Serial editor build passed after correcting the test snapshot type to MemoryBlock; Archive-hidden editor suite passed1/1 in41.75s (pattern-rename-tests.log). git diff --check passed. Live REAPER and installed artifacts untouched. Build65043/test71923 terminal; last RAM28GiB available. Full parity remains OPEN, including original Swing delivery, sample-arrow semantics, exact flash envelope and remaining native GUI comparison. Windows credential approval remains pending; storage cleanup withdrawn.

## Randomizer manual range coverage completed — 2026-11-24

Manual pages21-22 require 45 subdivision choices, page-local All and global Clear. Extended existing actual editor-control regression through remaining pages and wrap to first: all selections retained, sorted result exactly1..45, no duplicates, Clear empties every page. Serial editor build passed; Archive-hidden editor suite passed1/1 in40.94s (randomizer-manual-tests.log). Existing code/core tests cover Fill Empty preservation, cross-beat clustering, 100% Trigger Chance and brush velocity when velocity module off; no new product defect demonstrated. git diff --check passed. Owned build6030/test67200 terminal; RAM27GiB available, swap0. Live REAPER/installed artifacts untouched. Full parity remains OPEN; Windows credential approval still pending for original GUI comparisons.

## Sample portability checks and factory-menu test repair — 2026-11-24

Manual custom-sample requirements reviewed against existing portability/editor coverage. Current state suite passed (sample-portability-check-tests.log); editor initially failed ten assertions in factory-menu cases23/24. Screenshot case23 showed Hit instead of Body: test sorted vertically stacked controls by X; reference-aligned label offsets put lower layer first. Changed test ordering to Y, preserving product behavior. Serial editor rebuild passed; isolated editor suite rerun passed1/1 in40.90s (sample-menu-test-fix-tests.log), Archive hidden. Custom sample tests include removal of original files, embedded state restoration and identical rendered audio. git diff --check passed. No installed artifacts or live REAPER changes. RAM27GiB available, swap0 at check. Owned build69893/test2059 finished. Original Windows GUI login approval requested and pending; do not retry recovered credential without approval. Full parity remains OPEN, including original Swing delivery, sample-arrow semantics and flash timing. Storage cleanup withdrawn per relay.

## Duplicate-page glyph repaired — 2026-11-24

Enlarged Windows/candidate comparisons showed foreground copy page mirrored and oversized in both bar headers and Bars cards. Shared drawActionGlyph now places a smaller white filled page below/right of the outlined rear page. Serial standalone/VST3 builds passed; diff check passed; viewed final isolated screenshot copy-glyph-final/linux-tutorial-bars-stopped.png. Before/intermediate comparisons: copy-glyph-comparison.png and copy-glyph-verified/comparison.png. Orientation and foreground color corrected; remaining rasterization/geometry differences mean no pixel-identity claim. No runtime behavior or installed artifact changes. All owned build/capture handles terminal. Available RAM27GiB, swap0 at check. Archive hidden during runtime. Full parity remains OPEN.

## Kit-lock drawing repaired — 2026-11-24

Shared BeatScholarLookAndFeel kit.lock now draws an upright upper-half shackle and filled muted body, replacing the sideways arc and outlined body. Standalone and VST3 serial builds passed (kit-lock-build.log); git diff --check passed. Isolated standalone capture with Archive hidden completed; viewed full screenshot and original/candidate enlarged crop in kit-lock-verified/comparison.png. Shape defect corrected; small geometry/rasterization differences remain, so not pixel-identical. No installation, commit, live REAPER change, or VM access. Owned build14834 and capture63170 finished. Manual confirms hosted transport/BPM are DAW-controlled; existing code/tests already cover this, no transport change made. Full parity remains OPEN: original flash timing, extreme Swing delivery, sample-arrow semantics and remaining visual/native coverage. Storage cleanup withdrawn per relay.

## Current VST3 playback lighting verified — 2026-11-24

Ran latest candidate-a8748ed VST3 in isolated REAPER/private Xvfb/paced hardware-free audio with Archive hidden. Process51313 exited0; one audio node and two stereo links. Transport35/35 distinct playing positions,100 to150BPM,seek,editor close/reopen without stopping,final stopped state0. Viewed full playback screenshot and11-frame lighting contact sheet: pie highlights advance and corresponding pad backgrounds flash, including after editor reopening; final stopped frame clears transient highlights. Evidence vst3-lighting-current-20261124/{summary.json,audio-clock.json,capture-times.json,lighting-contact.png,transport.tsv}. This verifies candidate behavior, NOT original flash-envelope/timing identity, plugin-local transport buttons, or installed VST3. No product edits, installation, liveREAPER changes, or active owned jobs. Full parity OPEN. Storage cleanup remains withdrawn.

## Sample label geometry repaired — 2026-11-24

Measured saved Windows reference-bars-mode.png versus current capture: DW Sub ink29x6 vs24x5, SvN Kick31x6 vs27x5, with vertical offsets. Shared look-and-feel sample.layer font now0.68*buttonHeight instead max8/0.34; factory upper selector designx428/y577, lower y685. Custom selector positions preserved. Built standalone and VST3 serially; final capture sample-label-verified viewed. Both upper and lower bright-ink bounds now exactly match reference:513,543..541,548 and511,624..541,629. Bright pixel counts still differ71vs65 and74vs59: rasterization is not pixel-identical. Evidence sample-label-verified/measurement.json and measure-sample-labels.py. Actual VST3 smoke PASS1/1; diffcheck PASS. No audio/transport edits; no installed artifact or liveREAPER changes. All owned build/capture handles terminal. Full parity OPEN; remaining icon shapes, font rasterization, original flash timing and broader native GUI checks remain.

## Current visual capture — 2026-11-24

Ran current candidate-a8748ed standalone on private Xvfb and paced hardware-free audio, Archive hidden. Capture handle95643 exited0; one audio node and two stereo links confirmed. Artifacts visual-current-20261124 include stopped Bars/Patterns views and twelve 100ms playback frames, timestamps and lighting pixel samples. Viewed stopped Bars against saved Windows reference-bars-mode.png and playback06: beat circles and instrument pads visibly illuminate; this is standalone animation evidence, not original flash-curve or VST3 proof. Saved reference and candidate still differ in small icons and text sizing (sample labels and pad-header controls); no speculative product edit. Visual-only state overrides documented in capture directory. All owned children cleaned up. Full parity remains OPEN; next quantify a specific remaining icon/text discrepancy or obtain original moving-reference capture. User REAPER and installed artifacts untouched.

## Original Pitch controls investigated — 2026-11-24

Original Kick Pitch AllowPitch28 and Root29 have no candidate host bindings; rootNote is retained/imported but unused in DSP. Tested original AllowPitch=1 and Root=48/127 against unchanged candidate: sequencer48kHz passes17/17 buses, max3.725290298461914e-08; external kick MIDI44.1kHz passes17/17 bit-identically. Evidence jungle-extended-a8748ed-allowpitchroot48/comparison.json and jungle-extended-a8748ed-44100-allowpitchroot48midi/comparison.json. Names/readbacks confirmed. This does not establish that controls are globally inert or complete host-parameter parity; no audible omission demonstrated in these fixtures, so no guessed pitch implementation added. First MIDI attempt at48kHz was rejected by harness sample-rate assertion before rendering; use corrected44.1kHz result only.

Sample arrows candidate ordering/clamping confirmed in source; original semantics remain unverified. Known Windows VM monitor socket absent. No VM startup or Archive access attempted. Next work needs original GUI access or a materially different reference requirement, not repeated tests of these unchanged fixtures. Owned handles50200/50130 complete; liveREAPER and installed artifacts untouched. Full parity OPEN.

## Swing controller settling diagnostic — 2026-11-24

Added optional diagnostic-only --parameter-settle-ms to actual_vst3_host (default0, bounded0..1000); waits after parameter groups without advancing transport and excludes waits from realtime pacing catch-up. Comparison wrapper forwards it. Serial host build PASS; git diff --check PASS. No production behavior changed.

50ms settling, 64-frame callbacks: original first affected onset48448 vs candidate48242, compared with prior unpaused original49728. Both112MIDI events; audio13/17 buses pass1e-6. This supports wall-clock-sensitive original/bridge delivery but does not identify the layer or prove parity. Evidence jungle-extended-a8748ed-swingtransition64settle50/{comparison,transition-diagnosis}.json. Do not treat diagnostic pauses as realtime parity or introduce fixed product delays. Next distinguishing evidence: Windows-native delivery or direct process-only automation, not more arbitrary waits. Runtime handle64027 and build14528 terminal; liveREAPER/installed artifacts untouched, Archive hidden. RAM28GiB available. Full parity OPEN; storage cleanup withdrawn.

## Swing transition callback-size probe — 2026-11-24

Previous turn made progress: One Shot repair verified. New corrected 64-frame callback comparison completed (handle74373 exit0), 48kHz/4seconds, Swing index0 in both plugins at samples0/48128/144384. Both report exact parameter values on message thread,112MIDI events each. Original first affected note41 onset49728 versus candidate48242; earlier512-frame original runs49152/49664. All102 events outside50ms transition windows match exactly. Audio11/17buses pass1e-6; this is NOT full parity. Evidence jungle-extended-a8748ed-swingtransition64corrected/{comparison,transition-diagnosis}.json. First probe used incorrect candidateindex1 and is explicitly marked INVALID.txt; do not count it.

Read current JUCE host delivery: juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h VST3Parameter::setValue updates cachedParamValues and dispatcher; message-thread dispatcher calls controller synchronously; process copies cache into inputParameterChanges at offset0. Thus smaller callbacks do not remove original transition lag, and host code does not intentionally delay this by one callback. Reference bridge/plugin asynchronous application remains a hypothesis, not proven cause. No production changes or arbitrary delay introduced. Next useful probe must distinguish native/bridge controller application from process automation (or obtain Windows-native host comparison), not repeat static endpoint tests already passing. All ownedjobs terminal; installed plugins/liveREAPER untouched; Archive hidden. Full parity OPEN.

## One Shot automation and active-voice envelope verified — 2026-11-24

Added 16 One Shot host controls at indices397..412, preserving existing IDs. Connected audio, state recall, targeted undo/redo, host gestures and editor refresh. Live original comparison exposed a Length-envelope mismatch when changing One Shot during playback: 14/17 buses initially passed; holding it off passed17/17. Fixed each voice to retain its initial One Shot Length-envelope mode; new voices adopt the current mode. Gate/release behavior remains live.

Corrected dynamic original/candidate comparison (1/0/1 at samples0/48128/144384, 48kHz,512-frame callbacks,192000frames) passed17/17 buses, peak error3.725290298461914e-08, zero frames above1e-6. Evidence: jungle-extended-a8748ed-oneshotenvelope/comparison.json. Red comparison: jungle-extended-a8748ed-oneshotautomation/comparison.json; static isolation: jungle-extended-a8748ed-oneshotoffstatic/comparison.json.

Final serial standalone/VST3/audio/editor build PASS (oneshot-verified-build.log). Isolated audio/editor/actualVST3 tests PASS3/3 in54.78seconds (oneshot-verified-tests.log); regression checks active-voice retention and new-voice behavior in both directions. Whitespace check PASS. Handles31423 and87770 complete; no remaining owned job. RAM28GiB available, swap0. Installed artifacts and live REAPER unchanged; Archive hidden during execution. Full parity OPEN: extreme Swing automation timing, sample-arrow ordering/wrap, precise playback visuals and original GUI coverage remain. No storage cleanup pending.

# Current verification status — 2026-11-23

Full reference parity is **not complete**. The repository is public for source-only
CI; original binaries, licenses, factory content and private captures are not
included. Latest validated uninstalled standalone/VST3 candidate: `0e0c4ed`.
Earlier preset comparisons below used `fef0f64`; CascaDoron baseline captures
used `acfc485` and the repaired captures used `211e3ea`.
Installed artifacts and the user's REAPER session remain unchanged.

## Verified comparisons and candidate validation

- [GitHub Actions run 45263775498](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45263775498)
  passed standalone/VST3 compilation, core/audio/editor/actual-VST3 suites,
  strictness-10 plugin validation and standalone state recall.
- Local factory playback passed 244/244 patterns at 48 kHz and nine default
  preset/rate checks at 44.1/48/96 kHz. These check loading, expected notes,
  finite output and non-silence, not original sound identity. 123 patterns
  reached or exceeded full scale in floating-point output; this alone does
  not establish clipping or an incorrect gain stage.
- Controlled original/candidate Intruders full-pattern comparison at 48 kHz:
  192,000 stereo frames, peak error 2.3842e-7, RMS error 2.2978e-8,
  zero frames above 1e-6, and 138 matching MIDI events. The original repeat
  was bit exact; the candidate comparison passes tolerance, not bit identity.
- SikBit at 96 kHz with 64-frame callbacks and matched corrected preset state:
  384,000 stereo frames, peak error 4.7684e-7, RMS error 2.3983e-8,
  zero frames above 1e-6. An earlier 0.08955 peak discrepancy used an obsolete
  preset-state fixture and is not evidence of a current engine regression.

- Multi-output routing at 48 kHz with 512-frame callbacks: all 16 pads reach
  their dedicated stereo buses; the master bus is silent. Across 48,000 frames
  and 34 channels, peak original/candidate error is 1.0431e-7, with no frames
  above 1e-6. Eight pad buses are bit exact for this fixture.
- Reducing master gain by 20 dB leaves dedicated outputs bit exact against
  each plugin's own baseline. Original filter, crush and compression bypass
  controls separately also leave these outputs unchanged. A combined-control
  repeat matches baseline, but its first capture differed by 0.1449 on the
  hi-hat bus. That original-only inconsistency is not a confirmed candidate
  defect; its cause remains unresolved and both captures are retained.

## Source repairs and bounded candidate comparisons

- Sequencer playback now resolves the emitted MIDI note through the native
  input map before choosing a sampler pad (`ae0ce53`). The source beat/slice
  identity and emitted MIDI are retained.
- Empty-layer mixing now applies to the native kick/snare slots while
  single-layer pads retain unity layer gain (`f958c34`). The focused audio
  regression passed locally.
- [Run 45275609123](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45275609123)
  passed compilation, strictness-10 VST3 validation and standalone state
  recall. CTest passed 12/13 suites; the editor suite reported two dense audio
  assertions using inconsistent sequencer/replay MIDI maps.
- The dense fixture now uses the same native note map for both performances
  (`acfc485`). [Run 45277157931](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45277157931)
  passed the full workflow, including the core/audio/editor/actual VST3 suites,
  strictness-10 VST3 validation and standalone state recall.
- The checksum-verified candidate's CascaDoron dry/master-off comparison now
  matches 16/17 stereo buses within 1e-6 over 48,000 frames at 48 kHz, without
  alignment or normalization. Kick and ride match exactly. Only shaker remains:
  peak error 2.2612512e-5, RMS error 3.9195909e-6. This closes the earlier ride
  discrepancy; broader preset, effects and visual parity remain open.

- CascaDoron full-mix baseline (`acfc485`), 48,000 frames at 48 kHz:
  the audible master bus has peak error 2.2619963e-5, RMS error 3.6981168e-6,
  and 15,380 frames above 1e-6. The other 16 buses are silent in both captures;
  they do not constitute 16 independent audible matches. Reference master
  peak is 0.80382466. The repaired comparison below supersedes this baseline.

- Native attack arithmetic uses a double reciprocal duration rounded to a float
  increment, accumulated into a persistent float envelope each sample. Source
  repair `211e3ea` replaces truncated-frame division and adds a sustained attack
  and callback-boundary regression. [Run 45282156585](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45282156585)
  passed the full workflow. The checksum-verified candidate passes all 17 dry
  buses within 1e-6; worst peak error is 1.02034e-11. The full-mix audible
  master peak error is 1.49012e-8, with zero frames above 1e-6. These are
  48,000-frame CascaDoron comparisons at 48 kHz and 512-frame callbacks;
  broader presets, sample rates and automation remain unverified.

## Jungle Polyrhythm reference investigation

Matched 257-frame callbacks at 48 kHz bring five of seven audible dedicated
buses within 1e-6 over the first 48,000 frames. The original triangle bus
contains nonfinite samples, so the full capture fails the strict validity check.
The candidate capture is finite. These results do not establish full-preset parity.

The isolated snare uses original MIDI note 37 and candidate note 38. Repeated
original captures differ by a peak of 0.0898243; the original/candidate difference
is 0.0765131. Original repeatability must be resolved before attributing this
residual to candidate DSP. Earlier original-note-38 isolation used the wrong map
and must not be treated as evidence of a candidate defect.

Stopped DSP pre-roll did not resolve reference variability. The startup-delay
host in `8bc00e5` passed remote validation. Three controlled captures completed:
no delay, two seconds of delay, and a repeat with two seconds of delay. The two
delayed original captures still differ by 0.0533721 peak over 5,368 of 48,000
frames (threshold 1e-6). A two-second startup delay therefore does not resolve
the reference instability.

Opt-in wall-clock pacing in diagnostic host `2845a88` resolved the isolated
snare repeatability issue: two original captures and the mapped candidate
capture are bit exact over 48,000 frames at 48 kHz. The default host mode is
unchanged. Earlier unpaced differences above are investigation history.

Paced wet Multi-output comparisons then isolated a real master-return defect:
with the original master bypassed, its output matches candidate `211e3ea`
within 9.3133e-9. Original master low-pass and crush controls change the shared
return while leaving all 16 dedicated pad buses bit exact against baseline.
The candidate had bypassed these master stages on the shared Multi return.

Source commits `1a8c3af` and `7ab7f2d` apply master compression, filtering and
crushing to that return while keeping dedicated pad buses outside the chain.
Commit `0e0c4ed` corrects the impulse fixtures and an obsolete routing assertion.
[Run 45304125744](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45304125744)
completed successfully, including strictness-10 validation. The checksum-verified
candidate passed all four original-reference comparisons: baseline Multi, master
low-pass, master crush and stereo regression. Each checks 48,000 active frames
at 48 kHz with fixed alignment, no normalization and a 1e-6 error threshold.
Worst peak errors respectively are 1.3038516e-08, 1.0477379e-09, 2.7648639e-10, 5.9604645e-08.
All 68 measured stereo buses pass; silent buses are not independent audible
matches. This closes the tested shared-return discrepancy, not full parity.


Extended Jungle Multi comparison: current 0e0c4ed versus paced original over 192000 active frames at 48 kHz (four seconds), fixed alignment/no normalization. All 17 buses below 1e-6; worst peak error 3.725290298461914e-08. Evidence jungle-extended-0e0c4ed/comparison.json. Captures run serially with archive hidden and two CPU affinity; no installed or live REAPER changes. Full parity remains incomplete.


Jungle post-stop comparison passed: 192000 active + 192000 stopped frames at 48kHz; 17/17 buses below1e-6, worst3.725290298461914e-08. Original declares zero tail, so explicit post-stop duration was required. This establishes this fixture audio after stop, not matching tail metadata. Four new WAVs losslessly gzip-compressed and hash-verified to limit disk growth; see jungle-extended-compression.json. Full parity remains open.

## Remaining verification and repair scope

- Whole-interface visual identity, including playback lighting and interaction
  timing, is not established. The Windows VM reference ledger records working
  original GUI captures, timed playback lighting and a stereo audio capture;
  the earlier black Wine captures do not block that reference path. Exact MAP
  drag scaling, Swing drag mapping and complete matching candidate captures
  remain open. A fresh VM run still requires checking its current state.
- Broader controlled original-preset comparisons, shared-effect bus behavior
  and automation coverage remain incomplete. The bounded audio comparisons
  above must not be generalized to all presets or every control combination.
- Confirmed host-automation omission: pad volume, pan, pitch, filter and AUX
  controls exposed by the original are not registered in the candidate parameter
  layout. Current host parameters cover four global controls and pad bypasses.
  Original control-range capture is retained locally; implementation remains open.
- Physical audio/MIDI devices, reconnection and full live-host workflows still
  need direct checks. Isolated host tests do not establish those outcomes.

Licensing, trial, update and deactivation services remain excluded. No release
or complete compatibility claim is made. Private evidence and exact capture
manifests remain local; the sections below are historical and may describe
superseded defects, candidates and repository visibility.

---

# Historical GitHub snapshot — 2026-11-08

This private repository contains the current authored source and portable tests.
It does not contain original binaries, licenses, factory content, local captures,
or built app/VST bundles. It is not a release.

The latest recorded staged candidate is batch 58. Its ten runnable suites,
pluginval strictness 10, fifteen actual-VST MIDI cases and standalone checks
passed; private factory playback was skipped. Batch 60 added a passing focused
pattern-trigger integration check. Batch 62 corrected native Init object
ownership in the investigation; exact native reset samples and hidden defaults
remain unresolved. These are historical candidate results, not a certification
of every change in the current working source.

A fresh source-only export was configured and built on 2026-11-08 with the
plug-in disabled and two compile jobs. All five core CTest suites passed.
The full VST3/standalone build was not repeated for this GitHub sync.

Full sound and visual parity remain open. Local audit reports and captures stay
outside GitHub; the notes below retain their measurements for context.

---

# Current status

Historical audit notes updated through 2026-11-03. This project is staged privately for continued development.
There is no public release or claim of complete compatibility.

The historical standalone/VST pair described below is **batch 51**. It preserves the native
float multiplication order before the shared Aux3 distortion arctangent,
reducing maximum-drive waveform error from 2.93% to 1.34% in the saved native
comparison. Batch 52 then isolated PCM24 input quantization: using a new
floating-point native capture, the unchanged processor measures 0.000862%
maximum-drive error (48 kHz, 128-frame callbacks). Exact sound identity
across the full product remains open. The
auxiliary report (local audit record) records the repair
and measurements. Previous staged candidates remain available.

Batch 53 captured the original Intruders Must Perish / Fat pattern locally:
peaks 1.823298/1.821530, RMS 0.359205/0.359501. The current batch 51 processor
fixture has peak 1.695521 and RMS 0.269142 for the same preset and duration.
Above-unity output alone is therefore not a limiter defect. The roughly 25.1%
RMS difference and native repeat variation remain unresolved. The measurement
host now supports float WAV output; a source-only Modal build and synthetic
actual-VST test confirm above-unity preservation and the default PCM24 path.
No production DSP or GUI change was made in this batch.

The 18-video ledger (local audit record),
sidebar report (local audit record),
sound controls report (local audit record) and
manual section report (local audit record) track the
reference checks. Older batch reports below are historical evidence.

## Official tutorial comparison

Batch 19 corrected pie orientation, legacy Room/Reverb sends, standalone
Spacebar, the imported kit header, lane-control size and sampler styling using
Modalics' Basic Beat Making & Editing, Presets & Kits, and Samples & Sounds.
The same tutorial preset now shows Clear SvN and Kick Room 41% / Reverb 17%.
Fonts, icons, colors and spacing still differ in places; complete visual identity
is not established.

The repaired actual-VST render at 100 BPM has rhythm-envelope correlation
0.906 and waveform correlation 0.621 with the selected tutorial interval.
These are similarity measures, not correctness scores. Low-frequency energy
still differs, and exact sound identity remains unproven because the recording
chain and version/preset equivalence are uncontrolled.

## What has been checked

The [batch 51 source-only Modal run](https://modal.com/apps/snorkelai/main/ap-Lnsw535Aiiw0eqCKy8fwW2)
passed configure/build, ten available CTest suites (private factory suite skipped
remotely), pluginval strictness 10, fifteen actual VST3 MIDI cases, state/audio
smoke and isolated standalone settings, gestures, flam and project recall.
All 55 frozen/current files, remote source hashes and staged binary hashes match.

Local shared-distortion comparisons against saved native renders at 48 kHz and
128-frame callbacks have waveform errors 0.01293% (Drive 0),
1.33733% (Drive 1), and 0.01055% (half-send), all zero lag.
Batch 52 supersedes the maximum-drive PCM24 residual with a floating-point
comparison: 0.00086167% error, passing a 0.01% gate. All nine native dry/wet/
dry-return captures repeat exactly over four seconds. No production change.
These processor comparisons do not prove host sound or complete tutorial identity.

Whole-interface visual/sound identity, real DAW MIDI interchange, native desktop
picker input and physical devices remain unverified. Batch 50 completed all 244 patterns from 129 private presets with no reported
playback failures. Twelve patterns reached unity (peak 1.694586); original sound
parity and clipping-free output are not established. The following batch 19 results
are historical.

In batch 19, all 244 patterns from 129 private factory presets passed locally at 48 kHz
with Room/Plate responses loaded, rendering at least a cycle and four seconds
per pattern. 71 patterns reached or exceeded unity; the largest absolute peak was
3.618424. This checks loading, finite output and expected notes, not original
sound parity or absence of clipping. The separate tutorial render had no
non-finite samples or samples above unity.

The local tutorial capture probe passed 14 checks, including the reference
preset's kit identity, effect sends and tempo. The same-preset screenshot,
large lane controls and actual standalone playback capture were inspected.
Only reconstruction source, synthetic tests and build tools went to Modal;
private samples, responses, original binaries and licenses stayed local.

Earlier batch-16 private Room/Plate checks passed at 44.1/48/96 kHz, including
block-size consistency. Batch 18 verified real mouse meter entry and restart
recall. These checks were not repeated here. Their historical evidence is in the
November 1 audit (local audit record).
Physical audio/MIDI and the active REAPER session remain untested and untouched.
The new candidate is staged separately from installed copies.

## Repairs already in the source

- Temporary slash, Alt and number shortcuts preserve the selected Slicer
  division. Actual beat/lane input and one-step undo pass in an isolated window.
- The sample-folder button opens Load Custom Sample directly, matching the
  tutorial. It shares the pad-menu chooser and retains host-state/lifetime guards.
- Randomizer Spacing now separates hits at high settings and continues phrases
  at low settings, including Fill Empty across beats. Its exact curve is inferred.
- Randomizer Min/Max and toolbar velocity display and accept MIDI 0–127 values;
  Trigger Chance uses percent. Saved normalized precision and exact Alt-reset
  are preserved; the three Randomizer value dials use rising circular fill.
- Bar and pattern context menus include the tutorial commands, shared clipboard
  actions, insertion and one-step undo. Insertion placement/defaults are inferred.
- Held C copies the whole beat in every brush mode. Held V previews it and
  drag-fills visited beats in one undo step; empty/identical pastes are no-ops.
- Linux Ctrl+Z/Ctrl+Y use key codes, so real X11 control-character text no
  longer prevents undo/redo. Actual standalone input and restart pass.
- Pie drawing and picking use the tutorial's six-o'clock clockwise origin;
  imported legacy Aux1/Aux2 map to Room/Reverb without swapping native states.
- Standalone unmodified Space toggles Play/Pause with text-entry and host guards.
- Imported presets resolve a unique factory kit identity, preserve embedded
  sounds and honor kit lock; the tutorial preset now reads Clear SvN.
- Lane controls use the measured smaller dimensions, and sampler body/selector
  styling is closer to the official tutorial captures.

- Double-click accepts typed numerator and denominator values in both the main
  bar header and arrangement pads. Invalid values stay editable; cancellation,
  unchanged values and stale targets leave the project and undo history intact.

- Drive knob travel follows the native gain-domain range centered at 12 dB;
  intermediate markers and inverse mouse conversion now agree.

- Room and Plate now use shared stereo convolution with verified local responses,
  native resampling/normalization and +3/+6 dB main-bus returns. Both routing
  modes receive the shared returns before master processing.

- Master Filter uses the native two-stage low-pass response and cutoff/Q ranges.
  Raw preset import and exact resonance Alt-click reset/undo are repaired.
- Native pad and master compressor envelopes, curves and smoothing replace the
  earlier approximations. Pad compression follows voice summing and pan.
- Pad volume follows the native 100 ms ramp and fixed 0.3 output trim while
  preserving stored/UI gain. The unsupported master hard clip is removed.
- Drive uses the native insert curve, 0–24 dB range and 100 ms post-gain ramp,
  independently of the parallel Aux3 send. Master Crush follows its native
  nonlinear rate/depth, raw hold and nearest-even quantization behavior.
- Turning a note off and back on remembers its velocity. Fresh notes use the
  current default, as does assigning a different pad.
  Saved states retain this distinction; older states keep their stored levels.
- Shift-click fills a partly empty beat or empties a full one in all four brush
  modes, without subdividing or randomizing it. The Fill / empty menu also
  changes the whole beat uniformly and preserves its pad assignments.
- Invalid bar-meter edits and native imports fail before changing the project
  or its undo history. Fractional, string, and overflowing inputs are rejected.
- The Trigger Chance label scales with the compact editor and stays readable.
- Standalone Play/Pause/Stop and tempo follow the app's own clock. The audio
  device's continuously advancing clock no longer overrides those controls.
- Standalone projects saved with multiple outputs play through the main stereo
  output. Restored and subsequently saved routing agrees with that behavior;
  the VST3 keeps multiple outputs and host timing.
- Corrected factory sample resource assignments and the chord-versus-sector
  drawing error. Batch 19 also corrects the tutorial-derived pie orientation.
- Fixed event loss in dense patterns and block-size-dependent playback.
- Repaired host-start and seek alignment, pattern triggers, and MIDI boundaries.
- Corrected several undo, automation, routing, sample-history, and state paths.
- Added host drum-note names and the documented dark and MIDI-display options.
- Subdivision and velocity drags publish each change to playback, with one Undo
  step per changed drag and none for an unchanged click.
- MIDI Note Off releases its matching held voice without cutting overlapping
  notes, sequenced hits, or pad previews. Stolen and finished voices keep their
  release pairing.
- Muted pads and pads excluded by Solo stop feeding the shared Room/Plate sends
  and do not light the pad meter. Existing shared ambience tails continue;
  sampler-disable and reset clear them.
- Sampler dividers follow the manual's control groups without crossing knobs.
- Keyboard rows now match the documented lower and upper pad rows.
- Arrangement items remain reachable by scrolling; hit targets and duplicate
  controls follow the visible items. Right-clicking a delete corner opens its
  menu without deleting an item.
- Zoom buttons and scroll rails control the visible beats and lanes. Scrolled
  lane controls stay within the pattern area, and small editor sizes retain the
  complete title and One Shot label.
- Invalid bar signatures and malformed randomizer indices are rejected before
  changing project state. Compressed state expansion and file reads are bounded.
- Undecodable embedded samples are rejected before replacing state or sound.
  Factory migration skips symbolic links and non-regular files.

Earlier core, state, and editor regressions reproduced the meter, velocity,
and fill/empty defects on the earlier source and passed after repair on Modal.
The separate VST3 host's synthetic state fixture was updated for the additive
velocity-memory field; its exact state-restore comparison still passes.

Earlier regressions covered arrangement and sampler behavior from manual pages
14 and 16, keyboard mapping from page 5, and scrolled lane-control clipping.

The standalone regressions also failed on the earlier processor and passed
with the fixes. They exercise a continuously advancing device clock, pause and
resume timing, Stop followed by Play, and main-output audio after restoring or
changing multi-output routing. A separate VST3 case retains multiple outputs.

Private sample verification established exact decoded PCM for the recovered
resources. That does not establish original preset or effect-output identity.
Effect controls, routing, and behavior have regression coverage, but the
proprietary wet signal path has not been matched exhaustively.

## Still open

Exhaustive wet-preset sound comparisons, native Drive knob angles, whole-interface
visual matching, exact MAP/Swing gesture calibration, controlled live-host use
and real audio/MIDI devices remain open. Multi-mode master compression, filter
and crush processing have since been repaired; the bounded Jungle comparisons
recorded below pass at 44.1/48/96 kHz. These fixture results do not establish
identity across every preset or effect combination. A same-source-PCM comparison is
needed to settle the earlier Pitch +2 replay discrepancy. The complete sound
and visual identity requested by the user has not been established.

The retained filter safety cap can differ from the uncapped original below
approximately 40.08 kHz; it is inactive at the tested 44.1/48/96 kHz rates.
Original-payload uploads for fresh reference renders were rejected by automatic
approval review and await explicit approval. The source-only reconstruction
upload subsequently received explicit approval and its Modal validation passed.
The final candidate is staged only; installed copies and the live REAPER session
are unchanged.

Licensing, trial, update, and deactivation services are outside this project's
implementation. Original commercial content and private validation captures
are not distributed here.


## Final checkpoint — 2026-11-03T11:36:55.771618+00:00

The bounded batch 50 private factory run reported 244 patterns from 129 presets, with 0 reported failures. It completed successfully. 12 patterns reached unity; the maximum absolute peak was 1.694586. This checks loading, finite output and expected notes, not original sound parity or absence of clipping.

Matching standalone and VST remain staged in batch-50; installed copies and live REAPER were untouched. Standalone initial and five-slice/flam screenshots were inspected without blank or overlapping controls; exact reference identity remains open. Heavy builds ran on Modal. The local sweep used CPUQuota=150% (1.5 cores of 16), MemoryHigh=1 GiB, MemoryMax=1.5 GiB, TasksMax=32, nice=15 and one numerical-library thread. The recorded scope had no OOM or memory-limit events and the workstation had over 16 GiB available with zero memory PSI. See system-headroom.json and factory-playback-summary.json in batch-50.

## Attack repair runtime confirmation — 211e3ea
GHA45282156585 passed all checks. Candidate download checksum verified and extracted to candidate-211e3ea. Isolated 48kHz/512-block/48000-frame CascaDoron captures both exit0. Dry/master-off 17 buses: zero above1e-6, worstpeak1.0203393685515039e-11. Full-mix audible master worstpeak1.4901161193847656e-08 (other16 buses silent), versus prior2.2619962692260742e-5. Reports candidate-211e3ea-cascadoron-buses-comparison.json and candidate-211e3ea-cascadoron-full-mix-comparison.json. Confirms this preset/capture attack residual repaired, not general full parity. Installed plugins and live REAPER unchanged.

### Paced original reference capture correction
The opt-in real-time-paced diagnostic host (2845a88, GHA45294700335 passed) resolves the previously variable Jungle snare and nonfinite triangle capture. Two isolated snare captures are finite, nonsilent and bit-identical to candidate211e3ea over48000frames/48kHz/block257. Full dry/master-off pad-bus comparison has7 audible buses, all below1e-6;6 bit-identical, remaining peak2.765e-10. This closes these specific capture discrepancies, not full preset/effects or visual parity. Effects-enabled comparison still requires matching routing. Evidence: recovery jungle-realtime-paced-comparison.json and jungle-full-paced-comparison.json.

### Multi-output shared ambience compression — bounded validation passed
Paced Jungle Polyrhythm captures isolate a shared-return gain discrepancy: individual instrument buses match within 1e-6, while the main effects return is approximately 21% low. Disabling original master compression is bit-identical to disabling its master module, and that bypassed original matches the candidate across all 17 buses within 9.314e-9. Room-send mapping and post-pad-volume routing were independently matched. Commit 1a8c3af applies master compression to the multi-output shared return while preserving individual pad buses. A regression checks return gain, stereo/multi agreement and pad-bus isolation. GHA 45300187376 compiled successfully and passed VST3 strictness-10 and standalone state recall, but its audio suite failed two assertions; no candidate was packaged. Repaired audio parity remains unverified. Evidence: jungle-wet-master-bypass.json, jungle-wet-no-comp.json and jungle-separated-ambience-paced-comparison.json in local recovery storage. No original content was uploaded.

### Multi-output master filter and crush
Additional paced original-plugin captures establish that the shared main effects return also receives master filtering and crush. Setting the master filter to 1 kHz changes the main return by a peak 0.0481511; setting crush to 0.65 changes it by 0.0475057. All 16 individual pad buses remain bit-identical in both cases. Commit 7ab7f2d shares the compressor, crusher and filter stages across stereo and Multi main returns, retaining the existing individual-bus bypass. Legacy distortion and delay remain stereo-only. The new regression exercises filter/crush processing and pad-output isolation. GHA 45301929576 is running remotely; candidate/reference audio equivalence is not established yet. Local evidence: jungle-multi-master-stages.json. The capture and comparison helpers now have explicit master-lowpass/master-crush variants and preserve separate reports.

### Jungle Polyrhythm stereo reference comparison
Candidate 211e3ea and the paced original plugin match over 48,000 frames at 48 kHz with 512-frame callbacks in stereo mode. Both isolated captures completed successfully; all captured samples are finite. Maximum absolute sample difference is 5.960464477539063e-8, with no alignment or normalization, and zero of 17 captured stereo buses exceed 1e-6. This covers this preset and configuration only. Report: candidate-211e3ea-jungle-full-mix-comparison-stereo.json in local recovery storage. The repaired Multi-output candidate still requires validation, including rerunning this stereo case as a regression check.

### Audio regression correction — 0e0c4ed
The isolated shared-return fixtures now disable pad filters and the length envelope and use the existing normalized-sampler input helper. The outdated assertion that Master leaves the entire Multi block unchanged was replaced with separate checks for a silent main bus when AUX is disabled and Master processing of the shared AUX return. Individual pad-bus equality checks remain. Source test commit 0e0c4ed passed GHA 45304125744 and the four local Jungle original-reference comparisons described above. Earlier run 45303973097 was cancelled because its default inputs omitted candidate artifacts. Broader presets, automation and original GUI parity remain open.

Jungle rate expansion (0e0c4ed, four seconds, block512, fixed alignment/no normalization): 96kHz17/17 buses pass1e-6, worst7.0781e-8. 44.1kHz16/17 pass; bus1 peak0.00011226534843444824 with17969 frames above1e-6. Other buses pass, master3.9116e-8. Evidence jungle-extended-0e0c4ed-{44100,96000}/comparison.json. Captures gzip-compressed and SHA-verified; receipt jungle-rate-capture-compression.json. Next establish 44.1kHz original repeatability and isolate bus1 before assigning DSP cause. GHA45308976133 remains pending validation of separate tail metadata repair.

### Length conversion and editor regression repair — staged a8748ed plus working changes
Matched four-second Jungle captures at 44.1/48/96 kHz now pass all 51 bus comparisons at a 1e-6 absolute threshold. All 16 instrument buses are bit-identical at each rate; main-return maximum differences are 3.912e-8, 3.725e-8 and 7.078e-8 respectively. This closes the recorded 44.1 kHz Length conversion discrepancy for this fixture, not general sound parity.

An installed multi-bar tutorial preset contaminated editor fixtures that assumed a default four-beat pattern. The editor test launcher now creates an isolated temporary XDG data directory; startup loading still has explicit synthetic fixtures for standalone and VST3 wrappers. Full editor CTest passed in 37.44 seconds. The shared editor also rejects missing beats and empty slices during hit testing, preventing an invalid Randomizer access in incomplete lane data. Both staged formats rebuilt successfully after the guard change. The added missing-beat and empty-slice regression passed within the full editor suite (37.75 seconds, zero failures). Installed copies and the user's REAPER session remain unchanged; broader reference parity remains open.

## Initial toolbar velocity rendering repaired
Parent the default velocity slider before configuring its style so JUCE caches the inherited custom numeric layout. New pixel regression initially failed, then passed after parenting fix: first-open image equals resize-round-trip image. Full editor CTest passed in37.69s (velocity-parent-test.log). Viewed regenerated midi-map-inline.png:102 digits visible. Both staged standalone/VST3 built successfully (velocity-parent-formats.log); no installation or live REAPER changes. Full reference parity remains incomplete.

## Swing first-open geometry variant repaired
Expanded initial-versus-resized pixel checks to velocity, Swing, BPM, master gain and pad Level. Before repair only Swing failed (initial-layout-test.log); after parenting Swing before style setup all passed in full editor CTest37.69s (swing-initial-test.log). Both staged formats rebuilt (swing-initial-build.log); regenerated screenshot inspected. No installed or live-host changes. ntfy delivered HTTP200. Host28GiB available; full parity remains incomplete, including exact original gesture calibration.

## Current staged VST3 host validation
Actual VST3 MIDI, panic and routing CTests passed (3/3, 40 seconds; current-host-checks.log). MIDI report: 102/102 cases pass. All 16 stereo instrument routes pass. Note-off, CC120 and CC123 each reduce the gated synthetic voice to exact zero after the event; the control remains audible. This verifies the staged plugin through the VST3 ABI, not original timbre, DAW GUI behavior or complete reference parity. Installed artifacts and live REAPER remain untouched.

## Visible-slider first-open sweep
Expanded the cached-layout regression from five named controls to every visible nonempty direct-child slider in the default editor. Serial editor build succeeded; full editor CTest passed (all-slider-test.log). No further first-open-versus-resize mismatch found on this screen. Hidden modules and exact original gesture calibration are not covered by this sweep. Exact MAP/Swing gestures remain unresolved: saved probes do not establish scaling and fresh VM access remains gated by the recorded credential approval rejection. No runtime mapping was guessed or changed.

## Duplicate action glyph corrected
Changed shared bar/card duplicate glyph to upright overlapping pages, filled foreground lower-left, using saved original Bars screenshot. Both staged formats and editor tests rebuilt successfully; inspected matched-duplicate-glyph/linux-tutorial-bars-stopped.png. Full editor CTest passed37.75s (duplicate-glyph-tests.log), git diff --check clean. Uncommitted changes retained; no installation/live REAPER changes. Remaining MAP glyph, sample panel geometry and broader parity still open.

## MAP glyph and reference-fixture correction
MAP keyboard glyph change built for both staged formats; full editor CTest passed37.65s (map-glyph-tests.log). Focus outline remains intentionally visible for keyboard accessibility. Direct original screenshot reinspection disproved prior audit claim of Room/Reverb0%: original displays41%/17%. Removed erroneous zero overrides from capture-client-matched.py and corrected VISUAL-AUDIT.md. Fresh isolated capture matched-map-corrected-state completed and inspected:41%/17% and velocity100 now aligned. Earlier override screenshots are not matching Room/Reverb evidence. Brush1vs2, MAP position, lower panel geometry/typography and broader parity remain unresolved. No installed artifacts or live REAPER changed; archive hidden during capture.

## Built-in sample selector placement
Moved built-in selectors from design x415/width118 to427/122 and sample glyph right/up to match saved original screenshot. Custom sample geometry preserved. Both staged formats rebuilt serially; editor CTest passed38.16s (sample-placement-tests.log). Inspected matched-sample-placement/linux-tutorial-bars-stopped.png: selector horizontal placement corresponds; lower panel boundaries, one-shot, fader positions, typography and exact icon shape still differ. Updated VISUAL-AUDIT.md. git diff --check passed. No installed/live REAPER changes; archive hidden during verification. Full parity remains open.

## One Shot visual correction
Removed ordinary button frame/background from sample.oneshot while preserving focus/hover feedback; enabled indicator is now a filled dot. Built-in position moved design x440 to452; custom sample position unchanged. Both staged formats rebuilt serially (oneshot-visual-build.log). Fresh matched-oneshot screenshot inspected against saved original: indicator/label placement corresponds. Full editor CTest passed (oneshot-visual-tests.log); git diff --check clean. No callback/audio change or installed/live REAPER change. ntfy delivered HTTP200. Remaining panel/fader geometry, typography and broader parity stay open.

## Factory sample panel placement corrected
Moved factory layer blend slider +12 design pixels horizontally and +17 vertically; folder control +12 horizontally. Custom sample placement and parameter mappings unchanged. Both staged standalone/VST3 built serially; full editor CTest passed. Inspected matched-layer-placement/linux-tutorial-bars-stopped.png against saved original: blend center and folder placement now correspond. Level/Master mapping and panel boundaries remain unresolved; full parity open. Installed artifacts and live REAPER untouched.

## Pad Level interaction verification
Strengthened editor regression: seed non-unity preset gain before editor creation; verify opening preserves it, captured native travel conversion, and +6 dB endpoint through the actual slider callback. Serial test build passed; full editor CTest passed37.65s (level-interaction-tests.log). Existing matched-level-taper screenshot reviewed with saved original; Master mapping and panel details remain open. No installed artifacts or live REAPER touched. Host28GiB available/no swap. Authorized ntfy attempt failed DNS resolution (curl28); not delivered. Full parity remains incomplete.

## Panel boundaries and playback lighting regression
Aligned kit bank and factory sample-panel boundaries with saved original capture; custom sample layout retained. Both staged standalone and VST3 built successfully. Matched screenshot: matched-panel-boundaries/linux-tutorial-bars-stopped.png. Initial editor run failed two single-pixel pad lighting probes: wider pads placed their pixel on the instrument glyph. Frame pixel comparison confirmed background flashes; moved those probes to clear interior background. Rebuilt editor tests; full editor CTest passed (pad-lighting-probe-tests.log). git diff --check passed. Source, installed plugins and live REAPER preserved; archive hidden during tests. Full parity remains incomplete: Master mapping, typography, exact icons and broader original behavior/audio coverage remain open.

## Bar delete glyph proportions
Saved original reference shows a compact square X inside the rectangular delete control; candidate stretched the X across its width. Updated shared drawActionGlyph to center a square cross using the smaller interior dimension. Header and sidebar controls share the correction; hit targets/actions unchanged. Both staged formats and editor test target built successfully (delete-glyph-build.log). Inspected fresh matched-delete-glyph/linux-tutorial-bars-stopped.png against original: cross proportions now correspond. Prior full editor regression passed; not rerun for this drawing-only change. Full parity remains incomplete.

## Non-power-of-two host buffer comparison
Current staged a8748ed VST3 compared to original Jungle Polyrhythm at48kHz,127-sample blocks,192000frames. All17stereo buses below1e-6;16instrument buses bit-identical,main peak error3.1665e-8. No shift or normalization. Evidence:jungle-extended-a8748ed-block127/comparison.json. WAVs gzip-compressed and SHA256 roundtrip verified; raw files removed. Original harness isolated with archive hidden; live REAPER and installed artifacts untouched. Full parity remains incomplete.

## Stop/tail parity at non-power-of-two buffer
Jungle Polyrhythm,48kHz,block127:4seconds playing plus4seconds host-stopped compared against original.17/17buses within1e-6;16instrument buses bit-identical;main peak error3.1665e-8. Evidence:jungle-extended-a8748ed-poststop-block127/comparison.json includes post-stop peaks. Raw WAVs compressed with SHA256 roundtrip verification and removed. This checks host-stop audio behavior, not editor transport gestures or full reference parity. No installed artifacts/live REAPER changes.

## Factory panel lighting coverage completed
Added the factory selected-sample panel to the existing host-driven lighting fixture, retaining custom-sample coverage. Serial rebuild and full editor CTest passed in38.00seconds; six factory snapshots written and playback-first inspected. Initial run had six screenshot-write failures from a stale build; forced source rebuild resolved them. Covers trigger, swing, stop, rest and reopen for both layouts; packaged live VST3 GUI timing and full reference parity remain unverified. No installed artifact or REAPER changes.

## Settings cog reference correction
Replaced oversized ring/dot settings icon with smaller filled eight-tooth cog and hollow center, matching the saved Windows reference form. Hit area and callback unchanged. Both staged formats built serially (settings-cog-build.log); git diff --check passed. Fresh isolated matched-settings-cog/linux-tutorial-bars-stopped.png inspected with original. Capture explicitly used --stop-x430; default387 hits Play on this toolbar and is unsuitable for stopped comparisons. Prior full editor regression passed; drawing-only change not rerun through suite. ntfy delivered HTTP200. Master fader mapping, typography and broader parity remain open; no installed artifacts/live REAPER changes.

## Gain fader reference shape corrected
Shared LookAndFeel now paints effect.9 with a thin rail and flat handle; proportional inset restores the longer small-fader travel observed in saved original 960px capture. Both staged formats built serially; matched-gain-shape/linux-tutorial-bars-stopped.png captured in archive-hidden isolated app and inspected. Full editor CTest passed38.15s (gain-shape-tests.log); git diff --check clean. No installed plugins or live REAPER changes. Exact Gain value mapping and Master mapping remain separate unresolved comparisons; full parity incomplete.

### Length host automation and active voices (2026-11-24)
Added all16 pad Length host parameters with state recall and editor gestures/history. Fixed Length automation changing the fade rate of an already-playing voice: the native plugin configures that envelope on note-on. Regression checks cover active and retriggered voices at44.1/48kHz. Staged standalone/VST3 builds and audio/editor/actualVST3 checks passed3/3. A four-second original/candidate capture with Length0→.85→0 matched all17stereo buses within1e-6 (peak3.73e-8), without alignment or normalization. Evidence: recovery jungle-extended-a8748ed-padlengthlatched/comparison.json. This closes the tested Length discrepancy, not full reference parity.

## Attack original isolation — 2026-11-24
Previous relay-only turn made no goal progress; resumed current checkpoint. Two serial archive-hidden 4s/48kHz/512 comparisons completed successfully. Original parameter22=.85 vs unchanged candidate changed kick (peak .3700225782), proving effective Attack control. Matching candidate attackSeconds/attackNormalized=.85 matches all17buses within1e-6; kick peak6.984919309616089e-10, master3.725290298461914e-8, zero badframes. Evidence jungle-extended-a8748ed-adsrattackisolation and adsrattackstatic085/comparison.json; fixture attack085-candidate.json. Existing static Attack DSP is correct at this tested value; missing host automation remains next repair (append indices332..347, preserve existing IDs, synchronize UI/history/state/audio, test dynamic native behavior). No production edits or installs this turn; both hosts ended; live REAPER untouched; available RAM28GiB,swap0. Full parity remains OPEN.


## Attack automation repair verified — 2026-11-24
Added pad1Attack..pad16Attack indices332..347 (continuous0..1), editor host gestures/undo, state and audio synchronization. Legacy attackSeconds0/attackNormalized fallback is preserved during unrelated edits. Editor timer now refreshes Attack and Damp from automation. Dynamic comparison exposed active-voice Attack rate changes; per-voice attackStep now remains fixed until retrigger, matching the original. Added44.1/48kHz active-voice/next-note regression.
Serial staged standalone/VST3/editor/audio build passed (attack-latch-build.log). Audio/editor/actualVST3 checks3/3 passed53.79s (attack-latch-tests.log). Original22/candidate332 automation0:0,48128:.85,144384:0: all17buses pass1e-6, peak3.725290298461914e-8, zero badframes (jungle-extended-a8748ed-padattacklatched/comparison.json). Candidate SHA e97d85de3613db90d13fd3924516610ab3fd90358018f2b4ebfad0b80bdfdd39. git diff --check passed. Owned builds/tests/comparisons all ended. No installed artifacts or live REAPER changes. Full parity OPEN; next remaining automation candidates original Decay23/Sustain24/Release25/Note14 require behavior isolation before adding bindings. No storage cleanup pending.


## Remaining ADSR isolation checkpoint — 2026-11-24
No production edits this step. Original Sustain24=.15 differs from candidate on kick in both One Shot and gated fixtures (peaks .0127631426/.0076692402). Decay23=.85 alone matched because default Sustain is full; do not interpret as inactive. With Sustain24=.15, changing Decay23 to .85 changes original kick by .0022776126861572266 peak,149715frames above1e-6 versus sustain-only capture. Combined original/candidate comparison kick peak .01504075527,156605badframes; other16buses pass. Evidence jungle-extended-a8748ed-adsrdecaysustain085/comparison.json. Model/engine lack Decay/Sustain and native StateCodec import drops both: confirmed missing behavior, next repair must preserve native stage timing and float arithmetic.
Existing Release DSP static .85 with OneShot off matches original15=0/25=.85:17/17buses within1e-6, kick bit-identical,masterpeak3.725290298461914e-8. Evidence jungle-extended-a8748ed-adsrreleasestatic085/comparison.json; candidate fixture adsr-release085-candidate.json. Release host binding remains absent; dynamic timing not yet tested. Candidate SHA e97d85de3613db90d13fd3924516610ab3fd90358018f2b4ebfad0b80bdfdd39 unchanged.
All owned comparison jobs ended0, serial/archive-hidden; installed/live REAPER untouched. MemAvailable about28.5GiB. No storage cleanup pending. Full parity remains OPEN. Next implement Decay/Sustain state+DSP after narrow native-stage inspection, and Release host binding with dynamic original comparison.


Decay/Sustain implementation in progress: native d06420 initializes float(1-Sustain)/(double(Decay)*sampleRate), d066fe subtracts float each frame and clamps to Sustain. Zero-Attack reference waveform starts decay on first frame. Added decaySeconds(default1)/sustainLevel(default1) to model/audio settings, validation, state serialization/native import; per-voice decay and sustain latches and attack-to-decay transition. Release starts from actual envelope level. Added testDecaySustainStages at44.1/48kHz. UNVERIFIED: serial standalone/VST3 build session21337 running, log decay-sustain-build.log; await same handle. Next build audio/editor/state tests, run regression gates and original comparison using decay-sustain085-candidate.json with original23=.85/24=.15. Host Decay/Sustain/Release automation still absent. Native release from lowered sustain and nonzero Attack transition require comparison. No installed/live REAPER changes.


## Decay/Sustain static repair verified — 2026-11-24
Standalone/VST3 build and serial audio/editor/state tests PASS (3/3,53.78s; decay-sustain-tests.log). State tests cover new native import and roundtrip fields. Initial reference run failed strict restore because older fixture omitted default decaySeconds/sustainLevel on pads1..15; decoded restored component confirmed those defaults were the only mismatch. Canonical input decay-sustain085-canonical.json supplies all fields; strict host check unchanged.
Original23=.85/24=.15 versus candidate decaySeconds=.85/sustainLevel=.15: 4sec48kHz/512,17/17 buses pass1e-6, zero badframes; maximum master error3.725290298461914e-8. Evidence jungle-extended-a8748ed-decaysustaincanonical085/comparison.json. Candidate SHA c3f11bab839b3de4bfcf209555c0227da97c311fa9c566041d1b37cb70d55d28. Handles70050,11466,16593 completed. No installed artifacts/live REAPER changes; Archive hidden; available RAM28GiB.
Full parity OPEN. Next: compare nonzero Attack transitions and gated Release from lowered Sustain; add missing Decay/Sustain/Release host bindings (nextfree348) with native dynamic comparisons. Do not infer those behaviors from this static case. Storage question withdrawn; no cleanup pending.


## Attack rounding repaired and verified — 2026-11-24
Attack host range and editor step now .01, matching original .024->.02 and .025->.03. Added all-pad host rounding assertions; numeric entry now expects .625->.63 and two-decimal initial text. Standalone/VST3 build passed; editor regression passed40.35s (attack-rounding-tests-final.log). Original22/candidate332 automation .025 at0,.024 at48128 with Decay.85/Sustain.15 matches all17 buses: peak3.725290298461914e-8,zero frames above1e-6. Evidence jungle-extended-a8748ed-attackroundingfixed/comparison.json; SHA69e9fdcc4e01e4bf627e2cbd94bf3f3c910dac8ec4a1bca645dc93595c3bfe12. No DSP changes in this step. All owned jobs terminal. Installed/live REAPER untouched; ntfy HTTP200. Full parity OPEN; next missing Decay/Sustain/Release host bindings, then dynamic native comparisons.


## Remaining envelope host bindings — 2026-11-24
Appended all-pad Decay348..363/Sustain364..379/Release380..395, control members16..18, state/history/audio synchronization; existing indices retained. Ranges continuous0..1 pending native fractional-rounding measurement; defaults1/1/.03. Added all48 parameter index/automation/state-recall regressions. Serial standalone/VST3/editor build PASS; editor tests PASS40.35s (remaining-envelope-tests.log).
Dynamic original23/24/25 vs candidate348/364/380 comparison jungle-extended-a8748ed-remainingenvelopedynamic/comparison.json: Decay.85->.25 at48128 and Sustain.15->.65 at96256 match; Release.85->.25 at144384 FAILS kickbus1 peak.0017251484096050262,4601badframes. Other16 buses pass1e-6. Kickleft mismatch starts exactly144384 and ends148991, next hit matches. CandidateSHAf31a0f4d53bc31d27d016ea8ffc6bee7ce8a7c5b8c0477c975ef0d2ff067f3d3. Likely active-release rate is recalculated from settings eachframe (AudioEngine.cpp896), whereas original latches it; must isolate before repair. Need determine note-on versus release-start latch. No Release DSP edit yet. All build/test/comparison jobs ended; installed/live REAPER untouched. Full parity OPEN.


## Release voice capture repair in progress — 2026-11-24
Original Release .85->.25 at1024 (note-on0/note-off5880) differs from current candidate, but its first voice is bit-identical to unchanged original .85 capture. Confirms note-on capture, not note-off capture. Evidence jungle-extended-a8748ed-releasebeforenoteoff/comparison.json (kick peak .002898082137107849); direct first30000 kickleft samples versus adsrgatedtransition03 original have zero difference. AudioEngine now captures releaseOutputStep alongside attackStep and keeps it for the voice. Added44.1/48kHz regressions for pre-note-off and mid-release changes, plus next-voice effect. Serial build15548 running, release-latch-build.log. Need finish build, audio/editor tests, and rerun dynamic and pre-note-off comparisons. ntfy HTTP200. No installs/live REAPER changes. Full parity OPEN.


## Release automation and arithmetic repaired — 2026-11-24
Original retains Release captured at voice start, including automation before note-off. Engine now latches releaseDurationFrames at first voice frame, initializes releaseOutputStep from actual envelope level at release start, and subtracts directly from that level. The initial normalized-ramp latch fixed the large automation defect but exposed a1.6838312149047852e-6 arithmetic mismatch; direct level subtraction resolves it. Added44.1/48kHz pre-note-off/mid-release automation tests and next-voice effect assertions.
Serial standalone/VST3/audio/editor build PASS (release-level-build.log). Audio/editor2/2 PASS (release-level-tests.log). Final original comparisons releaselevelbeforeoff and releaseleveldynamic both17/17 buses pass1e-6,zero badframes,max3.725290298461914e-8. CandidateSHA9d09b41a574d54d7dc6a913779df82e2df98105b231a00034f91c99ee122e635. git diff --check PASS. All owned jobs terminal; installed artifacts/live REAPER untouched. Archive hidden for runtime. Full parity OPEN. Next: determine native fractional snapping for Decay/Sustain/Release (currently continuous host ranges), remaining Note binding/native obsolete enable semantics, then remaining visual/functional checklist. Storage cleanup withdrawn; no cleanup pending.


## Decay/Sustain/Release rounding repaired — 2026-11-24
Six isolated original comparisons confirm each parameter rounds .024 to .02 and .025 to .03; all17 buses pass1e-6, maximum5.960464477539063e-8. Evidence jungle-extended-a8748ed-{decay,sustain,release}rounded{024,025}/comparison.json. Changed all48 host ranges to .01 intervals and added all-pad lower/midpoint regression assertions, preserving stable indices348..395 and state recall.
Serial standalone/VST3/editor build PASS (envelope-rounding-build.log). Audio/editor tests2/2 PASS53.41sec (envelope-rounding-tests.log). Rebuilt VST3 dynamic original comparison enveloperoundingfixed uses identical original23/24/25 and candidate348/364/380 normalized values .025@0,.024@48128,.855@96256;17/17 buses pass1e-6,zero badframes,max3.725290298461914e-8. CandidateSHA0ef6a44d5b2c28ca26e81886c915d172789437e8773f66996abcc3fa5b52c83e. git diff --check PASS. Handles24923/1037/93535/61932 terminal. No installation/live REAPER changes; Archive hidden during runtime. AvailableRAM28GiB,swap0. Full parity OPEN. Next: remaining Note host binding/native obsolete enable semantics and outstanding visual/functional reference checks. Storage cleanup withdrawn; no cleanup pending.


## Note automation probe checkpoint — 2026-11-24
Original Kick Note parameter14 accepted .25 at0, .5 at48128, and .75 at96256, but this sequencer fixture produced unchanged MIDI and audio versus the untouched candidate. Evidence jungle-extended-a8748ed-notemapautomationprobe:17/17 buses below1e-6,maximum3.725290298461914e-8;106 MIDI events in each capture. This does not establish general Note behavior or justify binding the legacy host parameter to editable output midiNote. No Note source change made. Next distinguish native host Note semantics from GUI output mapping before implementation. Previous envelope rounding repair remains verified; full parity OPEN. Claude relayed withdrawal of the70GiB cleanup concern and reported no defect in its bounded routing diff review; no deletion or storage question pending.


## Note MIDI-input isolation — 2026-11-24
Followup jungle-extended-a8748ed-44100-noteinputmapping injected kick MIDI with the sequencer disabled, while original Kick Note14 changed .25/.5/.75. Original readbacks accepted all three values. All17 audio buses bit-identical to unchanged candidate, zero badframes;20 output MIDI events each. Together with sequencer probe, this rules out an audible input-remap effect in these fixtures, but does not establish GUI-map or state-reload semantics. No speculative Note binding added. Handle62141 completed0. Next inspect native GUI mapping/state linkage or remaining reference checklist; full parity OPEN. MemAvailable28GiB. Live REAPER and installed plugins preserved.


## Output map distinguished from host Note — 2026-11-24
Inspected original saved state: Sampler.Pads.Pads[0].Params.Note=C2 is distinct from Sampler.Mapping.Mappings[36].number. Controlled outputmap60 fixture changes only mapping destination36->60 in original and candidate pad0.midiNote=60, with matched ADSR automation. Capture jungle-extended-a8748ed-outputmap60 completed0 (handle80204). Results recorded in comparison.json and original/candidate.log; next assess MIDI/audio result before classifying map parity. Source behavior unchanged; no installs.

Outputmap60 result: MIDI arrays identical including remapped note60, but audio FAILS (maximum0.31743091344833374;168638 summed bus-frames above1e-6). This is a concrete output-map/audio-routing discrepancy requiring isolation, not a passing map-parity check. Next compare kick-bus silence and native mapping effect on sampler triggering before repair. Full parity OPEN.


## Independent sampler route repair — 2026-11-24
Added PadState.samplerNote, encoded/validated independently of midiNote. Native import derives sampler routing from configured note through fixed default map before applying editable output mapping. Old candidate states lacking samplerNote retain prior midiNote routing. Defaults initialize both. Sequencer uses samplerNote; MIDI still uses editable midiNote. Corrected misleading mapped-sampler regression to require source kick audio for output notes38/60; added native import identity assertion. Serial standalone/VST3/editor/state/audio build PASS (output-map-routing-final-build.log). Tests handle15658 running; audio passed, editor pending. Original comparison still pending, do not claim repaired parity yet. New canonical fixture note-outputmap60-separated-candidate.json includes all samplerNote fields. No installed/live REAPER changes. Archive hidden in runtime. Full parity OPEN.


## Output-map audio repair verified — 2026-11-24
Original/candidate outputmap60fixed comparison passes17/17 buses at1e-6, zero badframes, kick bit-identical, maximum master error3.725290298461914e-8. All106 output MIDI events identical. CandidateSHA05aaf9c4df4dc5189fd68e5002757c7a57f83489c2ccad5dda8c7e7f269b7264. Evidence jungle-extended-a8748ed-outputmap60fixed/comparison.json and host logs. Audio/state tests passed; initial editor suite exposed dense replay fixture setting only midiNote under its old conflated routing assumption. Fixture now explicitly sets equal samplerNote/midiNote for its unremapped replay contract. Rebuilt editor suite PASS (output-map-routing-editor-final.log). All build/test/capture handles terminal (38504,47942,15658,51828,60136,64750). git diff --check PASS. No installs, live REAPER untouched.
Next: rerun native CascaDoron import and controlled capture to ensure configured-note/default-map sampler identity preserves earlier unusual preset routing. Legacy candidate states without samplerNote deliberately fall back to midiNote; previously custom-remapped old states cannot recover lost original routing from output note alone and need native reimport. Keep this compatibility limitation explicit. Full reference parity remains OPEN.


## Fresh CascaDoron routing verification — 2026-11-24
Rebuilt factory exporter serially (sampler-route-exporter-build.log); freshly imported native CascaDoron to factory-cascadoron-a8748ed. Output notes and new samplerNote fields both equal expected [36,37,38,39,49,43,40,56,57,55,51,46,54,42,53,56]. Isolated one-second48kHz512-block candidate capture succeeded. Historical reference WAV missing; regenerated original with same preset/dry/master-off settings using current host, capture exit0. Report candidate-a8748ed-cascadoron-buses-comparison.json:15/17 buses pass1e-6, bus8 peak error0.00028872303664684296 (526frames,12288..12818), bus15 peak7.171183824539185e-05 (12989frames,24112..40096). This is unresolved PCM mismatch; previous candidate211e3ea historical checkpoint had17/17 passing, so investigate envelope/retrigger changes and fresh-reference equivalence before further production edits. No production change this continuation. Capture helper now accepts explicit --state, comparator explicit --reference. Initial capture pathname quoting failed before host startup; empty attempt directory removed, retry passed. Build90239 and original capture2657 terminal; no active task jobs. RAM available28GiB,swap0. Archive hidden for all runtime captures; installed artifacts/live REAPER untouched. Storage cleanup withdrawn. Full parity OPEN.


## CascaDoron residual isolated to reference variability — 2026-11-24
Repeated original capture with identical preset/dry/master-off settings (--repeat), handle87087 exit0. Current candidate versus repeat original: all17/17 buses bit-identical, peak0 and zero badframes; report candidate-a8748ed-cascadoron-buses-comparison.json now points to repeat reference. Original first WAV SHA42728bb3603d2b0cee3f2974e049ca099f354f109485cd03b958061c8c4dad7b differs from repeated SHA6207a1ad6c02caf98638844a1c40cf6163dac2173bf19d692dc15d0c3ff0bd1d. Thus prior bus8/15 residual is not a reproduced candidate regression: unchanged candidate matches repeated original exactly. Both original recordings retained; reference startup/voice-state variability mechanism remains unresolved. No production/DSP changes justified by this discrepancy. Fresh native sampler-route regression check passes on reproducible matching capture; full parity remains OPEN. Next continue outstanding feature checks, controlling for original run variability when small audio residuals recur. Live REAPER and installed artifacts preserved; no active capture job.


## Bars selector label sizing — 2026-11-24
Compared saved Windows reference-bars-mode.png with velocity-label-final candidate: Bars selector text was visibly undersized. Its dedicated drawing branch used height*.42 with minimum8, independent of generic button font. Added arrangement.toggle font at height*.82 and made dedicated draw branch use it. Serial staged Standalone/VST3 builds PASS (bars-label-build.log); fresh Archive-hidden Xvfb capture bars-label-capture/linux-tutorial-bars-stopped.png inspected. Label size improved toward original; exact typography/antialiasing remains unverified and is not claimed pixel-identical. This is drawing-only; hit area/callback unchanged, no broad test rerun required. Build11276/capture71712 terminal0; git diff --check PASS. No installed artifact/live REAPER changes. Memory available28GiB before build. Full parity remains OPEN. Next: continue remaining reference UI differences and original interaction gaps; original sample navigation remains blocked by black Wine editor/VM credential issue documented earlier. Storage cleanup withdrawn.


## Bars card meter typography — 2026-11-24
Replaced combined meter text with embedded Montserrat numerals, explicit spacing and a subdued drawn separator. Staged Standalone/VST3 build passed (bars-meter-build.log); Archive-hidden capture bars-meter-final inspected against saved Windows reference-bars-mode.png. Denominator horizontal bright bounds now match1080..1087; numerator remains1px left and bright top row1px lower than reference, so pixel identity is not claimed. git diff --check passed. No installed artifacts/live REAPER changes.
Followup screenshot revealed Bars pattern-name label6px too high: design y88 corrected to96, matching neighboring Patterns quantization row. Serial build96495 in progress, bars-name-build.log; capture still needed. Full parity OPEN. Storage cleanup withdrawn.


## Bars pattern-name placement verified — 2026-11-24
Moved Bars sidebar pattern name down8 design units (6 client pixels), from y88 to96. Staged Standalone/VST3 builds passed (bars-name-build.log, handle96495 exit0); isolated Archive-hidden capture58177 exit0. Inspected bars-name-final/linux-tutorial-bars-stopped.png against saved Windows reference: label now occupies the reference row beneath Bars. Original pointer overlaps this region, so no pixel-exact typography claim. git diff --check PASS. Changes are drawing-only; callbacks/hit regions unchanged. All owned jobs ended; no install/live REAPER changes. Full parity OPEN. Next distinct visual differences visible in matched screenshots: row separators accumulate roughly2px vertical drift by bottom lane; toolbar active/disabled glyph styling still differs. Measure before changes. Original sample navigation/menu behavior remains unverified due documented GUI access obstacle.


## Internal lane separators aligned — 2026-11-24
Measured screenshot scanlines showed original internal bands y262..265,333..336,404..407 at960px client width. Candidate used three-pixel bands with fractional positions; changed shared paintPattern to rounded16/3 design-pixel height and floor-aligned row center. No beat/layout/hit geometry changed. Serial Standalone/VST3 build89818 passed (lane-bands-build.log). Archive-hidden capture63604 passed; lane-bands-final/grid-crop.png inspected. band-comparison.json confirms all3/3 internal band bounds match original exactly at this captured size. Color differs (candidate39,39,97 versus original varying purple); outer top/bottom boundaries and roughly1px circle bounds remain separate open visual differences. Do not claim full grid parity or infer all sizes. git diff --check passed before final capture. Available RAM27GiB,swap0. All owned jobs ended; installed artifacts/live REAPER preserved. Full parity OPEN. Next: original separator color variation/outer borders or toolbar glyph styling; avoid shifting all beats from a single image without stronger geometry evidence.


## Undo/redo glyph direction corrected — 2026-11-24
Saved Windows reference shows filled bent undo/redo arrows. Candidate drawUndo used thin arcs with incorrectly oriented arrowheads. Replaced shared drawing with a filled left-pointing undo path mirrored for redo; enabled/disabled tint and callbacks unchanged. Serial staged Standalone/VST3 build passed (undo-glyph-build.log,33315 exit0). Archive-hidden capture83898 passed; undo-glyph-final/linux-tutorial-bars-stopped.png visually inspected confirms correct direction and filled silhouettes. Exact outline/color equality is not claimed. git diff --check passed. No installed artifacts/live REAPER changes;28GiB available RAM before build. All owned jobs completed. Full parity remains OPEN. Next: grid outer boundary/color and remaining toolbar differences; original sample navigation remains unverified under documented original GUI limitation. Storage cleanup concern withdrawn via Claude relay; no cleanup action pending.


## Grid outer bands restored — 2026-11-24
Added solid top/bottom grid bands outside paintPattern clip after bar-header rendering. No beat geometry, hit targets or scrolling state changed. Staged Standalone/VST3 serial build78796 passed (grid-outer-bands-build.log). Archive-hidden capture93857 passed; grid-outer-bands-final/grid-crop.png inspected. Scanline comparison atx300 confirms5/5 horizontal band bounds match saved original at960px client width:191..194,262..265,333..336,404..407,475..479. Evidence band-comparison.json. Color still differs; this is bounds parity at the captured size only, not all-size/rendering parity. git diff --check passed. All owned jobs terminal; no installed artifact/live REAPER changes. Full parity OPEN. Next: investigate reference band color variation with a second saved original capture before modeling it; remaining circle offset and toolbar/interaction/audio-reference gaps remain separate.

## Grid band gradient verification — 2026-11-24
- Replaced flat default grid-band shading with the measured elliptical radial gradient in shared editor rendering; dark mode retains its flat band color.
- Staged Standalone and VST3 targets built successfully with -j1 (grid-band-gradient-build.log). Isolated capture succeeded and grid-crop.png was visually inspected.
- At 45 fixed reference band positions, mean absolute RGB channel error fell from 6.444 to 0.370; maximum error fell from 23 to 1. Evidence: grid-band-gradient-final/band-colour-comparison.json. This verifies sampled shading, not full visual parity.
- Live REAPER and installed plugins untouched. Full reference parity remains open, including broader original interactions, playback flash timing, circle geometry and DSP coverage.

## Whole-pixel lane spacing — 2026-11-24
- Four saved original captures agree on populated circle bounds. Candidate fractional row heights accumulated about one pixel of downward drift across four lanes at 960x540.
- Shared gridLaneHeight rounds base row height down before zoom; painting, lane controls, pie/lane hit testing and grid dragging use the same geometry. Staged Standalone and VST3 rebuilt successfully, installed artifacts untouched.
- Fresh isolated screenshot visually inspected. All 4 sampled circle outline bounding boxes now match the original exactly; evidence lane-rounding-final/circle-outline-comparison.json. The 45 grid-band samples retain mean channel error 0.370 and max 1. This does not establish other lane counts/sizes against original.
- Initial editor regression run exposed nine obsolete flat-band color assertions from the earlier gradient change. Replaced those with reference-sampled colors and corrected the measured row-pitch comment. Final complete editor regression executable passed in 40.35 seconds (lane-rounding-editor-tests-final.log); git diff --check passed.
- Full parity remains open: original sample navigation order/wrap/menu behavior, precise flash timing, broader original interaction/audio and other geometry coverage remain. Archive volume never traversed; runtime checks masked it.

## Factory sample arrow audit — 2026-11-24
- Static trace confirms sample.layer paints left/right arrow gutters, but layerOne_ and layerTwo_ are ordinary TextButtons whose sole callbacks open showFactorySampleMenu. No layer mouse listener or positional arrow handler exists. Clicking either arrow therefore invokes the same menu callback as the label; separate previous/next selection is not implemented.
- This establishes candidate behavior only. Original stepping order, boundary wrap and None behavior still require reference observation; do not invent these semantics or claim parity. No production edits or rebuild performed in this pass.
- Existing uncommitted work preserved; git diff --check passes. No runtime jobs launched, no Archive traversal, no installed artifacts or live REAPER changes. Storage question closed following relayed user instruction.

## Native VST3 legacy-state checker repair — 2026-11-24
- Four actual VST3 host checks initially failed strict state comparison before playback. Diagnostic restored-state comparison found only migration defaults: 48 pad fields (samplerNote, decaySeconds, sustainLevel) plus the three defaults already normalized by the checker. No supplied values differed.
- Updated tools/actual_vst3_host.cpp to expect absent samplerNote from midiNote and absent decay/sustain as 1.0, matching StateCodec and Model. Explicit values still undergo strict comparison. Production plugin code unchanged in this repair.
- Rebuilt actual_vst3_host serially. Smoke, MIDI, panic and routing checks all passed (4/4, 38.82 seconds); host-schema-defaults-tests.log records results. git diff --check passed.
- Tests ran with Archive masked, without live REAPER or installed plugin changes. Full parity remains open, including original sample navigation, precise playback flash timing and broader original interaction/audio comparisons.

## Manual page 18 master-effects bypass — 2026-11-24
- Expanded actual VST3 routing regression beyond master-gain bypass: independently engage master filter/resonance, crush and compression in Stereo and Multi. Each Stereo positive control must change audio by >1e-4, preventing an inactive-effect false pass.
- For all 16 pads, Multi outputs must match the dry Stereo reference within 2 PCM24 units, preserve L/R and remain silent on every other bus. Existing gain-zero bypass also retained.
- Passed staged VST3 routing test in 4.66 seconds (master-effects-routing-tests.log), 80 pad/bypass comparisons. Python syntax and git diff --check passed. No production repair needed for this manual requirement. Original effect-transfer-function parity remains a separate open requirement. Runtime was isolated with Archive masked; live REAPER and installed artifacts untouched.

## Audition speaker arc orientation — 2026-11-24
- Compared current candidate with saved Windows reference-bars-mode.png. Candidate audition wave used angles around twelve o'clock, placing it above the speaker instead of to its right. Corrected shared LookAndFeel arc to center on halfPi.
- Serial staged Standalone/VST3 build passed (audition-wave-build.log). Fresh isolated screenshot audition-wave-final/linux-tutorial-bars-stopped.png inspected: wave now faces right. Exact icon silhouette/size parity remains unproven. Drawing-only change; audition callback unchanged. git diff --check passed.
- Runtime capture used Archive masking/private Xvfb. No installed artifacts or live REAPER changes. Available RAM28GiB, swap0. All owned jobs terminal. Full parity remains open.

## Saved original playback lighting review — 2026-11-24
- Visually inspected original playback-0.png and playback-1.png in the known VM reference directory. Both show populated sectors near-white, silent sectors gray, and instrument pads colored on triggers. Existing REFERENCE-LEDGER.md already records measured D0D0D5 / 727282 targets.
- Shared PluginEditor rendering implements those neutral targets and per-pad playbackActivity; existing editor regression covers corresponding hits, rests, stop/restart and stale events. No new discrepancy established by these stills, so no production change or redundant test run.
- Exact original flash envelope remains unverified: sparse stills without synchronized event timestamps cannot establish fade duration. Candidate currently combines event-age attenuation over 150ms with per-timer 0.60 decay. Do not call this timing parity. A synchronized original screen/event capture is needed before changing the envelope.
- No runtime jobs, Archive access, installed artifact changes or live REAPER interaction. Full parity remains open; next actionable reference gap is factory sample arrow navigation semantics or synchronized original playback capture when original GUI access is restored.

## Factory sample None repair — 2026-11-24

Added the original screenshot's None sample selection to the factory menu and empty-layer labels. Processor accepts an empty factory ID while retaining nonempty filename validation. Clearing one layer preserves the other; clearing both produces silence in the editor regression. Staged standalone and VST3 built serially successfully (sample-none-build.log); editor regression passed in 40.55 seconds (sample-none-tests.log). Headless screenshots sample-none-final/factory-none-menu.png and factory-none-selected.png visually confirm the option and selected label. git diff --check passed. Installed artifacts and live REAPER unchanged. Separate previous/next sample arrow behavior remains unresolved; full reference parity remains incomplete.

## None sample host-state regression — 2026-11-24

Extended testFactorySamplesRespectXdgDataHome through getStateInformation, fresh processor creation, setStateInformation and prepareToPlay. Both empty factory IDs survive, rendering stays silent, and loading both factory layers restores nonzero output. The positive control initially populated only layer one, which the default layer-mix endpoint mutes; populating both isolates loading from mix position. Final editor regression passed in 40.20s (sample-none-reopen-tests.log); test target rebuild passed (sample-none-reopen-build.log). No additional production changes. Sample arrow ordering/wrap still lacks usable original GUI evidence: saved sample-nav-builtin4.png is black, not a reference for interaction. Full parity remains open.

## Sample arrow interaction repair — 2026-11-24

The shared editor now gives factory-sample arrow gutters distinct previous/next actions, using the same filtered and sorted list as the center menu. Custom sample labels retain menu behavior. Mouse-click regression confirms forward/back selection and preservation of the other layer. Staged standalone, VST3 and editor test targets built serially (sample-arrows-build-final.log); isolated editor regression passed in 40.45s (sample-arrows-tests.log). No installed artifacts changed. End-of-list behavior currently clamps; original ordering and wrap behavior still need direct reference verification, so this is not a claim of exact interaction parity. Full goal remains open.

## Sample arrow real-input capture — 2026-11-24

Ran staged standalone under archive-masked private Xvfb with real xdotool input. Inspected sample-arrows-visual/sample-next.png and sample-previous.png: upper layer advances to GRB Body and returns to DW Sub; lower SvN Kick remains unchanged; no popup opens. This closes candidate GUI input verification for the arrow repair, not original ordering/wrap parity. Capture script: capture-sample-arrows.py. No production changes this pass. Reviewed manual Sounds pages 16–17 against loadCustomSample/clearCustomSample: preserved factory IDs provide the return path after removing custom audio; no newly confirmed defect from that source check. Full original processing and interaction comparison remains incomplete.

## Stereo master compressor reference check — 2026-11-24
Earlier mastercompautomation/mastercrushautomation/mastercutoffautomation captures used multiOutput=true and never switched routing. They establish bypass agreement, not active master-effect transfer parity. Fresh mastercompstereo explicitly sets original MultiOut8=0 and candidate Routing2=0, then compressor0 -> .85 at48128 ->0 at144384. All event names/readbacks verified. Four seconds48kHz/512 frames:17/17 buses within1e-6, master peak5.960464477539063e-08, pad buses silent. Independent masterstereodrycontrol original capture has identical pre-event PCM and peak0.28539255261421204 difference after automation, confirming an active effect. Evidence jungle-extended-a8748ed-mastercompstereo/{comparison,positive-control}.json. No product defect reproduced; no code/build/install needed. Both capture handles completed; live REAPER untouched and Archive masked. Full parity OPEN. Next compare master Crush/Cutoff/Resonance in explicit Stereo with audible positive controls; do not count earlier Multi-mode comparisons as active-effect coverage.

## Active Stereo master Crush/filter comparisons — 2026-11-24
Explicit Stereo original/candidate comparisons passed for Crush, Cutoff and Resonance:4sec48kHz/512,192000frames, zero frames above1e-6 across17buses; peaks5.960464477539063e-08,1.043081283569336e-07,3.5762786865234375e-07 respectively. Crush0 ->.85 ->0; Cutoff1 ->.25 ->1; Resonance0 ->.85 ->0 at fixed cutoff.5. Events at48128/144384, original MultiOut8=0/candidate Routing2=0; compressor disabled in both. Names/readbacks retained and checked. Controls have exactly matching pre-event audio and nonzero post-event effects (Crush.547029, Cutoff.452533, resonance 0.7389501333236694). Evidence jungle-extended-a8748ed-{mastercrushstereo,mastercutoffstereo,masterresostereo}/{comparison,positive-control}.json. Prior Multi-mode evidence is bypass-only; these new cases exercise processing. No production defect found, no install or live REAPER changes. All four isolated captures exited0; Archive masked. Full parity OPEN; master Filter Enable in Stereo and broader original GUI/gesture coverage remain.

## Stereo master filter enable reference check — 2026-11-24
Explicit Stereo with fixed cutoff.5/resonance0 and compressor0: original Filter Enable12 and candidate299 transition1 ->0 at48128 ->1 at144384. Four seconds48kHz/512,17/17buses within1e-6, peak7.450580596923828e-08 and zero badframes. Compared original against retained always-enabled masterresostereocontrol: pre-event PCM exact, effect peak difference.3673587143421173. All requested parameter names/readbacks match. Evidence jungle-extended-a8748ed-masterenablestereo/{comparison,positive-control}.json. No product defect reproduced; no code changes or rebuild. Capture38475 exited0, Archive masked, installed artifacts/live REAPER untouched. RAM28GiB available, swap0. Combined with previous Stereo checks, active Master Comp/Crush/Cutoff/Reso/FilterEnable each now has a bounded original-reference automation case with a positive control. This does not establish all sample rates, sample-offset automation, GUI/gesture or full reference parity. Full goal remains OPEN.

## Stereo master gain reference check — 2026-11-24
Explicit Stereo with compressor disabled: original Master5/candidate MasterGain3 transition normalized .75 -> .25 at48128 ->1 at144384 (including +6 dB endpoint). Four seconds48kHz/512,17/17 buses within1e-6, peak1.1920928955078125e-07, zero bad frames. Fixed .75 original control has identical pre-event PCM and effect peak .5982751250267029. Requested names/readbacks verified. Evidence jungle-extended-a8748ed-mastergainstereo/{comparison,positive-control}.json and mastergainstereocontrol capture. No production defect reproduced, no code/build/install changes. Both isolated captures exited0; Archive masked, live REAPER preserved. Six targeted active Stereo master controls now have bounded reference automation checks. Harness currently explicitly restricts parameter events to block boundaries (actual_vst3_host.cpp:206); these results do not prove intra-block automation. Full parity remains OPEN, including original sample navigation ordering/wrap, precise flash timing and broader GUI interactions.

## Manual Sounds / Randomizer audit — 2026-11-24
Read local manual pages16-17 and19-22 against current shared editor/core and existing regressions. Sample drop routes implement pad and Sounds-area import; editor testSampleDropTargets exercises both, mixed file selection, hidden-panel rejection. Randomizer source implements empty drum-selection Kick fallback, Fill Empty preserving active slices, disabled velocity module using brush default, low-spacing phrase continuation and 100-percent trigger override. Existing core tests exercise these behaviors and cross-beat spacing. Rebuilt missing beat_scholar_core_tests serially and ran it: PASS0.63s, logs manual-randomizer-core-{build,tests}.log. Editor page-All test selects only the current nine-value page and retains prior selections; no editor rerun needed for unchanged code already covered in prior pass. Randomizer preset menu enumerates factory/user directories afresh, offers save and Show Preset Dir. This is source/test coverage of documented behavior, not original stochastic-distribution or GUI parity. Manual does not specify sample-arrow ordering/wrap; original observation remains required. No new product defect established, no production edits or installs. Full parity OPEN.


### 44.1 kHz active Stereo master resonance comparison
- Original and candidate captured 176400 frames at 44100 Hz, 512-frame blocks, explicit Stereo routing and compressor off; cutoff 0.5, resonance 0 -> 0.85 at sample 44032 -> 0 at 132096.
- All 17 buses passed 1e-6 tolerance; maximum error 2.384185791015625e-7, zero frames above tolerance. Fixed-resonance control also passed all 17 buses (maximum 1.1920928955078125e-7).
- Original active/control PCM identical before automation, then peak difference 0.6494804620742798, confirming an active effect. Parameter names and readbacks retained in jungle-extended-a8748ed-44100-masterresostereo/positive-control.json.
- No product changes or installation. Both isolated capture processes completed. This checks block-aligned automation, not arbitrary sample-offset automation.
- Tutorial qOY2oMsUEMs retained sparse stills depict iPad; no retained continuous video found in bounded project roots. They do not establish desktop flash duration. Exact playback-light timing and other outstanding original-GUI gaps remain open; full parity is not complete.


## Pattern MIDI drag integration audit
Manual key commands page5 explicitly require Shift + Drag Pattern Pad to export a MIDI file to the DAW; page14 abbreviates this as dragging. PluginEditor.cpp pattern mouseDown arms midiDragPending_ on Shift, mouseDrag writes the MIDI and invokes performExternalDragDropOfFiles after 5px. Existing editor tests verify host/standalone export tempo and pattern meter/name/gates; core tests verify swing/playback timing. These do not prove desktop external drag delivery or DAW import. No dedicated external-drag integration check found in current tools/editor tests. Next concrete integration check: isolated REAPER with the staged VST3, Shift-drag a known pattern to its timeline and inspect imported item note count/timing and screenshot. Preserve live REAPER and mask Archive. Do not replace Shift behavior based on page14 shorthand. No product defect confirmed or source edits made in this pass. RAM28GiB available, swap0.


## Current staged VST3 external MIDI drag PASS
Correction to the preceding audit: an existing integration harness and earlier passing captures were found under reaper-playback-probe, outside worktree/tools. Reused capture.py --midi-drop against current candidate-a8748ed, on private 1920x1080 Xvfb with disposable REAPER -newinst/-cfgfile and Archive masked. Completed exit0; owned REAPER cleaned up. Evidence reaper-playback-probe/midi-fidelity-a8748ed: midi-drop.json records 1 item/31 notes; midi-import-comparison.json confirms all channels/pitches/velocities and relative start/end times, maximum error 3.3333469318108655e-10 quarter notes at960PPQ. Absolute drop offset is excluded. Visually inspected094-done.png: populated Beat MIDI item visible in REAPER timeline beside candidate Patterns panel. This closes the current VST3 external Shift-drag integration check for this fixture; it does not establish original MIDI-export byte identity or standalone external drag. No product repair/install or live REAPER changes. Full parity remains open.


## Standalone external MIDI drag PASS — 2026-11-24
Extended existing reaper-playback-probe/capture.py and midi-drop.lua with optional standalone source using disposable settings and separate process group. Isolated Xvfb/private REAPER run exited0; Archive masked, live REAPER and installed artifacts preserved. Evidence reaper-playback-probe/midi-fidelity-a8748ed-standalone: midi-drop.json records1 item/31 notes; midi-import-comparison.json verifies channels/pitches/velocities and relative note timing, max error3.3333469318108655e-10 quarter notes at960PPQ. Visually inspected080-done.png showing standalone beside populated Beat MIDI item. Both current candidate formats pass this fixture. No production defect or repair needed. Harness owns and cleans up both processes. RAM28GiB available, swap0. Original export identity, precise playback flash timing, original sample navigation ordering/wrap and broader original GUI parity remain open.


## Swing toolbar label alignment — 2026-11-24
Saved Windows reference bright Swing label bounds512,123..534,129 versus candidate516,118..531,123 established undersized/high text. Changed only Swing font10->14 design pixels and label y12->18, resetting font before subdivision value. Serial staged Standalone/VST3 build82403 exited0; isolated Archive-hidden matched capture82978 exited0 and screenshot visually inspected. New bounds513,123..534,130; size/position improved but antialiasing and one-pixel bounds differences remain, not pixel identity. Evidence swing-label-final/{linux-tutorial-bars-stopped.png,label-bounds.json}, swing-label-build.log. git diff --check passed. No slider geometry, callbacks or audio changes; no installed artifact/live REAPER changes. Full parity remains open.

## 2026-11-24: Swing transition discrepancy narrowed to variable reference timing
Repeated exact extreme Swing automation comparison in jungle-extended-a8748ed-swingextremeautomationrepeat (host completed exit 0; raw parity still fails). Candidate full 112-event MIDI sequence identical between runs. Original note 41/40 onset after first automation changes from sample 49152 to 49664 between identical runs, one 512-frame block later; original is not repeat-identical. All 102 events outside two 50 ms windows following automation at samples 48128/144384 match candidate exactly in both runs. Evidence: repeat/transition-diagnosis.json plus both original/candidate logs. This demonstrates reference-path timing variability, not a proven candidate scheduling defect. No fixed scheduling delay introduced. Raw audio parity remains unproven during live extreme Swing transitions; do not relabel these comparisons passing. Next useful probe: determine reference parameter delivery/application mechanism or test extreme values applied before playback, rather than fitting candidate timing to one variable capture.

## 2026-11-24: Preloaded extreme Swing parity verified
Two four-second isolated comparisons at 48 kHz / 512 frames, Jungle Polyrhythm matched Multi state: Swing -200% and +200% restored before playback. Corrected captures jungle-extended-a8748ed-swingstaticminus200corrected and swingstaticplus200corrected each pass all 17 buses at 1e-6 and exactly match all 106 MIDI events. Maximum audio errors 3.166496753692627e-08 and 3.91155481338501e-08 respectively. Summary swing-extreme-static-fixtures/results.json. Original component stores Swing both in Params/Swing and Seq/Seq/Transport/Swing/Value; both must be updated. Initial uncorrected captures modified only Params and left original sequencer at 0%; those captures are invalid comparisons, not product defects. Corrected input fixtures retained. No product scheduling changes warranted by these results. Live automation-transition parity remains open due to variable original-path delivery timing. No installed artifacts or live REAPER modified.

## 2026-11-24: Eighth-reference extreme Swing comparison
Original/candidate preloaded +200% Swing with SwingRef=1/8 (candidate enum 0), four seconds at48kHz/512. Evidence jungle-extended-a8748ed-swingstaticplus200eighth/comparison.json and midi-comparison.json. Result: {"buses_pass": 17, "peak": 3.3527612686157227e-08, "midi_equal": true, "event_counts": [106, 106]}. No production changes. Manual contains no sample-start offset control; earlier sample-offset gap refers to intra-block automation, not sampler start. F2 runtime coverage already recorded; not rerun. Full parity remains open.

## 2026-11-24: Half-reference Swing verification and consolidated results
Preloaded +200% SwingRef1/2: all17 buses within1e-6, maximum3.6197889130562544e-08, all106 MIDI events exact. Evidence jungle-extended-a8748ed-swingstaticplus200half. Consolidated four settings and live-transition limitation in swing-reference-results-20261124.md. No source changes, installation, or live REAPER changes. Owned capture68668 exited0. Full parity remains open.

## 2026-11-24: Runtime Swing host thread diagnostic
Added injected_on_message_thread to actual_vst3_host parameter-event reports, rebuilt only host serially, and ran isolated swingthreadtrace comparison. All three injections for BOTH original and candidate report true; requested/readback values match. Combined with JUCE dispatcher source, this rules out the Linux JUCE 60 Hz controller queue for these injections. Audio still passes only13/17 buses; both emit112 MIDI events, so transition parity remains unresolved downstream (bridge/original application), not repaired or declared passing. Evidence jungle-extended-a8748ed-swingthreadtrace/thread-diagnosis.json, logs and comparison.json. Build and git diff --check passed. Capture31239 exited0 and private hosts cleaned up. No product binary or installed/live REAPER changes. Next: bridge/original-side application timing, or independent native reference timing evidence; do not add candidate delay based on current variable reference captures.

## 2026-11-24: SwingRef host automation and scoped undo verified
Added discrete SwingRef host parameter at the end of the parameter layout (existing indices preserved), connected per-block transport override, project synchronization, saved-state restoration, and menu host gestures. Original FormatParamValueNormalized probe captured 0..0.25 as 1/8, >0.25..<0.75 as 1/4, >=0.75 as 1/2; custom parameter mapping preserves those boundaries. Evidence: corrected-wet-reference-projects/original-swingref-threshold-run/control-mapping.tsv. This is original formatting evidence, not live application timing evidence.
Scoped SwingRef undo with HistoryParameters::swingReference; unrelated project undo preserves host automation. Added focused --swing-reference-automation test and default-suite invocation: discrete choices, appended ordering, boundary text, save/restore, project synchronization, undo/redo and unrelated undo. Serial editor target rebuild succeeded; isolated archive-masked Xvfb focused run exited 0 with Swing reference automation failures: 0. Earlier full editor run also passed but used a stale binary without the focused flag, so is not evidence for new SwingRef coverage. git diff --check passed. No installed artifacts or live REAPER changed. Staged standalone/VST3 wrappers still need rebuilding and runtime verification. Live extreme Swing transition discrepancy remains open (13/17 buses); full parity not complete. ntfy sent HTTP 200. Last resource check: 28 GiB available RAM, no swap.

## 2026-11-24: SwingRef staged formats runtime verification
Rebuilt staged VST3 and standalone serially successfully (swingref-formats-build.log). Actual VST3 host parameter index396 SwingRef applied at startup to normalized1 (half) and0 (eighth): both comparisons pass17/17 audio buses and106/106 exact MIDI events. Evidence jungle-extended-a8748ed-swingrefhosthalf and jungle-extended-a8748ed-swingrefhosteighth; maximum audio errors3.6197889130562544e-08 and3.3527612686157227e-08 respectively. These verify startup parameter application, not mid-playback transitions.
Rebuilt standalone rendered under private Xvfb and exported a31-note MIDI item into isolated REAPER; evidence reaper-playback-probe/swingref-standalone-runtime-xvfb/midi-drop.json and080-done.png. Initial run without DISPLAY failed at harness startup; rerun with Xvfb succeeded. This smoke check verifies rendering/export presence, not complete visual parity or exported timing fidelity. git diff --check passed. No installed artifacts or live REAPER changed. Owned jobs finished. Full parity remains open; live Swing transition discrepancy and original visual timing remain unresolved. Storage cleanup concern withdrawn via Claude relay; no cleanup performed.

## 2026-11-24: Live SwingRef transitions match original
Staged VST3 SwingRef changed eighth at0, half at48128, quarter at144384 samples with Swing+200%,4seconds48kHz512blocks. Original parameter1 and candidate396 report exact readbacks on message thread. All17 audio buses pass1e-6; maximum error3.725290298461914e-08; all106 MIDI events exactly match. Evidence jungle-extended-a8748ed-swingrefdynamic/comparison.json and midi-comparison.json. Positive control against fixed-eighth original has65 differing MIDI event positions, establishing this fixture exercises reference changes. This closes this live SwingRef transition fixture, not the separate variable original Swing amount transition discrepancy. No production edits needed. git diff --check passed, runtime69467 terminal0. AvailableRAM28GiB; no installed artifact/live REAPER changes; Archive masked. Full parity remains open for Swing amount transitions, original sample navigation semantics and outstanding visual timing/geometry comparisons.


## 2026-11-24: Confirmed missing One Shot host automation
Manual Sounds page16 specifies One Shot switches between beat-length gating and full sample playback. Retained original parameters.tsv exposes 16 OneShot parameters at indices15+26*pad (15..405), with per-pad stored values. Current candidate makeParameterLayout ends with SwingRef and contains no OneShot binding; editor callback at PluginEditor.cpp:693 changes only project pad.oneShot. StateCodec preserves/imports the flag, and AudioEngine consumes it, so this is missing DAW automation, not absence of the underlying sample mode. Source inspection of syncParametersFromProject/syncProjectFromParameters and padEnableMembers confirms no indirect OneShot binding.
Next repair: append 16 boolean parameters after current index396 without changing existing IDs/indices; cache raw values, apply in audio settings, synchronize project save/restore, wire editor host gesture and scoped undo. Add focused all-pad defaults/save/restore/undo and audio gating regressions, then compare an active OneShot transition against original with a positive control. Do not expand padEnableMembers in-place: its earlier nested parameter loop would renumber existing parameters. No production edits or runtime jobs this turn. 28GiB RAM available, swap0; live REAPER/installed artifacts unchanged. Full parity remains open.


## 2026-11-25: Original factory sample menus and navigation repaired
Original VM captures established wrap-through-None navigation and six hidden factory resources across five menus. Shared editor picker/arrow enumeration now excludes those resources without changing persisted sample loading or unknown custom entries. Factory-only menu styling restores23px rows, reference columns, white text and no selection ticks; a Clap minimum width compensates a one-pixel platform rounding difference.

Isolated editor regressions passed120.83s. Final editor/standalone/VST3 build completed successfully. Headless capture of all18 full-library menus measured18/18 exact outer widths and heights at960px client size; final Clap screenshot inspected. Evidence: recovery/sample-menu-geometry-tests.log, sample-menu-final-build.log, full-factory-menu-final-a8748ed/geometry-comparison.json and PNGs. This is menu-dimension/reference-behavior evidence, not complete raster or whole-UI parity. Intermittent Slicer click-test failure did not recur; added delivery diagnostics remain, root cause unproven. Installed artifacts and live REAPER untouched. Full parity remains open for playback flash timing, live Swing amount transitions and the broader reference checklist.


### 2026-11-26 — Native saved Note control separated from import routing

Original 1.6.2 fresh-load C2/B6 controls retained the saved nested Note but produced identical audio and MIDI in both external-input and sequencer modes. The candidate previously changed sequencer routing on the same value change. Import now keeps nested Params.Note separate from fixed sampler/pattern identity; top-level Note compatibility and explicit MIDI output Mapping remain intact.

New preset/kit regressions fail before and pass after the fix. Both uninstalled formats built; full serial CTest passed 13/13 (137.63 s), including actual VST3 host checks. Four actual VST3 after-renders produce identical C2/B6 pairs. CascaDoron fresh import remains bit-identical on all 17 dry output buses to the recorded original repeat and earlier candidate. Private source captures remain in local recovery storage; no original binaries or factory content added to the repository. One focused review completed.

Full reference parity remains open; this result closes the native Note reload discrepancy. No installed artifacts or live REAPER session changed.


### 2026-11-26 — Measured zoom and pie-row geometry corrected

Original 1.6.2 captures establish horizontal zoom 1–10 in 0.45 button steps and vertical zoom 1–7 in 0.30 steps. The shared editor now preserves fractional row pitch, scales gutters with row height, and uses the measured pie centres and radii. Paint and hit testing share the corrected geometry. Previously measured subdivision drag and three/four-lane control targets are retained.

Both uninstalled formats built. Focused editor checks passed across 11 groups, including both wrapper modes for measured geometry. Full serial CTest passed 13/13 in 138.50 s, including actual VST3 host checks. Original and candidate zoom screenshots were inspected; one source review found no actionable defect.

Full reference parity remains open. Compact dense-lane controls are a separate confirmed mismatch; normalized partial-scroll preservation is inferred, and the exact zoom-rail drag law remains unmeasured. No installed artifacts or live REAPER session changed.


### 2026-11-26 — Compact lane controls corrected

Original 1.6.2 captures and bounded layout inspection establish four desktop lane-control modes. The shared editor now changes from the tall S/M/−/+ stack to separated or tight M S / − + rows, then M/S only for dense lanes. Paint and hit testing use the same rectangles; hidden controls are inert. Compact glyphs and lane gutters were corrected after screenshot comparison.

Both uninstalled formats built. New reference-coordinate fixtures fail against the old implementation and pass for both wrappers at three sizes. All 11 focused groups and the full 13-group CTest suite passed; the latter includes actual VST3 host checks and took 140.57 seconds. Final screenshots were inspected and one source review found no actionable defect.

This closes the dense-control mismatch recorded above. Exact nested padding/rounding and font raster identity are not established; full reference parity remains open. The installed artifacts and live REAPER session are unchanged.


## 2026-11-26: Zoom rail gesture and visual correction verified

Original1.6.2 captures in zoom-rail-original-20261126 confirm horizontal relative drag in physical pixels (+25px -> +0.9x at both960/1280), inert track clicks and off-thumb relative dragging. Vertical stays absolute. Shared editor now uses that law, corrected thumb travel, rail/thumb colours, four-pixel vertical rail, matching gutter and +/- brightness. Dark palette retained. Viewport state/Undo unchanged.

New regression failed26 assertions on old code. Both uninstalled formats built; 12focused groups passed0failures. Full serial CTest63955 exited0:13/13 in141.04s, including actualVST3 smoke/MIDI/panic/routing. Final screenshot-driven paint refinements rebuilt both formats in25945; focused12groups again0failures, completeeditor+actualVST3smoke2/2 in61.28s. Service exit0, peak1,145,978,880bytes (~1.07GiB), swap0. No audio/routing changes after fullsuite. Inspected final comparison PNGs and wrapper fixtures. One focused source review found no actionable defect; no repeat review. gitdiffcheck clean.

RESULT.md, OBSERVATIONS.md, original/final captures, logs, zoom-only.diff, wholetrackedrepair.patch and copied FactorySampleOrder.h saved. All owned helper jobs finished; existing offlineVM inactive/MainPID0/Resultsuccess and own input marker removed. Existing5min ntfy timeractive. No liveREAPER/installedartifact/governor/Archive changes; no commit/push. FullgoalOPEN. Rail edge antialiasing/subpixelcentroids and separate roughly2px lowerpanelboundary remain approximate/open; broader manual/visual/audio coverage remains. Do not repeat closed rail law, compactcontrols, glow, arrows, Noteimport or menu checks without new evidence.


## 2026-11-26: Lower-panel frame/header repair verified

Original captures reused from zoom-rail-original-20261126/18 and /23; no VM needed. lower-panel-original-20261126/OBSERVATIONS.md records headers y525..554 and kit7..401, sound410..1056, master1064..1272 at1280, plus960 edges. Shared editor frames/header colours/gaps/gutter corrected; M/S moved into sound header. Body controls and audio unchanged; randomizer sound geometry preserved.

Both uninstalled formats built. Focused3groups passed0failures, both wrappers960/1280. Completeeditor+actualVST3smoke2/2 in61.74s. Bounded service88566 exit0/runtime1m48s, peak~1.59GiB, swap0. Final screenshots inspected; gitdiffcheck clean. One focused review complete: its predicted custom-pixel assertion failure was refuted by passing test and subsequent samplePanel paint. Customx402 layout predates this change, retained as a separate original-comparison question. No repeat review needed.

RESULT.md, OBSERVATIONS.md, logs, fourwrapper screenshots, before/after comparisons, lower-panel-only.diff, wholetrackedrepair.patch and FactorySampleOrder.h saved. Original body shading, separator positions, icon proportions and typography visibly still differ; fullgoalOPEN. No installed/liveREAPER/governor/Archive changes; no commit/push. VM stays stopped. Next use saved lower-panel evidence to address the remaining body geometry/shading; do not repeat closed frame or zoom checks without new evidence.


## 2026-11-26T20:42:10.138966+00:00: Lower-panel body shading and internal frames verified

Reused original1.6.2 saved captures; no VM input. Three measured elliptical body gradients now cover kit/sound/Master, including factory sample body. Paired module frames follow original edges. Exact gradient mechanism, alpha and antialiasing remain approximate; no pixel-perfect claim. Dark palette and custom/randomizer layouts preserved.

Both uninstalled formats built. Focused lower-panel checks pass both wrappers960/1280, zero failures. Complete editor+actualVST3smoke2/2 in62.43s. Final service65927 exit0/runtime1m17.952s, peak969MiB/swap0. Initial test exposed factory frame crossing custom sample tile; first frame skipped for custom layout, unchanged assertion now passes. No test weakened. One source inspection complete; final screenshots inspected; gitdiffcheck clean.

lower-body-original-20261126 contains RESULT.md, OBSERVATIONS.md, fit measurements, initial/final logs, final-comparison.png, body-only.diff, wholetrackedrepair.patch and copieduntrackedFactorySampleOrder.h. All owned heavy jobs ended, VM remains stopped. No liveREAPER/install/commit/push/governor/Archive changes. FullgoalOPEN. Next remaining lower-panel differences are pad-button body shading, icon proportions, typography and control geometry; use existing captures before starting another VM. Do not repeat closed body/frame checks without new evidence.


## 2026-11-26T20:53:43.462028+00:00: Lower-control alignment verified

Original 1.6.2 saved captures18/23 reused. Dist moved down5px; Level/effect labels aligned; Master rails now6px, rounded and RGB40/40/86 with measured thumb geometry. Removed surplus Level-side meter. No audio or parameter-law change. Both uninstalled formats built. Initial complete editor+actual VST3 smoke2/2 in62.91s; final colour-only rebuild passed focused lower-panel checks (both wrappers960/1280, custom sample and live meters) and actualVST3smoke1/1 in0.50s. Final screenshots and source diff inspected; gitdiffcheck clean. Final bounded service inactive/MainPID0/Resultsuccess. Initial peak about1GiB, swap0.

lower-controls-original-20261126 contains RESULT.md, OBSERVATIONS.md, logs, final screenshots/comparison, controls-only.diff, wholetrackedrepair.patch and copied FactorySampleOrder.h. No install/liveREAPER/VM/governor/Archive changes; no commit/push. FullgoalOPEN. Next measured mismatch: neutral kit pad bodies are too dark and flat; saved original shows repeating per-pad shading. Do not repeat closed controls, glow, nativeNote, zoom, arrows or menus absent new evidence.


## 2026-11-26T21:02:42.417028+00:00: Kit pad body and geometry verified

Kit pad body and geometry repair verified. Reused original 1.6.2 captures18/23; no VM input. Pad frames now touch, selection changes the white frame without an idle colour overlay, and the default neutral background uses a measured radial fit. At960, per-row design coordinates fix integer-truncation drift. The audio-derived playback overlay, strip/MAP logic, callbacks and sample-panel glyphs are unchanged.

Both uninstalled formats built. Focused lower-panel checks pass for standalone/VST3 wrappers at960/1280. Full editor plus actualVST3smoke passed2/2 in63.23s. First focused run exposed960 row-rounding and a test sample overlapping the Kick glyph; both resolved and original logs retained. Final960/1280 crops inspected; one batch source review and gitdiffcheck complete. Service bounded to3GiB/twoCPUs; observed peak1,155,117,056bytes (~1.08GiB), swap0, systemPSIzero. LiveREAPER/installedartifacts/governor/Archive untouched; no commit/push.

Saved OBSERVATIONS, gradient fit data/script, before/final images, logs, kit-pad-only.diff, whole tracked repair.patch, FactorySampleOrder.h. Shading is a close empirical fit, not exact source reconstruction; glyph shapes/proportions, typography and broader reference coverage remain open. Next measured fix: Snare Hit pad icon body should occupy clientx66..93/y678..687 at1280; current glyph is oversized. Full parity goal OPEN.

## 2026-11-26T21:13:22.017637+00:00: Snare kit icon geometry and tint verified

Two snare kit glyphs resized/aligned to saved original1.6.2 captures18/23. Default kit tint corrected to d7dbf6; screenshot review removed surplus inner shell lines. Sample-panel glyphs/audio/routing unchanged. Both uninstalled formats built; focused reference checks passed both wrappers960/1280; full editor+actualVST3smoke2/2 in63.04s. Final comparison inspected, focused source review and gitdiffcheck complete. Final bounded service exited0/MainPID0; observed peak432,500,736bytes, swap0, host available28GiB/PSI0.

snare-kit-icon-original-20261126 contains RESULT.md, OBSERVATIONS.md, first/second/geometry/final logs and renders, final-snare-comparison.png, snare-only.diff, whole tracked repair.patch and FactorySampleOrder.h. Upper marks, lug proportions and fine raster remain approximate; fullgoalOPEN. Read-only ledger check found no new confirmed functional gap; envelope bindings and variable original Swing timing are already closed/documented, do not reopen without new evidence. All owned heavy jobs finished; VM untouched/stopped. Existing5min ntfy active. No install/liveREAPER/commit/push/governor/Archive changes.


## 2026-11-26T21:25:36.851116+00:00: Remaining kit glyph frames verified

Individual kit glyph proportions/placement corrected from saved original1.6.2 captures18/23. Top-row cymbal tilt reversed to match original, lower-stand brightness fitted per cymbal, seven measured silhouettes checked at960/1280. Sample-panel drawings/audio/routing unchanged. Both uninstalled candidates built; four wrapper/size combinations passed0failures; full editor+actualVST3smoke2/2 in62.98s. Final comparison inspected; one source inspection and gitdiffcheck complete. Final3GiB/twoCPU/zeroswap service inactive/MainPID0/Resultsuccess. HostPSI0/about28GiB available.

kit-glyph-frames-original-20261126 contains RESULT.md, OBSERVATIONS.md, first/second/final logs and screenshots, final-comparison.png, kit-glyph-only.diff, wholetrackedrepair.patch and FactorySampleOrder.h. FullgoalOPEN: tom shell/filled-head shapes, snare details, cymbal-disc paths, FX rings, sparkle topology and Master header typography visibly differ. Next use saved images for targeted corrections; no VM needed. All owned heavy helpers ended, installed artifacts/liveREAPER/governor/Archive untouched; no commit/push.


## 2026-11-26T21:30:59.335453+00:00: GitHub source checkpoint

User requested a GitHub update before continued repairs. Checkpoint includes the accumulated sampler mapping, audio/meters, slice-duration lighting, audio-derived pad glow, state/envelope handling, sample menus/arrows, and reference-driven editor corrections. Previously recorded full serial CTest passed 13/13. Latest shared-editor changes built both uninstalled formats; lower-panel reference checks passed all four wrapper/size combinations, and complete editor plus actual VST3 smoke passed 2/2 in 62.13 s. No installed artifact or live REAPER change.

Master header typography now matches measured original bright-pixel bounds at 960 and 1280. Kit tom head proportions improved, but curved drumhead separators and other fine glyph contours remain visibly different. Full reference parity remains open; this is a development checkpoint, not a parity-complete release. Current comparison: kit-shapes-master-original-20261126/first-comparison.png.
