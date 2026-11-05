# Mechanics and evidence

## 0.1.2 editor redesign — 2026-11-05

The VST3 editor was rebuilt visually while keeping the same controls and
parameter IDs:

- layered gradient cards, a branded top bar, and a stronger visual hierarchy
- painted tick-ring knobs with value + unit readouts
- styled combo boxes, pills, toggles and linear sliders
- a custom piano: neutral white/black keys, scale dots, cyan input outline,
  violet target fill and white selected-decision outline
- improved key/scale/shift/range and secondary-control labels
- an opaque history deck matching the new surface system

The 860×680 size, all control IDs, the history behavior, Undo/Redo and A/B are
unchanged. Native, Rosetta x86_64, AddressSanitizer and host checks pass; the
idle, active and history screenshots were regenerated.

## 0.1.1 behavior and range pass — 2026-11-05

- Negative Speed now selects an immediate ratio update instead of the zero-speed
  smoothing path, giving the harder correction described by the original UI
  messages. Exact original negative-speed DSP remains unknown.
- Shift now transposes even when Amount is zero; Amount only sets the correction
  strength. Tests cover 440 Hz in → 493.88 Hz out at Amount 0, Shift +2.
- The delay ring is 131072 samples, so 120 ms Signalsmith bypass remains aligned
  through 768 kHz. A 768 kHz impulse regression checks the 92160-sample onset.
- A 4000-block engine/parameter churn test covers bypass, PSOLA, Signalsmith,
  low-latency and doubler transitions.

Native, Rosetta x86_64 and AddressSanitizer/UndefinedBehaviorSanitizer runs all
pass. Pluginval 1.0.4 at strictness 8 passes with its two discrete-parameter
state-restoration tests disabled: JUCE's host proxy retains the last randomly
assigned normalised value for a discrete parameter until an asynchronous host
refresh, even though the plugin restored the correct step. A new in-process test
round-trips every parameter through `getStateInformation`/`setStateInformation`
and verifies all values. DSP estimates and parameter IDs otherwise remain
unchanged.

## 0.1.0 stability pass — 2026-11-05

Additional host-facing fixes, each covered by a regression:

- Signalsmith now honours Global Bypass and applies Doubler/Stereo Width after
  the engine, matching the normal path.
- Host/global bypass while Signalsmith is selected now uses the Signalsmith
  latency for its dry delay, so the reported and measured onset stay aligned.
  The delay ring was enlarged to cover 120 ms through 384 kHz.
- Non-finite input is sanitised before both engine paths, and Signalsmith output
  is sanitised before it reaches the host or the doubler tail.
- `reset()` now flushes the delay lines, PSOLA grains and Signalsmith state
  without changing the prepared configuration.
- Processing is clamped to two channels defensively; mono and extra-channel
  buffers are safe.
- The test suite skips the optional real-vocal fixture instead of dereferencing
  a missing reader. The full suite now passes natively and under Rosetta x86_64.
- A 3 sample-rate × 4 block-size × 4 engine matrix checks finite output.

Signalsmith was also verified through the packaged VST3 at 96 kHz: declared and
measured latency both report 11,520 samples (120 ms), and the same suite passes
under AddressSanitizer/UndefinedBehaviorSanitizer.

## RC2 stability fixes — 2026-11-05

Four host-facing defects found in review are fixed with regressions:

- Signalsmith staging buffers now grow to the prepared block size, with an
  additional audio-thread guard for hosts that exceed it. A 16,384-sample block
  regression checks capacity, finite output and energy.
- Signalsmith reconfigures when the host changes sample rate. The regression
  moves 48 → 96 kHz and requires the declared latency to scale and output to stay
  finite.
- YIN keeps reusable scratch storage. `prepareToPlay` reserves it, so a
  steady-state analysis window makes no heap allocation; a global allocation
  counter verifies zero allocations inside `processBlock`.
- The history reason now compares the target with the previous target. A fresh
  target change is “nearest enabled note”; only a retained target is “previous
  target held”.

The same tests pass under AddressSanitizer and UndefinedBehaviorSanitizer with
leak detection disabled. `build.sh` now builds universal arm64 + x86_64 by
default. DSP behavior and parameter IDs are unchanged.

## Editor repair — 2026-11-05

The native editor now has an opaque, keyboard-focusable history panel in the main
control area. It replaces the covered knobs, hides them from hit-testing and
focus, keeps the monitor, key/scale, shift/range and piano visible, and closes
without changing values. History rows are clickable; the selected target gets a
white piano outline. Enable/Exclude edits the selected note through the existing
Undo path, and a decision with no target disables both actions.

The cents readout and marker now use input against the selected target, so 432 Hz
against 440 Hz reads about -31.8 cents and draws left of zero. Negative, zero and
positive mapping is tested. Shift, Range, Doubler, Stereo Width and Pitch
Reference labels are readable again. The Experimental badge is now a child shown
only while TD-PSOLA or Signalsmith is selected. Keys use neutral fills, scale dots
come from the effective processor mask, live input draws an outline and the
current target draws violet.

DSP, parameter IDs, presets and A/B/Undo/Redo behavior are unchanged. Verification:
the CTest suite passes, the packaged VST3 is re-signed and passes strict signature
verification, an independent VST3 host loads it and passes audio/parameter/latency
probes, and RenderCheck measures 2048/128/5760-sample declared latency for
quality/low-latency/Signalsmith. Idle, active and selected-history screenshots are
in `docs/screenshots/`.

## Fresh binary reads and build — current pass

Opened workspace original main program `original/MetaTune.vst3/Contents/MacOS/MetaTune` with core_target_open; SHA-256 `7453f5ec9f816261f935f3d19d371ee4a65f741e2ffa8394e2bc19f5567f2010`, Deep mode. Name search `ev_5bec3ca89950d7dc7ca166b09a0e7cbaddac0f78fed3702e1a4ccfc1ccb425b9` covers all 17 matches. Settings/messages search `ev_447ef97cfb53f0d1a01dd09cf18bb7f04d48cdadaeeacc36a0680d17b24b4ac5` covers 45/125 matches. Explicit Speed string xrefs `ev_a62ed7a5606c357eaf1af98feb18934ecd943e558189579bb6dd7f9782beb3d8` lead to registration 0xfa1e98. Read its broad fallback function 0xead874 (`ev_b9fa3b395d53df3602ca272b2e9ee9cc02a3587cbd05afcc2427114b6350ad81`) and followed calls into the curve constructors. Awaited each pseudocode result sequentially:

