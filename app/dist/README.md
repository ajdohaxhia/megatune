# MegaTune 0.1.2 — VST3 (macOS, universal arm64 + x86_64)

Install: double-click `Install MegaTune.command` (or copy `MegaTune.vst3` to
`~/Library/Audio/Plug-Ins/VST3/`), then rescan plug-ins in your DAW.

0.1.2 is a visual redesign with layered cards, painted tick-ring knobs, styled
menus/toggles and a custom piano. All controls, parameter IDs and DSP behavior
are unchanged from 0.1.1.

## Engines
- **Quality** — 4-grain shifter, 2048 samples (42.7 ms @ 48 kHz)
- **Low Latency** — 2-grain shifter, 128 samples (2.67 ms @ 48 kHz)
- **TD-PSOLA (experimental)** — pitch-synchronous OLA, opt-in
- **Signalsmith (experimental)** — 120 ms, opt-in; Smith Comp toggles formant compensation

## Known limitations
- Ad-hoc signed, **not notarized**. Gatekeeper may block it on other Macs.
- Formant preservation is **unvalidated** (measurement tooling misclassifies known cases).
- Mono/stereo vocal tuning only; polyphonic material is out of scope.
- Verified headlessly; not yet tested inside your DAW.
