#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "DspConstants.h"

// State-of-the-art tom: dual-stage pitch sweep, LP filtered body,
// HP-filtered click, output saturation.
class TomVoice
{
public:
    struct Params
    {
        float tune     = 90.0f;
        float decay    = 0.30f;
        float pitchEnv = 0.65f;
        float attack   = 0.20f;
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        pitchFastCoeff = static_cast<float>(std::exp(-1.0 / (0.015 * sr)));
        pitchSlowCoeff = static_cast<float>(std::exp(-1.0 / (0.060 * sr)));
        bodyLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 8000.0 / sr));
        clickHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 1500.0 / sr));
    }

    void setParams(const Params& p) { params = p; }

    void trigger(float velocity = 1.0f)
    {
        vel = std::clamp(velocity, 0.0f, 1.0f);
        durationSamples  = static_cast<int>(sampleRate * params.decay * 3.0);
        samplesRemaining = durationSamples;
        phase      = 0.0;
        pitchFast  = 1.0f;
        pitchSlow  = 1.0f;
        noiseGen.reset(55555);
        bodyLpState = 0.0f;
        clickHpState = 0.0f;
        attackSamples = static_cast<int>(sampleRate * 0.0003);
        attackCounter = 0;

        // Dual-stage amp envelope: a fast attack punch layered over the slower
        // resonant body tail (shaped attack→body, not a single exponential).
        bodyEnvCoeff = static_cast<float>(std::exp(-1.0 / (params.decay * 0.8 * sampleRate)));
        bodyEnv = 1.0f;
        attackCoeff = static_cast<float>(std::exp(-1.0 / (0.008 * sampleRate)));
        attackEnv = 1.0f;

        clickEnv   = 1.0f;
        clickCoeff = static_cast<float>(std::exp(-1.0 / (0.003 * sampleRate)));
    }

    bool isActive() const { return samplesRemaining > 0; }

    float tick()
    {
        if (samplesRemaining <= 0) return 0.0f;
        --samplesRemaining;

        bodyEnv *= bodyEnvCoeff;
        attackEnv *= attackCoeff;

        // Dual-stage pitch sweep (fast snap + slow body)
        pitchFast *= pitchFastCoeff;
        pitchSlow *= pitchSlowCoeff;
        const float sweep = pitchFast * 0.6f + pitchSlow * 0.4f;
        // Velocity → pitch-sweep depth (vel=1 unchanged; harder hits snap higher).
        const float extraHz = params.tune * params.pitchEnv * 3.0f * (0.7f + 0.3f * vel);
        const double freq = static_cast<double>(params.tune + extraHz * sweep);

        phase += Dsp::TWO_PI * freq / sampleRate;
        if (phase >= Dsp::TWO_PI) phase -= Dsp::TWO_PI;

        float body = fastSinD(phase) * bodyEnv;
        const float velPunch = 0.6f + 0.4f * vel;
        body *= 1.0f + attackEnv * 0.5f * velPunch;            // velocity-driven attack punch
        double h2 = phase * 2.0; if (h2 >= Dsp::TWO_PI) h2 -= Dsp::TWO_PI;
        body += fastSinD(h2) * bodyEnv * attackEnv * 0.15f * velPunch;  // 2nd-harmonic thunk at attack

        // LP filter on body @ 8kHz (warm analog tone)
        bodyLpState += bodyLpAlpha * (body - bodyLpState);
        body = bodyLpState;

        // HP-filtered click transient (clean attack)
        float click = 0.0f;
        if (clickEnv > 0.0001f)
        {
            float raw = noiseGen.tick() * clickEnv;
            clickHpState += clickHpAlpha * (raw - clickHpState);
            click = (raw - clickHpState) * params.attack * 0.5f * (0.6f + 0.4f * vel);
            clickEnv *= clickCoeff;
        }

        // Output saturation + 0.9x for headroom
        float out = fastTanh((body * 0.85f + click) * 1.2f) * 0.90f;

        if (attackCounter < attackSamples)
            out *= static_cast<float>(attackCounter++) / static_cast<float>(attackSamples);

        return out;
    }

private:
    Params   params;
    float    vel = 1.0f;
    double   sampleRate = 44100.0;
    int      samplesRemaining = 0, durationSamples = 0;
    double   phase = 0.0;
    float    pitchFast = 1.0f, pitchFastCoeff = 0.999f;
    float    pitchSlow = 1.0f, pitchSlowCoeff = 0.9999f;
    float    bodyEnv = 0.0f, bodyEnvCoeff = 0.999f;
    float    attackEnv = 0.0f, attackCoeff = 0.99f;   // fast attack-punch stage
    float    clickEnv = 0.0f, clickCoeff = 0.0f;
    float    bodyLpState = 0.0f, bodyLpAlpha = 0.1f;
    float    clickHpState = 0.0f, clickHpAlpha = 0.1f;
    NoiseGen noiseGen { 55555 };
    int      attackSamples = 22, attackCounter = 22;
};