| Function | Evidence |
| --- | --- |
| `0xf9a798` | `ev_20786eb4b88d410621d7bbe1c59924f3e0349bf68e1ff5392d4b977cfc0f83d8` |
| `0x13f3588` | `ev_3f10344fd9c877b71760c1a06d74cf840a0a63b348650259ff6a55e6bfded51e` |
| `0x13f3658` | `ev_0cbf31ea56c3654dc0404047442fd0b575a73ae43312a36de00dad1f8a1c4ce2` |
| `0xf9a8c8` | `ev_468ecb2f44f922c0749b5bb6ec359ffbfd6b8f81bf00f2782a04d24822616131` |
| `0x13f34b8` | `ev_952f5f01ec0d1a2d2b6abb63d3029cdf3c581390e385ff1a6e50a52ed09b17eb` |
| `0x13f3808` | `ev_bd86a384339ea1ede9c5286f557d6134823f3e8e7f63f100446ceaf5f765fbc4` |
| `0xf9a84c` | `ev_e22fb525fbf96ccbd0e85b4d247c5ad724c9cdc76dc6ef56e1eea37cde851d05` |
| `0xf18b00` | `ev_a279eb338dc0100303b372fd76452bae79520553d88302da1732be670f09b572` |

Recovered rules retained in the build (n clamped to [0,1]): Speed = (30625*49^(-n/0.784)-625)/150 below 0.784, otherwise -3*(n-0.784)/0.21599999999999997; default25 ms. Amount =112.5*(1-9^(-n)), default100%. Sustain =200*n-100, default0. Doubler and Width =100*n, default0%. Registration blocks are 0xfa1d24, 0xfa1758, 0xfa1940, 0xfa2228, 0xfa2408 respectively. Tutorial 0xf18b00 connects 0–20ms to robotic retuning, negative speed to a harder effect, Amount0/100 to no/full correction, and Sustain to held-note retuning. Previous instruction corroboration remains the basis for arithmetic lost in fallback FP registers.

Change: standalone Sustain now calls named recovered forward/inverse mappings and the recovered default, replacing inline UI arithmetic. Both directions enforce recovered range boundaries. Existing Speed/Amount mappings and plugin linear ranges already agree; no invented replacement DSP was introduced. Regression first failed for missing mapping functions, then passed after implementation.

Limitations: every fresh pseudocode request reported Ghidra unavailable and used register-level fallback. This is not successful Ghidra decompilation. Exact pitch detector, sustain timing, stabilizer and doubler DSP remain unresolved; existing DSP estimates remain estimates.



Validation for the fresh binary reads above: all 17 Rust tests pass; offline standalone build succeeds. VST3 rebuild succeeds, MegaTuneStreaming CTest passes, and strict bundle signature verification succeeds. Coverage and original audio equivalence were not measured.

## Requested fresh function reads — 2026-11-04, previous pass

Opened the original main executable with `core_target_open` in Deep mode, SHA-256 `7453f5ec9f816261f935f3d19d371ee4a65f741e2ffa8394e2bc19f5567f2010`. Exact-name search `ev_df8d7cbc3b1617183faac98c470f8342c424ae06ad6097c147a52ec68ce821b2` completed 22 matches. Settings/messages search `ev_6493688157c1826893f701e6069ec19de92a54843146943c24615032d41241a1` read 40 of 114 matches. Followed the returned string references to registration `0xfa1e98` and tutorial `0xf18b00`, then registration calls. Awaited every pseudocode result before requesting the next: `0xfa1e98`, `0xf9a798`, `0x13f3588`, `0x13f3658`, `0xf9a8c8`, `0x13f34b8`, `0x13f3808`, `0xf9a84c`, `0xf18b00`. Their evidence IDs match the earlier function table; inverse-speed evidence is `ev_e22fb525fbf96ccbd0e85b4d247c5ad724c9cdc76dc6ef56e1eea37cde851d05`.

Confirmed recovered control rules (n clamped to [0,1]):

| Control | Physical value | Default | Original ARM64 addresses |
| --- | --- | --- | --- |
| Speed | `(30625*49^(-n/0.784)-625)/150` below 0.784; otherwise `-3*(n-0.784)/0.21599999999999997` | 25 ms | registration `0xfa1d24`, constructor `0xf9a798`, curve `0x13f3588`/`0x13f3658`, split `0xf9a84c` |
| Amount | `112.5*(1-9^(-n))` | 100% | `0xfa1758`, `0xf9a8c8`, `0x13f34b8` |
| Sustain | `200*n-100` | 0 | `0xfa1940`, `0x13f3808` |
| Doubler / Width | `100*n` | 0% / 0% | `0xfa2228` / `0xfa2408`, `0x13f3808` |

Tutorial `0xf18b00` wires the messages for robotic 0–20 ms speed, harder negative speed, full/no retuning at Amount 100/0, and dynamic held-note Sustain. Exact timing magnitudes and DSP algorithms remain unresolved. Current builds already implement the recovered mappings/defaults and sustain direction. This pass changes both conversion implementations to named split weights and uses the exact recovered double `0.21599999999999997` instead of rounded `0.216`; no perceptible sound change is expected.

Ghidra failed for these reads; all returned pseudocode uses the built-in register-level fallback. Floating-point register losses prevent treating its zero assignments as arithmetic. The formulas retain their prior ARM64 instruction corroboration documented below. This pass does not establish original detector, retuning, stabilizer or doubler DSP equivalence.


Validation of the current pass: `cargo test --offline` passed 14 tests; `cargo build --offline` succeeded; `bash app/vst3/build.sh` rebuilt and packaged the plug-in, passed streaming CTest and strict signature verification. Coverage and original-sound equivalence were not measured.

## Current requested trace — 2026-11-04

Reopened `/Users/ade/Downloads/MetaTune.vst3/Contents/MacOS/MetaTune` using `core_target_open` in Deep mode. Exact names search `ev_79016e2661c934dd7e08bdd9d4efb5f4f733df997def9bc6c2665fb0fcb44d37` completed 17 matches; settings/messages search `ev_ea63729b041367a211236c97bfc5c3f293eeb41172bafb1021686cea8d247e32` returned 60 of 126 matches. Followed string references to registration `0xfa1e98` and tutorial `0xf18b00`, then followed registration calls. Each pseudocode request completed before the next request. Read `0xfa1e98`, `0xf9a798`, `0x13f3588`, `0x13f3658`, `0xf9a8c8`, `0x13f34b8`, `0x13f3808`, `0xf18b00`, and `0xf9a84c`. The first eight reproduce evidence IDs in the table below.

Inverse Speed assembly at `0xf9a84c`, evidence `ev_02bfa7a0e8f8cdafd713c022308124b2325411961d3048bcaf26f812fc268094`, confirms split constants as IEEE-754 doubles `0x3fe916872b020c4a` = 0.784 and `0x3fcba5e353f7ced8` = 0.21599999999999997. Pseudocode evidence is `ev_e22fb525fbf96ccbd0e85b4d247c5ad724c9cdc76dc6ef56e1eea37cde851d05`. Requests for `0xf9a7f8` and inverse generic curve `0x13f36f8` returned function-not-found; no formula is claimed from those failed reads.

