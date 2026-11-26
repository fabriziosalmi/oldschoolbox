#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "DspConstants.h"

// State-of-the-art Perc2: pitched noise percussion with
// dual-stage pitch sweep, band-passed noise, LP-filtered body,
// output saturation. Good for cowbells, claves, rimshots.
class Perc2Voice
{
public:
    struct Params
    {
        float tune  = 350.0f;
        float decay = 0.12f;
        float noise = 0.50f;
        float pitch = 0.40f;
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        pitchFastCoeff = static_cast<float>(std::exp(-1.0 / (0.008 * sr)));
        pitchSlowCoeff = static_cast<float>(std::exp(-1.0 / (0.040 * sr)));
        // BP noise: HP@1kHz + LP@8kHz
        noiseHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 1000.0 / sr));
        noiseLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 8000.0 / sr));
        // Body LP @ 10kHz
        bodyLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 10000.0 / sr));
    }

    void setParams(const Params& p) { params = p; }

    void trigger(float velocity = 1.0f)
    {
        vel = std::clamp(velocity, 0.0f, 1.0f);
        durationSamples  = static_cast<int>(sampleRate * params.decay * 3.5);
        samplesRemaining = durationSamples;
        phase = 0.0;
        pitchFast = pitchSlow = 1.0f;
        noiseGen.reset(31337);
        noiseHpState = noiseLpState = bodyLpState = 0.0f;
        attackSamples = static_cast<int>(sampleRate * 0.0003);
        attackCounter = 0;

        if (durationSamples > 0)
        {
            envCoeff = static_cast<float>(std::exp(-4.5 / (std::max(0.02, static_cast<double>(params.decay)) * sampleRate)));
            noiseEnvCoeff = static_cast<float>(std::exp(-6.0 / (std::max(0.02, static_cast<double>(params.decay)) * sampleRate)));
        }
        envState = noiseEnvState = 1.0f;
    }

    bool isActive() const { return samplesRemaining > 0; }

    float tick()
    {
        if (samplesRemaining <= 0) return 0.0f;
        --samplesRemaining;

        envState *= envCoeff;
        noiseEnvState *= noiseEnvCoeff;

        // Dual-stage pitch sweep
        pitchFast *= pitchFastCoeff;
        pitchSlow *= pitchSlowCoeff;
        // Velocity → pitch-sweep depth (vel=1 unchanged; harder hits ping higher).
        const float sweep = (pitchFast * 0.5f + pitchSlow * 0.5f) * (0.6f + 0.4f * vel);
        const double freq = static_cast<double>(params.tune * (1.0f + params.pitch * 2.5f * sweep));

        phase += Dsp::TWO_PI * freq / sampleRate;
        if (phase >= Dsp::TWO_PI) phase -= Dsp::TWO_PI;

        float tone = fastSinD(phase) * (1.0f - params.noise * 0.6f) * envState;

        // LP filter on tone body
        bodyLpState += bodyLpAlpha * (tone - bodyLpState);
        tone = bodyLpState;

        // Band-passed noise (HP@1kHz + LP@8kHz)
        float raw = noiseGen.tick();
        noiseHpState += noiseHpAlpha * (raw - noiseHpState);
        float hp = raw - noiseHpState;
        noiseLpState += noiseLpAlpha * (hp - noiseLpState);
        float n = noiseLpState * params.noise * noiseEnvState;

        // Mix + saturation
        float out = fastTanh((tone + n) * 1.3f) * 0.8f;

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
    float    envCoeff = 0.999f, envState = 1.0f;
    float    noiseEnvCoeff = 0.999f, noiseEnvState = 1.0f;
    float    noiseHpState = 0.0f, noiseHpAlpha = 0.1f;
    float    noiseLpState = 0.0f, noiseLpAlpha = 0.1f;
    float    bodyLpState = 0.0f, bodyLpAlpha = 0.1f;
    NoiseGen noiseGen { 31337 };
    int      attackSamples = 22, attackCounter = 22;
};
