#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstddef>

// Freeverb-style plate reverb: 8 lowpass-feedback combs in parallel + 4 allpass in series.
// Tunings scaled by sample rate. Fixed max buffer sizes to avoid runtime allocation.
class PlateReverb
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        const double scale = sr / 44100.0;
        for (int i = 0; i < 8; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            combL[idx].size = static_cast<int>(combTunings[idx] * scale);
            combR[idx].size = static_cast<int>((combTunings[idx] + stereoSpread) * scale);
            if (combL[idx].size > MAX_COMB) combL[idx].size = MAX_COMB;
            if (combR[idx].size > MAX_COMB) combR[idx].size = MAX_COMB;
        }
        for (int i = 0; i < 4; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            allpassL[idx].size = static_cast<int>(allpassTunings[idx] * scale);
            allpassR[idx].size = static_cast<int>((allpassTunings[idx] + stereoSpread) * scale);
            if (allpassL[idx].size > MAX_AP) allpassL[idx].size = MAX_AP;
            if (allpassR[idx].size > MAX_AP) allpassR[idx].size = MAX_AP;
        }
        reset();
    }

    void reset()
    {
        for (auto& c : combL)    { c.buf.fill(0); c.filterStore = 0; c.idx = 0; }
        for (auto& c : combR)    { c.buf.fill(0); c.filterStore = 0; c.idx = 0; }
        for (auto& a : allpassL) { a.buf.fill(0); a.idx = 0; }
        for (auto& a : allpassR) { a.buf.fill(0); a.idx = 0; }
    }

    // size: 0..1 (room size — feedback). damp: 0..1 (HF damping, sample-rate invariant!).
    void setParams(float size, float damp)
    {
        roomFeedback = 0.70f + size * 0.27f;     // 0.70 .. 0.97
        const float baseDamp = 0.10f + damp * 0.40f; // 0.10 .. 0.50 @ 44.1 kHz
        // Scale 1-pole damping coefficient by 44100/sr so reverb tone is identical at 44.1..192 kHz!
        damping = std::pow(baseDamp, static_cast<float>(44100.0 / std::max(8000.0, sampleRate)));
    }

    // Process one stereo sample. Returns wet signal (caller mixes with dry).
    // Output is tuned for ~unity gain at maximum room size.
    void process(float inL, float inR, float& outL, float& outR)
    {
        // Lower input scale (0.0075 -> 0.0045) — the previous value pushed wet
        // ≈1.8× input at heavy feedback, which clipped when stacked on dry.
        const float scaledIn = (inL + inR) * 0.0045f;
        float wL = 0.0f, wR = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            wL += combL[idx].tick(scaledIn, roomFeedback, damping);
            wR += combR[idx].tick(scaledIn, roomFeedback, damping);
        }
        for (int i = 0; i < 4; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            wL = allpassL[idx].tick(wL);
            wR = allpassR[idx].tick(wR);
        }
        outL = wL;
        outR = wR;
    }

private:
    static constexpr int combTunings[8]    = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr int allpassTunings[4] = { 556, 441, 341, 225 };
    static constexpr int stereoSpread      = 23;
    static constexpr int MAX_COMB          = 8192;   // covers up to 192kHz
    static constexpr int MAX_AP            = 2048;

    struct Comb
    {
        std::array<float, MAX_COMB> buf {};
        int   size = 1, idx = 0;
        float filterStore = 0.0f;
        float tick(float in, float fb, float damp)
        {
            const float out = buf[static_cast<std::size_t>(idx)];
            filterStore = out * (1.0f - damp) + filterStore * damp;
            buf[static_cast<std::size_t>(idx)] = in + filterStore * fb;
            idx = (idx + 1) % size;
            return out;
        }
    };

    struct Allpass
    {
        std::array<float, MAX_AP> buf {};
        int size = 1, idx = 0;
        float tick(float in)
        {
            const float bufout = buf[static_cast<std::size_t>(idx)];
            const float out    = bufout - in;
            buf[static_cast<std::size_t>(idx)] = in + bufout * 0.5f;
            idx = (idx + 1) % size;
            return out;
        }
    };

    std::array<Comb, 8>     combL, combR;
    std::array<Allpass, 4>  allpassL, allpassR;
    double sampleRate     = 44100.0;
    float  roomFeedback   = 0.85f;
    float  damping        = 0.20f;
};