The verified control formulas remain Speed `(30625*49^(-n/0.784)-625)/150` below n=0.784, then `-3*(n-0.784)/0.216`; Amount `112.5*(1-9^(-n))`; Sustain `200*n-100`; Doubler and Width `100*n`, for clamped n in [0,1]. Registration blocks `0xfa1758`, `0xfa1940`, `0xfa1d24`, `0xfa2228`, and `0xfa2408` supply defaults Amount100, Sustain0, Speed25, Doubler0, Width0. Startup and the Default factory preset now reference named recovered constants in both engines, preventing independent default literals from drifting. The existing curve implementations already match these findings. This is a provenance refactor, with no intended audible change.

Ghidra again produced no decompilation. Successful reads are register-level fallback pseudocode, with lost floating-point register values; assembly corroborates the constants. Exact detector, negative-speed effect, sustain timing and doubler processing remain unknown, and their implemented estimates below remain estimates.

Validation of this pass: all 14 Rust tests pass; standalone build succeeds; VST3 rebuild and streaming CTest pass; packaged bundle passes strict signature verification. Read-only code review found no blocking issues. No 80% coverage measurement or original-sound equivalence is established.

## Current binary trace and correction — 2026-11-04

Opened the original main executable again with `core_target_open` in Deep mode. Name search `ev_f662041232986631ff8290f8cafe2499cb396e1bc64664144795d094a7244a70` completed all 17 matches. Settings/message search `ev_f8247740a20a6ea61b2904a4d0edc904395afaa4befddf157a9f26a9e65235e8` examined 35 of 292 matches. Followed the returned string references, then awaited pseudocode sequentially for `0xfa257c`, `0x13f3808`, `0xf9a798`, `0x13f3588`, `0x13f3658`, `0xf9a8c8`, `0x13f34b8`, and `0xf18b00`. The latter seven returned the evidence IDs already listed below.

New registration evidence `ev_3665d2906d578398c4b6f00fa7d4ae8e4e24a33c3f9b6baa0c7f75e382e9adfe` resolves `0xfa257c` into broad fallback function `0xead874`. Its block `0xfa2408` constructs Width with physical endpoints 0 and 100 through `0x13f3808`, then passes zero to the inverse conversion for its default. Thus Width = 100*n, default 0%. Block `0xfa2228` establishes the same range/default for Doubler. Corrected VST3 Width initialization and both Default factory presets from 70% to 0%; other presets remain independent creative choices. The existing Speed, Amount and Sustain mappings below already match recovered values.

Validation for this pass: the new Default-preset regression failed at 70% before the fix; all 14 Rust tests and VST3 CTest pass after it. Both builds succeed, bundle signature verification succeeds, and `rebuild_check` passes build, launch and mechanics checks (19 sourced values, 28 estimates). Visual similarity is reported at about 40%; this pass addresses mechanics.

Ghidra again failed; results are register-level fallback pseudocode with unresolved floating-point instructions. Exact original detector, retuning, negative-speed, sustain timing, stabilizer and doubler DSP formulas remain unknown. This pass does not establish identical sound. Earlier statements that the VST3 lacks doubler controls describe the earlier build; the current VST3 has both controls.

## Fresh original-program verification — 2026-11-04

Reopened the original universal main program with `core_target_open` in confirmed Deep mode. A new exact-name `native_strings_find` search completed all 17 matches (`ev_17a51fb6a0b59e601dac7d3c3f5d94b5c5c22a0a9acac413fff5ae295b598322`). It directly connects Amount `0x216b4f9` → `0xfa18d4`, Sustain `0x216b500` → `0xfa1aac`, Speed `0x216b512` → `0xfa1e98`, DoublerDryWet `0x216b52e` → `0xfa239c`, and DoublerStereoWidth `0x216b53c` → `0xfa257c`. A separate settings/messages search returned 30 of 219 matches (`ev_9355017cbad300a7da565fe7a887da7296c011fc16526a4ed744f15ee0fbfb5a`).

Followed those references and awaited every pseudocode result sequentially:

| Requested address | Returned evidence | Read result |
| --- | --- | --- |
| `0xfa1e98` | `ev_b9fa3b395d53df3602ca272b2e9ee9cc02a3587cbd05afcc2427114b6350ad81` | Broad `0xead874` fallback grouping, 9,015 lines; focused registration blocks confirm calls to speed/amount constructors and default Speed 25. |
| `0xf9a798` | `ev_20786eb4b88d410621d7bbe1c59924f3e0349bf68e1ff5392d4b977cfc0f83d8` | Speed midpoint constants 25 and −1.5; calls `0x13f3588`. |
| `0x13f3588` | `ev_3f10344fd9c877b71760c1a06d74cf840a0a63b348650259ff6a55e6bfded51e` | Curve construction and calls to log2/pow. |
| `0x13f3658` | `ev_0cbf31ea56c3654dc0404047442fd0b575a73ae43312a36de00dad1f8a1c4ce2` | Forward curve and endpoint clamp. |
| `0xf9a8c8` | `ev_468ecb2f44f922c0749b5bb6ec359ffbfd6b8f81bf00f2782a04d24822616131` | Amount midpoint construction; calls `0x13f34b8`. |
| `0x13f34b8` | `ev_952f5f01ec0d1a2d2b6abb63d3029cdf3c581390e385ff1a6e50a52ed09b17eb` | Amount curve construction. |
| `0x13f3808` | `ev_bd86a384339ea1ede9c5286f557d6134823f3e8e7f63f100446ceaf5f765fbc4` | Linear range constructor. |
| `0xf18b00` | `ev_a279eb338dc0100303b372fd76452bae79520553d88302da1732be670f09b572` | Tutorial message wiring, 443 lines. |

These fresh results reproduce the control findings below; they do **not** recover additional DSP formulas. Ghidra again reported no result or inability to disassemble; every successful result above is fallback pseudocode. The existing Rust and VST3 Speed, Amount and Sustain control mappings already implement the recovered values, so no additional behavior change is justified by this retry. Doubler and Width exist only in the standalone app; the VST3 does not implement those controls. Rebuilt both outputs: `cargo test --offline` passed **13 tests**, `cargo build --offline` succeeded, and `bash app/vst3/build.sh` passed CTest and repackaged `app/MegaTune.vst3` with signature verification. Craft Anything `rebuild_check` also passed build, launch/rendering and mechanics validation (19 sourced values, 28 estimates).

## Target and method

Original main program opened with `core_target_open`: `/Users/ade/Downloads/MetaTune.vst3/Contents/MacOS/MetaTune` (102,739,856 bytes), SHA-256 `7453f5ec9f816261f935f3d19d371ee4a65f741e2ffa8394e2bc19f5567f2010`. Deep mode was confirmed enabled. All addresses below are ARM64 virtual addresses, not file offsets.

An exact ARM64 slice was extracted to `capture/MetaTune-arm64`, from universal-file offset 54,312,960, length 48,426,896, SHA-256 `149690405ff6faaf6ddd73563f9ac686d5a2c2a8a2b93b626c3e9f93d8a9fbea`. It was opened separately for a focused retry. This extraction changes no instructions.

