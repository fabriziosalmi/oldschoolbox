#pragma once
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

// Per-channel step sequencer. Patterns are uint64_t bitmasks (up to 64 steps).
// Velocities, pattern lengths, and current playhead positions are all thread-safe atomics.
struct Sequencer
{
    static constexpr int NUM_CHANNELS = 8;
    static constexpr int MAX_STEPS    = 64;

    Sequencer()
    {
        for (auto& p : patterns)       p.store(0, std::memory_order_relaxed);
        for (auto& m : mute)           m.store(false, std::memory_order_relaxed);
        for (auto& s : solo)           s.store(false, std::memory_order_relaxed);
        for (auto& h : humanize)       h.store(false, std::memory_order_relaxed);
        for (auto& a : autoVar)        a.store(false, std::memory_order_relaxed);
        for (auto& b : bassSlide)      b.store(0, std::memory_order_relaxed);
        for (auto& b : bassAccent)     b.store(0, std::memory_order_relaxed);
        for (auto& b : bassOctUp)      b.store(0, std::memory_order_relaxed);
        for (auto& b : bassOctDown)    b.store(0, std::memory_order_relaxed);
        for (auto& row : velocities)
            for (auto& v : row)
                v.store(100, std::memory_order_relaxed);
        for (auto& row : probabilities)
            for (auto& p : row)
                p.store(100, std::memory_order_relaxed);
        for (auto& row : microTiming)
            for (auto& t : row)
                t.store(0, std::memory_order_relaxed);
        for (auto& l : patternLengths)
            l.store(16, std::memory_order_relaxed);
        for (auto& s : currentSteps)
            s.store(-1, std::memory_order_relaxed);
        static constexpr int8_t acidSeed[16] = {
            0, 0, 7, 0, 10, 7, 0, 12, 3, 0, 7, 10, 0, 5, 7, 0
        };
        for (auto& row : bassNoteOffsets)
            for (int st = 0; st < MAX_STEPS; ++st)
                row[stepIndex(st)].store(acidSeed[st % 16], std::memory_order_relaxed);
    }

    // ── Step on/off ──────────────────────────────────────────────────────────

    void toggleStep(int ch, int step)
    {
        if (!valid(ch, step)) return;
        const uint64_t mask = 1ULL << step;
        patterns[chIndex(ch)].fetch_xor(mask, std::memory_order_acq_rel);
    }

    bool getStep(int ch, int step) const
    {
        if (!valid(ch, step)) return false;
        return (patterns[chIndex(ch)].load(std::memory_order_acquire) >> step) & 1ULL;
    }

    void setStep(int ch, int step, bool on)
    {
        if (!valid(ch, step)) return;
        const uint64_t mask = 1ULL << step;
        if (on)
            patterns[chIndex(ch)].fetch_or(mask, std::memory_order_acq_rel);
        else
            patterns[chIndex(ch)].fetch_and(~mask, std::memory_order_acq_rel);
    }

    uint64_t getPattern(int ch) const { return validChannel(ch) ? patterns[chIndex(ch)].load(std::memory_order_acquire) : 0; }
    void     setPattern(int ch, uint64_t pat) { if (validChannel(ch)) patterns[chIndex(ch)].store(pat, std::memory_order_release); }

    // ── Velocity ─────────────────────────────────────────────────────────────

    uint8_t getVelocity(int ch, int step) const
    {
        if (!valid(ch, step)) return 100;
        return velocities[chIndex(ch)][stepIndex(step)].load(std::memory_order_acquire);
    }

    void setVelocity(int ch, int step, uint8_t vel)
    {
        if (!valid(ch, step)) return;
        velocities[chIndex(ch)][stepIndex(step)].store(vel, std::memory_order_release);
    }

    // ── Probability (0-100, default 100 = always fire) ───────────────────────
    uint8_t getProbability(int ch, int step) const
    {
        if (!valid(ch, step)) return 100;
        return probabilities[chIndex(ch)][stepIndex(step)].load(std::memory_order_acquire);
    }

    void setProbability(int ch, int step, uint8_t prob)
    {
        if (!valid(ch, step)) return;
        probabilities[chIndex(ch)][stepIndex(step)].store(prob > 100 ? 100 : prob, std::memory_order_release);
    }

