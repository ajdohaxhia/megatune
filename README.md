# MegaTune

A clean-room reconstruction of the **Slate Digital MetaTune** vocal tuner: a JUCE VST3
plug-in with a live pitch monitor, a diagnostic "Why this note?" history, four selectable
pitch engines, factory presets and a fully themed editor.

> **Status: experimental.** Pitch correction, latency reporting, presets, automation and
> the exclude/undo workflow are verified. **Formant preservation is unvalidated** and the
> build is **ad-hoc signed, not notarized**. See [Status](#status) before shipping anything.

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
| Pitch engines | Quality (4-grain OLA), Low Latency (2-grain), TD-PSOLA*, Signalsmith Stretch* |
| Controls | Speed, Sustain, Amount, Doubler, Stereo Width, Key, Scale, Shift, Range, Pitch Reference (A4) |
| Keyboard | One octave, scale membership dots, live input outline, target fill |
| Pitch monitor | Input → Target with signed cents and a ±100¢ meter |
| Diagnostics | "Why this note?" history of recent decisions with a one-click Exclude/Enable action |
| Presets | 16 factory programs with per-preset key and scale |
| Host integration | Preset recall, parameter automation, Undo/Redo, A/B comparison, bypass |

\* experimental, opt-in.

---

## Engines and latency

| Engine | Latency @ 48 kHz | Notes |
|---|---|---|
| Quality | **2048 samples / 42.7 ms** | default; 4-grain overlap-add with cubic interpolation |
| Low Latency | **128 samples / 2.67 ms** | 2-grain crossfade; only corrects notes above ~375 Hz |
| TD-PSOLA (experimental) | 2048 samples | pitch-synchronous overlap-add |
| Signalsmith (experimental) | **5760 samples / 120 ms** | with optional formant compensation |

Declared latency matches measured impulse onset and cross-correlation lag at 44.1 and
48 kHz. A live engine switch republishes the correct latency on the next
`prepareToPlay`; whether a specific DAW re-reads compensation mid-session is **unverified**.

---

## Screenshots

| Idle | Active | History |
|---|---|---|
| ![](app/vst3/build-cli/MegaTune-vst3-idle.png) | ![](app/vst3/build-cli/MegaTune-vst3.png) | ![](app/vst3/build-cli/MegaTune-vst3-history.png) |

---

## Install

Prebuilt universal binary (arm64 + x86_64):

```bash
# from app/dist
open "Install MegaTune.command"     # or:
cp -R MegaTune.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

Then rescan plug-ins in your DAW. Logic/GarageBand only host AU; this build is **VST3
only**.

Uninstall: delete `~/Library/Audio/Plug-Ins/VST3/MegaTune.vst3`.

---

## Build from source

Requirements: macOS, CMake ≥ 3.22, a C++17 toolchain, JUCE 8.0.x sources.

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
xcrun notarytool submit MegaTune.zip --keychain-profile … --wait
xcrun stapler staple MegaTune.vst3
```

---

## Testing

```bash
cd app/vst3/build-cli && ctest --output-on-failure     # in-process DSP + editor suite
```

The suite covers, among others:

- parameter landmarks from the recovered maps (Speed curve, Amount curve, defaults)
- state round-trip, bypass, latency-aligned dry path, preset navigation, A/B, Undo/Redo
- 16 presets render finite audio; per-preset key/scale application
- Range limiting, breath/uviolation gate, stereo-linked detection
- engine switching republishes latency (2048 ↔ 5760 ↔ 128)
- **Why this note?** end-to-end: 432 Hz tone A4 → exclude A → G♯4 → Undo → A4
- 490 Hz disabled-target control (with and without Note Stabilizer)
- real-take target change: at 17.71 s of the dry fixture, excluding G retargets
  110/110 analysed blocks and Undo restores it bit-identically

Host-level checks (load, instantiate, parameters, audio, editor) run through
`app/vst3/host/`:

```bash
MegaTuneHostCheck /path/to/MegaTune.vst3
```

Measurement harnesses used during development live in `app/vst3/verify/`
(`formant_harness.py`, `paired2.py`, `score.py`, `measure.py`).

---

## Architecture

```
app/
  src/                 Rust/egui standalone prototype (historical)
  vst3/
    Plugin.cpp         processor + editor (single translation unit)
    TuningRules.h      recovered parameter curves (Speed, Amount, smoothing)
    TuningDsp.h        YIN detector, TD-PSOLA, helpers, diagnostic trace
    Tests.cpp          in-process regression suite
    CMakeLists.txt     JUCE plug-in target + tests
    build.sh           build + test + package + ad-hoc sign
    host/              VST3 host check, WAV renderer, latency probe
    verify/            measurement scripts and exported renders
    third_party/       vendored Signalsmith Stretch + Linear (MIT)
  dist/                installer, universal VST3, README
  MECHANICS.md         recovered semantics, evidence and open questions
```

### Audio path

1. Input is summed to mono for analysis, so both channels always share one correction target.
2. YIN detects F0 with aperiodicity; aperiodic frames pass through untouched (breath gate).
3. The nearest **enabled** scale degree becomes the target; the decision and its reason are
   recorded in a lock-free 16-entry ring.
4. Speed/Sustain shape a one-pole smoothing of the correction ratio.
5. The selected engine renders the shifted signal; Doubler and Width are applied after.

---

## Status

| Item | State |
|---|---|
| Pitch accuracy (synthetic vibrato) | ✅ 1.4¢ median / 1.4¢ p95 |
| Pitch accuracy (dry vocal) | ✅ 5.63¢ median, jumps 4.17 % |
| Low latency | ✅ 128 samples verified |
| Why this note? → Exclude → Undo | ✅ verified on synthetic tone and on a real take |
| Presets, automation, recall, A/B | ✅ verified |
| **Formant preservation** | ❌ **unvalidated** — the estimator misclassifies known test cases |
| **DAW compatibility** | ❌ not tested in a real host |
| **Distribution signing** | ❌ ad-hoc only; no Developer ID in this environment |
| **Parity with the original DSP** | ❌ not attempted; documented as unknown |

Known engine limits: Low Latency cannot correct notes below roughly 375 Hz (its window is
shorter than the period); polyphonic material is out of scope; the experimental engines are
opt-in until formant behaviour can be measured.

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