`native_strings_find` searched names, settings and messages with `Speed|Sustain|NormalizedAmount|ScalePattern|DoublerStereoWidth|negative|robotic`. Evidence `ev_a0695d9cfe0a51d86c4663883f43dff69efb625b1a87a0131d2bad336013d0d0` contains the first 100 of 219 matches; this is a bounded search, not exhaustive DSP coverage. References in those results were followed to the functions below. A separate `native_xrefs_list(0x216b512)` request timed out after 300 seconds.

## Functions actually read

Each `native_function_pseudocode` request was awaited before the next pseudocode request. Complete returned pseudocode is retained in `investigation.cany/evidence/` under the evidence IDs below.

| Function | Trace and result | Evidence |
| --- | --- | --- |
| `0xd09968` | References negative-speed property text at `0x216861c`; 39 lines show property construction/callback setup, not a pitch formula. | `ev_6388a72b5a6b20d3550fce53c5b1a4232260f79d4a0dce7004e9c751a64baa43` |
| `0xf18b00` | References speed and sustain tutorial messages; 443 lines show tutorial object construction, string copies and calls to `0xf1671c` and `0x1072004`. Establishes UI message wiring. | `ev_a279eb338dc0100303b372fd76452bae79520553d88302da1732be670f09b572` |
| `0xba2b60` | References MON/STE ScalePattern, NoteC–NoteB, Amount, Sustain, Speed and doubler identifiers. 1,978 lines show identifier registration through `0xba2d74` and `0xba3360`. The integer stores here are identifiers, not established DSP settings. | `ev_f4d0fe3e540a817a64c05a839be8ac13a72025a9be92987f21112ebbff1ee391` |

Important limitation: these are **built-in register-level fallback pseudocode**, not successful Ghidra decompilations. Ghidra returned no result or could not disassemble the requested address. The fallback explicitly warns about assumed calling conventions, unrecovered types and approximate conditions. It contains artifacts such as assignments to literal zero and lost register values; these must not be treated as recovered arithmetic. The first tooltip request also timed out after 300 seconds; the later retry returned the fallback above.

## Recovered control values and formulas

These are **derived from original ARM64 instructions**, using the returned pseudocode to follow calls, and corroborating floating-point operations directly because the fallback leaves them as inline assembly. They describe parameter conversion, not the pitch-correction DSP.

Trace: string `Speed` at `0x216b512` → reference **`0xfa1e98`** (xref evidence `ev_111b361b2b0559db57c161612a62076c366bede11c3600db6a71c143b3c8216a`) → registration block `0xfa1d4c` → constructor **`0xf9a798`** → mapping constructor **`0x13f3588`** and forward conversion **`0x13f3658`**. The string-containing function is reported as `0xead874` by the fallback; its broad grouping is not a reliable original function boundary.

| Function / code address | What was read | Evidence |
| --- | --- | --- |
| `0xead874`, relevant blocks `0xfa1758`–`0xfa257c` | Retried `native_function_pseudocode(0xfa1e98)` returned 9,016 lines; relevant registration blocks bind Amount, Sustain, Speed, DoublerDryWet and DoublerStereoWidth. Initial thin-slice request timed out, retry completed. | `ev_9b14238d9db27b4a4774534e64788e4b62cf35447e889557d1f788615e72c814` |
| `0xf9a798` | Two speed mapping branches: positive side midpoint 25; negative side midpoint −1.5. 22-line pseudocode read. | `ev_38839edd705adc82cd9fc36844b77618bdbd3ca49ccfd717dd9ba3952b8c0e77` |
| `0x13f34b8` | Amount mapping coefficient construction; 51-line pseudocode read. | `ev_12243f63f8af18dd116838d63a8609d06ce0089934c5ed75a9a6cd27a4f331c7` |
| `0x13f3588` | Mapping coefficient construction using `pow` and `log2`; 51-line pseudocode read. | `ev_1b4435a3922eb8943e16bab585c0dffd0adea8d49fdc2eefea59b6a9bcf0c7ee` |
| `0x13f3658` | Forward curve and endpoint clamp; 41-line pseudocode read. | `ev_daae763e979d38fda2438e5755a86ecd61e06e99c73dacf7a110ee76e3f5be55` |
| `0xf9a8c8` | Amount curve midpoint 75 (double constant `0x4052c00000000000`); 10-line pseudocode read and MOV/MOVK instructions checked. | `ev_1768a913beaf0af1f3e6f70ff838d270891077b92022c29b026406b2aaa83ebc` |
| `0x13f3808` | Linear range constructor used for sustain and doubler controls; 8-line pseudocode read. | `ev_38aeaa841861763719a12a37e8a7132df3e80e49ed99387a37042cc5bfefb817` |
| `0xf9a7f8`, `0xf9a84c`, `0x13f36f8` | Forward/inverse speed dispatch and generic inverse checked as raw instructions. Pseudocode requests at `0xf9a7f8` and `0x13f36f8` returned `function_not_found`, so these are not claimed as successful pseudocode reads. | Raw bytes `ev_272c1da1c160d39db526d3c02bec7cac49e068b044fb468d57553507df411a28`, `ev_adadc508eb9efd3d4683c256e12f70abac180513544a2d569d26d5b7772481f9` |

Raw registration bytes: `ev_739b13c8db669b09e9ab2def73d8dafd1e43ae0a393a6910d8fa52eaa2a923cb` and `ev_43d277e7b86df30b692d80438986210cea37d63f550ecf48a5604add5ba89485`. Focused disassembly is retained in `capture/parameter-registration.asm`, `capture/speed-mapping.asm`, `capture/mapping-math.asm`. Import stubs confirm `0x2161164 = log2` and `0x21613f8 = pow`.

Vtable bytes at `0x2cdde78` point to speed forward/inverse `0xf9a7f8`/`0xf9a84c`; the amount vtable at `0x2cddea8` points to generic forward/inverse `0x13f3658`/`0x13f36f8`. Evidence `ev_8a24028b56b290088a9a698522523f9a83df0cc00c10bca6eb1461959adaa981` establishes these dispatch links.

For normalized control value `n` clamped to [0,1]:

- **Speed**: range **−3…200 ms**, default **25 ms**. Constants `a=0.784` (`0x3fe916872b020c4a`) and `b=0.216` (`0x3fcba5e353f7ced8`). If `n<a`, `speed=(30625*49^(-n/a)-625)/150`; otherwise `speed=-3*(n-a)/b`. Inverse for positive speed `v`: `n=a*log((150*v+625)/30625)/log(1/49)`; for `v<=0`: `n=a-b*v/3`. Landmarks: n=0→200, .392→25, .784→0, .892→−1.5, 1→−3. Registration loads max 200 via `0x4069000000000000` at `0xfa1d58`, min −3 at `0xfa1d60`, default 25 at `0xfa1d74`.
- **Amount**: range **0…100%**, default **100%**. `amount=112.5*(1-9^(-n))`; inverse `n=log(1-amount/112.5)/log(1/9)`. Midpoint n=.5→75%. Constructor `0xf9a8c8` supplies midpoint75 to the generic curve.
- **Sustain**: range **−100…100%**, default **0%**, linear conversion `sustain=-100+200*n`. Registration block `0xfa1940` calls `0x13f3808` with −100/+100 and inverse at physical0.
- **Doubler dry/wet and width**: range **0…100%**, default **0%**, linear conversion `100*n`. Registration blocks `0xfa2228` and `0xfa2408` use `0x13f3808` with0/+100 and inverse at physical0.

