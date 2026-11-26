#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include "../Sequencer.h"

// Snapshot of the entire sequencer state — used for scenes (live performance switching)
// and for FILL pre-state preservation.
//
// Plain POD: written/read by both UI and audio threads, but writes only happen at
// well-defined moments (UI: save scene; Audio: copy-in at bar boundary). The "queued"
// atomic flag in Sequencer serialises the handoff.
namespace Scenes
{
    static constexpr int COUNT = 4;  // A, B, C, D

    struct Snapshot
    {
        uint64_t patterns       [Sequencer::NUM_CHANNELS] {};
        uint8_t  velocities     [Sequencer::NUM_CHANNELS][Sequencer::MAX_STEPS] {};
        uint8_t  probabilities  [Sequencer::NUM_CHANNELS][Sequencer::MAX_STEPS] {};
        int8_t   microTiming    [Sequencer::NUM_CHANNELS][Sequencer::MAX_STEPS] {};
        uint8_t  patternLengths [Sequencer::NUM_CHANNELS] { 16, 16, 16, 16, 16, 16, 16, 16 };
        uint8_t  humanize       [Sequencer::NUM_CHANNELS] {};   // 0/1 flag per channel
        uint64_t bassSlide      [2] {};
        uint64_t bassAccent     [2] {};
        uint64_t bassOctUp      [2] {};
        uint64_t bassOctDown    [2] {};
        int8_t   bassNoteOffsets[2][Sequencer::MAX_STEPS] {};
    };

    // UI thread → snapshot current sequencer state into target
    inline void capture(Snapshot& s, const Sequencer& seq)
    {
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            const auto chIdx = static_cast<std::size_t>(ch);
            s.patterns[ch]       = seq.getPattern(ch);
            s.patternLengths[ch] = static_cast<uint8_t>(seq.getPatternLength(ch));
            s.humanize[ch]       = seq.humanize[chIdx].load(std::memory_order_relaxed) ? 1 : 0;
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
            {
                s.velocities[ch][st]    = seq.getVelocity(ch, st);
                s.probabilities[ch][st] = seq.getProbability(ch, st);
                s.microTiming[ch][st]   = seq.getMicroTiming(ch, st);
            }
        }
        for (int bi = 0; bi < 2; ++bi)
        {
            s.bassSlide[bi]    = seq.getBassSlidePattern(bi);
            s.bassAccent[bi]   = seq.getBassAccentPattern(bi);
            s.bassOctUp[bi]    = seq.getBassOctUpPattern(bi);
            s.bassOctDown[bi]  = seq.getBassOctDownPattern(bi);
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
                s.bassNoteOffsets[bi][st] = seq.getBassNoteOffset(bi, st);
        }
    }

    // Audio thread (at bar boundary) → apply snapshot to sequencer
    inline void apply(const Snapshot& s, Sequencer& seq)
    {
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            const auto chIdx = static_cast<std::size_t>(ch);
            seq.setPattern(ch, s.patterns[ch]);
            seq.setPatternLength(ch, s.patternLengths[ch]);
            seq.humanize[chIdx].store(s.humanize[ch] != 0, std::memory_order_release);
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
            {
                seq.setVelocity(ch, st, s.velocities[ch][st]);
                seq.setProbability(ch, st, s.probabilities[ch][st]);
                seq.setMicroTiming(ch, st, s.microTiming[ch][st]);
            }
        }
        for (int bi = 0; bi < 2; ++bi)
        {
            seq.setBassSlidePattern(bi,    s.bassSlide[bi]);
            seq.setBassAccentPattern(bi,   s.bassAccent[bi]);
            seq.setBassOctUpPattern(bi,    s.bassOctUp[bi]);
            seq.setBassOctDownPattern(bi,  s.bassOctDown[bi]);
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
                seq.setBassNoteOffset(bi, st, s.bassNoteOffsets[bi][st]);
        }
    }
}
