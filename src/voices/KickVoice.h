#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "DspConstants.h"

// Premium synthesized kick.
// Layered transient/body/sub engine with pitch drop, beater tone, saturation,
// clean sub fade-in, DC control and final soft ceiling.
class KickVoice
{
public:
    struct Params
    {
        float tune     = 60.0f;   // Hz — fundamental frequency (40–120)
        float decay    = 0.45f;   // sec — overall decay (0.05–0.80)
        float punch    = 0.40f;   // 0–1 — click transient intensity
        float pitchEnv = 0.80f;   // 0–1 — pitch sweep depth
        float drive    = 0.30f;   // 0–1 — body distortion
        float sub      = 0.50f;   // 0–1 — sub-octave mix
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        phase = 0.0;
        subPhase = 0.0;
        knockPhase = 0.0;
        samplesRemaining = 0;
        pitchDecayCoeff = static_cast<float>(std::exp(-1.0 / (0.030 * sr)));
        clickHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 3600.0 / sr));
        outLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 14500.0 / sr));
        outLpState = 0.0f;
        subLpState = 0.0f;
        // DC blocker — removes the asymmetry from drive saturation
        dcBlockerAlpha = static_cast<float>(std::exp(-Dsp::TWO_PI * 20.0 / sr));
        dcInPrev = dcOut = 0.0f;
        beaterPhase = 0.0;
        beaterEnv = 0.0f;
        knockEnv = 0.0f;
        subFade = 1.0f;
    }

    void setParams(const Params& p)
    {
        params.tune     = std::clamp(p.tune, 35.0f, 130.0f);
        params.decay    = std::clamp(p.decay, 0.05f, 0.95f);
        params.punch    = std::clamp(p.punch, 0.0f, 1.0f);
        params.pitchEnv = std::clamp(p.pitchEnv, 0.0f, 1.0f);
        params.drive    = std::clamp(p.drive, 0.0f, 1.0f);
        params.sub      = std::clamp(p.sub, 0.0f, 1.0f);
    }

    void trigger(float velocity = 1.0f)
    {
        vel = std::clamp(velocity, 0.0f, 1.0f);
        durationSamples  = static_cast<int>(sampleRate * params.decay * 3.25);
        samplesRemaining = durationSamples;
        phase            = 0.0;
        subPhase         = 0.0;
        beaterPhase      = 0.0;
        knockPhase       = 0.0;
        pitchEnvState    = 1.0f;
        noiseGen.reset(12345);

        // Two-stage amp envelope: fast body decay + slow sub tail.
        bodyEnv  = 1.0f;
        subEnv   = 1.0f;
        bodyDecayCoeff = static_cast<float>(std::exp(-1.0 / (params.decay * 0.30 * sampleRate)));
        subDecayCoeff  = static_cast<float>(std::exp(-1.0 / (params.decay * 1.35 * sampleRate)));
        punchBodyEnv = 1.0f;
        punchBodyCoeff = static_cast<float>(std::exp(-1.0 / (0.026 * sampleRate)));
        knockEnv = 1.0f;
        knockCoeff = static_cast<float>(std::exp(-1.0 / ((0.018 + params.decay * 0.030) * sampleRate)));
        subFade = 0.0f;
        subFadeInc = 1.0f / static_cast<float>(std::max(1.0, sampleRate * 0.012));
        subLpState = 0.0f;
        subLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * (170.0 + params.tune * 2.0) / sampleRate));

        // Click transient and short tonal beater layer.
        clickEnv   = 1.0f;
        clickCoeff = static_cast<float>(std::exp(-1.0 / ((0.0014 + 0.0012 * params.punch) * sampleRate)));
        clickHpState = 0.0f;
        beaterEnv = 1.0f;
        beaterCoeff = static_cast<float>(std::exp(-1.0 / ((0.0045 + 0.0065 * params.punch) * sampleRate)));

        // Anti-click attack ramp (~0.3ms)
        attackSamples = static_cast<int>(sampleRate * 0.0003);
        attackCounter = 0;

        // Pitch sweep: fast knock + slower body movement.
        pitchEnvSlow = 1.0f;
        const double fastPitchMs = 0.010 + (1.0 - params.pitchEnv) * 0.016;
        pitchDecayCoeff = static_cast<float>(std::exp(-1.0 / (fastPitchMs * sampleRate)));
        pitchSlowCoeff = static_cast<float>(std::exp(-1.0 / ((0.060 + params.decay * 0.045) * sampleRate)));
    }

    bool isActive() const { return samplesRemaining > 0; }

    float tick()
    {
        if (samplesRemaining <= 0) return 0.0f;
        --samplesRemaining;

        // ── Two-stage amplitude envelope ──
        bodyEnv *= bodyDecayCoeff;
        subEnv  *= subDecayCoeff;
        punchBodyEnv *= punchBodyCoeff;
        knockEnv *= knockCoeff;

        // ── Pitch envelope: fast + slow blend for realistic kick sweep ──
        pitchEnvState *= pitchDecayCoeff;
        pitchEnvSlow  *= pitchSlowCoeff;

        const float pitchSweep = pitchEnvState * 0.7f + pitchEnvSlow * 0.3f;
        const float extraPitch = params.tune * params.pitchEnv * (3.0f + params.pitchEnv * 3.6f);
        const double freq = static_cast<double>(std::clamp(params.tune + extraPitch * pitchSweep,
                                                           25.0f, 1300.0f));

        // ── Main body oscillator (2× oversampled for cleaner drive harmonics) ──
        const double phaseInc = (Dsp::TWO_PI * freq) / sampleRate;

        // Midpoint sample
        phase += phaseInc * 0.5;
        if (phase >= Dsp::TWO_PI) phase -= Dsp::TWO_PI;
        float bodyMid = fastSinD(phase);

        // Actual sample
        phase += phaseInc * 0.5;
        if (phase >= Dsp::TWO_PI) phase -= Dsp::TWO_PI;
        float bodyEnd = fastSinD(phase);

        // Average for 2× oversampled body
        float body = (bodyMid + bodyEnd) * 0.5f;
        double harmonicPhase = phase * 2.0;
        if (harmonicPhase >= Dsp::TWO_PI) harmonicPhase -= Dsp::TWO_PI;
        body += fastSinD(harmonicPhase) * (0.035f + params.drive * 0.075f) * bodyEnv;
        double thirdPhase = phase * 3.0;
        while (thirdPhase >= Dsp::TWO_PI) thirdPhase -= Dsp::TWO_PI;
        body += fastSinD(thirdPhase) * params.drive * 0.115f * bodyEnv;
        body *= 1.0f + punchBodyEnv * params.punch * 4.60f;

        knockPhase += (Dsp::TWO_PI * (115.0 + params.tune * 1.25 + params.pitchEnv * 85.0)) / sampleRate;
        if (knockPhase >= Dsp::TWO_PI) knockPhase -= Dsp::TWO_PI;
        const float knockAmt = (0.08f + params.punch * 0.16f + params.pitchEnv * 0.06f) * (1.0f - params.sub * 0.18f);
        body += fastSinD(knockPhase) * knockEnv * knockAmt;

        // Body saturation — parallel asymmetric clip keeps weight while adding edge.
        if (params.drive > 0.001f)
        {
            const float dry = body;
            const float shapedIn = body + body * body * (0.10f * params.drive);
            float driven = fastTanh(shapedIn * (1.0f + params.drive * 12.5f));
            if (driven > 0.0f) driven *= (1.0f - params.drive * 0.10f);
            float bite = driven - dry;
            bite = fastTanh(bite * (1.8f + params.drive * 4.4f));
            body = dry * (1.0f - params.drive * 0.50f)
                 + driven * (0.30f + params.drive * 0.56f)
                 + bite * params.drive * 0.30f;
            body *= 0.88f / (0.72f + params.drive * 0.24f);
        }

        body *= bodyEnv;

        // ── Sub oscillator (octave below, faded in to keep transient clean) ──
        subFade = std::min(1.0f, subFade + subFadeInc);
        const double subTracking = 0.08 + params.pitchEnv * 0.12;
        const double subFreq = (static_cast<double>(params.tune)
                              + (freq - static_cast<double>(params.tune)) * subTracking) * 0.5;
        subPhase += (Dsp::TWO_PI * subFreq) / sampleRate;
        if (subPhase >= Dsp::TWO_PI) subPhase -= Dsp::TWO_PI;
        const float subRaw = fastSinD(subPhase) * subEnv * params.sub * subFade;
        subLpState += subLpAlpha * (subRaw - subLpState);
        float subOsc = subLpState;

        // Velocity → attack snap (vel=1 unchanged; soft hits lose the beater
        // click so they sit back in the mix).
        const float velAttack = 0.6f + 0.4f * vel;

        // ── Click transient — HP noise + tonal beater for speaker translation ──
        float click = 0.0f;
        if (clickEnv > 0.0001f)
        {
            float raw = noiseGen.tick() * clickEnv;
            // HP filter: removes low-end from click to avoid muddiness
            clickHpState += clickHpAlpha * (raw - clickHpState);
            click = (raw - clickHpState) * params.punch * 1.5f * velAttack;
            clickEnv *= clickCoeff;
        }

        float beater = 0.0f;
        if (beaterEnv > 0.0001f)
        {
            const double beaterFreq = 820.0 + 1350.0 * params.punch + 430.0 * params.pitchEnv + 320.0 * params.drive;
            beaterPhase += (Dsp::TWO_PI * beaterFreq) / sampleRate;
            if (beaterPhase >= Dsp::TWO_PI) beaterPhase -= Dsp::TWO_PI;
            double beaterOvertonePhase = beaterPhase * 2.0;
            if (beaterOvertonePhase >= Dsp::TWO_PI) beaterOvertonePhase -= Dsp::TWO_PI;
            beater = (fastSinD(beaterPhase) * beaterEnv * params.punch * (0.10f + params.drive * 0.12f)
                   +  fastSinD(beaterOvertonePhase) * beaterEnv * params.punch * params.drive * 0.055f) * velAttack;
            beaterEnv *= beaterCoeff;
        }

        // ── Final mix ──
        float out = body * 0.52f + subOsc * 0.41f + click * 0.17f + beater * 0.24f;

        // ── DC blocker (removes drive-asymmetry offset) ──
        // y[n] = (x[n] - x[n-1]) + alpha * y[n-1]
        const float dcIn = out;
        dcOut = (dcIn - dcInPrev) + dcBlockerAlpha * dcOut;
        dcInPrev = dcIn;
        out = dcOut;

        // ── Output LP filter @ 14.5kHz (analog bandwidth) ──
        outLpState += outLpAlpha * (out - outLpState);
        out = outLpState;

        // ── Anti-click ramp ──
        if (attackCounter < attackSamples)
            out *= static_cast<float>(attackCounter++) / static_cast<float>(attackSamples);

        // Soft ceiling: louder perceived punch, bounded output.
        return fastTanh(out * (1.10f + params.drive * 0.28f)) * (0.94f - params.drive * 0.04f);
    }