Both builds now use recovered speed/amount conversions. Preset loading clamps speed to200; standalone audio Settings and UI/VST3 default to25. Standalone width default is corrected from80 (UI)/100 (audio Settings) to0. VST3 host automation now uses the recovered normalization; existing sessions made with the previous estimated mapping may need automation adjustment. Physical parameter state is still stored by JUCE.

## Original values and rules established by messages

All rows below are wired by function **`0xf18b00`**, with the evidence ID given above. Full string endings were checked directly against the ARM64 slice by mapping its Mach-O segments and reading to NUL.

| Setting | String address | Original user-facing rule | Build use |
| --- | --- | --- | --- |
| Speed | `0x216a160` | 0–20 ms is described as robotic correction; slower speeds sound more natural. | Existing speed control remains; this band is recorded in `mechanics.json`. The full range/default is established separately by the control trace above. |
| Negative speed | `0x216a1f2` | Negative speed creates a harder effect and is suggested with Note Stabilizer. | Exact negative-speed DSP remains unresolved. Both builds expose −3…200 ms, but still clamp negative physical speed to zero in their independent DSP. |
| Positive sustain | `0x216a27a` | Positive values retune the sustained portion of longer notes more quickly. | **Corrected** both engines: positive sustain now reduces the estimated smoothing time as a target is held. |
| Negative sustain | `0x216a2f8` | Negative values retune the sustained portion of longer notes more slowly. | **Corrected** both engines: negative sustain increases the estimated smoothing time. VST3 now applies the effect progressively to held notes. |
| Amount | `0x216a375` | 100% gives full retuning; 0% gives none. | Existing amount endpoints retained; normalized control curve is recovered above; how physical Amount scales DSP correction strength remains an implementation choice. |
| Note Stabilizer | `0x216a3f1`, `0x216a437` | Described as preventing retuning during vibrato/transitions and unwanted flutter on sustained notes. | Existing target hysteresis remains an estimate; no original threshold recovered. |

Full message bytes and address provenance are retained in `capture/original-messages.json`; `native_bytes_show` evidence `ev_f73eb9b3d5fc471e4b68ddf5abe07968d86573ac92010864994699741ea63940` independently retains 880 bytes at `0x216a160`. No pitch formula or timing curve is established merely by these descriptions.

## Implemented formulas (independent estimates)

The sustain **direction and held-note scope** above are sourced. The following numeric coefficients and formulas are still independent estimates, not recovered original DSP.

Standalone (`src/audio.rs`):

- `MIDI = 69 + 12*log2(hz/440)`; nearest enabled pitch class across octaves; empty note mask bypasses correction.
- `desired = (target - MIDI) * amount/100`.
- `seconds = max(0.00001, max(speed,0)/1000 * (1 - clamp(sustain,-100,100)/100 * min(held_seconds,1) * 0.9))`.
- `blend = 1 - exp(-period/(sample_rate*seconds))`; smooth semitone correction; pitch ratio `2^((correction+transpose)/12)`.
- Autocorrelation: 55–1200 Hz, confidence 0.65, 80 ms window/20 ms hop, downsample target 12 kHz, silence energy 0.00001, peak threshold `max(best*0.92,0.65)`. Stabilizer hysteresis 0.65 semitones.
- Pitch synchronous overlap grains: radius 1.5 periods (minimum 16 samples), rising-zero-crossing search radius 0.6 periods, sine-squared windows. Doubler wet fraction `doubler/100*0.5`, delay 18 ms, opposite 6 ms width offsets, 1 ms modulation at 1.7 rad/s. Export 24-bit PCM.

VST3 (`vst3/Plugin.cpp`, `vst3/TuningRules.h`):

- Detector: 1024 downsampled samples; 60–1200 Hz search, correlation 0.7, relative peak 0.94, analysis every 512 input samples. Stabilizer hysteresis 0.6 semitones.
- Held time resets on target changes, silence, failed detection or no enabled notes; it advances only while a target is retained and caps at one second.
- `seconds = max(0.001, max(speed,0)/1000 * (1 - clamp(sustain,-100,100)*0.005*clamp(held_seconds,0,1)))`.
- Per-sample blend `1-exp(-1/(sample_rate*seconds))`; smooth ratio towards `2^(target_correction/12)`.
- Two overlapping delay taps with 2048-sample phase cycle; reported latency 2048 samples retained for internal/host bypass. These are independent streaming choices.

Control ranges/defaults and normalization are now sourced as described above. `mechanics.json` distinguishes those values from estimated DSP constants.

## Verification and remaining gaps

Regression tests were run RED before reversing sustain direction. Standalone tests check positive < neutral < negative smoothing time and no sustain change at zero held time. The VST3 regression checks the same rule; existing streaming tests check pitch correction, disabled targets, finite output, state round-trip and latency-aligned bypass. Review found no blocking issue, but noted that the held-time lifecycle itself is not exercised by a dedicated streaming regression.

Original pitch-detection formulas, negative-speed DSP, exact sustain timing modulation, doubler DSP and stabilizer thresholds remain **unresolved**. Returned fallback pseudocode is insufficient to claim an exact sound reconstruction. Formant preservation is not implemented. The build is an independent monophonic vocal tuner with recovered original control mappings, defaults and selected message rules now applied.

Packaged output: `app/MegaTune.vst3`. `Install MegaTune.command` copies it to the user's plug-in folder when run by the user; this session does not install outside the workspace.

Final validation: `cargo test --offline` passes all 11 tests; `cargo build --offline` succeeds. `bash app/vst3/build.sh` rebuilds and packages the VST3, passes CTest and strict bundle signature verification. Mapping regressions check the original landmarks and 101-point forward/inverse round trips; they failed against the old linear controls before implementation. These checks do not establish identical original DSP or an 80% coverage measurement.

Craft Anything `rebuild_check` passes build, window rendering and mechanics validation: 47 values, 19 sourced and 28 estimated.

The final VST3 state regression saves physical Speed25/Amount75, changes a fresh processor to different values, restores the state and asserts both recovered physical values. CTest passes after this stronger check.

## Factory presets and doubler

Added Default plus eight vocal starting points: Natural Lead, Polished Pop, Hard Tune, Smooth R&B, Rap Lead, Wide Chorus, Intimate Double and Airy Backing. They preserve the chosen key and target notes. These are independently designed settings, not presets recovered from the original.

