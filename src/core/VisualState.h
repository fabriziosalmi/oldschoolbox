#pragma once
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

// Real-time visual feedback state.
//
// Audio thread writes timestamps + intensities when voices fire; UI thread
// reads them at 30 Hz and renders glow / pulse effects.
//
// Time is tracked as a monotonic sample counter (so transport pauses do
// NOT freeze the visualisation — UI animates on wall-clock terms). The
// audio thread increments by buffer size each block; the UI thread
// reads the counter snapshot and computes (counter - lastFire) for
// decay calculations.
struct VisualState
{
    static constexpr int NUM_CH = 8;  // mirrors Sequencer::NUM_CHANNELS

    // Monotonic sample counter (increments every block, never resets)
    std::atomic<int64_t> sampleCounter { 0 };

    // Per-channel last-fire info
    std::array<std::atomic<int64_t>, NUM_CH> lastFireSample;
    std::array<std::atomic<float>,   NUM_CH> lastFireVel;

    // Kick-specific quick pulse & toggle counter for kick-synced animations
    std::atomic<float> kickPulse  { 0.0f };  // jumps to 1 on kick, decays in UI render
    std::atomic<int>   kickCount  { 0 };     // increments on every kick (drives invader frame)

    // Total energy accumulator: sum of recent triggers, decays over time.
    // Audio thread adds to it on each fire; UI thread also can read decayed
    // value via (now - lastUpdate) extrapolation if needed.
    std::atomic<float> totalEnergy { 0.0f };

    // Bar tracker for "sustained kicks" intensity escalation
    std::atomic<int>    kickHitsInBar { 0 };
    std::atomic<float>  sustainedIntensity { 0.0f };  // 0..1, rises with N consecutive kick-heavy bars

    VisualState()
    {
        for (auto& f : lastFireSample) f.store(INT64_MIN, std::memory_order_relaxed);
        for (auto& v : lastFireVel)    v.store(0.0f,     std::memory_order_relaxed);
    }

    // Called by audio thread when a step fires for channel `ch`.
    void noteFire(int ch, float velocity)
    {
        if (ch < 0 || ch >= NUM_CH) return;
        const int64_t t = sampleCounter.load(std::memory_order_relaxed);
        const auto idx = static_cast<std::size_t>(ch);
        lastFireSample[idx].store(t, std::memory_order_release);
        lastFireVel[idx].store(velocity, std::memory_order_release);

        // Accumulate global energy (capped)
        float e = totalEnergy.load(std::memory_order_relaxed);
        e += velocity;
        if (e > 64.0f) e = 64.0f;
        totalEnergy.store(e, std::memory_order_release);

        if (ch == 0)  // KICK
        {
            kickPulse.store(1.0f, std::memory_order_release);
            kickHitsInBar.fetch_add(1, std::memory_order_relaxed);
            kickCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Called by audio thread at bar boundary
    void onBar()
    {
        const int hits = kickHitsInBar.exchange(0, std::memory_order_acq_rel);
        float si = sustainedIntensity.load(std::memory_order_relaxed);
        // 3+ kicks in a bar → rise; less than 2 → fall
        if (hits >= 3)       si += 1.0f / 16.0f;   // 16 sustained bars → max
        else if (hits <= 1)  si -= 1.0f / 8.0f;    // 8 quiet bars → zero
        if (si < 0.0f) si = 0.0f;
        if (si > 1.0f) si = 1.0f;
        sustainedIntensity.store(si, std::memory_order_release);
    }

    // Audio thread: advance sample counter (called once per block)
    void advance(int numSamples)
    {
        sampleCounter.fetch_add(numSamples, std::memory_order_release);
        // Decay totalEnergy ~exponentially. At 48k SR, blockSize=256, decay factor
        // per block ~= 0.97 → energy halves in ~25 blocks ≈ 130ms.
        float e = totalEnergy.load(std::memory_order_relaxed);
        e *= 0.985f;
        if (e < 0.001f) e = 0.0f;
        totalEnergy.store(e, std::memory_order_release);
        // Same for kick pulse
        float kp = kickPulse.load(std::memory_order_relaxed);
        kp *= 0.92f;
        if (kp < 0.001f) kp = 0.0f;
        kickPulse.store(kp, std::memory_order_release);
    }
};
