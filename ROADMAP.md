# OldSchoolBox — SOTA Roadmap & Status

Working doc for the "extreme SOTA in rigorous iterations" effort. Resume point.

**Current version:** v0.5.4 (VST3 + AU built & installed locally).

---

## Where we are (done & committed/pushed)

| Commit | Content |
|--------|---------|
| `3955def` | **Phase A + B.1** — acid 303 resonance + de-alias/velocity |
| `774f5e8` | **Phase B.2** — shaped envelopes + layered transients (snare, tom) |
| `138e5ec` | **Iteration 1** — FFT harness + oversampled 303 anti-aliasing |
| `630e2b7` | **Iteration 2** — 303 A/B harness vs real reference |
| `f577752` | bump v0.5.4 |

### Phase A — the 303 now resonates (`src/voices/BassVoice.h`)
- Zero-delay-feedback (TPT) 4-pole Moog ladder; gentle unity-slope feedback
  saturator → resonant peak forms, self-oscillates at k≈4 (acid squelch).
  (Old code clipped `fastTanh(fb*k)` with k≈9 → no resonance.)
- Cutoff unclamped 6.2k → 18k (top of the pot was dead).
- Dedicated **accent resonance envelope** (~180ms, accumulates on legato) + a
  **dynamic makeup** that restores body lost to high reso. Accent HF bark ~4x,
  peaks bounded.

### Phase B — drum character
- **Hats**: 6 squares now band-limited (polyBLEP + Nyquist guard); alias −3.8x.
- **Velocity → timbre** on all 6 voices (was gain-only), normalized so vel=1.0
  reproduces the prior sound.
- **Snare/Tom**: shaped attack→body envelopes + velocity-driven layered
  transients (snare thwack+crack, tom punch + 2nd-harmonic thunk).

### Iteration 1 — measurement foundation + 303 anti-aliasing
- `tests/SpectralProbe.h`: radix-2 FFT, magnitude spectrum, centroid, FIR
  decimation, alias-vs-reference comparison. **Reusable harness.**
- `src/voices/AntiAlias.h`: biquad LP + 96-tap windowed-sinc FIR decimator.
- BassVoice oscillator + full nonlinear chain now run 4× oversampled, FIR-decimated.
- **Honest finding:** the 303's earlier "−35 dB alias" was a window-leakage
  artifact, not real aliasing. Kick measures clean (−54.7 dB). Oversampling kept
  as correct/defensive DSP.

### Iteration 2 — 303 A/B vs real reference
- `tests/SpectralProbe.h`: WAV loader (16/24/32-bit), fundamental detection.
- `tests/ref303_probe.cpp`: A/B vs a real "One Note Sweep" 303 recording.
- **Reliable finding:** our 303 is **DARKER** than the real unit — centroid tops
  ~2.5 kHz vs reference ~3.6 kHz.
- **Honest limitation:** a spectral "resonance prominence" metric was attempted
  twice and both versions conflated saw harmonic rolloff with true resonance →
  marked EXPERIMENTAL, not gated. Trustworthy spectral measures: centroid, f0.

### Reference assets (local only — `references/` is gitignored, do NOT commit)
- `references/kick1..3.wav` — user-provided kick refs (Iteration 3).
- `references/303/` — royalty-free MusicRadar 303 pack (One Note Sweeps,
  Basslines, +FX, High Arps). "One Note Sweeps Dry" = filter-sweep refs.

### Tests (all green; in CMake)
`acid303_probe`, `kick_probe`, `hats_probe`, `aa_probe`, `ref303_probe`.

---

## Key lessons (so we don't repeat them)
1. **Absolute spectral metrics confound easily** on a resonant nonlinear voice
   (window leakage; SR-dependent tuning; thermal drift; saw rolloff vs
   resonance). Burned real time on this twice.
2. **What to trust:** centroid (brightness), f0, regression golden files, and
   **ears + reference WAVs**. The final SOTA judgement for an analog emulation
   is ear-based; the harness supports it, it doesn't replace it.
3. Plugin builds clean (VST3+AU, 0 code warnings); installed for Ableton A/B.

---

## Recalibrated plan

### Iteration 2 (finish) — 303 fidelity, ear-guided
- [ ] **USER**: Ableton A/B — OldSchoolBox vs `references/303/One Note Sweeps/`.
      Report: resonance character (too thin/sine-like? too aggressive? right?),
      confirm "darker than real" by ear.
- [ ] **ME**: brightness fix (mine darker than ref — verifiable via centroid,
      low risk), then resonance-character tuning per ear feedback.

### Iteration 3 — drum engine SOTA
- A/B drums vs `references/kick*.wav` (need 808/909 refs for snare/hat/clap).
- Velocity layers, vary the per-voice noise seed (currently fixed → mechanical
  repetition), extract magic constants into macro params (tone/punch/character).

### Iteration 4 — master chain & loudness SOTA
- True-peak limiter + look-ahead + oversampling; LUFS + true-peak metering;
  oversampled master drive; verify (then fix) GlueCompressor detector;
  reverb pre-delay + real stereo input.

### Iteration 5 — robustness, performance, lock-in
- RT-safety audit (audio thread alloc/lock); CPU budget (the 96-tap FIR
  decimator × 2 bass voices needs a perf check / possible halfband cascade);
  full preset/state-recall coverage; golden-file spectral regression in CI;
  final A/B sign-off.

---

## Tomorrow — first concrete steps
1. USER: ear A/B (above) and tell me the resonance/brightness verdict.
2. ME: implement the brightness fix (centroid-verified), then tune resonance to
   the ear verdict.
3. (Optional) grab 808/909 reference samples to set up Iteration 3.