The VST3 now exposes automatable Doubler and Width parameters, saved in host state. It mixes up to 50% of a modulated delayed tuned voice with opposite stereo offsets (18 ms base, ±6 ms at full width, 1.2 ms modulation at 1.7 rad/s). Bypass remains latency aligned and excludes doubling. Preallocated delay buffers avoid audio-thread allocation. Standalone factory presets use the existing doubler engine.

Validation: the new missing-preset regression failed before implementation. All 13 Rust tests and VST3 CTest pass after implementation, including factory program selection, stereo decorrelation and doubler state restoration. Both builds succeed; rebuild_check passes. Fresh standalone and plug-in screenshots were compared with reference/picture-1.jpg; the existing orb, three main knobs and purple keyboard layout are retained.

## Readiness for use (2026-11-04)

Status: experimental build for evaluation, not a validated production release.
The standalone app imports, previews and exports vocal WAV files; the VST3 is
packaged for local macOS testing. Retain original recordings and evaluate exports.

The standalone interface now schedules idle animation at approximately 30 frames
per second instead of requesting unlimited redraws. User interaction can still
request immediate repaint through egui, preserving the existing appearance.

Release gaps: no measured 80% test coverage, no documented DAW compatibility
matrix or long-session CPU/dropout tests, no formant preservation, and no
validated equivalence to the original DSP. The plug-in reports 2048 samples
of latency (about 42.7 ms at 48 kHz); low-latency monitoring is not validated.
Bundle signature verification does not establish Developer ID signing or Apple
notarization for public distribution.

Before production release, validate save/reopen, automation, bypass, mono and
stereo operation at supported sample rates in the intended DAWs; measure CPU,
latency and output quality on real vocals, and complete distribution signing.
Build and automated regression success alone do not establish these outcomes.

## Control and DAW session fixes — 2026-11-04

The native VST3 now saves its selected factory-program index alongside parameter
state, restores it without reapplying a factory preset over customized values,
and keeps the preset menu synchronized with host program changes. Older sessions
without the index fall back to Default while retaining their parameter values.
The key label follows the selected root, and keyboard attachments update
synchronously when the root selector changes. Every rotary control supports
double-click reset to its parameter default. Numeric readouts use one decimal.
The standalone preset menu identifies its current settings, and Help explains
how to use the supplied VST3 installer and rescan in a DAW.

Regression: the session-reopen test failed before the state fix. Extended native
editor checks exercise the key selector, Chromatic action, remapped note button,
bypass, stabilizer, and all five sliders through their UI notifications. Existing
streaming tests cover corrected audio, doubler, saved parameters and host bypass.
These are automated JUCE host/interface checks; an actual DAW scan and playback
session has not been performed here. The reference's orb, three-knob arrangement
and purple keyboard are preserved; the build is not visually identical.

## Interactive controls and animation — 2026-11-04

Both interfaces now provide previous/next factory presets, complete-setting undo
and redo, and two editable A/B comparison sounds. Standalone drags are grouped
into one undo step; comparison navigation is excluded from undo history.
Standalone double-click knob reset now receives click events. Its particle shell
and faceted core animate with elapsed time, with playback-responsive pulse and
an actual playback cursor. Native animation smooths input amplitude and maintains
decaying note heatmaps; the output display follows the processor's smoothed pitch
rather than assuming full correction at every Amount setting. Native rotary
controls have shaded faces and segmented arcs; existing parameter attachments
continue to connect knobs, notes, scales, effects and bypass to audio processing.

Validation: 16 Rust tests pass, including complete-control undo/redo and editable
comparison slots; offline build succeeds. Native control regression exercises
preset navigation, undo/redo and comparison. Fresh screenshots were compared to
reference/picture-1.jpg; the central orb, three-knob arrangement and purple keys
are retained, but the design remains visibly different. Rebuild_check passes
build, rendering and mechanics. Original DSP equivalence, original preset parity,
all original secondary features and measured 80% coverage remain unverified.

## Load-blocking signature defect — 2026-11-04

The packaged VST3 never actually loaded in a host. Every build passed the
in-process streaming test but failed a real module load:

```
format=VST3 types=1 name="MegaTune" version="1.0.1"
RESULT instantiate failed: Unable to load VST-3 plug-in file
```

Direct `dlopen` gave the cause: `code signature ... not valid for use in process:
library load disallowed by system policy`. The VST3 helper signs the bundle
before it writes `Contents/Resources/moduleinfo.json`, so the delivered seal was
stale. `codesign --verify` still reported "valid on disk" for the executable
alone, which is why earlier signature checks did not catch it.

Fix in `vst3/build.sh`: after `ditto` into the final bundle, re-seal with
`codesign --force --deep --sign -` and then verify. The script now also runs the
independent host harness so this regression cannot return silently.

## Independent host verification — 2026-11-04

`vst3/host/` is a separate JUCE console host that loads the finished `.vst3`
through `addHeadlessDefaultFormatsToManager` and instantiates it as a real VST3
module, rather than linking the processor statically. Results for the installed
copy at `~/Library/Audio/Plug-Ins/VST3/MegaTune.vst3` (version 1.0.1):

| Check | Result |
| --- | --- |
| Module discovery | 1 type, "MegaTune", vendor "Craft Anything" |
| Instantiation | succeeded |
| Host parameters | 21 (20 own + VST3 "Program") |
| `prepareToPlay` latency | 2048 samples |
| 40 blocks of 220 Hz stereo | finite, in=6638.52 out=5970.85 |

The exposed set is Key, Speed, Sustain, Amount, Global Bypass, Note Stabilizer,
12 note toggles, Doubler, Width and Program. Regression: `ctest` 1/1 passes and
the native tests cover state round-trip, bypass, presets, A/B and UI wiring.

## Original artifact identity and surface — 2026-11-04

Target: `original/MetaTune.vst3`, SHA-256
`7453f5ec9f816261f935f3d19d371ee4a65f741e2ffa8394e2bc19f5567f2010`
(the handoff hash `7453f5ec9f81`). Slate Digital MetaTune 1.2.3, universal
x86_64 + arm64, Developer ID signed and notarized, wrapped by PACE Eden/iLok
(`__Pace_Eden.bundle`, `libpacefusion_shared.dylib`, `Signatures/codesign.dsig`).
The original was read only; nothing in it was modified.

REA evidence: `ev_4bb292344e628f6659eb78ad779e9fb1435463a9e9b3563db57ce461dc702b69`
(Mach-O summary: 2 architectures, 28 dependencies, 1102 imports, 13 exports,
5 segments) and `ev_2f4aa3ed33892a0398eacf8b7a52f8e3d1fe6dbce25467be76831aa4dfb39e24`
(exports). Exports are those of a multi-format plug-in: `GetPluginFactory`,
`bundleEntry`, `bundleExit` (VST3); `ACF*` and `AUEplWrapper*` (AU);
`VSTPluginMain` (VST2).

