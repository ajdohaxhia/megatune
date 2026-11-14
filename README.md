# ademusic MegaTune

A clean-room reconstruction of the **Slate Digital MetaTune** vocal tuner: a JUCE VST3 +
AUv2 plug-in with a live pitch trace, a diagnostic "Why this note?" history, three
selectable pitch engines, factory and user presets, a doubler and a stereo widener.

> **Status: 0.6.2.** 0.6.2 reports a real tail length to the host (see
> [What changed in 0.6.2](#what-changed-in-062)); clicks are **still reported in Logic** and
> their cause is not yet found. 0.6.1 removes clicks that 0.6.0 introduced at note changes, found on the
> first real vocal take run through the plug-in; see [What changed in 0.6.1](#what-changed-in-061).
> 0.6.0 itself was more engine work: steady notes now land exactly on pitch, downward
> shifts actually reach the lower note, Stereo Width no longer changes the level, and
> formant preservation has been measured; see [What changed in 0.6.0](#what-changed-in-060).
> Prebuilt macOS binaries pass `ctest`, `auval`, the VST3 host check and pluginval at
> strictness 8. It has had **one listening pass, on one take** ("sounds good, but there are clicks"; the
> clicks are what 0.6.1 fixes, and the fix has been measured but not yet listened to). Most
> numbers here still come from synthetic signals. The factory presets are **not tuned by ear** and
> builds are **ad-hoc signed, not notarized**. See [Status](#status) before shipping anything.

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
| Signalsmith | **5760 samples / 120 ms** | experimental; formant compensation is switched off because it was measured to cancel the pitch shift |

Declared latency matches measured impulse onset and cross-correlation lag at 44.1 and
48 kHz. A live engine switch republishes the correct latency on the next
`prepareToPlay`; whether a specific DAW re-reads compensation mid-session is **unverified**.

---

## What changed in 0.6.2

Clicks were still reported with 0.6.1 in Logic Pro. They do not reproduce offline: the same
take rendered through the installed AU in a JUCE host is clean. One host-side defect was
found and fixed; whether it is the one being heard is **not confirmed**.

- **Tail length.** The plug-in holds its latency's worth of audio (43 ms in Quality) plus the
  Doubler's delay lines, but reported a tail of zero. A host that stops calling a plug-in as
  soon as its input ends would cut that audio off and get it back, stale, when processing
  resumes. The tail is now the current latency plus 60 ms.
- `MegaTuneBench` prints the slowest single call as a fraction of the time that block
  represents. On the take at 44.1 kHz: 39 to 45 % at 32-sample blocks, 23 % at 64, under 12 %
  from 128 up. The average load is a little over 1 % of a core, so the cost is spiky at small
  buffers.

Other things measured on the take that could be heard as clicks and are not plug-in bugs:
it reaches full scale in 29 places, and after processing the output exceeds it (28 places
with the Radio Ready preset; +2.1 dB with Doubler 50 and Width 60), so an output at unity
gain clips.

---

## What changed in 0.6.1

The first real vocal take through 0.6.0 sounded right but clicked. Measured on that take
(150 s, 44.1 kHz), the output had level holes of about 8 ms followed by an abrupt return,
three to six times per pass, always where a note started or changed.

| Cause | Fix |
|---|---|
| 0.6.0 stopped dividing the grain sum by the window sum so that downward shifts would work. That also removed what had been hiding every scheduling irregularity: wherever grains of different sizes met, the level dipped. | The division is back for everything except downward shifts, where the divisor is floored and the floor rises with the size of the shift. Octave-down still works (66 % of the energy on the old harmonics, down from 85 %). |
| A stretch with no grain at all was silence. | It is filled with dry signal again (not for deep downward shifts, where such gaps are intended). |
| When the detected period dropped (a jump to a higher note, or the detector leaving a sub-harmonic), the next grain was still scheduled one *old* period ahead. | It is brought forward to one new period from now. |
| When the period grew, the next grain should already have started and was cut off mid-window. | Its rising half is squeezed into the time that is left, so it starts from zero. |
| The first frames of a note often report a sub-harmonic or an in-between value (on the take: 57 Hz, then 95 Hz, before settling on 356 Hz), and the tuner acted on them. | A pitch is only acted on once two consecutive frames agree within a semitone; the detector prefers the shortest period among near-equal candidates; and when the octave-slip filter overrides a pitch, the shifter gets the matching period. |

On the take, sharp edges the input does not have (output second difference more than twice
the input's within 8 ms):

| Setting | 0.5.0 | 0.6.0 | 0.6.1 |
|---|---|---|---|
| Default | 1 (2.2x) | 1 (4.2x) | 0 (1.1x) |
| Hard tune, chromatic | 0 (1.8x) | 0 (1.3x) | 0 (1.1x) |
| Speed 90 | 1 (2.1x) | 0 (1.3x) | 0 (1.2x) |
| Low Latency | 1 (2.2x) | 0 (1.7x) | 0 (1.1x) |
| Radio Ready preset | 2 (3.2x) | 1 (4.1x) | 0 (1.2x) |

Also clean with host-style varying block sizes (1 to 512 samples per call) and on a mono
bus. This is one take and an offline render; whether it matches what was heard in the DAW
needs a listen. `verify/clicks.py` runs a high-frequency burst check on synthetic signals or
on a recording (`clicks.py <bench> <workdir> <mono float32 file> <rate>`); the bench accepts
`file=` and `varblock=1` for this. The take itself is not in the repository.

The cost: on the synthetic bench the vibrato vowel at Speed 0 reads 1.0 cents instead of 0.4.

---

## What changed in 0.6.0

Engine only.

| Problem | Fix |
|---|---|
| **A steady note was still 0.4 cents off.** The whole-sample lag for each pitch mark could be chosen one sample away from the true correlation peak, and the sub-sample fit was then clamped short. | The search steps onto the raw peak before fitting. Steady tones from 97 to 470 Hz now measure 0.00 cents of bias. |
| Pitch marks jittered on low notes and at 96 kHz. The correlation window was rectangular, so whatever sat at its two edges moved the peak. | Tapered window. On the bench the 110 Hz vowel goes from 3.1 to 1.5 cents. |
| **Shifting down by an octave did almost nothing.** Where grains do not overlap, the sum was divided by the window sum, which rebuilt the periods the window had removed: 99 % of the output energy stayed on the original harmonics. | No division where grains do not overlap. The octave-down output is now standard TD-PSOLA: about 15 % of the energy on the new harmonics, 85 % still on the old ones. It is lower, but thin. |
| Stereo Width made the signal up to 1.3 dB louder. | A gain that depends only on the Width and Doubler settings undoes the added energy: within 0.08 dB of the dry level on the bench. **Trade-off:** the mono sum used to be identical to the dry signal and is now the dry signal times one gain (0.86 at full Width). Nothing is added to it and nothing rides the signal. |

### Formant preservation, measured

`app/vst3/verify/formant_bench.py` shifts a vowel with a known spectral envelope and fits
how far the envelope moved. 1.00 means the formants stayed put; the last column is what a
plain resampling shifter would give.

| Shift | Quality | Low Latency | If not preserved |
|---|---|---|---|
| -3 st | 0.99 | 0.98 | 0.84 |
| +3 st | 1.02 | 1.02 | 1.19 |
| +6 st | 1.00 | 1.00 | 1.41 |
| +12 st | 1.00 | 1.00 | 2.00 |

At -6 and -12 the fit is too poor to quote (the output there is the uneven mix described
above). The Signalsmith engine does not fit well either except at +12, where it reads 2.01,
as expected from a shifter with formant compensation switched off. One synthetic vowel at
110 Hz is not a listening test.

### What the remaining error is

A finer meter (instantaneous frequency of the fundamental) shows 2 to 4 cents of fast
wobble on the vibrato test, but the *input* shows the same amount: it is the noise in the
test signal, not something the tuner adds. Below 15 Hz, where tracking error lives, the
output is 0.7 to 1.8 cents from the note.

### Also checked

- pluginval at strictness 8: VST3 and AU pass.
- White noise passes through bit-exact (nothing is mistaken for a note).
- A voice with heavy breath noise is still tuned (0.9 cents on the bench).
- I looked at making the Doubler drift randomly instead of on two sine waves. The two rates
  per voice already take about 100 seconds to repeat, so there was nothing measurable to
  fix, and I cannot judge its character without listening.

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
8.0.15 is the version the releases are built with).

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

Signing for distribution (requires an Apple Developer ID):

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
- engine switching republishes latency (2048 ↔ 5760 ↔ 512)
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
- Width alone widens a mono source, holds the level, and leaves the mono sum a scaled copy of the dry signal
- a steady off-pitch tone lands within 0.2 cents of the note
- a bypass toggle does not step the signal; the Doubler holds its level at every setting
- Speed reaches the default engine: a slow setting leaves more vibrato than a hard one
- 490 Hz disabled-target control (with and without Note Stabilizer)
- real-take target change: at 17.71 s of the dry fixture, excluding G retargets
  110/110 analysed blocks and Undo restores it bit-identically

pluginval at strictness 8 passes with all of its tests enabled (VST3 on 0.6.0; the AU was
last run on 0.5.0):

```bash
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 8 --validate MegaTune.vst3
```

Host-level checks (load, instantiate, parameters, audio, editor) run through
`app/vst3/host/`:

```bash
MegaTuneHostCheck /path/to/MegaTune.vst3
auval -v aufx Mgtn AdMs          # after installing the AU component
```

Measurement scripts live in `app/vst3/verify/` and drive the in-process `MegaTuneBench`
target:

| Script | Measures |
|---|---|
| `bench.py` | tuning error, Doubler level, stereo width and mono compatibility |
| `stress.py` | tuning error across sample rates, block sizes, mono buses and pitch extremes |
| `formant_bench.py` | whether the formants stay put when the pitch is shifted |
| `clicks.py` | high-frequency bursts the input does not have, on synthetic signals or a recording |

```bash
python3 verify/bench.py build-cli/MegaTuneBench_artefacts/Release/MegaTuneBench /tmp
```

The older `formant_harness.py`, `paired2.py`, `score.py` and `measure.py` are kept from
earlier audits.

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
    verify/            MegaTuneBench render bench (Bench.cpp) and the measurement scripts
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
| Pitch accuracy (synthetic vibrato, 0.6.1 bench) | ✅ 1.0¢ mean / 1.9¢ p95 at Speed 0; steady tone 0.0¢ |
| Speed / Sustain reach the default engine | ✅ regression test |
| Doubler / Width | ✅ level within 0.1 dB of dry at every setting on the bench; level and mono compatibility in the regression suite |
| Pitch accuracy (dry vocal) | ⚠️ last measured on 0.3.0 (5.63¢ median); not re-measured since, because the vocal fixture is not in the repo |
| Low latency | ✅ 512 samples (10.7 ms); tunes 200 Hz correctly |
| Large host blocks / sample-rate changes | ✅ 16384-sample block and 48 → 96 kHz verified |
| Audio-thread allocations in analysis | ✅ none after prepare |
| Signalsmith bypass / doubler / latency | ✅ verified through 768 kHz |
| Negative Speed / zero-Amount Shift | ✅ verified |
| Non-finite input / reset / channel safety | ✅ verified |
| Long-session engine/parameter churn | ✅ 4000 blocks verified |
| AUv2 | ✅ `auval -v aufx Mgtn AdMs` passes on 0.3.1 through 0.6.0 |
| pluginval, strictness 8 | ✅ VST3 and AU pass on 0.5.0, VST3 on 0.6.0 (one warning on the AU: current program reported as -1 after a program change) |
| macOS build and test suite | ✅ every release since 0.3.1 is built universal on macOS (JUCE 8.0.15); `ctest` and the VST3 host check pass |
| Key lock and user presets | ✅ regression test |
| Factory preset values | ⚠️ designed from the control ranges; not tuned by ear |
| Full parameter state round-trip | ✅ in-process regression |
| Why this note? → Exclude → Undo | ✅ verified on synthetic tone and on a real take |
| Presets, automation, recall, A/B | ✅ verified |
| Formant preservation | ✅ measured for -3 to +12 semitones on a synthetic vowel (spectral stretch 0.99 to 1.02; 1.00 is perfect). ⚠️ -6 and -12 are inconclusive: the fit is poor there |
| **DAW compatibility / listening test** | ❌ Logic Pro: sounds good but **clicks, still present in 0.6.1**; not reproduced offline, cause open |
| **Distribution signing** | ❌ ad-hoc only; notarization needs an Apple Developer ID |
| **Parity with the original DSP** | ❌ not attempted; documented as unknown |

Known engine limits: the detector range is 55 Hz to 1.2 kHz; with Width up, the mono sum is the dry
signal up to 1.3 dB quieter (the price of holding the stereo level); a full octave down is thin, with
about 15 % of the energy on the new harmonics; polyphonic
material is out of scope.

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

1. A listening pass on real vocals, and factory presets tuned by ear.
2. A formant measurement that also works for large downward shifts and for the Signalsmith
   engine, and a fuller-sounding octave-down shift.
3. Verify host latency re-compensation when switching engines during playback.
4. DAW compatibility matrix (Logic, Ableton, Reaper, Cubase) and long-session stability.
5. Developer ID signing + notarization for public distribution.

---

*Built with JUCE 8.0.15 and Signalsmith Stretch. Every numeric claim above is reproducible
from the scripts in `app/vst3/verify/`.*