    // ── Micro-timing (-50..+50, units of % of step width; default 0 = on grid)
    int8_t getMicroTiming(int ch, int step) const
    {
        if (!valid(ch, step)) return 0;
        return microTiming[chIndex(ch)][stepIndex(step)].load(std::memory_order_acquire);
    }

    void setMicroTiming(int ch, int step, int8_t t)
    {
        if (!valid(ch, step)) return;
        if (t < -50) t = -50;
        if (t >  50) t =  50;
        microTiming[chIndex(ch)][stepIndex(step)].store(t, std::memory_order_release);
    }

    // ── Pattern length (1..64 steps — true polyrhythm!) ──────────────────────

    int  getPatternLength(int ch) const { return validChannel(ch) ? patternLengths[chIndex(ch)].load(std::memory_order_acquire) : 16; }
    void setPatternLength(int ch, int len)
    {
        const int clamped = (len < 1) ? 1 : (len > MAX_STEPS) ? MAX_STEPS : len;
        if (validChannel(ch))
            patternLengths[chIndex(ch)].store(static_cast<uint8_t>(clamped), std::memory_order_release);
    }

    // ── Acid Scale Lock (underground scales for infallible live randomization) ─
    enum class AcidScale : int { Phrygian23 = 0, DarkMinor = 1, Hypno2Note = 2, Diminished = 3, Count = 4 };
    std::atomic<int> acidScale { 0 };

    static const char* acidScaleName(int s)
    {
        switch (s % static_cast<int>(AcidScale::Count))
        {
            case 0: return "PHR23";
            case 1: return "DMIN";
            case 2: return "HYP2";
            case 3: return "DIM";
            default: return "PHR23";
        }
    }

    static int8_t pickScaleNote(int scaleIdx, uint32_t rnd)
    {
        static constexpr int8_t kScales[4][8] = {
            { 0, 1, 3, 5, 7, 8, 10, 12 },   // Phrygian 23 (dark Free Tekno tension)
            { 0, 0, 2, 3, 5, 7, 10, 12 },   // Dark Minor
            { 0, 0, 0, 1, 0, 12, 1, 0  },   // Hypno 2-Note (Tribe/Mental)
            { 0, 3, 6, 9, 12, 0, 3, 6  }    // Diminished
        };
        const int s = (scaleIdx < 0 || scaleIdx >= 4) ? 0 : scaleIdx;
        return kScales[s][rnd % 8];
    }

    // ── Utility ───────────────────────────────────────────────────────────────

    void clearChannel(int ch)
    {
        if (ch >= 0 && ch < NUM_CHANNELS)
        {
            patterns[chIndex(ch)].store(0, std::memory_order_release);
            if (ch >= 6) { // bass channels
                const int bi = ch - 6;
                bassSlide[biIndex(bi)].store(0, std::memory_order_release);
                bassAccent[biIndex(bi)].store(0, std::memory_order_release);
                bassOctUp[biIndex(bi)].store(0, std::memory_order_release);
                bassOctDown[biIndex(bi)].store(0, std::memory_order_release);
                for (int st = 0; st < MAX_STEPS; ++st)
                    bassNoteOffsets[biIndex(bi)][stepIndex(st)].store(0, std::memory_order_release);
            }
        }
    }