Hopper could not be launched: macOS denied the Apple Events permission
(`Not authorized to send Apple events to Hopper Disassembler`), so no
decompiler was available and no pseudocode was recovered in this pass. The
parameter surface below comes from the identifier string table read with
`strings` over the arm64 slice — an ordinary shell observation, not REA
Evidence, and not proof that every name is registered at runtime.

Observed identifiers: `Bypass`, `ScalePattern`, `NoteC`…`NoteB`,
`Amount`, `Sustain`, `Reference`, `Speed`, `LowLatency`, `Stabilizer`,
`DoublerDryWet`, `DoublerStereoWidth`, `Group`, `ToneVolume`, `Tone-C`…`ToneB`,
`ToneOctaveShift-C`…`ToneOctaveShiftB`. Scale names present: None, Major, Minor,
MajPent, MinPent, HarmMaj, HarmMin, Custom; groups: No Group, Group 1–16,
Miscellaneous. A `Pitch Reference` slider and `Cents` unit string are present.

Clone gaps, stated as unknowns rather than behaviour: the rebuild has no
`Reference` (A4 is hard-coded to 440 Hz), no `LowLatency`, no `ScalePattern`
(only Major/Minor/Chromatic note toggles), no per-note `Tone` or
`ToneOctaveShift` offsets, and no `Group` bank. The displayed Speed range reads
200 → -3 in the host because the recovered curve decreases with normalized
value; that matches the recovered mapping but is unconventional for hosts. The
Key default reports "E" and has not been checked against the original.

Not recovered: the original pitch detector, formant handling, correction
timing and doubler topology. The rebuild's DSP remains an independent
estimate, so this copy is a working plug-in, not a measured equivalent of the
original.

## Interface completion — 2026-11-04

The editor previously drew a static "Pitch Reference / 440 Hz" caption with no
control behind it, and the processor registered no Reference or Low Latency
parameter at all. Both are now real:

- **Reference** (415–466 Hz, default 440) drives pitch detection:
  `midi = 69 + 12*log2(hz / reference)`, so A4 can be retuned. The editor has a
  horizontal slider under the `PITCH REFERENCE` caption in place of the dead
  caption, with an integer Hz readout.
- **Low Latency** switches the correction window from 2048 to 512 samples and
  reports the matching host latency. The window is now a variable
  (`window`/`half`) instead of hard-coded 2048/1024, covering the dry tap, the
  crossfade pair and the phase wrap.

