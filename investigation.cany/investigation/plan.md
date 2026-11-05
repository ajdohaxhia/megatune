# Plan

1. [done] Open original main program in Deep mode and trace strings/references — Original universal executable and exact ARM64 slice opened; message/control references retained.
2. [done] Read relevant pseudocode and corroborate control arithmetic — Awaited each pseudocode request; Ghidra unavailable, fallback FP inline assembly checked against raw ARM64 and dispatch tables.
3. [done] Apply recovered control curves, defaults and sustain direction — Both builds use recovered Speed/Amount mappings; width default and sustain direction corrected. Regression tests passed after RED.
4. [done] Rebuild and verify outputs — 11 Rust tests, offline build, VST3 CTest/signature and Craft Anything rebuild_check pass.
5. [blocked] Recover exact original pitch detector and DSP timing — No successful Ghidra decompilation; fallback insufficient to establish proprietary DSP. Remaining formulas explicitly estimated in app/MECHANICS.md.
