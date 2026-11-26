#pragma once
#include <cmath>
#include "DspConstants.h"

// Shared anti-aliasing primitives.
//
// BiquadLP: RBJ-cookbook lowpass, transposed Direct Form II (one state pair).
// Decimator4: 6th-order Butterworth lowpass (3 cascaded biquads) used as the
// decimation filter when a 4x-oversampled nonlinear stream is downsampled back
// to the base rate — push every oversampled sample through process(), keep the
// value at the last sub-sample. Cuts the >base-Nyquist images the nonlinearity
// generates before they fold back as aliasing.

struct BiquadLP
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void set(double fc, double fs, double Q)
    {
        const double w0 = 2.0 * Dsp::PI * fc / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        b0 = static_cast<float>(((1.0 - cw) * 0.5) / a0);
        b1 = static_cast<float>((1.0 - cw) / a0);
        b2 = b0;
        a1 = static_cast<float>((-2.0 * cw) / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    void reset() { z1 = z2 = 0.0f; }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Linear-phase windowed-sinc FIR decimation lowpass. A Butterworth is far too
// gentle for the narrow transition a 4x→1x decimation needs (pass 0-20k, reject
// 24k+), leaving ~20dB of fold-back. This FIR (Blackman window, ~-75dB stopband)
// gives the steep transition. Coefficients are computed from osRate at prepare.
// Usage: push() every oversampled sample, read output() once per output sample.
struct FirDecimator
{
    static constexpr int TAPS = 96;
    float h[TAPS] = {};
    float z[TAPS] = {};
    int   pos = 0;

    void prepare(double osRate)
    {
        const double fc = 20500.0;                 // just below base Nyquist (22.05k @ 44.1k out)
        const double wc = 2.0 * Dsp::PI * fc / osRate;
        const double m  = (TAPS - 1) / 2.0;
        double sum = 0.0;
        for (int i = 0; i < TAPS; ++i)
        {
            const double x = i - m;
            const double sinc = (std::abs(x) < 1e-9) ? (wc / Dsp::PI)
                                                     : (std::sin(wc * x) / (Dsp::PI * x));
            const double w = 0.42 - 0.5 * std::cos(2.0 * Dsp::PI * i / (TAPS - 1))
                                  + 0.08 * std::cos(4.0 * Dsp::PI * i / (TAPS - 1));  // Blackman
            h[i] = static_cast<float>(sinc * w);
            sum += h[i];
        }
        const float norm = static_cast<float>(1.0 / sum);    // unity DC gain
        for (int i = 0; i < TAPS; ++i) h[i] *= norm;
        reset();
    }

    void reset() { for (auto& v : z) v = 0.0f; pos = 0; }

    void push(float x) { z[pos] = x; pos = (pos + 1) % TAPS; }

    float output() const
    {
        float acc = 0.0f;
        int idx = pos;
        for (int i = 0; i < TAPS; ++i) { idx = (idx == 0 ? TAPS - 1 : idx - 1); acc += h[i] * z[idx]; }
        return acc;
    }
};
