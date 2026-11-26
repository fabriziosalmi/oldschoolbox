#pragma once
#include <algorithm>
#include <cmath>
#include "DspConstants.h"
#include "AntiAlias.h"

// TB-303-inspired acid voice with:
// - HP in feedback loop (303 reso loses bass)
// - 4× oversampling (cleaner resonance/drive harmonics)
// - Accent shortens decay (circuit side-effect)
// - Capacitor accumulation (envelope doesn't reset on legato)
// - Asymmetric slide (up faster, down slower — RC circuit behavior)
// - Gate click transient (VCA DC bump)
// - VCA bleed (subtle oscillator leakage when "off")
// - Post-filter waveshaping + DC blocker
struct BassVoice
{
    struct Params
    {
        float cutoff  = 800.0f;
        float reso    = 0.15f;
        float envMod  = 0.20f;
        float decay   = 0.30f;
        float accent  = 0.00f;
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        osRate = sr * 4.0;  // 4× oversampling for acid resonance/drive
        releaseCoeff = static_cast<float>(std::exp(-1.0 / (0.015 * sr)));
        smoothAlpha  = 1.0f - static_cast<float>(std::exp(-1.0 / (0.005 * sr)));
        glideAlphaUp   = 1.0f - static_cast<float>(std::exp(-1.0 / (0.045 * sr)));  // ~45ms RC slide up
        glideAlphaDown = 1.0f - static_cast<float>(std::exp(-1.0 / (0.065 * sr)));  // ~65ms RC slide down
        fbHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 150.0 / osRate));
        sqrHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 15.0 / osRate));
        subCompLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 120.0 / osRate));
        outHpAlpha = (outHpHz > 1.0f)
            ? static_cast<float>(std::exp(-Dsp::TWO_PI * static_cast<double>(outHpHz) / sr))
            : 0.0f;
        attackRampInc = 1.0f / static_cast<float>(std::max(1.0, 0.0005 * sr)); // 0.5 ms anti-click ramp
        accentEnvCoeff = static_cast<float>(std::exp(-1.0 / (0.045 * sr)));
        // Dedicated accent "Wow" RC capacitor (~160ms) — consecutive accents accumulate charge
        accentResCoeff = static_cast<float>(std::exp(-1.0 / (0.160 * sr)));
        accentResEnv = 0.0f;
        // Thermal drift LFO increment (~0.15Hz)
        thermalInc = static_cast<float>(Dsp::TWO_PI * 0.15 / sr);
        for (auto& s : stage) s = 0.0f;
        lastY4 = 0.0f;
        fbHpState = 0.0f;
        sqrHpState = 0.0f;
        subCompLpState = 0.0f;
        outHpPrev = outHpState = 0.0f;
        dcBlock = dcPrev = 0.0f;
        decimator.prepare(osRate);
        decimator.reset();
        // Component tolerance: per-stage mismatch (±0.5% from seed)
        const auto self = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        uint32_t seed = 0x5DEECE66u
                      ^ static_cast<uint32_t>(self)
                      ^ static_cast<uint32_t>(self >> 32);
        for (int i = 0; i < 4; ++i) {
            seed = seed * 1103515245u + 12345u;
            stageMismatch[i] = 1.0f + (static_cast<float>(seed & 0xFFFF) / 65536.0f - 0.5f) * 0.01f;
        }
    }

    void setParams(const Params& p)
    {
        params = p;
        updateEnvelopeCoeffs();
    }

    void trigger(int midiNote, float velocity = 1.0f)
    {
        trigger303(midiNote, velocity, false, false);
    }

    void trigger303(int midiNote, float velocity, bool slide, bool accented)
    {
        targetFreq = midiNoteToHz(midiNote);
        const bool wasActive = active;
        const bool retrigger = !slide || !wasActive;
        if (!wasActive) currentFreq = targetFreq;
        vel = velocity;
        stepAccent = accented ? 1.0f : 0.0f;
        if (accented)
        {
            accentEnv = 1.0f;
            // Capacitor charge: a fresh accent barks at full; stacked (legato)
            // accents accumulate, so consecutive accents get progressively fiercer.
            accentResEnv = retrigger ? 0.9f : std::min(1.0f, accentResEnv + 0.5f);
        }
        else if (retrigger)
        {
            accentEnv = 0.0f;
            accentResEnv = 0.0f;
        }
        updateEnvelopeCoeffs();
        // Phase reset on retrigger when enabled (BASS1) for rock-solid sub consistency vs kick
        if (!wasActive || (retrigger && phaseResetEnabled))
        {
            phase = 0.0;
            subPhase = 0.0;
            attackRamp = 0.0f;
        }
        else if (retrigger)
        {
            attackRamp = 0.0f;
        }

        // Capacitor accumulation: slide/legato changes pitch without a hard envelope reset.
        if (!retrigger)
            filterEnv = std::min(1.0f, filterEnv + 0.85f);  // accumulate charge
        else
            filterEnv = 1.0f;

        // Gate click transient (VCA DC bump — ~0.5ms)
        clickEnv = retrigger ? 1.0f : 0.20f;
        clickCoeff = static_cast<float>(std::exp(-1.0 / (0.0005 * sampleRate)));

        if (retrigger) ampEnv = 1.0f;
        else           ampEnv = std::max(ampEnv, 0.78f);
        releasing = false;
        active = true;
        samplesRemaining = static_cast<int>(sampleRate * 2.5);
        if (!wasActive) { smoothCutoff = params.cutoff; smoothReso = params.reso; }
    }

    void noteOff() { releasing = true; }
    bool isActive() const { return active && samplesRemaining > 0; }

    float tick()
    {
        if (!active || samplesRemaining <= 0) {
            active = false;
            // VCA bleed: tiny oscillator leakage
            if (phase != 0.0) {
                const double dt = currentFreq / sampleRate;
                phase += dt; if (phase >= 1.0) phase -= 1.0;
                return fastSin(static_cast<float>(phase) * Dsp::TWO_PI_F) * 0.0003f;
            }
            return 0.0f;
        }
        --samplesRemaining;

        // Release
        if (releasing) {
            ampEnv *= releaseCoeff;
            if (ampEnv < 0.0001f) { active = false; samplesRemaining = 0; return 0.0f; }
        } else {
            ampEnv *= ampDecayCoeff;
            if (ampEnv < 0.001f) ampEnv = 0.001f;
        }

        // Asymmetric slide: up faster, down slower (RC circuit behavior)
        const float glideAlpha = (targetFreq > currentFreq) ? glideAlphaUp : glideAlphaDown;
        currentFreq += (targetFreq - currentFreq) * static_cast<double>(glideAlpha);

        // Thermal drift: slow pitch wandering ±2 cents (~0.15Hz LFO)
        thermalPhase += thermalInc;
        if (thermalPhase >= Dsp::TWO_PI_F) thermalPhase -= Dsp::TWO_PI_F;
        const double drift = 1.0 + static_cast<double>(fastSin(thermalPhase)) * 0.0012;

        // Oscillator is generated INSIDE the oversampled loop below (at osRate)
        // so the saw/square are band-limited to the oversampled Nyquist — the
        // old base-rate oscillator left alias spurs the decimator can't recover.

        // Filter envelope
        filterEnv *= filterEnvCoeff;
        accentEnv *= accentEnvCoeff;
        accentResEnv *= accentResCoeff;

        // Smooth params
        smoothCutoff += smoothAlpha * (params.cutoff - smoothCutoff);
        smoothReso   += smoothAlpha * (params.reso   - smoothReso);

        // ── Cutoff modulation in OCTAVES (not exploding multipliers) ──
        // base cutoff is what the user dials in. envMod opens the filter by
        // up to ~5 octaves during the attack; accent adds up to ~1.2 extra
        // octaves on the accented step. This keeps the CUTOFF knob musically
        // meaningful at every accent setting.
        const float accentDepth = 0.25f + std::clamp(params.accent, 0.0f, 1.0f) * 0.75f;
        const float accentAmt = std::clamp(accentEnv    * accentDepth, 0.0f, 1.0f);  // fast: level + drive
        const float accResAmt = std::clamp(accentResEnv * accentDepth, 0.0f, 1.0f);  // dedicated reso sweep
        const float envOct    = params.envMod * filterEnv * 4.6f;
        const float accentOct = accResAmt * 2.1f * filterEnv;      // accent opens the filter harder
        const float totalOct  = envOct + accentOct;
        // Cutoff now travels the full knob range (was clamped at 6.2k, which
        // left the top ~13kHz of the CUTOFF pot dead).
        const float baseCutoff = std::clamp(smoothCutoff, 24.0f, 18000.0f);
        float modCutoff = baseCutoff * std::exp2(totalOct);
        modCutoff = std::clamp(modCutoff, 18.0f, 16500.0f);

        // ── Resonance ── nonlinear mapping: the useful acid range starts earlier.
        // Allowed all the way to 1.0 so the ladder can reach self-oscillation
        // (k≈4 inside ladderTick) and actually SING — the acid squelch.
        const float baseReso = 1.0f - std::pow(1.0f - std::clamp(smoothReso, 0.0f, 1.0f), 1.65f);
        // The big accent resonance push now happens via k inside ladderTick
        // (driven by accResAmt); this is just a gentle static lift.
        float modReso = baseReso + accResAmt * 0.08f + filterEnv * params.envMod * 0.045f;
        modReso = std::clamp(modReso, 0.0f, 1.0f);

        // Pre-filter drive: MODEST. The old ×5 drive saturated the oscillator
        // into a square before the filter, which collapsed the resonant peak.
        // Grit now comes from the resonance itself + the post-filter waveshaper.
        const float preDrive    = 1.0f + accentAmt * 1.1f + modReso * 0.25f;
        const float shaperDrive = 1.55f + accentAmt * 1.15f + modReso * 0.45f;
        const float teknoBias   = 0.10f + accentAmt * 0.14f;
        const float teknoFold   = accentAmt * 0.16f + modReso * 0.08f;

        // ── Fully oversampled oscillator + ZDF Diode Ladder + Tekno RAT/Wavefolder (4× → 1×) ──
        const double dt = (currentFreq * drift) / osRate;
        const float  fd = static_cast<float>(dt);
        for (int os = 0; os < 4; ++os)
        {
            phase += dt; if (phase >= 1.0) phase -= 1.0;
            const float t = static_cast<float>(phase);
            float saw = 2.0f * t - 1.0f;
            saw -= polyBLEP(t, fd);

            // Authentic TB-303 asymmetric square wave shaper:
            // saturating transistor on the sawtooth + ~15 Hz HPF gives hollow tilt & variable PW.
            const float rawSqr = fastTanh((saw + 0.16f) * 9.5f);
            sqrHpState += sqrHpAlpha * (rawSqr - sqrHpState);
            const float sqr = rawSqr - sqrHpState;

            float osc = saw * (1.0f - waveMix) + sqr * waveMix;

            subPhase += dt * 0.5; if (subPhase >= 1.0) subPhase -= 1.0;
            osc += fastSin(static_cast<float>(subPhase) * Dsp::TWO_PI_F) * subMix;

            osc *= preDrive;

            // Pre-filter 120 Hz low-pass tap for low-end resonance compensation (#6)
            subCompLpState += subCompLpAlpha * (osc - subCompLpState);

            float y = ladderTick(osc, modCutoff, modReso, accResAmt);
            // Restore sub weight when resonance climbs high (prevents hollowing out the groove)
            y += subCompLpState * (modReso * 0.24f);

            // Stompbox RAT-style asymmetric diode clip + subtle wavefolder (DC-compensated)
            const float biased = y * shaperDrive + teknoBias;
            const float folded = biased + teknoFold * fastSin(Dsp::PI_F * std::clamp(biased, -1.0f, 1.0f));
            y = fastTanh(folded) - fastTanh(teknoBias);
            decimator.push(y);
        }
        float filtered = decimator.output();        // steep FIR decimation 4× → 1×

        // Optional output HPF (e.g. 135 Hz on BASS2 so it never clashes with BASS1 sub)
        if (outHpAlpha > 0.0f)
        {
            const float hp = outHpAlpha * (outHpState + filtered - outHpPrev);
            outHpPrev = filtered;
            outHpState = hp;
            filtered = hp;
        }

        // DC blocker
        const float dc = filtered - dcPrev + 0.9975f * dcBlock;
        dcPrev = filtered; dcBlock = dc; filtered = dc;

        // 0.5 ms anti-click attack micro-ramp
        attackRamp = std::min(1.0f, attackRamp + attackRampInc);

        // Gate click transient (brief DC bump on attack)
        float click = 0.0f;
        if (clickEnv > 0.001f) { click = clickEnv * 0.08f; clickEnv *= clickCoeff; }

        // ── Dynamic makeup ──
        const float makeup   = 1.0f + smoothReso * 0.15f + accResAmt * 0.25f;
        const float volBoost = (1.0f + accentAmt * 0.25f) * makeup;
        return (filtered + click) * ampEnv * attackRamp * vel * volBoost * 0.50f;
    }

    void setWaveMix(float w) { waveMix = w; }
    void setSubMix(float s) { subMix = s; }
    void setPhaseReset(bool enabled) { phaseResetEnabled = enabled; }
    void setOutputHighPassHz(float hz)
    {
        outHpHz = std::max(0.0f, hz);
        outHpAlpha = (outHpHz > 1.0f && sampleRate > 0.0)
            ? static_cast<float>(std::exp(-Dsp::TWO_PI * static_cast<double>(outHpHz) / sampleRate))
            : 0.0f;
    }