private:
    Params  params;
    float   vel              = 1.0f;
    double  sampleRate       = 44100.0;
    int     samplesRemaining = 0;
    int     durationSamples  = 0;

    // Oscillator phase
    double  phase            = 0.0;
    double  subPhase         = 0.0;
    double  beaterPhase      = 0.0;
    double  knockPhase       = 0.0;

    // Pitch envelope (dual-stage: fast transient + slow body)
    float   pitchEnvState    = 1.0f;
    float   pitchDecayCoeff  = 0.999f;   // precomputed in prepare()
    float   pitchEnvSlow     = 1.0f;
    float   pitchSlowCoeff   = 0.9999f;

    // Amplitude envelope (dual-stage: body + sub tail)
    float   bodyEnv          = 0.0f;
    float   bodyDecayCoeff   = 0.999f;
    float   punchBodyEnv     = 0.0f;
    float   punchBodyCoeff   = 0.999f;
    float   knockEnv         = 0.0f;
    float   knockCoeff       = 0.999f;
    float   subEnv           = 0.0f;
    float   subDecayCoeff    = 0.9999f;
    float   subFade          = 1.0f;
    float   subFadeInc       = 1.0f;
    float   subLpState       = 0.0f;
    float   subLpAlpha       = 0.04f;

    // Click transient
    float   clickEnv         = 0.0f;
    float   clickCoeff       = 0.0f;
    float   clickHpState     = 0.0f;
    float   clickHpAlpha     = 0.01f;
    float   beaterEnv        = 0.0f;
    float   beaterCoeff      = 0.0f;

    // Output LP filter (analog bandwidth limit)
    float   outLpState       = 0.0f;
    float   outLpAlpha       = 0.1f;

    // DC blocker (removes asymmetry from `drive` saturation)
    float   dcBlockerAlpha   = 0.997f;
    float   dcInPrev         = 0.0f;
    float   dcOut            = 0.0f;

    NoiseGen noiseGen { 12345 };
    int     attackSamples    = 22;
    int     attackCounter    = 22;
};
