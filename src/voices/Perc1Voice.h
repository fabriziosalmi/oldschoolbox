#pragma once
#include <algorithm>
#include <cmath>
#include "DspConstants.h"

// State-of-the-art metallic percussion: ring modulation between two
// inharmonic oscillators, pitch sweep, BP filtering, output saturation.
class Perc1Voice
{
public:
    struct Params
    {
        float tune   = 600.0f;
        float decay  = 0.10f;
        float detune = 0.40f;
        float drive  = 0.30f;
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        pitchSweepCoeff = static_cast<float>(std::exp(-1.0 / (0.008 * sr)));
        // BP: HP@800Hz + LP@12kHz
        bpHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 800.0 / sr));
        bpLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 12000.0 / sr));
    }

    void setParams(const Params& p) { params = p; }

    void trigger(float velocity = 1.0f)
    {
        vel = std::clamp(velocity, 0.0f, 1.0f);
        durationSamples  = static_cast<int>(sampleRate * params.decay * 4.0);
        samplesRemaining = durationSamples;
        phase1 = phase2 = 0.0;
        pitchSweepEnv = 1.0f;
        bpHpState = bpLpState = 0.0f;
        attackSamples = static_cast<int>(sampleRate * 0.0003);
        attackCounter = 0;

        if (durationSamples > 0)
        {
            const float rate = 5.5f / std::max(params.decay, 0.02f);
            envCoeff = static_cast<float>(std::exp(static_cast<double>(-rate) / durationSamples));
        }
        envState = 1.0f;
    }

    bool isActive() const { return samplesRemaining > 0; }

    float tick()
    {
        if (samplesRemaining <= 0) return 0.0f;
        --samplesRemaining;

        envState *= envCoeff;
        pitchSweepEnv *= pitchSweepCoeff;

        // Pitch sweep at attack (metallic "ping").
        // Velocity → ping depth (vel=1 unchanged; soft hits ping less).
        const float sweepMul = 1.0f + pitchSweepEnv * 0.8f * (0.6f + 0.4f * vel);
        const float freq = params.tune * sweepMul;

        const double ratio = 1.0 + static_cast<double>(params.detune) * 1.45;
        phase1 += Dsp::TWO_PI * static_cast<double>(freq)         / sampleRate;
        phase2 += Dsp::TWO_PI * static_cast<double>(freq) * ratio / sampleRate;
        if (phase1 >= Dsp::TWO_PI) phase1 -= Dsp::TWO_PI;
        if (phase2 >= Dsp::TWO_PI) phase2 -= Dsp::TWO_PI;

        // Ring modulation between the two oscillators (metallic character)
        float osc1 = fastSinD(phase1);
        float osc2 = fastSinD(phase2);
        float ring = osc1 * osc2;
        float sig = (osc1 * 0.5f + osc2 * 0.3f + ring * 0.4f) * envState;

        // Band-pass filter (HP@800Hz + LP@12kHz)
        bpHpState += bpHpAlpha * (sig - bpHpState);
        float hp = sig - bpHpState;
        bpLpState += bpLpAlpha * (hp - bpLpState);
        sig = bpLpState;

        // Drive
        if (params.drive > 0.01f)
            sig = fastTanh(sig * (1.0f + params.drive * 5.0f)) / (1.0f + params.drive * 0.5f);

        float out = sig * 0.75f;

        if (attackCounter < attackSamples)
            out *= static_cast<float>(attackCounter++) / static_cast<float>(attackSamples);

        return out;
    }

private:
    Params params;
    float  vel = 1.0f;
    double sampleRate = 44100.0;
    int    samplesRemaining = 0, durationSamples = 0;
    double phase1 = 0.0, phase2 = 0.0;
    float  envCoeff = 0.999f, envState = 1.0f;
    float  pitchSweepEnv = 1.0f, pitchSweepCoeff = 0.999f;
    float  bpHpState = 0.0f, bpHpAlpha = 0.1f;
    float  bpLpState = 0.0f, bpLpAlpha = 0.1f;
    int    attackSamples = 22, attackCounter = 22;
};
