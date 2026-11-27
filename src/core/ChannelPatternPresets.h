#pragma once
#include <array>
#include <cstdint>
#include "../Sequencer.h"

// Per-channel 4-slot quick jam pattern presets (1-click switch, Shift+Click capture custom, Alt+Click vary).
// Operates strictly on the UI thread via Sequencer's lock-free atomic setters.
namespace ChannelPatternPresets
{
    static constexpr int SLOTS_PER_CH = 4;

    struct Slot
    {
        uint64_t pattern     = 0;
        uint64_t slide       = 0;
        uint64_t accent      = 0;
        uint64_t octUp       = 0;
        uint64_t octDown     = 0;
        std::array<uint8_t, 16> vel   {};
        std::array<int8_t,  16> notes {};
        bool isCustom        = false;
    };

    inline uint64_t tile16(uint16_t mask16)
    {
        const uint64_t m = static_cast<uint64_t>(mask16);
        return m | (m << 16) | (m << 32) | (m << 48);
    }

    inline Slot makeSlot(uint16_t pat16,
                         const std::array<uint8_t, 16>& vel16,
                         uint16_t sl16 = 0, uint16_t ac16 = 0,
                         uint16_t ou16 = 0, uint16_t od16 = 0,
                         const std::array<int8_t, 16>& n16 = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 })
    {
        Slot s;
        s.pattern = tile16(pat16);
        s.slide   = tile16(sl16);
        s.accent  = tile16(ac16);
        s.octUp   = tile16(ou16);
        s.octDown = tile16(od16);
        s.vel     = vel16;
        s.notes   = n16;
        s.isCustom = false;
        return s;
    }

    inline uint16_t bits(std::initializer_list<int> steps)
    {
        uint16_t m = 0;
        for (int st : steps)
            if (st >= 0 && st < 16) m |= static_cast<uint16_t>(1u << st);
        return m;
    }

    inline Slot factorySlot(int ch, int slotIdx)
    {
        static const std::array<uint8_t, 16> vFlat  = { 115,100,105,100, 115,100,105,100, 115,100,105,100, 115,100,105,100 };
        static const std::array<uint8_t, 16> vRoll  = { 115, 70, 85, 75, 115, 70, 90, 80, 115, 80, 95, 85, 120, 95,110,125 };
        static const std::array<uint8_t, 16> vHat16 = {  80, 62,115, 68,  82, 60,118, 70,  80, 62,115, 72,  85, 65,120, 88 };
        static const std::array<uint8_t, 16> vSync  = { 110, 78,100, 92, 112, 75,105, 88, 110, 80,102, 95, 115, 82,108, 98 };

        switch (ch)
        {
            case 0: // KICK 909
                if (slotIdx == 0) return makeSlot(bits({0,4,8,12}), vFlat);
                if (slotIdx == 1) return makeSlot(bits({0,4,8,12,14}), { 120,0,0,0, 120,0,0,0, 120,0,0,0, 120,0,88,0 });
                if (slotIdx == 2) return makeSlot(bits({0,3,6,10,12}), vSync);
                return makeSlot(bits({0,4,8,10,12,14,15}), vRoll);

            case 1: // SNARE 909
                if (slotIdx == 0) return makeSlot(bits({4,12}), vFlat);
                if (slotIdx == 1) return makeSlot(bits({4,11,14}), { 0,0,0,0, 118,0,0,0, 0,0,0,82, 0,0,92,0 });
                if (slotIdx == 2) return makeSlot(bits({4,7,10,12,15}), vSync);
                return makeSlot(bits({0,2,4,6,8,10,12,13,14,15}), vRoll);

            case 2: // HATS 909
                if (slotIdx == 0) return makeSlot(bits({2,6,10,14}), vFlat);
                if (slotIdx == 1) return makeSlot(0xFFFFu, vHat16);
                if (slotIdx == 2) return makeSlot(bits({2,3,6,10,11,14,15}), vSync);
                return makeSlot(bits({1,2,5,6,9,10,13,14}), vHat16);

            case 3: // TOM 909
                if (slotIdx == 0) return makeSlot(bits({11,14}), vFlat);
                if (slotIdx == 1) return makeSlot(bits({0,3,6,9,12,15}), vSync);
                if (slotIdx == 2) return makeSlot(bits({3,7,10,13,15}), vSync);
                return makeSlot(bits({2,5,7,10,11,14,15}), vRoll);

            case 4: // PERC 1 FM
                if (slotIdx == 0) return makeSlot(bits({3,11}), vFlat);
                if (slotIdx == 1) return makeSlot(bits({1,4,7,10,13}), vSync);
                if (slotIdx == 2) return makeSlot(bits({2,5,9,12,15}), vSync);
                return makeSlot(bits({1,3,5,7,9,11,13,15}), vHat16);

            case 5: // PERC 2 NOISE
                if (slotIdx == 0) return makeSlot(bits({6,14}), vFlat);
                if (slotIdx == 1) return makeSlot(bits({2,6,10,14,15}), vSync);
                if (slotIdx == 2) return makeSlot(bits({0,5,10,15}), vFlat);
                return makeSlot(bits({1,3,6,8,11,14,15}), vSync);

            case 6: // BASS 1 303 (Sub / Offbeat)
            {
                static const std::array<int8_t, 16> nOff  = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,3,0 };
                static const std::array<int8_t, 16> nRoll = { 0,0,0,7, 0,0,0,3, 0,0,0,7, 0,5,3,0 };
                static const std::array<int8_t, 16> nHyp  = { 0,0,7,0, 10,0,7,0, 0,0,12,7, 0,3,5,0 };
                static const std::array<int8_t, 16> nOct  = { 0,0,0,12, 0,0,0,12, 0,0,3,12, 0,0,7,12 };
                if (slotIdx == 0) return makeSlot(bits({2,6,10,14}), vFlat, 0, bits({2,10}), 0, 0, nOff);
                if (slotIdx == 1) return makeSlot(bits({1,2,3,5,6,7,9,10,11,13,14,15}), vHat16, bits({2,6,10,14}), bits({3,11}), 0, 0, nRoll);
                if (slotIdx == 2) return makeSlot(bits({0,2,3,6,8,10,11,14}), vSync, bits({2,10}), bits({0,6,14}), bits({10}), 0, nHyp);
                return makeSlot(bits({2,3,6,7,10,11,14,15}), vSync, bits({2,6,10,14}), bits({3,7,11,15}), bits({3,11}), 0, nOct);
            }

            default: // CH 7: BASS 2 303 (Acid Lead)
            {
                static const std::array<int8_t, 16> nStab = { 0,0,7,0, 0,0,0,10, 0,0,0,12, 0,0,7,0 };
                static const std::array<int8_t, 16> nAcid = { 0,0,12,7, 0,0,3,10, 0,0,7,0, 12,0,10,7 };
                static const std::array<int8_t, 16> nMent = { 0,0,0,1, 0,7,8,0, 0,10,0,12, 7,0,1,0 };
                static const std::array<int8_t, 16> nScr  = { 0,7,12,10, 0,12,7,3, 0,10,12,15, 7,10,12,0 };
                if (slotIdx == 0) return makeSlot(bits({2,7,11,14}), vFlat, bits({7}), bits({2,11}), 0, 0, nStab);
                if (slotIdx == 1) return makeSlot(bits({0,2,3,6,7,10,12,14,15}), vSync, bits({2,6,14}), bits({0,7,12}), bits({2,12}), 0, nAcid);
                if (slotIdx == 2) return makeSlot(bits({0,3,5,6,9,11,12,14}), vSync, bits({3,5,11}), bits({0,6,12}), bits({11}), 0, nMent);
                return makeSlot(0xFFFFu, vRoll, bits({1,2,5,9,10,13,14}), bits({0,3,6,10,12,15}), bits({2,5,11,14}), 0, nScr);
            }
        }
    }

    inline const char* slotTooltip(int ch, int slotIdx)
    {
        static const char* kNames[8][4] = {
            { "4/4 Straight", "Tribe Bounce",  "Syncopated",     "Hard Roll" },
            { "Backbeat 2&4", "Tribe Sync",    "Breakbeat",      "Build Roll" },
            { "Offbeat",      "16th Driving",  "Gallop",         "Tribal Sync" },
            { "Minimal",      "Poly 3/16",     "Call-Response",  "Dense Roll" },
            { "Off-Grid",     "3/16 Hypno",    "Mental Counter", "Stutter Rush" },
            { "Sparse Zap",   "Upbeat Chop",   "Poly 5/16",      "Chatter" },
            { "Offbeat Sub",  "Rolling 16th",  "Acid Hypno",     "Octave Bounce" },
            { "Acid Stab",    "303 Squelch",   "Mental Riff",    "Scream 16th" }
        };
        if (ch < 0 || ch >= 8 || slotIdx < 0 || slotIdx >= 4) return "Preset";
        return kNames[ch][slotIdx];
    }

    inline void apply(const Slot& s, Sequencer& seq, int ch)
    {
        if (ch < 0 || ch >= Sequencer::NUM_CHANNELS) return;
        seq.setPattern(ch, s.pattern);
        for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
            seq.setVelocity(ch, st, s.vel[static_cast<size_t>(st % 16)] > 0 ? s.vel[static_cast<size_t>(st % 16)] : 100);
        if (ch >= 6)
        {
            const int bi = ch - 6;
            seq.setBassSlidePattern(bi, s.slide);
            seq.setBassAccentPattern(bi, s.accent);
            seq.setBassOctUpPattern(bi, s.octUp);
            seq.setBassOctDownPattern(bi, s.octDown);
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
                seq.setBassNoteOffset(bi, st, s.notes[static_cast<size_t>(st % 16)]);
        }
    }

    inline Slot capture(const Sequencer& seq, int ch)
    {
        Slot s;
        s.pattern = seq.getPattern(ch);
        for (int st = 0; st < 16; ++st)
            s.vel[static_cast<size_t>(st)] = seq.getVelocity(ch, st);
        if (ch >= 6)
        {
            const int bi = ch - 6;
            s.slide   = seq.getBassSlidePattern(bi);
            s.accent  = seq.getBassAccentPattern(bi);
            s.octUp   = seq.getBassOctUpPattern(bi);
            s.octDown = seq.getBassOctDownPattern(bi);
            for (int st = 0; st < 16; ++st)
                s.notes[static_cast<size_t>(st)] = seq.getBassNoteOffset(bi, st);
        }
        s.isCustom = true;
        return s;
    }
}
