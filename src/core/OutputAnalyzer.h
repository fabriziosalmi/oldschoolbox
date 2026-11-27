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
    static constexpr int NUM_VOICES = 8;
    static constexpr int SCOPE_SIZE = 128;
    // Log-spaced octave centres
    static constexpr float bandFreqs[NUM_BANDS] = { 60.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f };

    void prepare(double sr)
    {
        sampleRate = sr;
        peakReleaseCoef = static_cast<float>(std::exp(-1.0 / (0.300 * sr)));   // 300ms peak fall
        rmsCoef         = 1.0f - static_cast<float>(std::exp(-1.0 / (0.030 * sr)));  // 30ms RMS smoothing
        voiceBlockDecay = static_cast<float>(std::exp(-256.0 / (0.180 * sr))); // ~180ms fall per 256-sample block
        corrCoef        = 1.0f - static_cast<float>(std::exp(-1.0 / (0.060 * sr)));  // 60ms phase correlation

        for (int b = 0; b < NUM_BANDS; ++b)
        {
            const auto idx = bandIndex(b);
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
        for (int v = 0; v < NUM_VOICES; ++v)
        {
            voicePeakLocal[static_cast<std::size_t>(v)] = 0.0f;
            voicePeakOut[static_cast<std::size_t>(v)].store(0.0f, std::memory_order_relaxed);
        }
        chaosPeakLocal = 0.0f;
        chaosPeakOut.store(0.0f, std::memory_order_relaxed);
        compGrDbOut.store(0.0f, std::memory_order_relaxed);
        corrNum = corrDenL = corrDenR = 0.0f;
        phaseCorrOut.store(1.0f, std::memory_order_relaxed);

        peakLLocal = peakRLocal = 0.0f;
        rmsLLocal  = rmsRLocal  = 0.0f;
        scopeLocal.fill(0.0f);
        for (auto& s : scopeOut) s.store(0.0f, std::memory_order_relaxed);
        scopeWrite = 0;
        scopeDiv   = 0;
        scopeHead.store(0, std::memory_order_relaxed);
    }

    // Audio thread: decay all per-voice & chaos peaks once per block before feeding active voices
    void beginBlock(int numSamples)
    {
        const float decay = static_cast<float>(std::exp(-static_cast<double>(numSamples) / (0.180 * std::max(8000.0, sampleRate))));
        for (int v = 0; v < NUM_VOICES; ++v)
            voicePeakLocal[static_cast<std::size_t>(v)] *= decay;
        chaosPeakLocal *= decay;
    }

    // Audio thread: feed post-fader stereo audio for voice 0..7
    void feedVoiceBlock(int voice, const float* L, const float* R, int n)
    {
        if (voice < 0 || voice >= NUM_VOICES || !L || !R) return;
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max(std::abs(L[i]), std::abs(R[i]));
            if (a > pk) pk = a;
        }
        auto& dst = voicePeakLocal[static_cast<std::size_t>(voice)];
        if (pk > dst) dst = pk;
    }

    // Audio thread: feed Chaos Sampler output sample
    void feedChaosSample(float L, float R)
    {
        const float a = std::max(std::abs(L), std::abs(R));
        if (a > chaosPeakLocal) chaosPeakLocal = a;
    }

    // Audio thread: store Glue Compressor Gain Reduction (dB)
    void setCompGrDb(float grDb)
    {
        compGrDbOut.store(std::max(0.0f, grDb), std::memory_order_relaxed);
    }

    // Process one stereo sample (post all FX, NaN/Inf sanitized).
    void process(float L, float R)
    {
        if (!std::isfinite(L)) L = 0.0f;
        if (!std::isfinite(R)) R = 0.0f;
        const float aL = std::abs(L), aR = std::abs(R);

        peakLLocal = aL > peakLLocal ? aL : peakLLocal * peakReleaseCoef;
        peakRLocal = aR > peakRLocal ? aR : peakRLocal * peakReleaseCoef;

        rmsLLocal += rmsCoef * (L * L - rmsLLocal);
        rmsRLocal += rmsCoef * (R * R - rmsRLocal);

        // L/R Phase Correlation (-1..+1)
        corrNum  += corrCoef * (L * R - corrNum);
        corrDenL += corrCoef * (L * L - corrDenL);
        corrDenR += corrCoef * (R * R - corrDenR);

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
        for (int v = 0; v < NUM_VOICES; ++v)
            voicePeakOut[static_cast<std::size_t>(v)].store(voicePeakLocal[static_cast<std::size_t>(v)], std::memory_order_relaxed);
        chaosPeakOut.store(chaosPeakLocal, std::memory_order_relaxed);

        const float denom = std::sqrt(std::max(1.0e-12f, corrDenL * corrDenR));
        const float corr = (corrDenL + corrDenR > 1.0e-7f) ? std::clamp(corrNum / denom, -1.0f, 1.0f) : 1.0f;
        phaseCorrOut.store(corr, std::memory_order_relaxed);

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
    float getVoicePeak(int v) const
    {
        return (v >= 0 && v < NUM_VOICES) ? voicePeakOut[static_cast<std::size_t>(v)].load(std::memory_order_relaxed) : 0.0f;
    }
    float getChaosPeak() const { return chaosPeakOut.load(std::memory_order_relaxed); }
    float getCompGrDb()  const { return compGrDbOut.load(std::memory_order_relaxed); }
    float getPhaseCorr() const { return phaseCorrOut.load(std::memory_order_relaxed); }
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
    float  voiceBlockDecay = 0.95f;
    float  corrCoef        = 0.001f;

    // Audio-thread local state (not atomic)
    float peakLLocal = 0.0f, peakRLocal = 0.0f;
    float rmsLLocal  = 0.0f, rmsRLocal  = 0.0f;
    std::array<float, NUM_VOICES> voicePeakLocal {};
    float chaosPeakLocal = 0.0f;
    float corrNum = 0.0f, corrDenL = 0.0f, corrDenR = 0.0f;
    float svfLp[NUM_BANDS] {}, svfBp[NUM_BANDS] {};
    float svfG[NUM_BANDS]  {}, svfDamp[NUM_BANDS] {};
    float bandEnvLocal[NUM_BANDS] {};
    float bandReleaseCoef[NUM_BANDS] {};

    // UI-thread visible
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> rmsL  { 0.0f }, rmsR  { 0.0f };
    std::array<std::atomic<float>, NUM_VOICES> voicePeakOut {};
    std::atomic<float> chaosPeakOut { 0.0f };
    std::atomic<float> compGrDbOut  { 0.0f };
    std::atomic<float> phaseCorrOut { 1.0f };
    std::array<std::atomic<float>, NUM_BANDS> bandEnv;
    std::array<float, SCOPE_SIZE> scopeLocal {};
    std::array<std::atomic<float>, SCOPE_SIZE> scopeOut {};
    int scopeWrite = 0;
    int scopeDiv   = 0;
    std::atomic<int> scopeHead { 0 };
};
