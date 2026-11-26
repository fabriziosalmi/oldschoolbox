#pragma once
#include <cmath>
#include <algorithm>
#include "../voices/DspConstants.h"

// Master peak-following soft limiter.
//
// Replaces the previous single-fastTanh master "limit" which clipped hard for
// |x| > 3 and introduced ~6th-order harmonic distortion already at |x| > 0.5.
// With 8 voices summing to potentially ±2-3 the tanh-only approach made the
// output sound crunchy.
//
// New approach:
//   1. Peak detector with fast attack (~1 ms) + slow release (~50 ms)
//   2. Gain reduction = threshold / envelope when envelope > threshold
//   3. fastTanh at the very end as a final clean safety net
//
// The envelope smoothing avoids audible clicks when peaks change quickly,
// the soft-knee just under threshold avoids sudden "pumping", and the final
// tanh catches anything still over ±1.0 cleanly.
class MasterLimiter
{
public:
    void prepare(double sr)
    {
        sampleRate   = sr;
        attackCoef   = static_cast<float>(std::exp(-1.0 / (0.0005 * sr)));  // 0.5 ms
        releaseCoef  = static_cast<float>(std::exp(-1.0 / (0.080 * sr)));   // 80 ms
        envelope     = 0.0f;
        prevL = prevR = 0.0f;
    }

    void setThreshold(float t) { threshold = std::clamp(t, 0.5f, 1.0f); }
    void setCeiling(float c) { ceiling = std::clamp(c, 0.80f, 0.966f); } // max -0.3 dBTP
    void setHeadroomDb(float dB)
    {
        // Optional pre-gain reduction in dB (negative). Adds headroom before
        // the limiter — useful when many voices stack up.
        headroomGain = std::pow(10.0f, dB / 20.0f);
    }

    void process(float& L, float& R)
    {
        // Pre-gain (headroom)
        L *= headroomGain;
        R *= headroomGain;

        // 2× True-Peak estimator (catches inter-sample overshoots)
        const float midL = 0.5f * (prevL + L);
        const float midR = 0.5f * (prevR + R);
        prevL = L;
        prevR = R;
        const float peakNow = std::max(std::abs(L), std::abs(R));
        const float peakMid = std::max(std::abs(midL), std::abs(midR));
        const float peak = std::max(peakNow, peakMid);

        const float coef = (peak > envelope) ? attackCoef : releaseCoef;
        envelope = peak + (envelope - peak) * coef;

        // Gain reduction with soft knee under threshold
        float gain = 1.0f;
        if (envelope > threshold)
        {
            gain = threshold / envelope;
        }
        else if (envelope > threshold * 0.707f)  // -3 dB below threshold
        {
            const float over = (envelope - threshold * 0.707f) / (threshold - threshold * 0.707f);
            gain = 1.0f - over * 0.08f;
        }

        L *= gain;
        R *= gain;

        L = softCeil(L);
        R = softCeil(R);
    }

    float getGainReductionDb() const
    {
        if (envelope <= threshold || envelope < 1e-6f) return 0.0f;
        return 20.0f * std::log10(envelope / threshold);
    }

private:
    double sampleRate     = 44100.0;
    float  attackCoef     = 0.99f;
    float  releaseCoef    = 0.999f;
    float  envelope       = 0.0f;
    float  threshold      = 0.92f;
    float  ceiling        = 0.966f; // -0.3 dBTP
    float  headroomGain   = 1.0f;  // 1.0 = no pre-gain reduction
    float  prevL = 0.0f, prevR = 0.0f;

    float softCeil(float x) const
    {
        const float ax = std::abs(x);
        if (ax <= ceiling * 0.95f)
            return x;

        const float knee = ceiling * 0.95f;
        const float over = ax - knee;
        const float span = std::max(1.0e-4f, ceiling - knee);
        const float shaped = knee + span * fastTanh(over / span);
        return std::copysign(std::min(shaped, ceiling), x);
    }
};
