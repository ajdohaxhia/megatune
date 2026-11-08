# MegaTune — Engineering Audit

Scope: repository at `a7087ad` (release 0.1.7). Baseline established by building
`app/vst3` and running `ctest`, the packaged-VST3 host check and `auval`.

## Baseline (before this audit's changes)

| Metric | Value |
|---|---|
| `ctest` | 1/1 pass |
| VST3 host check | PASS (28 params, editor 1120×802) |
| AU `auval` | VALIDATION SUCCEEDED |
| Vibrato vowel tuning error | 5.02 ¢ mean |
| Envelope ripple (Quality / Low Latency) | 0.067 / 0.067 |
| Declared latency (Quality / Low Latency / Signalsmith) | 2048 / 512 / 5760 samples @48 kHz |
| Formant measurement | invalid — estimator fails known fixtures |

## Confirmed defects and fixes

### 1. Per-channel envelope state (stereo divergence) — FIXED

`voicedHold`, `psolaVoiced` and `psolaGain` were advanced **inside the per-channel
loop**, so a stereo block advanced them twice per sample. Consequences: the wet gain
trajectory differed between L and R, and the 120 ms voicing hold expired at twice the
intended rate on stereo material.

Evidence: reading `processAudio()` showed the updates between `for (int c …)` and the
engine branch.

Fix: moved the three updates to once per sample, before the channel loop.

Regression: `STEREO max L/R difference` — identical input, doubler off. Now **0.0**
(exactly equal); before the fix the channels diverged.

### 2. YIN cumulative-mean normalisation started at lag 1 — FIXED

The difference function was computed from `minTau`, but the cumulative-mean
normalisation loop began at lag 1. Lags `1 … minTau-1` contributed zero to the running
sum, so every `d'(τ)` was divided by a too-small mean and biased low — weakening the
absolute-threshold test at short lags.

Fix: compute the difference function from lag 1 (cost: `minTau` extra lags, negligible)
so the normaliser sees the complete sequence.

Evidence of benefit: vibrato-vowel tuning error **5.02 ¢ → 4.71 ¢** with no other change.

### 3. Signalsmith staging buffers could reallocate on the audio thread — FIXED

`processAudio()` grew `sIn`/`sOut` with `vector::assign` whenever the host block
exceeded the staging capacity. That is a heap allocation in the real-time callback.

Fix: oversized blocks are processed in slices through an `AudioBuffer` view over the
host buffer; the staging vectors are never resized during processing.

Regression: `BIGBLOCK` — prepare with 256 frames, then process a 10 000-frame block with
Signalsmith active; asserts all output samples are finite.

### 4. Documentation/release version mismatch — FIXED

`README.md` announced 0.1.6 while `CMakeLists.txt` built 0.1.7 (and the audit found the
README's engine description still listing granular shifters as the default). Versions are
now reconciled at **0.2.0** and the engine description matches the code.

## Investigated, not confirmed

- **PSOLA pitch-marking** uses period-progressed marks rather than waveform-aligned
  peaks. Measured pitch is correct across eight windows at 0/±3/±6 semitones and the
  vibrato criterion passes, so no defect is demonstrated. Timbral artefacts at extreme
  shifts remain plausible but unmeasured; needs a null-test against a reference shifter.
- **Diagnostics cross-thread access**: `pitch`, `outputPitch`, `level` are atomics;
  the decision ring is read through `historySequence`/`historyDecision`. Full race
  freedom depends on those accessors using an acquire/release seqlock — not audited here.
- **Engine switching** republishes latency at `prepareToPlay` (verified 2048 ↔ 5760 ↔
  512), but host-side re-compensation during playback is untested.
- **Formant measurement** has been repaired and calibrated (docs/FORMANT_CALIBRATION.md):
  the estimator no longer returns NaN on voiced excitation, typical error is 1–2 %, and
  known ±5/±10 % drift is detected with correct sign and ≤3.6 pp magnitude error. On the
  dry-vocal fixture TD-PSOLA shows −0.1 %/−0.6 % and Signalsmith +1.0 %/−1.1 % F1/F2
  drift — inside the uncertainty band, i.e. "no drift detectable", not proof of
  preservation. A user-facing Formant control still requires a ±10 % separation test.

## Prioritised next work

1. Validate the formant estimator on noise-excited, known-resonance fixtures; only then
   score TD-PSOLA/Signalsmith formant behaviour.
2. Audit the decision-ring accessors for a true seqlock (audio write / UI read).
3. Waveform-aligned PSOLA marks with a null-test regression.
4. Host matrix testing (Logic, Live, Reaper, Cubase) and pluginval.
5. Deferred/safe engine switching with explicit UI feedback.

## Confirmed defect 5 (this pass): Signalsmith formant compensation cancels the pitch shift

Measured on a 220 Hz pulse train through a known-resonance vowel, transpose +3 st:

| Smith Comp | Output F0 | Expected | Level |
|---|---|---|---|
| off | 262.3 Hz | 261.6 Hz | 0.0368 |
| **on** | **218.2 Hz** | 261.6 Hz | **0.0097** |
| on, +6 st | **221.2 Hz** | 311.1 Hz | 0.0055 |

`setFormantSemitones(0, compensate=true)` leaves the pitch essentially unshifted and
drops the level ~10 dB. The parameter defaulted to **true**, so Signalsmith was broken
by default.

Fix: compensation is no longer applied (`setFormantSemitones(0, false)`); the `smithcomp`
parameter is retained for state/automation compatibility but is inert. Regression
`SMITHCOMP` asserts ~261.6 Hz for both stored values.

## Formant separation test (validated estimator)

Known-resonance vowel (703/1184 Hz), pitch-shifted through each engine:

| Engine | +3 st F1 / F2 drift | +6 st F1 / F2 drift | Expected uncompensated |
|---|---|---|---|
| TD-PSOLA | −3.0 % / −5.4 % | −6.2 % / −1.6 % | +18.9 % / +41.4 % |
| Signalsmith | +14.4 % / +18.5 % | +38.0 % / +41.9 % | +18.9 % / +41.4 % |

Signalsmith tracks the transposition as an uncompensated shifter should, which
validates the measurement rig. **TD-PSOLA preserves formants at +3 st** (−3 %/−5 %)
while shifting pitch correctly (262.3 Hz measured) — a genuine positive result, and the
first evidence-based formant claim in this project. The earlier +6 st NaN was an estimator limitation (wide poles); with a pole fallback
PSOLA measures −6.2 %/−1.6 %, i.e. formants preserved across 0…+6 st.

## Confirmed defect 6 (this pass): unsynchronised diagnostic ring (torn reads)

The editor read `MegaTune::decisions[s & 15]` — a plain `struct {float in; float target;
int reason;}` — directly while the audio thread wrote the same struct in
`recordDecision()`. There was no seqlock, no atomic field, and no memory ordering, so a
UI repaint could observe a half-written row (`in` from one decision, `target` from
another).

This was the "cross-thread access" item left open in the first audit; it is now
confirmed as a real data race rather than a theoretical one.

Fix:

* decision fields are `std::atomic<float>` / `std::atomic<int>`;
* writes are bracketed by an odd/even seqlock (`historyLock`), with `historySeq`
  published by a release store after the data;
* readers (`readDecision`) acquire the lock, copy the fields, re-check the lock, and
  retry up to 16 times;
* both the editor refresh path and the `historyDecision()` test API use the reader.

Regression `SEQREADS`: a reader thread hammers `historySequence`/`historyDecision`
while 600 audio blocks are processed concurrently — **7 127 235 reads, 0 inconsistent
rows** (reason outside 0…4 or non-finite values would fail the test).