    // Smart randomizer: respects musical rules per channel type
    void smartRandomize(int ch)
    {
        if (ch < 0 || ch >= NUM_CHANNELS) return;
        rngSeed ^= rngSeed << 13; rngSeed ^= rngSeed >> 17; rngSeed ^= rngSeed << 5;
        uint32_t rng = rngSeed ^ static_cast<uint32_t>(ch * 7919);
        auto next = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        auto prob = [&](float p) { return (static_cast<float>(next() & 0xFFFF) / 65535.0f) < p; };

        const int len = getPatternLength(ch);
        uint64_t pat = 0;

        if (ch == 0) { // KICK: 4/4 with occasional variations
            for (int s = 0; s < len; ++s)
                if (s % 4 == 0 || (s % 2 == 0 && prob(0.15f))) pat |= 1ULL << s;
        } else if (ch == 1) { // SNARE: 2&4 with ghost notes
            for (int s = 0; s < len; ++s) {
                if (s % 8 == 4) pat |= 1ULL << s;
                else if (prob(0.12f)) { pat |= 1ULL << s; setVelocity(ch, s, static_cast<uint8_t>(40 + (next() % 40))); }
            }
        } else if (ch == 2) { // HATS: 8ths/16ths
            for (int s = 0; s < len; ++s)
                if (s % 2 == 0 || prob(0.4f)) pat |= 1ULL << s;
        } else if (ch >= 3 && ch <= 5) { // TOM/PERC: sparse
            for (int s = 0; s < len; ++s)
                if (prob(0.18f)) pat |= 1ULL << s;
        } else { // BASS: acid patterns locked to active underground scale
            const int bi = ch - 6;
            const int sc = acidScale.load(std::memory_order_relaxed);
            uint64_t sld = 0, acc = 0, up = 0, dn = 0;
            for (int s = 0; s < len; ++s) {
                if (prob(0.55f)) {
                    pat |= 1ULL << s;
                    if (prob(0.25f)) sld |= 1ULL << s;  // slide
                    if (prob(0.20f)) acc |= 1ULL << s;  // accent
                    if (prob(0.15f)) up  |= 1ULL << s;  // octave up
                    else if (prob(0.10f)) dn |= 1ULL << s; // octave down
                    bassNoteOffsets[biIndex(bi)][stepIndex(s)].store(pickScaleNote(sc, next()), std::memory_order_release);
                }
            }
            bassSlide[biIndex(bi)].store(sld, std::memory_order_release);
            bassAccent[biIndex(bi)].store(acc, std::memory_order_release);
            bassOctUp[biIndex(bi)].store(up, std::memory_order_release);
            bassOctDown[biIndex(bi)].store(dn, std::memory_order_release);
        }
        patterns[chIndex(ch)].store(pat, std::memory_order_release);
    }

    // Randomize only 303 melody/slide/accent/octave for a bass channel (locked to active AcidScale)
    void randomizeBassAcid(int bi)
    {
        if (!validBass(bi)) return;
        rngSeed ^= rngSeed << 13; rngSeed ^= rngSeed >> 17; rngSeed ^= rngSeed << 5;
        uint32_t rng = rngSeed ^ static_cast<uint32_t>((bi + 6) * 104729);
        auto next = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        auto prob = [&](float p) { return (static_cast<float>(next() & 0xFFFF) / 65535.0f) < p; };

        const int ch  = bi + 6;
        const int len = getPatternLength(ch);
        const int sc  = acidScale.load(std::memory_order_relaxed);
        uint64_t sld = 0, acc = 0, up = 0, dn = 0;
        for (int s = 0; s < len; ++s)
        {
            bassNoteOffsets[biIndex(bi)][stepIndex(s)].store(pickScaleNote(sc, next()), std::memory_order_release);
            if (getStep(ch, s))
            {
                if (prob(0.32f)) sld |= 1ULL << s;
                if (prob(0.28f)) acc |= 1ULL << s;
                if (prob(0.20f)) up  |= 1ULL << s;
                else if (prob(0.14f)) dn |= 1ULL << s;
            }
        }
        bassSlide[biIndex(bi)].store(sld, std::memory_order_release);
        bassAccent[biIndex(bi)].store(acc, std::memory_order_release);
        bassOctUp[biIndex(bi)].store(up, std::memory_order_release);
        bassOctDown[biIndex(bi)].store(dn, std::memory_order_release);
    }

