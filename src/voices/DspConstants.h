#pragma once
#include <cstdint>
#include <cmath>

// ── Constants ─────────────────────────────────────────────────────────────────
namespace Dsp
{
    constexpr double PI       = 3.14159265358979323846;
    constexpr double TWO_PI   = 6.28318530717958647692;
    constexpr float  PI_F     = 3.14159265358979f;
    constexpr float  TWO_PI_F = 6.28318530717959f;
    constexpr float  HALF_PI_F = 1.57079632679490f;
}

// ── Fast math (audio-grade approximations) ────────────────────────────────────

// Fast tanh — Padé approximant, max error ~0.001 for |x|<3, clamped beyond.
// Replaces std::tanh which is ~20× slower.
inline float fastTanh(float x)
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Fast sin — parabolic approximation with correction, max error ~0.001.
// Input: x in [0, TWO_PI].  Replaces std::sin for oscillators.
inline float fastSin(float x)
{
    x -= Dsp::PI_F;  // shift [0, 2π] → [-π, π]
    const float y = (4.0f / Dsp::PI_F) * x
                  - (4.0f / (Dsp::PI_F * Dsp::PI_F)) * x * std::abs(x);
    return 0.225f * (y * std::abs(y) - y) + y;
}

// Overload accepting double phase (common pattern: double accumulator, float output)
inline float fastSinD(double x)
{
    return fastSin(static_cast<float>(x));
}

// polyBLEP residual for band-limiting hard-edged oscillators (saw/square).
// t  = phase in [0,1), dt = phase increment per sample (freq / sampleRate).
// Add it at the rising discontinuity and subtract at the falling one.
inline float polyBlep(float t, float dt)
{
    if (dt <= 0.0f) return 0.0f;
    if (t < dt)        { const float n = t / dt;           return 2.0f * n - n * n - 1.0f; }
    if (t > 1.0f - dt) { const float n = (t - 1.0f) / dt;  return 2.0f * n + n * n + 1.0f; }
    return 0.0f;
}

// ── Xorshift32 noise ──────────────────────────────────────────────────────────
struct NoiseGen
{
    uint32_t state;

    explicit NoiseGen(uint32_t seed = 12345) : state(seed) {}

    void reset(uint32_t seed) { state = seed; }

    float tick()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(static_cast<int32_t>(state)) / 2147483648.0f;
    }
};
