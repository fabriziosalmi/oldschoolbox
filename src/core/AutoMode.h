#pragma once
#include <cstdint>
#include <cmath>
#include "../Sequencer.h"
#include "Scenes.h"

// AUTO mode — pure heuristic "cruise control".
//
// Every bar boundary (when enabled) mutates the live sequencer state around
// a captured anchor snapshot using:
//   - per-channel base mutation rates (kick stable, hats high entropy, ...)
//   - global MUTATION amount (knob 0..1, multiplier)
//   - global DRIFT amount (knob 0..1, mean-reversion strength toward anchor)
//   - energy coupling: sustainedIntensity dampens mutations (lock the groove)
//   - genre-aware bias: downbeats reassert themselves softly
//
// No ML, no inference, no dependencies — pure C++ + xorshift32 RNG.
class AutoMode
{
public:
    void prepare(const Scenes::Snapshot& initialAnchor)
    {
        anchor = initialAnchor;
    }

    void setAnchor(const Scenes::Snapshot& a) { anchor = a; }
    const Scenes::Snapshot& getAnchor() const { return anchor; }

    // Called at bar boundary on the audio thread when AUTO is enabled.
    void evolve(Sequencer& seq,
                float     mutation,         // 0..1 knob
                float     drift,            // 0..1 knob (anchor pull strength)
                float     sustainedEnergy,  // 0..1 from VisualState
                uint32_t& rng,
                int64_t   currentBar = 0)
    {
        // Energy dampener — when the floor is hot, freeze the groove
        const float energyFactor = 1.0f - sustainedEnergy * 0.75f;
        const int barIn16 = static_cast<int>(currentBar >= 0 ? (currentBar % 16) : 0);
        const int barIn32 = static_cast<int>(currentBar >= 0 ? (currentBar % 32) : 0);

        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            const int  len = seq.getPatternLength(ch);
            const float chRate = baseMutationRate[ch] * mutation * energyFactor;
            uint64_t pat = seq.getPattern(ch);
            const uint64_t anchorPat = anchor.patterns[ch];

            for (int s = 0; s < len; ++s)
            {
                const uint64_t bit = 1ULL << s;
                const bool curOn    = (pat & bit) != 0;
                const bool anchorOn = (anchorPat & bit) != 0;

                bool nextOn = curOn;

                // Drift step — with probability `drift`, pull this step back toward anchor
                rng = nextRng(rng);
                if (frac(rng) < drift)
                    nextOn = anchorOn;

                // Mutation step — with probability `chRate`, flip
                rng = nextRng(rng);
                if (frac(rng) < chRate)
                    nextOn = !nextOn;

                // Genre-aware bias: kick + snare on standard beats reassert softly
                if (ch == 0 && (s % 4) == 0)
                {
                    // KICK on quarter beats — extra pull toward ON
                    rng = nextRng(rng);
                    if (!nextOn && frac(rng) < 0.55f) nextOn = true;
                }
                else if (ch == 1 && (s % 8) == 4)
                {
                    // SNARE on 2 & 4 — pull toward ON
                    rng = nextRng(rng);
                    if (!nextOn && frac(rng) < 0.45f) nextOn = true;
                }

                if (nextOn) pat |=  bit;
                else        pat &= ~bit;
            }

            seq.setPattern(ch, pat);

            // Velocity wobble — mutate velocity of active steps by a small amount
            for (int s = 0; s < len; ++s)
            {
                if (!((pat >> s) & 1ULL)) continue;
                const uint8_t curVel = seq.getVelocity(ch, s);
                rng = nextRng(rng);
                if (frac(rng) < chRate * 0.5f)
                {
                    // Walk velocity by +/- 10
                    const int delta = (int)(rng % 21u) - 10;
                    int nv = static_cast<int>(curVel) + delta;
                    if (nv < 30)  nv = 30;
                    if (nv > 127) nv = 127;
                    // Slight drift toward anchor velocity
                    if (drift > 0.0f)
                    {
                        const int av = anchor.velocities[ch][s];
                        nv = static_cast<int>(nv * (1.0f - drift * 0.4f) + av * drift * 0.4f);
                    }
                    seq.setVelocity(ch, s, static_cast<uint8_t>(nv));
                }
            }

            // Bass-specific: occasionally toggle slide/accent/octave
            if (ch >= 6)
            {
                const int bi = ch - 6;
                uint64_t sld = seq.getBassSlidePattern(bi);
                uint64_t acc = seq.getBassAccentPattern(bi);
                uint64_t oup = seq.getBassOctUpPattern(bi);
                uint64_t odn = seq.getBassOctDownPattern(bi);
                const float bassAttrRate = chRate * 0.35f;

                for (int s = 0; s < len; ++s)
                {
                    if (!((pat >> s) & 1ULL)) continue;
                    const uint64_t b = 1ULL << s;
                    rng = nextRng(rng); if (frac(rng) < bassAttrRate) sld ^= b;
                    rng = nextRng(rng); if (frac(rng) < bassAttrRate) acc ^= b;
                    rng = nextRng(rng); if (frac(rng) < bassAttrRate * 0.6f)
                    {
                        if (oup & b) { oup ^= b; }
                        else if (odn & b) { odn ^= b; }
                        else {
                            rng = nextRng(rng);
                            if ((rng & 1u) != 0) oup |= b; else odn |= b;
                        }
                    }
                }
                seq.setBassSlidePattern(bi,  sld);
                seq.setBassAccentPattern(bi, acc);
                seq.setBassOctUpPattern(bi,  oup);
                seq.setBassOctDownPattern(bi, odn);
            }
        }

        // ── 64-bar live micro-arrangement (#29) ──
        // Bar 16 turnaround (barIn16 == 15): drop the last quarter of the kick so
        // the phrase breathes and slams back hard on beat 1 of the next 16-bar phrase.
        if (barIn16 == 15)
        {
            uint64_t kPat = seq.getPattern(0);
            kPat &= ~(0xF000ULL); // mute steps 12..15 of bar 1
            seq.setPattern(0, kPat);
        }
        // Bar 32 turnaround: boost 303 accents on offbeats for tension build
        if (barIn32 == 31)
        {
            const uint64_t b2Pat = seq.getPattern(7);
            seq.setBassAccentPattern(1, seq.getBassAccentPattern(1) | (b2Pat & 0xAAAAULL));
        }
    }

private:
    static uint32_t nextRng(uint32_t r)
    {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return r;
    }
    static float frac(uint32_t r) { return static_cast<float>(r & 0xFFFFu) / 65535.0f; }

    // Per-channel base mutation rates (before global MUTATION + energy multipliers)
    // Tuned so the kick stays solid while hats and bass evolve more freely.
    static constexpr float baseMutationRate[8] = {
        0.04f,  // KICK   — very stable
        0.08f,  // SNARE  — quite stable
        0.28f,  // HATS   — high entropy
        0.18f,  // TOM
        0.18f,  // PERC1
        0.20f,  // PERC2
        0.14f,  // BASS1  — round bass mutates moderately
        0.20f,  // BASS2  — acid 303 evolves more freely
    };

    Scenes::Snapshot anchor;
};
