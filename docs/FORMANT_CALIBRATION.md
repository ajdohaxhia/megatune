# Formant Measurement — Calibration and First Valid Results

The previous formant pipeline could not be trusted: it returned `NaN` for
periodic (voiced) excitation and its pole filter discarded valid poles. This
document records the repaired estimator, its accuracy against known-resonance
fixtures, and the first measurements taken with it.

## Fixtures

* Generator: cascade of 2-pole resonators (`formant_harness.resonator`).
* Ground truth: **peaks of the cascade impulse response**, not the design
  parameters, so the estimator is scored against the actual filter.
* Vowels: V1 = (700, 110), (1200, 150), (2600, 250);
  V2 = (400, 80), (2200, 200), (3000, 300).
* Excitation: white noise (aperiodic) and pulse trains at
  F0 = 155.6 / 185.0 / 220.0 / 261.6 / 311.1 Hz (periodic).

## Repair

The old estimator required **three** valid poles per frame, so voiced frames with
two well-defined formants were discarded and the median became `NaN`. It also
filtered poles at `bw < 600` and used an unregularised Levinson recursion that
could return non-finite coefficients.

Current estimator (`formant_harness.lpc_poles` / `formants_of`):

* order-12 LPC, Hamming-windowed 2048-sample frames, 50 % hop
* Levinson–Durbin with ridge regularisation and a stability guard
  (`|k| < 0.9999`, finite coefficients)
* poles kept for `150 < F < 4000` and `bw < 700`
* median of F1/F2 across frames; requires **two** poles, not three

## Accuracy (zero drift)

| Excitation | F1 error | F2 error |
|---|---|---|
| Noise (V1) | −11 Hz (−1.6 %) | +8 Hz (+0.7 %) |
| Noise (V2) | +8 Hz (+2.0 %) | +23 Hz (+1.0 %) |
| Pulse 155.6 Hz | −9 Hz | +16 Hz |
| Pulse 185.0 Hz | +13 Hz | +9 Hz |
| Pulse 220.0 Hz | −26 Hz | +10 Hz |
| Pulse 261.6 Hz | +29 Hz | +19 Hz |
| Pulse 311.1 Hz | −49 Hz | +49 Hz |

Worst case ≈ 7 % at the highest pulse rate; typical error ≈ 1–2 %.

## Drift detection (pulse F0 = 220 Hz, whole-vowel shift)

| True drift | Measured F1 | Measured F2 | Error (pp) |
|---|---|---|---|
| −10 % | −8.0 % | −7.5 % | +2.0 / +2.9 |
| −5 % | −6.1 % | −4.8 % | −1.1 / +0.2 |
| +5 % | +1.4 % | +7.8 % | −3.6 / +2.4 |
| +10 % | +9.0 % | +11.3 % | −0.9 / +0.9 |

**Sign is correct in every case.** Magnitude error is ≤ 3.6 pp, which means a
**10 % drift is reliably detected**, while **5 % sits at the edge of the
resolvability** of this method. Any engine score must be reported with that
uncertainty.

## First engine measurements (dry vocal fixture, chromatic)

| Engine | F1 | F2 | Drift vs baseline |
|---|---|---|---|
| baseline | 553 Hz | 3839 Hz | — |
| TD-PSOLA | 553 Hz | 3818 Hz | **−0.1 % / −0.6 %** |
| Signalsmith | 559 Hz | 3798 Hz | **+1.0 % / −1.1 %** |

Both engines show **no detected formant drift** on this fixture at the tested
shift; the figures are inside the ±3.6 pp measurement uncertainty, so the correct
reading is "no drift detectable", not "formant preservation proven". F2 near
3.8 kHz should be treated cautiously — it is close to the analysis band edge.

## What this enables

A defensible formant claim now requires: a fixture with a known ±10 % shift, the
engine's measured drift must separate from zero by more than the 3.6 pp error
band. That test is the next step before any user-facing Formant control is
exposed.