    void randomizeChannel(int ch, float density = 0.35f)
    {
        if (ch < 0 || ch >= NUM_CHANNELS) return;
        rngSeed ^= rngSeed << 13; rngSeed ^= rngSeed >> 17; rngSeed ^= rngSeed << 5;
        uint32_t rng = rngSeed ^ static_cast<uint32_t>(ch * 7919);

        const int len = getPatternLength(ch);
        uint64_t pat = 0;
        for (int s = 0; s < len; ++s)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if ((static_cast<float>(rng & 0xFFFF) / 65535.0f) < density)
                pat |= 1ULL << s;
        }
        patterns[chIndex(ch)].store(pat, std::memory_order_release);
    }

    // ── 303 per-step attributes (bass channels only, idx 0-1) ─────────────────

    bool getBassSlide(int bi, int step) const  { return validBassStep(bi, step) && (((bassSlide[biIndex(bi)].load(std::memory_order_acquire) >> step) & 1ULL) != 0); }
    bool getBassAccent(int bi, int step) const { return validBassStep(bi, step) && (((bassAccent[biIndex(bi)].load(std::memory_order_acquire) >> step) & 1ULL) != 0); }
    bool getBassOctUp(int bi, int step) const  { return validBassStep(bi, step) && (((bassOctUp[biIndex(bi)].load(std::memory_order_acquire) >> step) & 1ULL) != 0); }
    bool getBassOctDown(int bi, int step) const{ return validBassStep(bi, step) && (((bassOctDown[biIndex(bi)].load(std::memory_order_acquire) >> step) & 1ULL) != 0); }
    int8_t getBassNoteOffset(int bi, int step) const
    {
        if (bi < 0 || bi >= 2 || step < 0 || step >= MAX_STEPS) return 0;
        return bassNoteOffsets[biIndex(bi)][stepIndex(step)].load(std::memory_order_acquire);
    }

    void toggleBassSlide(int bi, int step)  { if (validBassStep(bi, step)) bassSlide[biIndex(bi)].fetch_xor(1ULL << step, std::memory_order_acq_rel); }
    void toggleBassAccent(int bi, int step) { if (validBassStep(bi, step)) bassAccent[biIndex(bi)].fetch_xor(1ULL << step, std::memory_order_acq_rel); }
    void toggleBassOctUp(int bi, int step)  { if (validBassStep(bi, step)) bassOctUp[biIndex(bi)].fetch_xor(1ULL << step, std::memory_order_acq_rel); }
    void toggleBassOctDown(int bi, int step){ if (validBassStep(bi, step)) bassOctDown[biIndex(bi)].fetch_xor(1ULL << step, std::memory_order_acq_rel); }
    void setBassNoteOffset(int bi, int step, int8_t semis)
    {
        if (bi < 0 || bi >= 2 || step < 0 || step >= MAX_STEPS) return;
        if (semis < -24) semis = -24;
        if (semis >  24) semis =  24;
        bassNoteOffsets[biIndex(bi)][stepIndex(step)].store(semis, std::memory_order_release);
    }

    uint64_t getBassSlidePattern(int bi) const  { return validBass(bi) ? bassSlide[biIndex(bi)].load(std::memory_order_acquire) : 0; }
    uint64_t getBassAccentPattern(int bi) const { return validBass(bi) ? bassAccent[biIndex(bi)].load(std::memory_order_acquire) : 0; }
    uint64_t getBassOctUpPattern(int bi) const  { return validBass(bi) ? bassOctUp[biIndex(bi)].load(std::memory_order_acquire) : 0; }
    uint64_t getBassOctDownPattern(int bi) const{ return validBass(bi) ? bassOctDown[biIndex(bi)].load(std::memory_order_acquire) : 0; }

    void setBassSlidePattern(int bi, uint64_t p)  { if (validBass(bi)) bassSlide[biIndex(bi)].store(p, std::memory_order_release); }
    void setBassAccentPattern(int bi, uint64_t p) { if (validBass(bi)) bassAccent[biIndex(bi)].store(p, std::memory_order_release); }
    void setBassOctUpPattern(int bi, uint64_t p)  { if (validBass(bi)) bassOctUp[biIndex(bi)].store(p, std::memory_order_release); }
    void setBassOctDownPattern(int bi, uint64_t p){ if (validBass(bi)) bassOctDown[biIndex(bi)].store(p, std::memory_order_release); }

    // ── Mute / Solo / Humanize ───────────────────────────────────────────────
    std::array<std::atomic<bool>, NUM_CHANNELS> mute      {};   // UI thread toggle
    std::array<std::atomic<bool>, NUM_CHANNELS> solo      {};   // UI thread toggle
    std::array<std::atomic<bool>, NUM_CHANNELS> humanize  {};   // ±15% vel, ±10% prob fudge

    bool isChannelAudible(int ch) const
    {
        // If any solo is active, only soloed channels are audible
        bool anySolo = false;
        for (int i = 0; i < NUM_CHANNELS; ++i)
            if (solo[chIndex(i)].load(std::memory_order_relaxed)) { anySolo = true; break; }
        if (!validChannel(ch)) return false;
        if (anySolo) return solo[chIndex(ch)].load(std::memory_order_relaxed) && !mute[chIndex(ch)].load(std::memory_order_relaxed);
        return !mute[chIndex(ch)].load(std::memory_order_relaxed);
    }

    // ── Copy/Paste clipboard ──────────────────────────────────────────────────
    int clipboardCh = -1;  // -1 = empty, UI thread only
    uint64_t clipboardPattern = 0;
    uint64_t clipboardBassSlide = 0;
    uint64_t clipboardBassAccent = 0;
    uint64_t clipboardBassOctUp = 0;
    uint64_t clipboardBassOctDown = 0;
    std::array<int8_t, MAX_STEPS> clipboardBassNotes {};

    // ── Scenes / FILL (live performance) ──────────────────────────────────────
    // Snapshots themselves live in PluginProcessor (to keep Sequencer.h slim).
    // These atomics serialise the handoff: UI thread queues, audio thread
    // consumes at bar boundary.
    static constexpr int NUM_SCENES = 4;
    std::atomic<int>     activeScene  { 0 };
    std::atomic<int>     queuedScene  { -1 };   // -1 = no pending switch
    std::atomic<int64_t> fillStartBar { -1 };   // bar at which FILL begins (-1 = no fill)
    // Live DJ quantization: 0 = OFF (immediate), 1 = 1/4 beat, 2 = 1B (bar boundary)
    std::atomic<int>     quantizeMode { 2 };
    // Bitmask of channels with a pending quantized MUTE toggle
    std::atomic<uint8_t> queuedMuteMask { 0 };
    // Set by audio thread when a quantized scene switch/revert fires, so UI applies saved APVTS params
    std::atomic<int>     pendingParamSceneApply { -1 };
    // Queue a Snap-Back / Panic Reload of activeScene on the next bar boundary
    std::atomic<bool>    revertQueued { false };

    // ── Audio-thread write / UI-thread read ───────────────────────────────────

    std::array<std::atomic<int>,  NUM_CHANNELS> currentSteps;
    std::atomic<bool>                           isPlaying { false };

    // Per-channel auto-variation (UI toggles)
    std::array<std::atomic<bool>, NUM_CHANNELS> autoVar {};
    // Bar tracking for auto-variation timing
    std::atomic<int64_t> currentBar { -1 };

    // Public for TransportSync and StepGrid
    std::array<std::atomic<uint8_t>, NUM_CHANNELS> patternLengths;

