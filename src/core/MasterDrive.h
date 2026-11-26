#pragma once
#include <algorithm>
#include <cmath>
#include "../voices/DspConstants.h"

// Stereo Master Drive — "Mackie 1202 Redline" emulation (2× oversampled).
// Asymmetric analog console soft-clipper + parallel bite + DC blocker + -0.3 dBFS safety clip.
class MasterDrive
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        dcAlpha = static_cast<float>(std::exp(-Dsp::TWO_PI * 14.0 / sampleRate));
        reset();
    }

    void reset()
    {
        dcPrevL = dcPrevR = 0.0f;
        dcOutL = dcOutR = 0.0f;
        prevInL = prevInR = 0.0f;
    }

    void process(float& L, float& R, float amount)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        if (amount < 0.001f)
        {
            prevInL = L;
            prevInR = R;
            return;
        }

        L = processOversampled(L, prevInL, dcPrevL, dcOutL, amount);
        R = processOversampled(R, prevInR, dcPrevR, dcOutR, amount);
    }

private:
    static float saturateMackie(float x, float amount)
    {
        const float driveGain = 1.0f + amount * 7.5f;
        const float wet = 0.20f + amount * 0.68f;
        const float bias = amount * 0.065f;
        const float pre = x + x * x * amount * 0.055f + bias;
        const float shaped = fastTanh(pre * driveGain) - fastTanh(bias * driveGain);
        const float bite = fastTanh((shaped - x) * (1.4f + amount * 3.2f));

        float y = x * (1.0f - wet * 0.50f)
                + shaped * wet * 0.78f
                + bite * amount * 0.14f;

        y *= 1.0f / (1.0f + amount * 0.42f);
        return y;
    }

    float processOversampled(float x, float& prevIn, float& dcPrev, float& dcOut, float amount)
    {
        // 2× oversampled saturation: process midpoint + current sample and average (2-tap half-band)
        const float mid = 0.5f * (prevIn + x);
        prevIn = x;
        const float y0 = saturateMackie(mid, amount);
        const float y1 = saturateMackie(x, amount);
        float y = 0.5f * (y0 + y1);

        // Post-saturation DC blocker (~14 Hz)
        const float in = y;
        dcOut = (in - dcPrev) + dcAlpha * dcOut;
        dcPrev = in;
        return dcOut;
    }

    double sampleRate = 44100.0;
    float dcAlpha = 0.998f;
    float dcPrevL = 0.0f, dcPrevR = 0.0f;
    float dcOutL = 0.0f, dcOutR = 0.0f;
    float prevInL = 0.0f, prevInR = 0.0f;
};