private:
    Params params;
    double sampleRate = 44100.0, osRate = 88200.0;
    double phase = 0.0, subPhase = 0.0;
    double currentFreq = 110.0, targetFreq = 110.0;
    float vel = 1.0f, ampEnv = 0.0f, ampDecayCoeff = 0.9999f;
    float filterEnv = 0.0f, releaseCoeff = 0.9985f;
    float stepAccent = 0.0f, accentEnv = 0.0f, accentEnvCoeff = 0.999f;
    float accentResEnv = 0.0f, accentResCoeff = 0.999f;  // dedicated accent resonance sweep
    float waveMix = 0.0f, subMix = 0.0f;
    float smoothCutoff = 800.0f, smoothReso = 0.15f, smoothAlpha = 0.01f;
    float glideAlphaUp = 0.03f, glideAlphaDown = 0.01f;
    float filterEnvCoeff = 0.999f;
    float clickEnv = 0.0f, clickCoeff = 0.0f;
    float dcPrev = 0.0f, dcBlock = 0.0f;
    float thermalPhase = 0.0f, thermalInc = 0.0f;  // thermal drift LFO
    int samplesRemaining = 0;
    bool releasing = false, active = false;

    // Filter state
    float stage[4] = {};                                // TPT integrator states
    float stageMismatch[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // component tolerance
    float lastY4 = 0.0f;                                // resonance feedback tap
    float fbHpState = 0.0f;
    float fbHpAlpha = 0.01f;
    float sqrHpState = 0.0f;
    float sqrHpAlpha = 0.001f;
    float subCompLpState = 0.0f;
    float subCompLpAlpha = 0.005f;
    float outHpHz = 0.0f;
    float outHpAlpha = 0.0f;
    float outHpPrev = 0.0f, outHpState = 0.0f;
    float attackRamp = 1.0f, attackRampInc = 0.05f;
    bool  phaseResetEnabled = false;
    FirDecimator decimator;   // 4× → 1× anti-alias decimation of the nonlinear chain

    void updateEnvelopeCoeffs()
    {
        // Accent shortens filter decay only on accented steps; the knob is depth.
        const float accentDepth = 0.25f + std::clamp(params.accent, 0.0f, 1.0f) * 0.75f;
        const float accentDrive = stepAccent * accentDepth;
        const double effectiveDecay = std::max(0.01, static_cast<double>(params.decay) * (1.0 - accentDrive * 0.40));
        filterEnvCoeff = static_cast<float>(std::exp(-1.0 / (effectiveDecay * sampleRate)));
        ampDecayCoeff = static_cast<float>(std::exp(-1.0 / (std::max(0.045, static_cast<double>(params.decay) * 1.35) * sampleRate)));
    }

    // Zero-delay-feedback (TPT) 4-pole TB-303 Diode Ladder (18 dB/oct real slope).
    //
    // Stage 0 uses half the capacitance (2× cutoff frequency) of stages 1..3,
    // reproducing the real 303's brighter 18 dB/oct rolloff and acid nasal bite.
    // Feedback is solved zero-delay via TPT with a 150 Hz high-pass in the loop
    // and gentle diode saturation so high resonance sings without blowing up.
    float ladderTick(float input, float cutoffHz, float resonance, float accentRes)
    {
        const float fsOS = static_cast<float>(osRate);
        const float nyq  = fsOS * 0.45f;
        const float fc   = std::clamp(cutoffHz, 18.0f, nyq);
        const float g    = std::tan(Dsp::PI_F * fc / fsOS);

        // Stage 0 has half capacitance (g0 = 2*g) → 18 dB/oct TB-303 diode ladder response
        const float g0Raw = g * 2.0f * stageMismatch[0];
        const float G0    = g0Raw / (1.0f + g0Raw);
        const float g1Raw = g * stageMismatch[1];
        const float G1    = g1Raw / (1.0f + g1Raw);
        const float g2Raw = g * stageMismatch[2];
        const float G2    = g2Raw / (1.0f + g2Raw);
        const float g3Raw = g * stageMismatch[3];
        const float G3    = g3Raw / (1.0f + g3Raw);

        // Resonance gain k calibrated for unbalanced 18 dB/oct ladder + accent "Wow" push
        const float k = resonance * 5.2f + accentRes * 1.6f;

        // Zero-delay state prediction S + 150 Hz feedback HPF (303 low-end thinning)
        const float S = G3 * (G2 * (G1 * stage[0] + stage[1]) + stage[2]) + stage[3];
        fbHpState += fbHpAlpha * (S - fbHpState);
        const float fbHp = S - fbHpState;

        const float Gtot = G0 * G1 * G2 * G3;
        const float fbSat = fastTanh(fbHp * 0.85f) * 1.18f;
        const float u = (input - k * fbSat) / (1.0f + k * Gtot * 0.65f);

        // TPT integration with subtle per-stage diode nonlinearity
        float y = fastTanh(u * 0.90f) * 1.11f;
        const float Gs[4] = { G0, G1, G2, G3 };
        for (int s = 0; s < 4; ++s)
        {
            const float v = (y - stage[s]) * Gs[s];
            y             = v + stage[s];
            stage[s]      = y + v;
            if (s == 1) y = fastTanh(y * 0.95f) * 1.05f; // mid-ladder diode softness
        }

        lastY4 = y;
        return y * (1.0f + resonance * 0.42f);
    }

    static float polyBLEP(float t, float dt)
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt) { const float n = t / dt; return 2.0f * n - n * n - 1.0f; }
        if (t > 1.0f - dt) { const float n = (t - 1.0f) / dt; return 2.0f * n + n * n + 1.0f; }
        return 0.0f;
    }

    static double midiNoteToHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }
};
