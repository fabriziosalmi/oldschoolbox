# Changelog

All notable changes to OldSchoolBox are documented in this file.

## [0.6.1] - 2026-11-26

### Added
- Repository hygiene and governance baseline (`SECURITY.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, GitHub Actions macOS CI).
- Clean single-commit public history release.

## [0.6.0] - 2026-11-26

### Added
- **303 Diode Ladder (18 dB/oct)**: asymmetric square shaper, sub-bass resonance compensation, non-slid attack phase reset, and BASS1/BASS2 role split.
- **Underground Chaos Sample Bus**: 8 procedural Tekno DNA one-shots, lock-free custom WAV/AIFF loader (max 8s, drag & drop), 400 Hz 24 dB/oct HPF + megaphone bandpass, Akai S950 SR reducer/grit, bar-aware triggering, rising-pitch FILL stutter, 1/16 Euclidean auto-gate, kick ducking, auto-panning, and momentary DUB THROW.
- **Master Sound System Chain**: 2x oversampled Mackie 1202 Drive, M/S Mono-Maker (< 120 Hz), FX wet HPF @ 250 Hz + kick ducking, Bipolar DJ Isolator (WASH), Momentary KILL SUB (< 150 Hz), per-sample smoothing, and -0.3 dBTP True-Peak Limiter.
- **DJ & Live Performance**: Quantized Scenes & Mutes (`QNT: 1B / 1/4 / OFF`), Elektron-style Snap-Back `REVERT`, `Alt+Drag` Spring-Back Tweaking, 1-Click MIDI CC Learn, Swipe-to-Paint sequencer, true polyrhythms (`1..64` steps), Acid Scale Lock (`PHR23`/`DMIN`/`HYP2`/`DIM`), Full-QWERTY live control, and zero-dialog 4-bar DAW Drag & Drop (`WAV>` / `MID>`).
- **MKII Hardware UI (60 FPS)**: Recessed CRT bezels, 4-family 23 Underground palette with `RAVE`/`CRT`/`AMB` live theme switcher, Ghost Dots, Live Modulation Rings, real Engineering Units HUD, optical LED persistence, ambient light bleed, hover spotlight, phase-locked CRT oscilloscope, 350 ms CRT power-on sequence, and 70%-200% scaling.
- **Standalone macOS App** + Multi-out routing (1 Main + 8 per-voice aux stereo buses) + sample-accurate MIDI Out + Undo/Redo.
