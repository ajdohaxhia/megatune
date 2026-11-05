# ademusic MegaTune 0.1.6 — VST3 + AUv2 (macOS, universal arm64 + x86_64)

Install: double-click `Install MegaTune.command` (or copy `MegaTune.vst3` to
`~/Library/Audio/Plug-Ins/VST3/` and `MegaTune.component` to
`~/Library/Audio/Plug-Ins/Components/`), then rescan plug-ins in your DAW.

Use the VST3 in Ableton, Reaper, Cubase or Studio One. Use the AUv2 component in
Logic or GarageBand. Both formats share the same editor, presets and DSP.

## Engines
- **Quality** — 4-grain shifter, 2048 samples (42.7 ms @ 48 kHz)
- **Low Latency** — TD-PSOLA, 512 samples (10.7 ms @ 48 kHz)
- **TD-PSOLA** — pitch-synchronous OLA (runtime default)
- **Signalsmith** — 120 ms; Smith Comp toggles formant compensation

## Known limitations
- Ad-hoc signed, **not notarized**. Gatekeeper may block it on other Macs.
- Formant preservation is **unvalidated** (measurement tooling misclassifies known cases).
- Mono/stereo vocal tuning only; polyphonic material is out of scope.
- Verified headlessly; not yet tested inside your DAW.
