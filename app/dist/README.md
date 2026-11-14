# ademusic MegaTune 0.6.2 — VST3 + AUv2 (macOS, universal arm64 + x86_64)

Install: double-click `Install MegaTune.command`. It copies `MegaTune.vst3` to
`~/Library/Audio/Plug-Ins/VST3/` and `MegaTune.component` to
`~/Library/Audio/Plug-Ins/Components/`, moving any existing copy to the Trash. The build is
not notarized: if macOS refuses to open the script, right-click it and choose Open, or copy
the two bundles by hand. Then rescan plug-ins in your DAW.

Use the VST3 in Ableton, Reaper, Cubase or Studio One. Use the AUv2 component in
Logic or GarageBand. Both formats share the same editor, presets and DSP.

## Engines
- **Quality** — TD-PSOLA, 2048 samples (42.7 ms @ 48 kHz)
- **Low Latency** — TD-PSOLA, 512 samples (10.7 ms @ 48 kHz)
- **Signalsmith** — about 120 ms, experimental

## Presets
- 32 factory sounds. Click one in the browser to hear it; Done keeps it, Cancel restores
  your sound.
- "Keep my key and scale" is on by default, so presets do not change the song's key.
- Your own presets are saved in `~/Library/Application Support/ademusic/MegaTune/Presets`.

## Known limitations
- Ad-hoc signed, **not notarized**. Gatekeeper may block it on other Macs.
- Formant preservation is measured on a synthetic vowel only.
- Mono/stereo vocal tuning only; polyphonic material is out of scope.
- Validated with `ctest`, `auval` and a headless VST3 host; not yet auditioned in a DAW.