private:
    static bool valid(int ch, int step)
    {
        return ch >= 0 && ch < NUM_CHANNELS && step >= 0 && step < MAX_STEPS;
    }

    static bool validChannel(int ch) { return ch >= 0 && ch < NUM_CHANNELS; }
    static bool validBass(int bi) { return bi >= 0 && bi < 2; }
    static bool validBassStep(int bi, int step) { return validBass(bi) && step >= 0 && step < MAX_STEPS; }
    static std::size_t chIndex(int ch) { return static_cast<std::size_t>(ch); }
    static std::size_t stepIndex(int step) { return static_cast<std::size_t>(step); }
    static std::size_t biIndex(int bi) { return static_cast<std::size_t>(bi); }

    std::array<std::atomic<uint64_t>, NUM_CHANNELS>                          patterns {};
    std::array<std::array<std::atomic<uint8_t>, MAX_STEPS>, NUM_CHANNELS>    velocities;
    std::array<std::array<std::atomic<uint8_t>, MAX_STEPS>, NUM_CHANNELS>    probabilities;
    std::array<std::array<std::atomic<int8_t>,  MAX_STEPS>, NUM_CHANNELS>    microTiming;
    // 303 per-step attributes
    std::array<std::atomic<uint64_t>, 2> bassSlide {};
    std::array<std::atomic<uint64_t>, 2> bassAccent {};
    std::array<std::atomic<uint64_t>, 2> bassOctUp {};
    std::array<std::atomic<uint64_t>, 2> bassOctDown {};
    std::array<std::array<std::atomic<int8_t>, MAX_STEPS>, 2> bassNoteOffsets {};
    uint32_t rngSeed = 12345;
};