Both were appended to the parameter list so existing automation indices and the
Speed/Amount positions used by the regression suite are unchanged. The VST3
host now enumerates 23 parameters (21 own plus the wrapper's "Program").

Verification: `ctest` passes; the suite additionally asserts the Reference
default, asserts latency 2048 → 512 → 2048 across host-suspended Low Latency,
and confirms that at A4=432 a 432 Hz input passes through at 432.4 Hz while the
default 440 Hz reference still corrects 432 Hz to 440.4 Hz. The existing UI
regression clicks every button, moves every slider and toggles every switch,
comparing each against its host parameter; the new slider and toggle are
covered by the same loops. The packaged bundle's own editor instantiates at
860×680 inside the host harness. A hosted VST3 editor draws through its native
`IPlugView`, so an offscreen screenshot of it is blank; the UI evidence is the
in-process render at `vst3/build-cli/MegaTune-vst3.png`.

Still missing from the interface, carried over from the earlier gap list: the
original's per-note `Tone-*` and `ToneOctaveShift-*` trims, `ToneVolume`, the
`Group` preset bank, and the eight `ScalePattern` modes (only Major, Minor,
Chromatic and free note toggles are exposed).

## Factory presets and effect verification — 2026-11-04

The nine generic programs were replaced by sixteen production-oriented presets,
shared byte-for-byte between the VST3 and the standalone `src/presets.rs`:

| # | Preset | Speed | Sustain | Amount | Doubler | Width | Stabilizer |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Default | 25 | 0 | 100 | 0 | 0 | on |
| 1 | Natural Tune | 90 | -20 | 70 | 0 | 0 | on |
| 2 | Modern Pop | 45 | -10 | 95 | 8 | 12 | on |
| 3 | Polished Pop | 70 | -25 | 85 | 10 | 20 | on |
| 4 | R&B Smooth | 120 | -40 | 75 | 15 | 25 | on |
| 5 | Hip-Hop Lead | 25 | 10 | 100 | 5 | 10 | on |
| 6 | Doubler Wide | 25 | 0 | 100 | 65 | 100 | on |
| 7 | Trap Robotic | -3 | 40 | 100 | 0 | 0 | off |
| 8 | EDM Vocal | 15 | 20 | 100 | 20 | 45 | on |
| 9 | Rock Anthem | 60 | -15 | 90 | 12 | 30 | on |
| 10 | Indie Folk | 150 | -45 | 60 | 0 | 0 | on |
| 11 | Lo-Fi Bed | 200 | -60 | 55 | 35 | 60 | on |
| 12 | Doubler Tight | 30 | 0 | 100 | 35 | 15 | on |
| 13 | Subtle Assist | 180 | -30 | 40 | 0 | 0 | on |
| 14 | Extreme Tune | -3 | 60 | 100 | 0 | 0 | off |
| 15 | Spoken Word | 100 | -10 | 50 | 0 | 0 | on |

A **Shift** control (-12..+12 semitones, default 0) was also added so the
corrected signal can be transposed down; the standalone build already had this
control and the VST3 did not. It is applied after the Amount-weighted
correction: `target = (nearest - midi) * amount + shift`.

Measured through the packaged VST3 module (not the static link), 432 Hz input,
48 kHz:

| Probe | Result |
| --- | --- |
| Autotune, Amount 100 | 439.80 Hz (target 440) |
| Autotune disabled, Amount 0 | 432.09 Hz (input unchanged) |
| Doubler 65 / Width 100 | stereo difference 423.8 |
| Doubler 0 | stereo difference 0.0 |
| Negative Speed (-3) steady state | 439.94 Hz |
| Slow Speed (120 ms) steady state | 439.85 Hz |
| Shift -1 semitone | 417.4 Hz (analytic target 415.3) |

The suite also asserts that every one of the sixteen presets renders finite
audio, that `Trap Robotic` uses a negative Speed, that `Doubler Wide` is
actually wet, and that negative Speed smooths faster than the default
(`megaTuneSmoothingSeconds(-3) < megaTuneSmoothingSeconds(25)`). The VST3 host
now enumerates 24 parameters and 16 programs.

Rate-of-change assertions are made at the unit level rather than from a host
frequency estimate: a window spanning a moving pitch cannot be estimated
reliably by autocorrelation, and an earlier version of this harness produced a
physically impossible reading (441 Hz while converging up to 440). That probe
was changed to steady state instead of being tuned until it agreed.

## Latency and quality modes — 2026-11-04

Low Latency previously reported 512 samples (10.7 ms at 48 kHz), which is not a
low-latency mode. It is now **128 samples (2.7 ms at 48 kHz)**, verified through
the packaged VST3 rather than from source:

| Mode | Reported latency | Shifter |
| --- | --- | --- |
| Quality (Low Latency off) | 2048 samples, 42.7 ms | 4-grain Hann overlap-add, 25 % hop |
| Low Latency (on) | 128 samples, 2.7 ms | 2-grain cosine crossfade |

Both delay taps now use Catmull-Rom cubic interpolation instead of linear, and
pitch detection gained a 3-point median filter so octave errors and jitter do
not reach the correction target.

Measured at 432 Hz input, 440 Hz A4:

| Mode | Output pitch | Envelope ripple |
| --- | --- | --- |
| Quality | 440.37 Hz | 0.145 |
| Low Latency | 440.37 Hz | 0.384 |

Quality is 2.6x smoother at the same tuning. The suite asserts both the tuning
equality and `qualityRipple < 0.8 * lowLatencyRipple`, so the quality advantage
cannot silently regress.

Two things learned by measuring rather than assuming:

- A 4-grain overlap-add cannot shift pitch with 128-sample grains, because the
  grain is shorter than one period at 432 Hz: that combination measured
  432.43 Hz, i.e. no correction. Low Latency therefore keeps the 2-grain
  crossfade, which does shift (440.37 Hz) at the cost of ripple.
- A hosted VST3 parameter change is only delivered to the processor on the next
  `processBlock`. Reading latency straight after `setValueNotifyingHost` and
  `prepareToPlay` reported the stale 2048 in both modes; the harness now
  flushes one block before reading.

Honest limit: 2.7 ms is low but not zero. A delay-line pitch shifter needs
roughly one period of signal history to correct a note, and the original's own
latency was never recovered, so "lowest on the DAW" is a measured claim about
this plug-in (128 samples), not a comparison against every host and competitor.

## DSP upgrade pass — 2026-11-04

Delivered: **YIN pitch detection** (`TuningDsp.h`) replaces the previous
autocorrelation detector. It uses cumulative-mean-normalised difference, an
absolute threshold, parabolic lag interpolation, and reports aperiodicity. The
aperiodicity value doubles as the breath/unvoiced gate: frames above threshold
produce no correction, which is the transient/breath behaviour from the wish
list. The 3-point median smoother is retained.

Validation on a vowel-like signal (harmonics shaped by F1 700 Hz / F2 1200 Hz,
210 Hz fundamental, +/-35 cents at 5.5 Hz vibrato, only A3 enabled): mean tuning
error 17.9 cents. Quality-mode envelope ripple is 0.139 against 0.387 for Low
Latency.

Attempted and rejected, with measurements:

- **8-grain overlap-add**: ripple improved 0.139 -> 0.079, but the extra delayed
  taps comb-filter the output and the 432 Hz streaming test collapsed below the
  level floor. Reverted to 4 grains. Smoother on one metric while losing level
  is not an improvement.
- **4-grain at 128 samples**: measured 432.43 Hz, i.e. no pitch shift, because
  the grain is shorter than one period. Low Latency keeps the 2-grain crossfade.

Not delivered, and not claimed:

- **Formant preservation**: needs spectral-envelope warping (cepstral or LPC
  pole-zero re-filtering) on the shifted signal. It was not shipped because the
  available validation, a harmonic vowel measured with a coarse DFT peak search,
  cannot distinguish a preserved formant from a harmonic peak, and shipping an
  unverifiable DSP claim is worse than leaving it out.
- **PSOLA/WSOLA**: needs block-based grain scheduling with waveform-similarity
  alignment and explicit read-pointer wrap handling at sustained ratios. A
  half-finished rewrite would risk the working shifter, so the 4-grain cubic
  path remains.
- **Note-range limiting** and **key/scale per factory preset** were not added.

Stereo-linked detection was already the case: analysis sums to mono, so both
channels always receive the same correction target.

### Latency floor — 2026-11-04

An attempt to lower Low Latency from 128 to 64 samples failed outright: the
2-grain crossfade measured 432.43 Hz on a 432 Hz input, i.e. no pitch shift at
all, because the 64-sample window is shorter than one period. Reverted to 128.

Testing below the previous measurement frequency exposed the real constraint.
The time-domain crossfade can only shift a signal whose period fits inside the
window, and 128 samples at 48 kHz is 2.7 ms, i.e. one period of roughly 375 Hz:

| Input (target A3 220 Hz) | Low Latency (128) | Quality (2048) |
| --- | --- | --- |
| 200 Hz | 201.9 Hz, barely shifts | 218.7 Hz, tunes correctly |
| 432 Hz | 440.4 Hz, tunes correctly | 440.4 Hz, tunes correctly |

So Low Latency is useful for notes above roughly 375 Hz and cannot tune the
lower half of the vocal range. That is a property of delay-line pitch shifting,
not a tuning parameter: lower latency and low-note coverage trade directly
against each other in this algorithm. A phase-vocoder path would remove the
one-period constraint and is the correct way to get both, but it is a new
engine, not a window-size change, and it was not attempted here.

## Range, scales and editor fixes — 2026-11-04

Added a **Range** parameter (1–12 semitones, default 12). Notes whose distance
from the target exceeds Range are left alone rather than pulled part way:
target correction becomes zero. Measured on a 200 Hz input aimed at A3 220 Hz:
Range 1 -> 200.0 Hz (untouched), Range 3 -> 219.2 Hz (corrected). A first
implementation clamped the correction instead, which leaves a note stranded
between two semitones; skipping is the behaviour a tuner should have.

Factory presets now carry a **key and scale** as well as the five controls:
Default/Rock Anthem E minor, Natural Tune/Modern Pop/Polished Pop/Doubler
Wide/EDM Vocal/Indie Folk/Lo-Fi Bed/Doubler Tight/Subtle Assist C major,
R&B Smooth A minor, Hip-Hop Lead C minor, Trap Robotic/Extreme Tune/Spoken Word
chromatic. Selecting a program sets key and the twelve note switches.

Fixed a real editor bug found while wiring this: restoring the key parameter
fired the key ComboBox `onChange`, which called the full `timerCallback`, which
ran `remember()` and **cleared the redo stack**. Undo then worked but redo was a
no-op. The key refresh now goes through a small `syncKey()` that rebuilds the
keyboard attachments without touching history.

Status against the six requested jobs:

| # | Item | State |
| --- | --- | --- |
| 1 | YIN/MPM detection | done (YIN) |
| 2 | Formant preservation | not done |
| 3 | PSOLA/WSOLA shifter | not done |
| 4 | Validation on vocals | done on a synthetic vibrato vowel (17.9 cents) |
| 5 | Range / breath / stereo-linked / preset scales | done |
| 6 | Lower latency | attempted; 64 fails, 128 is the floor |







