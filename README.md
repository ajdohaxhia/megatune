# ademusic MegaTune

A clean-room reconstruction of the **Slate Digital MetaTune** vocal tuner: a JUCE VST3 +
AUv2 plug-in with a live pitch monitor, a diagnostic "Why this note?" history, four
selectable pitch engines, factory presets and a fully themed editor.

> **Status: 0.5.0.** Engine fixes on top of the 0.4.0 editor and presets: a pitch-mark bug
> that detuned the output is fixed, the detector covers 55 Hz to 1.2 kHz, bypass cross-fades,
> large downward shifts no longer leak the original pitch, and the Doubler holds its level;
> see [What changed in 0.5.0](#what-changed-in-050). Prebuilt macOS binaries pass `ctest`,
> `auval` and the VST3 host check, but **MegaTune has still not been auditioned in a DAW or
> on real vocals**: every number here comes from synthetic signals. The factory presets are
> **not tuned by ear**, **formant preservation is unvalidated**, and builds are **ad-hoc
> signed, not notarized**. See [Status](#status) before shipping anything.

---

## What this is (and is not)

This project began as a reverse-engineering exercise on a shipped plug-in and became an
independent implementation. It is **not** a crack, patch, or redistribution of Slate
Digital's product, and it contains **no original code or binaries**.

- The original `MetaTune.vst3` is **not** included in this repository. It is PACE-protected
  commercial software and is not redistributable.
- Control semantics (parameter ranges, default values, curve shapes) were recovered from
  the shipped binary by static inspection and are documented in
  [`app/MECHANICS.md`](app/MECHANICS.md), together with the evidence and the limits of
  what was recovered.
- The DSP is an independent implementation. It is **not** a clone of Slate's algorithm:
  the original pitch detector, formant handling, correction timing and doubler topology
  were never recovered.

`MegaTune` and `MetaTune` are referenced for identification only. All trademarks belong to
their owners.

---

## Features

| Area | What you get |
|---|---|
| Pitch engines | Quality (TD-PSOLA), Low Latency (TD-PSOLA, 512), Signalsmith Stretch |
| Controls | Speed, Sustain, Amount, Doubler, Stereo Width, Key, Scale, Shift, Range, Pitch Reference (A4) |
| Doubler | Two extra voices, each with its own 15/24 ms offset and slowly drifting detune of a few cents |
| Stereo Width | Pans the doubled voices apart and, on its own, widens a mono vocal with a side signal that cancels in mono |
| Keyboard | One octave, scale membership dots, live input outline, target fill |
| Pitch monitor | Target note, signed cents and a ±100¢ meter, plus a scrolling trace of input and corrected pitch over the scale's note lanes; input and output level meters |
| Diagnostics | "Why this note?" history of recent decisions with a one-click Exclude/Enable action |
| Scales | Major, Minor, Harmonic/Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, both pentatonics, Blues, Whole Tone, Chromatic, or any custom set of notes |
| Presets | 32 factory sounds in 7 categories; user presets saved to disk; click to audition, Done to keep, Cancel to restore; "Keep my key and scale" (on by default) stops presets from changing the song's key |
| Host integration | VST3 + AUv2; preset recall, parameter automation, Undo/Redo, A/B comparison, bypass |


---

## Engines and latency

| Engine | Latency @ 48 kHz | Notes |
|---|---|---|
| Quality | **2048 samples / 42.7 ms** | default; stereo TD-PSOLA with correlation-aligned marks |
| Low Latency | **512 samples / 10.7 ms** | same engine, shorter delay and a shorter pitch-analysis window |
| TD-PSOLA | 2048 samples | pitch-synchronous overlap-add (also Low Latency) |
| Signalsmith | **5760 samples / 120 ms** | with optional formant compensation |

Declared latency matches measured impulse onset and cross-correlation lag at 44.1 and
48 kHz. A live engine switch republishes the correct latency on the next
`prepareToPlay`; whether a specific DAW re-reads compensation mid-session is **unverified**.

---

## What changed in 0.5.0

Engine only. The editor and presets are those of 0.4.0, apart from the footer, which now
names the engine the same way the engine menu does.

### Fixed

| Problem | Fix |
|---|---|
| **The output was detuned by up to half a sample per period.** Each pitch mark is aligned to the previous one by cross-correlation, but the measured lag was added to the previous mark's *rounded* position, so its fraction was thrown away on every grain. A steady 213 Hz tone came out 2.6 cents flat; the error grows with pitch. | The lag is added to the mark's true position. |
| The sub-sample part of that alignment was fitted on scores that favour the predicted position, which pulled every mark towards the prediction while the pitch was moving. | The preference only picks the whole-sample lag; the fraction is fitted on the raw correlation. |
| Notes below 70 Hz or above 1 kHz were treated as unvoiced and passed through untuned. | Detector range is 55 Hz to 1.2 kHz. |
| Toggling bypass, or automating Amount to zero, switched between tuned and dry audio in one sample (a click). | 5 ms cross-fade; the Doubler and Width fade with it. |
| With Shift near -12 the grains barely overlap, and the gaps were filled with the dry signal, so the original pitch leaked into the output. | Dry fill is only used before the first grains arrive. |
| The Doubler dipped the level by 0.9 dB at 50 %. | Equal-power blend: within 0.01 dB of the dry level from 0 to 100 % on the bench. |
| In Low Latency the pitch was measured on audio about 8 ms older than the audio being corrected. | The analysis span is shortened so it stays centred on the audio leaving the shifter. |
| The analysis interval and span were fixed in samples, so they halved in time at 96 kHz. | Both are fixed in time (10.7 ms and 32 ms). |
| The stabilizer released a note at a fixed 0.6 semitones, which is *earlier* than the midpoint when the neighbouring scale note is a whole tone away. | It releases 0.2 semitones past the point where another note becomes closer. |
| Every parameter was looked up by name several times per sample. | Read once per block. |
| The footer said "TD-PSOLA" while the engine menu said "Quality". | Same names in both places. |

### Measured, 0.4.0 → 0.5.0

`app/vst3/verify/stress.py` (synthetic vowel, ±35 cent vibrato plus drift, chromatic,
Speed 0, 48 kHz unless noted). Mean distance from the nearest semitone, in cents:

| Case | 0.4.0 | 0.5.0 |
|---|---|---|
| Steady 213 Hz sine | 2.6 | 0.4 |
| 330 Hz, Quality / Low Latency | 1.5 / 1.5 | 0.6 / 0.4 |
| 520 Hz, Quality / Low Latency | 1.5 / 2.0 | 1.0 / 0.4 |
| 700 Hz, Quality / Low Latency | 2.8 / 4.1 | 1.2 / 1.0 |
| 1100 Hz, Quality / Low Latency | 1.5 / 2.4 | 1.1 / 0.8 |
| 210 Hz at 44.1 kHz, Quality / Low Latency | 2.1 / 1.1 | 1.0 / 2.0 |
| 210 Hz at 88.2 kHz, Quality / Low Latency | 1.8 / 1.5 | 0.6 / 0.5 |
| 210 Hz at 96 kHz, Quality / Low Latency | 2.4 / 0.6 | 2.1 / 0.5 |
| 98 Hz, Quality / Low Latency | 2.8 / 2.4 | 2.9 / 2.5 |

| | 0.4.0 | 0.5.0 |
|---|---|---|
| Shift -12: output periodicity / frames tracked as voiced | 0.965 / 93 % | 0.993 / 100 % |
| Doubler 50 %, level vs dry | -0.86 dB | -0.01 dB |
| Doubler 100 %, level vs dry | -0.49 dB | +0.01 dB |

Not everything moved. Low notes (around 100 Hz) and Quality at 96 kHz are unchanged, and
Low Latency at 44.1 kHz measured worse in this run (1.1 → 2.0); repeated runs at other
pitches scatter between 0.4 and 2 cents with no pattern, so differences of a cent in this
table are within the noise of the method. The 900 Hz case is left out because the bench's
own tracker cannot follow that signal (it reports 28 cents of error on the *input*).

New regression checks: a steady off-pitch tone lands within 1 cent of the note (fails on
0.4.0 at -2.6 cents), a bypass toggle does not step the signal, and the Doubler holds its
level within 0.5 dB at 25, 50, 75 and 100 %.

---

## What changed in 0.4.0

Editor and presets only; the audio engine is unchanged from 0.3.1.

**Editor**

- **Pitch trace.** The decorative orb is gone. In its place is a scrolling trace of the last
  few seconds: input pitch (cyan) and corrected pitch (violet) drawn over lanes for the notes
  enabled in the current key and scale, with the current target lane highlighted.
- **Readout.** One large "correcting to" note with the input note and signed cents under it,
  and a flat / in tune / sharp meter.
- **Level meters** for input and output.
- **Scale menu** with 13 scales next to the Major / Minor / Chromatic shortcuts. It shows
  "Custom" when the enabled notes match no named scale.
- **Tooltips** on every control, a one-line hint under each knob, and Cmd+Z / Shift+Cmd+Z
  for Undo / Redo when the editor has keyboard focus.

**Presets**

- **32 factory sounds** (was 16) in Natural, Pop, Hip-Hop, R&B, Rock, Doubles and Effect,
  each with a one-line description. The first 16 keep their positions, so host program
  numbers saved by older versions still point at the same sound.
- **Audition before you commit.** Clicking a preset in the browser applies it immediately.
  Done keeps it (as one Undo step); Cancel or Esc puts back the sound you had. Arrow keys
  step through the list.
- **Keep my key and scale**, on by default. Presets used to overwrite the key and enabled
  notes, which is almost never what you want mid-song. Turn it off to let a preset set them.
  **This changes behaviour:** a host program change no longer changes the key unless you
  turn the option off. The setting is saved with the session.
- **User presets.** Name the current sound and press Save. Presets are stored as
  `~/Library/Application Support/ademusic/MegaTune/Presets/<name>.megatune`, appear under
  User in every project, and can be deleted from the browser.
- Search matches category names as well as preset names.

The factory presets were written from what the controls do (Speed is a retune time,
negative Speed is hard tune, and so on). They have not been tuned by ear on real vocals.

---

## What changed in 0.3.1

Three controls were reported as not working. Each had a concrete cause in the code.

### Tuning (Speed, Sustain, accuracy)

| Problem in 0.3.0 | Fix |
|---|---|
| Speed and Sustain never reached the default TD-PSOLA engine. The shifter was handed `2^(target/12)` once per 512-sample analysis block, so every setting behaved like a stepped hard tune. | The Speed/Sustain-smoothed ratio is passed to the shifter every sample. The Signalsmith path uses the same smoothed ratio. |
| Each channel ran its own PSOLA with its own pitch marks, so the two channels drifted out of phase. | One `StereoPsola` with shared marks for both channels. |
| Marks advanced by the detected period only. A slightly wrong or drifting period made consecutive grains incoherent (roughness). | Each mark is aligned to the previous one by normalised cross-correlation on the mono sum, with sub-sample interpolation. |
| Grain centres were rounded to whole samples, which jitters the output period. | Grains are placed fractionally and read with cubic interpolation. |
| The period came from a 12 kHz decimated YIN without anti-alias filtering (about ±7 cents at 500 Hz). | Box-filtered decimation, then the period is refined on the full-rate signal, on the span that is leaving the shifter, so the correction lines up with the audio it is applied to. |
| A 3-frame median delayed every pitch frame by about 10 ms. | The median is only used to reject octave slips. |
| Engaging the shifter left a gap of zeros until the first grain; grains for low notes read samples that were not written yet. | Dry fill under the first grains, and marks are clamped to written audio. |
| Toggling Low Latency while running changed the reported latency but not the shifter delay. | The shifter delay follows the switch. |
| Unvoiced audio kept being re-synthesised for 120 ms after a note. | Voicing hold is 60 ms, with a 4 ms / 12 ms crossfade to the bit-exact dry signal. |

### Doubler

0.3.0 mixed in **one** copy of the signal on the **same** channel, delayed 18 ms with a
±1.2 ms wobble (about 3.5 cents). That is a comb filter, not a second take. 0.3.1 uses two
voices fed from the tuned mono sum: base delays of 15 ms and 24 ms, each modulated by two
slow sines at unrelated rates (roughly ±7 and ±5 cents of drifting detune per voice),
high-passed at 120 Hz so the low end stays tight. Doubler is a dry/wet blend; output level
stays within about 1 dB of the dry signal.

### Stereo Width

In 0.3.0 Width only nudged the doubler delay by ±6 ms per channel, so with Doubler at 0 it
did nothing at all. In 0.3.1 Width:

- pans the two doubled voices from centre to hard left/right;
- on its own adds the *difference* of the two voices as a side signal, which widens a mono
  vocal and cancels exactly when the output is summed to mono;
- widens stereo content that is already present (side gain up to +4 dB).

### Measured

`app/vst3/verify/bench.py`, synthetic vowel at 210 Hz with ±35 cent vibrato plus random
drift, chromatic scale, 48 kHz. "Error" is the mean distance from the nearest semitone.

| Case | Input error | Output error |
|---|---|---|
| Speed 0, Quality | 23.8 ¢ | 0.6 ¢ |
| Speed 0, Low Latency | 23.8 ¢ | 1.1 ¢ |
| Speed 25 | 23.8 ¢ | 12.4 ¢ |
| Speed 100 | 23.8 ¢ | 18.6 ¢ |
| Off-pitch note steps with gaps, Speed 0 | 27.3 ¢ | 0.8 ¢ |
| 4-semitone glide, Speed 0 | 23.3 ¢ | 1.0 ¢ |
| 108 Hz vowel, Speed 0 | 26.6 ¢ | 2.9 ¢ |

| Doubler / Width | L/R correlation | Side vs mid | Level vs dry | Mono sum vs dry |
|---|---|---|---|---|
| 0 / 0 | 1.00 | none | 0.0 dB | identical |
| 100 / 0 | 1.00 | none | −0.5 dB | doubled |
| 0 / 100 | 0.49 | −4.6 dB | +1.3 dB | identical (residual −155 dB) |
| 65 / 100 | 0.37 | −3.3 dB | +0.3 dB | doubled |
| 100 / 100 | 0.35 | −3.1 dB | +0.1 dB | doubled |

The 0.3.0 column is missing on purpose: 0.3.0 was not run through this bench, so there is
no measured before/after, only the code-level causes above.

---

## Screenshots

| Idle | Active | History |
|---|---|---|
| ![](docs/screenshots/idle.png) | ![](docs/screenshots/active.png) | ![](docs/screenshots/history.png) |

| Preset browser |
|---|
| ![](docs/screenshots/presets.png) |

---

## Install

Prebuilt macOS download (universal arm64 + x86_64, VST3 + AUv2):

https://github.com/ajdohaxhia/megatune/releases/latest

`MegaTune-<version>-macOS.zip` contains both plug-ins and an installer script. The builds are
not notarized, so macOS may refuse to open the script on first try: right-click it and
choose Open, or copy the two bundles by hand.

```bash
# from the release archive or app/dist
open "Install MegaTune.command"     # or:
cp -R MegaTune.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R MegaTune.component ~/Library/Audio/Plug-Ins/Components/
```

Then rescan plug-ins in your DAW. Use the VST3 in Ableton/Reaper/Cubase/Studio One and the
AUv2 component in Logic/GarageBand.

Uninstall: delete `~/Library/Audio/Plug-Ins/VST3/MegaTune.vst3` and
`~/Library/Audio/Plug-Ins/Components/MegaTune.component`.

---

## Build from source

Requirements: macOS, CMake ≥ 3.22, a C++17 toolchain, JUCE sources **8.0.13 or newer**
(the test suite calls `createEditorAndMakeActive`, which older 8.0.x does not have;
8.0.15 is the version used for 0.3.1).

```bash
git clone --depth 1 -b 8.0.15 https://github.com/juce-framework/JUCE.git ~/juce
```

```bash
cd app/vst3
JUCE_DIR=/path/to/juce ./build.sh          # builds, runs ctest, packages and ad-hoc signs
```

Or manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/juce
cmake --build build -j 8
(cd build && ctest --output-on-failure)
```

Universal build:

```bash
cmake -S . -B build-universal -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/juce \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-universal -j 8
```

Signing for distribution (requires an Apple Developer ID — not available in this
environment):

```bash
codesign --force --deep --options runtime --sign "Developer ID Application: …" MegaTune.vst3
codesign --force --deep --options runtime --sign "Developer ID Application: …" MegaTune.component
xcrun notarytool submit MegaTune.zip --keychain-profile … --wait
xcrun stapler staple MegaTune.vst3
xcrun stapler staple MegaTune.component
```

---

## Testing

```bash
cd app/vst3/build-cli && ctest --output-on-failure     # in-process DSP + editor suite
```

The suite covers, among others:

- parameter landmarks from the recovered maps (Speed curve, Amount curve, defaults)
- state round-trip, bypass, latency-aligned dry path, preset navigation, A/B, Undo/Redo
- 32 presets render finite audio; key lock on and off; user preset save, load and delete
- Range limiting, breath/uviolation gate, stereo-linked detection
- engine switching republishes latency (2048 ↔ 5760 ↔ 128)
- Signalsmith latency rescales after a 48 → 96 kHz sample-rate change
- 16384-sample host blocks stay inside the staging buffers
- a prepared analysis window performs no heap allocations
- held-target decisions are distinguished from fresh target changes
- Signalsmith obeys Global Bypass and still applies Doubler/Width
- host bypass stays latency-aligned at the Signalsmith latency, through 768 kHz
- negative Speed uses the hard-correction path
- Shift transposes independently when Amount is zero
- non-finite input, `reset()` and extra/mono channel buffers stay safe
- the engine × sample-rate × block-size matrix produces finite audio
- 4000-block engine/parameter churn remains finite
- **Why this note?** end-to-end: 432 Hz tone A4 → exclude A → G♯4 → Undo → A4
- Width alone widens a mono source and leaves the mono sum unchanged (0.3.1)
- Speed reaches the default engine: a slow setting leaves more vibrato than a hard one (0.3.1)
- 490 Hz disabled-target control (with and without Note Stabilizer)
- real-take target change: at 17.71 s of the dry fixture, excluding G retargets
  110/110 analysed blocks and Undo restores it bit-identically

Pluginval 1.0.4 at strictness 8 passes with its `Plugin state restoration` and
`Discrete parameter state restoration` tests disabled. Those two hit a known JUCE
host-proxy cache issue for discrete parameters: the plugin restores the correct
step, but the host-side proxy keeps the last random normalised value until its
next asynchronous refresh. An in-process test round-trips every parameter through
state instead.

Host-level checks (load, instantiate, parameters, audio, editor) run through
`app/vst3/host/`:

```bash
MegaTuneHostCheck /path/to/MegaTune.vst3
auval -v aufx Mgtn AdMs          # after installing the AU component
```

Measurement harnesses used during development live in `app/vst3/verify/`
(`formant_harness.py`, `paired2.py`, `score.py`, `measure.py`), plus `bench.py`, which
drives the in-process `MegaTuneBench` target to measure tuning error, doubler level and
stereo width / mono compatibility.

---

## Architecture

```
app/
  src/                 Rust/egui standalone prototype (historical)
  vst3/
    Plugin.cpp         processor + editor (single translation unit)
    TuningRules.h      recovered parameter curves (Speed, Amount, smoothing)
    TuningDsp.h        YIN detector, StereoPsola, StereoDoubler, helpers, diagnostic trace
    Tests.cpp          in-process regression suite
    CMakeLists.txt     JUCE plug-in target + tests
    build.sh           build + test + package + ad-hoc sign
    host/              VST3 host check, WAV renderer, latency probe
    verify/            measurement scripts, MegaTuneBench render bench (Bench.cpp + bench.py)
    third_party/       vendored Signalsmith Stretch + Linear (MIT)
  dist/                installer, universal VST3 + AUv2, README
  MECHANICS.md         recovered semantics, evidence and open questions
```

### Audio path

1. Input is summed to mono for analysis, so both channels always share one correction target.
2. YIN detects F0 with aperiodicity; aperiodic frames pass through untouched (breath gate).
3. The nearest **enabled** scale degree becomes the target; the decision and its reason are
   recorded in a lock-free 16-entry ring.
3a. The period is refined at full sample rate on the audio that is leaving the shifter.
4. Speed/Sustain shape a one-pole smoothing of the correction ratio, which is fed to the
   engine every sample.
5. The selected engine renders the shifted signal (unvoiced audio stays dry); the Doubler
   voices and the Width side signal are added after it.

---

## Status

| Item | State |
|---|---|
| Pitch accuracy (synthetic vibrato, 0.5.0 bench) | ✅ 0.5¢ mean / 1.2¢ p95 at Speed 0; steady tone 0.4¢ |
| Speed / Sustain reach the default engine | ✅ regression test (0.3.1) |
| Doubler / Width | ✅ measured on the synthetic bench; Width-only mono compatibility in the regression suite |
| Pitch accuracy (dry vocal) | ⚠️ 5.63¢ median, jumps 4.17 % measured on 0.3.0; **not re-measured on 0.3.1** (fixture not in the repo) |
| Low latency | ✅ 512 samples (10.7 ms); tunes 200 Hz correctly |
| Large host blocks / sample-rate changes | ✅ 16384-sample block and 48 → 96 kHz verified |
| Audio-thread allocations in analysis | ✅ none after prepare |
| Signalsmith bypass / doubler / latency | ✅ verified through 768 kHz |
| Negative Speed / zero-Amount Shift | ✅ verified |
| Non-finite input / reset / channel safety | ✅ verified |
| Long-session engine/parameter churn | ✅ 4000 blocks verified |
| AUv2 | ✅ `auval -v aufx Mgtn AdMs` passes on 0.3.1, 0.4.0 and 0.5.0 |
| pluginval 1.0.4, strictness 8 | ⚠️ passed on 0.3.0 with two known JUCE state-cache tests disabled; not re-run since |
| macOS build and test suite | ✅ 0.3.1 and 0.4.0 built universal on macOS (JUCE 8.0.15); `ctest` and the VST3 host check pass |
| Key lock and user presets | ✅ regression test (0.4.0) |
| Factory preset values | ⚠️ designed from the control ranges; not tuned by ear |
| Full parameter state round-trip | ✅ in-process regression |
| Why this note? → Exclude → Undo | ✅ verified on synthetic tone and on a real take |
| Presets, automation, recall, A/B | ✅ verified |
| **Formant preservation** | ❌ **unvalidated** — the estimator misclassifies known test cases |
| **DAW compatibility / listening test** | ❌ not tested in a real host; not auditioned on real vocals |
| **Distribution signing** | ❌ ad-hoc only; no Developer ID in this environment |
| **Parity with the original DSP** | ❌ not attempted; documented as unknown |

Known engine limits: the detector range is 55 Hz to 1.2 kHz; Width on its own raises the level by up
to 1.3 dB (the added side signal; the mono sum is unchanged); below about 130 Hz the bench shows 2 to 3.5
cents of residual error; polyphonic
material is out of scope; formant behaviour is still unvalidated (measurement tooling).

---

## Licensing

- Project code: see `LICENSE` if present; otherwise all rights reserved by the author.
- **JUCE** — dual-licensed AGPLv3 / commercial. Building or distributing binaries requires
  compliance with one of those licences. This is the main release consideration.
- **Signalsmith Stretch** and **Signalsmith Linear** — MIT; vendored under
  `app/vst3/third_party/` with their licences and pinned revisions
  (`88061fd4…` and `7bb1c8f3…`, tag 0.6.6).

---

## Roadmap

1. A formant estimator that classifies known-drift fixtures correctly, then re-score
   TD-PSOLA and Signalsmith at ±3/±6 semitones.
2. Verify host latency re-compensation when switching engines during playback.
3. DAW compatibility matrix (Logic, Ableton, Reaper, Cubase) and long-session stability.
4. Developer ID signing + notarization for public distribution.

---

*Built with the DeepSeek Harness, JUCE 8.0.15 and Signalsmith Stretch. Every numeric claim
above is reproducible from the scripts in `app/vst3/verify/`.*
