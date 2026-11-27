#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include "../voices/DspConstants.h"

// Beat-Synced Per-Channel LFO Bank (8 channels, phase-locked to PPQ/Bar clock).
// Rates: 1/2B (2 beats), 1B (4 beats), 2B, 4B, 8B, 16B, 32B (128 beats).
// Depth is hard-capped to 0..50% of normalized parameter range so modulation
// stays musical and never slams cutoff/tune into extreme unusable corners.
struct BeatLfoBank
{
    static constexpr int NUM_CH    = 8;
    static constexpr int NUM_RATES = 7;
    static constexpr int NUM_WAVES = 4;

    static constexpr float kRateBeats[NUM_RATES] = { 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f };

    static const char* rateName(int r)
    {
        static const char* kNames[NUM_RATES] = { "1/2B", "1B", "2B", "4B", "8B", "16B", "32B" };
        return kNames[std::clamp(r, 0, NUM_RATES - 1)];
    }

    static const char* waveName(int w)
    {
        static const char* kNames[NUM_WAVES] = { "SIN", "TRI", "UP", "DN" };
        return kNames[std::clamp(w, 0, NUM_WAVES - 1)];
    }

    // 2 musical modulation targets per channel (slot 0 = primary, slot 1 = secondary)
    static const char* targetName(int ch, int slot)
    {
        static const char* kTargets[NUM_CH][2] = {
            { "TUNE",   "DECAY" },  // 0 KICK
            { "TUNE",   "SNAP"  },  // 1 SNARE
            { "DECAY",  "TONE"  },  // 2 HATS
            { "TUNE",   "DECAY" },  // 3 TOM
            { "TUNE",   "DRIVE" },  // 4 PERC1
            { "TUNE",   "PITCH" },  // 5 PERC2
            { "CUTOFF", "RES"   },  // 6 BASS1
            { "CUTOFF", "RES"   },  // 7 BASS2
        };
        return kTargets[std::clamp(ch, 0, NUM_CH - 1)][slot & 1];
    }

    struct Channel
    {
        std::atomic<bool>  enabled   { false };
        std::atomic<int>   rateIdx   { 6 };     // default: 32B (128 beats)
        std::atomic<int>   waveIdx   { 0 };     // default: SIN
        std::atomic<int>   targetIdx { 0 };     // default: primary target (CUTOFF / TUNE / DECAY)
        std::atomic<float> depth     { 0.35f }; // 0.0 .. 0.50 (max 50% of param range)

        // Published by audio thread each block for UI live modulation rings & phase display
        std::atomic<float> currentOffset { 0.0f }; // bipolar normalized offset in [-depth*0.5 .. +depth*0.5]
        std::atomic<float> currentPhase  { 0.0f }; // 0..1 cycle position
    };

    std::array<Channel, NUM_CH> channels;

    static std::size_t idx(int ch) { return static_cast<std::size_t>(std::clamp(ch, 0, NUM_CH - 1)); }

    // Evaluate bipolar waveform in [-0.5 .. +0.5] for phase in [0..1)
    static float evalWave(int waveIdx, float phase)
    {
        const float p = phase - std::floor(phase);
        switch (waveIdx)
        {
            case 0: // SIN (starts at 0, rises smoothly, falls smoothly)
                return 0.5f * std::sin(p * Dsp::TWO_PI_F);
            case 1: // TRI
                return (p < 0.25f) ? (p * 2.0f)
                     : (p < 0.75f) ? (1.0f - p * 2.0f)
                                   : (p * 2.0f - 2.0f);
            case 2: // UP (build-up sawtooth -0.5 -> +0.5)
                return p - 0.5f;
            case 3: // DN (decay sawtooth +0.5 -> -0.5)
                return 0.5f - p;
            default:
                return 0.0f;
        }
    }

    // Audio thread: update all 8 LFOs deterministically from musical PPQ clock
    void update(double ppqPos)
    {
        if (ppqPos < 0.0) ppqPos = 0.0;
        for (int ch = 0; ch < NUM_CH; ++ch)
        {
            auto& c = channels[idx(ch)];
            if (!c.enabled.load(std::memory_order_relaxed))
            {
                c.currentOffset.store(0.0f, std::memory_order_relaxed);
                continue;
            }
            const int rIdx = std::clamp(c.rateIdx.load(std::memory_order_relaxed), 0, NUM_RATES - 1);
            const int wIdx = std::clamp(c.waveIdx.load(std::memory_order_relaxed), 0, NUM_WAVES - 1);
            const float d  = std::clamp(c.depth.load(std::memory_order_relaxed), 0.0f, 0.50f);
            const double periodBeats = static_cast<double>(kRateBeats[rIdx]);
            const float phase = static_cast<float>(std::fmod(ppqPos, periodBeats) / periodBeats);
            const float offset = evalWave(wIdx, phase) * d; // peak-to-peak span = d (max 50% of range!)
            c.currentPhase.store(phase, std::memory_order_relaxed);
            c.currentOffset.store(offset, std::memory_order_relaxed);
        }
    }

    // Audio & UI thread: get active normalized modulation offset for (ch, targetSlot)
    float getOffsetFor(int ch, int targetSlot) const
    {
        const auto& c = channels[idx(ch)];
        if (!c.enabled.load(std::memory_order_relaxed)) return 0.0f;
        if (c.targetIdx.load(std::memory_order_relaxed) != targetSlot) return 0.0f;
        return c.currentOffset.load(std::memory_order_relaxed);
    }
};
