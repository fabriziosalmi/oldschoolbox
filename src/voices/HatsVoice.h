#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "DspConstants.h"

// State-of-the-art hi-hats: 6-oscillator metallic ring + band-passed noise,
// resonant HP filter, dual envelope (body + ring), output saturation.
class HatsVoice
{
public:
    struct Params
    {
        float decay = 0.08f;  // 0.02 (closed) – 0.50 (open)
        float tone  = 0.50f;  // metallic brightness
        float body  = 0.30f;  // metallic vs noise ratio
    };

    void prepare(double sr)
    {
        sampleRate = sr;
        // Resonant HP @ 4kHz (808-style hi-hat character)
        hpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 4000.0 / sr));
        // Band-pass for noise: HP@6kHz + LP@16kHz
        noiseHpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 6000.0 / sr));
        noiseLpAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 16000.0 / sr));
        declickCoeff = static_cast<float>(std::exp(-1.0 / (0.0018 * sr)));
        declickTail = lastOut = 0.0f;
    }

    void setParams(const Params& p) { params = p; }

    void trigger(float velocity = 1.0f)
    {
        if (samplesRemaining > 0)
            declickTail = std::clamp(declickTail + lastOut, -1.0f, 1.0f);
        vel = std::clamp(velocity, 0.0f, 1.0f);
        durationSamples  = static_cast<int>(sampleRate * params.decay * 4.0);
        samplesRemaining = durationSamples;
        for (auto& p : phases) p = 0.0f;
        noiseGen.reset(77777);
        hpState = 0.0f;
        noiseHpState = noiseLpState = 0.0f;
        attackSamples = static_cast<int>(sampleRate * 0.0003);
        attackCounter = 0;

        if (durationSamples > 0)
        {
            envCoeff  = static_cast<float>(std::exp(-6.0 / (params.decay * sampleRate)));
            ringCoeff = static_cast<float>(std::exp(-12.0 / (params.decay * sampleRate)));
        }
        envState = ringState = 1.0f;
    }

    bool isActive() const { return samplesRemaining > 0; }

    float tick()
    {
        if (samplesRemaining <= 0) return 0.0f;
        --samplesRemaining;

        envState  *= envCoeff;
        ringState *= ringCoeff;

        // 6 inharmonic square oscillators (808 hi-hat topology)
        // Frequency ratios from Roland TR-808 analysis.
        // Squares are now BAND-LIMITED via polyBLEP and partials above Nyquist
        // are skipped — the old hard `phase < PI` squares aliased badly
        // (tone=1.0 put the top partial at ~52kHz, folding garbage into the
        // audible band).
        const double baseHz = 6000.0 + params.tone * 8000.0;
        static constexpr double ratios[6] = { 1.0, 1.483, 1.932, 2.546, 2.985, 3.732 };
        static constexpr float  gains[6]  = { 0.14f, 0.12f, 0.10f, 0.09f, 0.08f, 0.07f };
        const float nyq = static_cast<float>(sampleRate) * 0.5f;

        float metallic = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            const double f = baseHz * ratios[i];
            if (f >= static_cast<double>(nyq) * 0.98) continue;  // would alias — drop it
            const float dt = static_cast<float>(f / sampleRate);
            float t = static_cast<float>(phases[i]) + dt;
            if (t >= 1.0f) t -= 1.0f;
            phases[i] = t;

            float sq = (t < 0.5f) ? 1.0f : -1.0f;
            sq += polyBlep(t, dt);
            float t2 = t + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
            sq -= polyBlep(t2, dt);
            metallic += sq * gains[i];
        }
        // Velocity → brightness (vel=1 leaves the sound unchanged; softer hits
        // pull back the metallic ring so they read as lighter taps).
        metallic *= params.body * ringState * (0.70f + 0.30f * vel);

        // Band-passed noise (HP@6kHz + LP@16kHz — shimmer)
        float raw = noiseGen.tick();
        noiseHpState += noiseHpAlpha * (raw - noiseHpState);
        float hp = raw - noiseHpState;
        noiseLpState += noiseLpAlpha * (hp - noiseLpState);
        float n = noiseLpState * (1.0f - params.body * 0.4f) * envState * (0.60f + 0.40f * vel);

        // Mix through resonant HP @ 4kHz
        float mix = metallic + n;
        hpState += hpAlpha * (mix - hpState);
        float out = (mix - hpState) * 0.7f;

        // Soft saturation, with 0.8x for headroom (hats spike on transients)
        out = fastTanh(out * 1.4f) * 0.80f;

        // Anti-click attack + 1.8 ms retrigger de-click tail
        if (attackCounter < attackSamples)
            out *= static_cast<float>(attackCounter++) / static_cast<float>(attackSamples);
        if (std::abs(declickTail) > 1.0e-5f)
        {
            out += declickTail;
            declickTail *= declickCoeff;
        }
        else declickTail = 0.0f;
        lastOut = out;
        return out;
    }

private:
    Params   params;
    double   sampleRate = 44100.0;
    int      samplesRemaining = 0, durationSamples = 0;
    float    phases[6] = {};          // normalised [0,1) for polyBLEP
    float    vel = 1.0f;
    float    hpState = 0.0f, hpAlpha = 0.5f;
    float    noiseHpState = 0.0f, noiseHpAlpha = 0.1f;
    float    noiseLpState = 0.0f, noiseLpAlpha = 0.1f;
    float    envCoeff = 0.999f, envState = 1.0f;
    float    ringCoeff = 0.999f, ringState = 1.0f;
    NoiseGen noiseGen { 77777 };
    int      attackSamples = 22, attackCounter = 22;
    float    declickCoeff = 0.985f, declickTail = 0.0f, lastOut = 0.0f;
};
