#pragma once
#include <cstdint>
#include <array>

// Pre-built pattern sets for auto mode.
// 4 variations × 8 channels. 64-bar cycle:
//   Bars 0–15:  Set A (main groove)
//   Bars 16–31: Set B (driving)
//   Bars 32–47: Set C (breakdown)
//   Bars 48–59: Set A (reprise)
//   Bars 60–63: Set D (fill/build)
namespace PatternPresets
{
    static constexpr int NUM_SETS = 4;
    static constexpr int NUM_CH   = 8;

    // [set][channel]  —  16-step bitmasks (bit N = step N)
    //
    // Channel order: KICK, SNARE, HATS, TOM, PERC1, PERC2, BASS1, BASS2
    //
    // Step grid (16th notes):
    //   Step:  0  1  2  3 | 4  5  6  7 | 8  9 10 11 |12 13 14 15
    //   Beat:  1           2           3           4

    static constexpr uint64_t PATTERNS[NUM_SETS][NUM_CH] = {

        // ── Set A — Main Groove (classic 4/4 techno) ──────────────────
        {
            0x1111,  // KICK:  x...x...x...x...  four-on-the-floor
            0x1010,  // SNARE: ....x.......x...  2 & 4
            0x5555,  // HATS:  x.x.x.x.x.x.x.x. 8th notes
            0x0000,  // TOM:   ................
            0x4444,  // PERC1: ..x...x...x...x.  offbeat accents
            0x0000,  // PERC2: ................
            0x2222,  // BASS1: .x...x...x...x..  offbeat quarters (round)
            0x1249,  // BASS2: x..x..x..x..x...  acid dotted pattern
        },

        // ── Set B — Driving Variation ─────────────────────────────────
        {
            0x1191,  // KICK:  x...x..xx...x...  ghost on &3
            0x9010,  // SNARE: ....x.......x..x  extra hit on 16
            0xFFFF,  // HATS:  xxxxxxxxxxxxxxxx  16th notes
            0x0080,  // TOM:   .......x........  accent hit
            0x0100,  // PERC1: ........x.......
            0x0404,  // PERC2: ..x.......x.....
            0xAAAA,  // BASS1: .x.x.x.x.x.x.x.x offbeat 8ths
            0x5252,  // BASS2: .x..x.x..x..x.x. acid variation
        },

        // ── Set C — Breakdown (minimal / tension) ─────────────────────
        {
            0x0101,  // KICK:  x.......x.......  half-time
            0x1000,  // SNARE: ............x...  only beat 4
            0x5555,  // HATS:  x.x.x.x.x.x.x.x. 8ths
            0x0400,  // TOM:   ..........x.....  single accent
            0x0000,  // PERC1: ................
            0x1010,  // PERC2: ....x.......x...
            0x0202,  // BASS1: .x.......x......  sparse
            0x1041,  // BASS2: x......x....x...  minimal acid
        },

        // ── Set D — Fill / Build ──────────────────────────────────────
        {
            0x5555,  // KICK:  x.x.x.x.x.x.x.x. 8th note build
            0xFC10,  // SNARE: ....x.....xxxxxx  snare roll
            0xFFFF,  // HATS:  xxxxxxxxxxxxxxxx  16ths
            0x1040,  // TOM:   ......x.....x...  tom accents
            0x1111,  // PERC1: x...x...x...x...  quarters
            0xAAAA,  // PERC2: .x.x.x.x.x.x.x.x offbeat
            0xFFFF,  // BASS1: xxxxxxxxxxxxxxxx  16th bass run
            0xB6DB,  // BASS2: xx.xx.xx.xx.xx.x  intense acid triplet-ish
        },
    };

    // Returns which pattern set to use for a given bar in the 64-bar cycle
    static constexpr int variationForBar(int barInCycle)
    {
        if (barInCycle >= 60) return 3;  // Fill (last 4 bars)
        if (barInCycle >= 48) return 0;  // Reprise (A)
        if (barInCycle >= 32) return 2;  // Breakdown (C)
        if (barInCycle >= 16) return 1;  // Driving (B)
        return 0;                        // Main (A)
    }
}
