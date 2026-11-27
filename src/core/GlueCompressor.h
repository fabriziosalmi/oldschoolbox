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
        scHpAlpha     = static_cast<float>(std::exp(-6.283185307179586 * 65.0 / std::max(8000.0, sr)));
        scHpL = scHpPrevL = scHpR = scHpPrevR = 0.0f;
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

        // 90 Hz Sidechain High-Pass on RMS body detector (SSL/API thrust style):
        // prevents sustained sub-bass from pumping hats/303 while Peak detector still tames heavy transients!
        scHpL = scHpAlpha * (scHpL + L - scHpPrevL); scHpPrevL = L;
        scHpR = scHpAlpha * (scHpR + R - scHpPrevR); scHpPrevR = R;

        const float hpPeak  = std::max(std::abs(scHpL), std::abs(scHpR));
        const float rawPeak = std::max(std::abs(L), std::abs(R));
        rmsState = hpPeak * hpPeak + (rmsState - hpPeak * hpPeak) * rmsCoeff;
        const float rms = std::sqrt(std::max(0.0f, rmsState));
        const float detector = rms * 0.55f + rawPeak * 0.45f;

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
    float  scHpAlpha     = 0.985f;
    float  scHpL = 0.0f, scHpPrevL = 0.0f, scHpR = 0.0f, scHpPrevR = 0.0f;

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
