#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "DspConstants.h"

// State-of-the-art snare: dual-tone body with pitch sweep, band-passed noise,
// snap transient, output saturation, LP/HP shaping.
class SnareVoice
{
public:
    struct Params
    {
        float tune  = 185.0f;
        float decay = 0.18f;
        float snap  = 0.50f;
        float noise = 0.60f;
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        // Noise band-pass: HP@2kHz + LP@10kHz for realistic snare sizzle
        noiseHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 2000.0 / sr));
        noiseLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 10000.0 / sr));
        // Output LP @ 14kHz (analog bandwidth)
        outLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 14000.0 / sr));
        // Pitch sweep coefficient (5ms — snappy)
        pitchSweepCoeff = static_cast<float>(std::exp(-1.0 / (0.005 * sr)));
    }

    void setParams(const Params& p) { params = p; }

    void trigger(float velocity = 1.0f)
    {
        vel = std::clamp(velocity, 0.0f, 1.0f);
        durationSamples  = static_cast<int>(sampleRate * params.decay * 3.5);
        samplesRemaining = durationSamples;
        phase1 = phase2 = 0.0;
        noiseGen.reset(99991);
        noiseHpState = noiseLpState = 0.0f;
        outLpState = 0.0f;
        pitchSweepEnv = 1.0f;
        attackSamples = static_cast<int>(sampleRate * 0.0003);
        attackCounter = 0;

        if (durationSamples > 0)
        {
            bodyEnvCoeff  = static_cast<float>(std::exp(-18.0 / durationSamples));
            noiseEnvCoeff = static_cast<float>(std::exp(-14.0 / durationSamples));
            snapEnvCoeff  = static_cast<float>(std::exp(-350.0 / durationSamples));
        }
        // Layered transient stages (time-based, independent of decay): a fast
        // body "thwack" and a fast noise "crack" that sit on top of the slower
        // body / wire-buzz tails — turns the single exponential into a shaped
        // attack→body envelope.
        bodyPunchCoeff = static_cast<float>(std::exp(-1.0 / (0.006 * sampleRate)));
        noiseFastCoeff = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate)));
        bodyEnvState = noiseEnvState = snapEnvState = 1.0f;
        bodyPunchEnv = noiseFastEnv = 1.0f;
    }

    bool isActive() const { return samplesRemaining > 0; }

    float tick()
    {
        if (samplesRemaining <= 0) return 0.0f;
        --samplesRemaining;

        // Envelopes
        bodyEnvState  *= bodyEnvCoeff;
        noiseEnvState *= noiseEnvCoeff;
        snapEnvState  *= snapEnvCoeff;
        pitchSweepEnv *= pitchSweepCoeff;
        bodyPunchEnv  *= bodyPunchCoeff;
        noiseFastEnv  *= noiseFastCoeff;

        // Pitch sweep: brief upward at attack (adds punch)
        const float sweepFreq = params.tune * (1.0f + pitchSweepEnv * 1.5f);

        // Two-tone body (inharmonic partials — classic snare)
        phase1 += Dsp::TWO_PI * static_cast<double>(sweepFreq)        / sampleRate;
        phase2 += Dsp::TWO_PI * static_cast<double>(sweepFreq * 1.47f) / sampleRate;
        if (phase1 >= Dsp::TWO_PI) phase1 -= Dsp::TWO_PI;
        if (phase2 >= Dsp::TWO_PI) phase2 -= Dsp::TWO_PI;

        float body = (fastSinD(phase1) + fastSinD(phase2) * 0.6f)
                     * bodyEnvState * (1.0f - params.noise * 0.5f);
        body *= 1.0f + bodyPunchEnv * 0.55f * (0.55f + 0.45f * vel);   // velocity-driven attack thwack

        // Band-passed noise (HP@2kHz + LP@10kHz — realistic snare wires)
        float raw = noiseGen.tick();
        noiseHpState += noiseHpAlpha * (raw - noiseHpState);
        float hp = raw - noiseHpState;
        noiseLpState += noiseLpAlpha * (hp - noiseLpState);
        // Two-stage noise: fast "crack" burst (velocity-driven) over the slower
        // wire-buzz tail.
        float n = noiseLpState * params.noise * (noiseEnvState + noiseFastEnv * 0.7f * (0.55f + 0.45f * vel));

        // Snap transient (very fast, adds attack definition).
        // Velocity → snap crack (vel=1 unchanged; soft hits crack less).
        float snap = noiseGen.tick() * params.snap * snapEnvState * 0.6f * (0.45f + 0.55f * vel);

        // Mix + soft saturation, then 0.85x for headroom
        float out = body * 0.5f + n * 0.7f + snap;
        out = fastTanh(out * 1.3f) * 0.85f;

        // Output LP
        outLpState += outLpAlpha * (out - outLpState);
        out = outLpState;

        // Anti-click
        if (attackCounter < attackSamples)
            out *= static_cast<float>(attackCounter++) / static_cast<float>(attackSamples);

        return out;
    }

private:
    Params   params;
    float    vel              = 1.0f;
    double   sampleRate       = 44100.0;
    int      samplesRemaining = 0;
    int      durationSamples  = 0;
    double   phase1 = 0.0, phase2 = 0.0;
    float    bodyEnvCoeff = 0.999f, noiseEnvCoeff = 0.999f, snapEnvCoeff = 0.99f;
    float    bodyEnvState = 1.0f, noiseEnvState = 1.0f, snapEnvState = 1.0f;
    float    bodyPunchEnv = 0.0f, bodyPunchCoeff = 0.99f;   // fast attack thwack
    float    noiseFastEnv = 0.0f, noiseFastCoeff = 0.99f;   // fast noise crack
    float    pitchSweepEnv = 1.0f, pitchSweepCoeff = 0.999f;
    float    noiseHpState = 0.0f, noiseHpAlpha = 0.1f;
    float    noiseLpState = 0.0f, noiseLpAlpha = 0.1f;
    float    outLpState = 0.0f, outLpAlpha = 0.1f;
    NoiseGen noiseGen { 99991 };
    int      attackSamples = 22;
    int      attackCounter = 22;
};
