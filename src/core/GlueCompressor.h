#pragma once
#include <cmath>
#include <algorithm>

// Stereo "glue" bus compressor.
// Single AMOUNT knob maps to threshold/ratio/makeup with a soft knee.
// The detector blends RMS body and peak events so kicks keep punch while the
// bus gets denser and more coherent.
class GlueCompressor
{
public:
    void prepare(double sr)
    {
        sampleRate    = sr;
        envelope      = 0.0f;
        rmsState      = 0.0f;
        lastReductionDb = 0.0f;
        updateCoeffs();
    }

    void setAmount(float a)
    {
        amount = std::clamp(a, 0.0f, 1.0f);
        updateCoeffs();
    }

    void process(float& L, float& R)
    {
        if (amount < 0.001f) return;

        const float aL = std::abs(L), aR = std::abs(R);
        const float peak = aL > aR ? aL : aR;
        rmsState = peak * peak + (rmsState - peak * peak) * rmsCoeff;
        const float rms = std::sqrt(std::max(0.0f, rmsState));
        const float detector = rms * 0.68f + peak * 0.32f;

        const float coef = (detector > envelope) ? attackCoeff : releaseCoeff;
        envelope = detector + (envelope - detector) * coef;

        const float thresholdDb = -6.0f - amount * 9.5f;
        const float ratio       = 1.30f + amount * 3.30f;   // 1.3:1 to 4.6:1
        const float kneeDb      = 5.0f + amount * 5.0f;
        const float makeupDb    = amount * 5.4f;

        const float envDb = 20.0f * std::log10(std::max(envelope, 1.0e-6f));
        const float overDb = envDb - thresholdDb;
        float reductionDb = 0.0f;

        if (overDb > kneeDb * 0.5f)
        {
            reductionDb = overDb * (1.0f - 1.0f / ratio);
        }
        else if (overDb > -kneeDb * 0.5f)
        {
            const float kneePos = overDb + kneeDb * 0.5f;
            reductionDb = (kneePos * kneePos / (2.0f * kneeDb)) * (1.0f - 1.0f / ratio);
        }

        lastReductionDb = reductionDb;
        const float makeupScale = std::clamp(reductionDb / 6.0f, 0.0f, 1.0f);
        const float gain = std::pow(10.0f, (makeupDb * makeupScale - reductionDb) / 20.0f);

        L *= gain;
        R *= gain;
    }

    float getGainReductionDb() const
    {
        return lastReductionDb;
    }

private:
    double sampleRate    = 44100.0;
    float  envelope      = 0.0f;
    float  rmsState      = 0.0f;
    float  attackCoeff   = 0.99f;
    float  releaseCoeff  = 0.999f;
    float  rmsCoeff      = 0.999f;
    float  amount        = 0.0f;
    float  lastReductionDb = 0.0f;

    void updateCoeffs()
    {
        const double attackMs = 0.010 - amount * 0.006;   // 10ms → 4ms
        const double releaseMs = 0.180 - amount * 0.060;  // 180ms → 120ms
        const double rmsMs = 0.026 - amount * 0.010;      // 26ms → 16ms
        attackCoeff = static_cast<float>(std::exp(-1.0 / (attackMs * sampleRate)));
        releaseCoeff = static_cast<float>(std::exp(-1.0 / (releaseMs * sampleRate)));
        rmsCoeff = static_cast<float>(std::exp(-1.0 / (rmsMs * sampleRate)));
    }
};
