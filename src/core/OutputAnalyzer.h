#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cmath>

// Output meter (peak + RMS, atomic for UI) + 8-band spectrum analyzer.
// Spectrum uses Chamberlin state-variable bandpass filters per band — cheap,
// no FFT, no FIFO, no threading concerns. Each band drives a peak envelope
// follower written atomically for the UI.
class OutputAnalyzer
{
public:
    static constexpr int NUM_BANDS  = 8;
    static constexpr int SCOPE_SIZE = 128;
    // Log-spaced octave centres
    static constexpr float bandFreqs[NUM_BANDS] = { 60.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f };

    void prepare(double sr)
    {
        sampleRate = sr;
        peakReleaseCoef = static_cast<float>(std::exp(-1.0 / (0.300 * sr)));   // 300ms peak fall
        rmsCoef         = 1.0f - static_cast<float>(std::exp(-1.0 / (0.030 * sr)));  // 30ms RMS smoothing

        for (int b = 0; b < NUM_BANDS; ++b)
        {
            const auto idx = bandIndex(b);
            // Chamberlin SVF: g = 2 sin(pi f / sr)
            const float f = bandFreqs[idx];
            svfG[idx] = 2.0f * std::sin(static_cast<float>(M_PI) * f / static_cast<float>(sr));
            svfDamp[idx] = 1.0f / 1.5f;   // Q = 1.5
            svfLp[idx] = 0.0f;
            svfBp[idx] = 0.0f;
            bandEnv[idx].store(0.0f, std::memory_order_relaxed);
            bandReleaseCoef[idx] = static_cast<float>(std::exp(-1.0 / (0.150 * sr)));  // 150ms band fall
            bandEnvLocal[idx] = 0.0f;
        }

        peakL.store(0.0f, std::memory_order_relaxed);
        peakR.store(0.0f, std::memory_order_relaxed);
        rmsL.store(0.0f, std::memory_order_relaxed);
        rmsR.store(0.0f, std::memory_order_relaxed);
        peakLLocal = peakRLocal = 0.0f;
        rmsLLocal  = rmsRLocal  = 0.0f;
        scopeLocal.fill(0.0f);
        for (auto& s : scopeOut) s.store(0.0f, std::memory_order_relaxed);
        scopeWrite = 0;
        scopeDiv   = 0;
        scopeHead.store(0, std::memory_order_relaxed);
    }

    // Process one stereo sample (post all FX).
    void process(float L, float R)
    {
        const float aL = std::abs(L), aR = std::abs(R);

        peakLLocal = aL > peakLLocal ? aL : peakLLocal * peakReleaseCoef;
        peakRLocal = aR > peakRLocal ? aR : peakRLocal * peakReleaseCoef;

        rmsLLocal += rmsCoef * (L * L - rmsLLocal);
        rmsRLocal += rmsCoef * (R * R - rmsRLocal);

        // Spectrum: mono mix through 8 SVF bandpasses
        const float mono = (L + R) * 0.5f;
        if ((scopeDiv++ & 1) == 0)
        {
            scopeLocal[static_cast<std::size_t>(scopeWrite)] = mono;
            scopeWrite = (scopeWrite + 1) & (SCOPE_SIZE - 1);
        }
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            const auto idx = bandIndex(b);
            // Chamberlin SVF tick
            const float hp = mono - svfLp[idx] - svfDamp[idx] * svfBp[idx];
            svfBp[idx] += svfG[idx] * hp;
            svfLp[idx] += svfG[idx] * svfBp[idx];
            const float mag = std::abs(svfBp[idx]);
            bandEnvLocal[idx] = mag > bandEnvLocal[idx] ? mag : bandEnvLocal[idx] * bandReleaseCoef[idx];
        }
    }

    // Publish atomic snapshots — call once per block (not per sample) to reduce traffic.
    void publish()
    {
        peakL.store(peakLLocal, std::memory_order_relaxed);
        peakR.store(peakRLocal, std::memory_order_relaxed);
        rmsL.store(rmsLLocal,   std::memory_order_relaxed);
        rmsR.store(rmsRLocal,   std::memory_order_relaxed);
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            const auto idx = bandIndex(b);
            bandEnv[idx].store(bandEnvLocal[idx], std::memory_order_relaxed);
        }
        for (int i = 0; i < SCOPE_SIZE; ++i)
            scopeOut[static_cast<std::size_t>(i)].store(scopeLocal[static_cast<std::size_t>(i)], std::memory_order_relaxed);
        scopeHead.store(scopeWrite, std::memory_order_release);
    }

    // ── UI thread getters ──
    float getPeakL() const { return peakL.load(std::memory_order_relaxed); }
    float getPeakR() const { return peakR.load(std::memory_order_relaxed); }
    float getRmsL()  const { return std::sqrt(rmsL.load(std::memory_order_relaxed)); }
    float getRmsR()  const { return std::sqrt(rmsR.load(std::memory_order_relaxed)); }
    float getBand(int b) const
    {
        return b >= 0 && b < NUM_BANDS
            ? bandEnv[bandIndex(b)].load(std::memory_order_relaxed)
            : 0.0f;
    }
    int getScopeHead() const { return scopeHead.load(std::memory_order_acquire); }
    float getScopeSample(int i) const
    {
        return scopeOut[static_cast<std::size_t>(i & (SCOPE_SIZE - 1))].load(std::memory_order_relaxed);
    }

private:
    static std::size_t bandIndex(int b) { return static_cast<std::size_t>(b); }

    double sampleRate     = 44100.0;
    float  peakReleaseCoef = 0.999f;
    float  rmsCoef         = 0.001f;

    // Audio-thread local state (not atomic)
    float peakLLocal = 0.0f, peakRLocal = 0.0f;
    float rmsLLocal  = 0.0f, rmsRLocal  = 0.0f;
    float svfLp[NUM_BANDS] {}, svfBp[NUM_BANDS] {};
    float svfG[NUM_BANDS]  {}, svfDamp[NUM_BANDS] {};
    float bandEnvLocal[NUM_BANDS] {};
    float bandReleaseCoef[NUM_BANDS] {};

    // UI-thread visible
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> rmsL  { 0.0f }, rmsR  { 0.0f };
    std::array<std::atomic<float>, NUM_BANDS> bandEnv;
    std::array<float, SCOPE_SIZE> scopeLocal {};
    std::array<std::atomic<float>, SCOPE_SIZE> scopeOut {};
    int scopeWrite = 0;
    int scopeDiv   = 0;
    std::atomic<int> scopeHead { 0 };
};
