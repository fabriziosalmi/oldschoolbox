#pragma once
#include <array>
#include <utility>
#include <vector>
#include <JuceHeader.h>
#include "Scenes.h"
#include "ParamIds.h"

// Factory presets — full Snapshot + synth/FX parameter overrides applied via INIT menu.
// Channel order: KICK, SNARE, HATS, TOM, PERC1, PERC2, BASS1, BASS2.
namespace FactoryPresets
{
    struct ParamOverride
    {
        const char* id;
        float       value;
    };

    struct Preset
    {
        const char*                name;
        Scenes::Snapshot           snapshot;
        std::vector<ParamOverride> params;
    };

    inline void applyParams(const Preset& preset, juce::AudioProcessorValueTreeState& apvts)
    {
        // Reset all parameters to defaults first so presets are deterministic
        for (auto* p : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
                ranged->setValueNotifyingHost(ranged->getDefaultValue());

        for (const auto& ov : preset.params)
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(ov.id)))
                ranged->setValueNotifyingHost(ranged->convertTo0to1(ov.value));
        }
    }

    inline Scenes::Snapshot makeSnapshot(
        const uint64_t patterns[8],
        const uint64_t bassSlide[2],
        const uint64_t bassAccent[2],
        const uint64_t bassOctUp[2],
        const uint64_t bassOctDown[2])
    {
        Scenes::Snapshot s{};
        for (int ch = 0; ch < 8; ++ch)
        {
            s.patterns[ch]       = patterns[ch];
            s.patternLengths[ch] = 16;
            s.humanize[ch]       = 0;
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
            {
                s.velocities[ch][st]    = 100;
                s.probabilities[ch][st] = 100;
                s.microTiming[ch][st]   = 0;
            }
        }
        for (int bi = 0; bi < 2; ++bi)
        {
            static constexpr int8_t lines[2][16] = {
                { 0, 0, 7, 0, 10, 7, 0, 12, 3, 0, 7, 10, 0, 5, 7, 0 },
                { 0, 7, 0, 10, 3, 12, 7, 0, 10, 0, 5, 7, 12, 3, 7, 0 }
            };
            s.bassSlide[bi]   = bassSlide[bi];
            s.bassAccent[bi]  = bassAccent[bi];
            s.bassOctUp[bi]   = bassOctUp[bi];
            s.bassOctDown[bi] = bassOctDown[bi];
            for (int st = 0; st < Sequencer::MAX_STEPS; ++st)
                s.bassNoteOffsets[bi][st] = lines[bi][st % 16];
        }
        return s;
    }

    // ── Detroit Techno: 4/4 kick, snare on 2&4, 16th hats, acid bass with slides ──
    inline Scenes::Snapshot detroitTechno()
    {
        const uint64_t pat[8] = {
            0x1111,  // KICK   four-on-the-floor
            0x1010,  // SNARE  2 & 4
            0xFFFF,  // HATS   16ths
            0x0080,  // TOM    fill on step 7
            0x4444,  // PERC1  offbeat
            0x2020,  // PERC2  step 5 / 13
            0x2222,  // BASS1  offbeat sine
            0x9249,  // BASS2  acid 1/8 dotted
        };
        const uint64_t sld[2] = { 0x0000, 0x0240 };  // slides on bass2
        const uint64_t acc[2] = { 0x0000, 0x1001 };
        const uint64_t oUp[2] = { 0x0000, 0x0008 };
        const uint64_t oDn[2] = { 0x0000, 0x0000 };
        return makeSnapshot(pat, sld, acc, oUp, oDn);
    }

    // ── Acid House: 4/4 kick, claps on 2&4, busy 303 with slides & octaves ──
    inline Scenes::Snapshot acidHouse()
    {
        const uint64_t pat[8] = {
            0x1111,  // KICK
            0x1010,  // SNARE/clap
            0xAAAA,  // HATS   offbeat 8ths
            0x0000,
            0x0444,  // PERC1
            0x0000,
            0x0000,  // BASS1 off
            0xDB6D,  // BASS2  intense 303 line
        };
        const uint64_t sld[2] = { 0, 0x4924 };
        const uint64_t acc[2] = { 0, 0x1041 };
        const uint64_t oUp[2] = { 0, 0x0220 };
        const uint64_t oDn[2] = { 0, 0x0004 };
        return makeSnapshot(pat, sld, acc, oUp, oDn);
    }

    // ── UK Garage: broken kick, shuffled, syncopated bass ──
    inline Scenes::Snapshot ukGarage()
    {
        const uint64_t pat[8] = {
            0x0521,  // KICK   1, &2, 3-and
            0x1080,  // SNARE  syncopated
            0xAAAA,  // HATS   offbeat 8ths
            0x0040,
            0x0440,
            0x2020,
            0x4844,  // BASS1  syncopated round
            0x0000,
        };
        const uint64_t sld[2] = { 0x0840, 0 };
        const uint64_t acc[2] = { 0x0004, 0 };
        const uint64_t oUp[2] = { 0, 0 };
        const uint64_t oDn[2] = { 0x0800, 0 };
        return makeSnapshot(pat, sld, acc, oUp, oDn);
    }

    // ── Industrial: pounding kick, aggressive snare + claps, heavy bass2 ──
    inline Scenes::Snapshot industrial()
    {
        const uint64_t pat[8] = {
            0x5555,  // KICK   8th note pounding
            0x1010,
            0xFFFF,  // HATS   16ths
            0x0808,  // TOM    pickup
            0x4444,
            0x2222,
            0x0000,
            0xAAAA,  // BASS2  offbeat 8ths aggressive
        };
        const uint64_t sld[2] = { 0, 0x0000 };
        const uint64_t acc[2] = { 0, 0x5555 };  // accent every offbeat
        const uint64_t oUp[2] = { 0, 0x0202 };
        const uint64_t oDn[2] = { 0, 0x0000 };
        return makeSnapshot(pat, sld, acc, oUp, oDn);
    }

    // ── Minimal: sparse, hypnotic ──
    inline Scenes::Snapshot minimal()
    {
        const uint64_t pat[8] = {
            0x1111,  // KICK
            0x1000,  // SNARE  only beat 4
            0x5555,  // HATS   8ths
            0x0000,
            0x0080,  // PERC1  single accent
            0x0400,
            0x0202,  // BASS1  sparse
            0x0021,
        };
        const uint64_t sld[2] = { 0x0002, 0 };
        const uint64_t acc[2] = { 0, 0x0021 };
        const uint64_t oUp[2] = { 0, 0 };
        const uint64_t oDn[2] = { 0, 0 };
        return makeSnapshot(pat, sld, acc, oUp, oDn);
    }

    inline const std::array<Preset, 5>& all()
    {
        static const std::array<Preset, 5> presets = {
            Preset{
                "Detroit Techno",
                detroitTechno(),
                {
                    { ParamId::KICK_TUNE,     54.0f },
                    { ParamId::KICK_DECAY,    0.42f },
                    { ParamId::KICK_PUNCH,    0.55f },
                    { ParamId::KICK_DRIVE,    0.35f },
                    { ParamId::HATS_DECAY,    0.06f },
                    { ParamId::HATS_TONE,     0.65f },
                    { ParamId::BASS1_CUTOFF,  320.0f },
                    { ParamId::BASS1_RESO,    0.45f },
                    { ParamId::BASS2_CUTOFF,  520.0f },
                    { ParamId::BASS2_RESO,    0.72f },
                    { ParamId::BASS2_ENVMOD,  0.62f },
                    { ParamId::BASS2_ACCENT,  0.55f },
                    { ParamId::SWING,         0.12f },
                    { ParamId::COMP_AMOUNT,   0.30f },
                    { ParamId::REVERB_MIX,    0.14f },
                    { ParamId::DELAY_SYNC,    3.0f  }, // 1/8d
                    { ParamId::DELAY_MIX,     0.18f },
                    { ParamId::TIGHT,         1.0f  },
                }
            },
            Preset{
                "Acid House",
                acidHouse(),
                {
                    { ParamId::KICK_TUNE,     58.0f },
                    { ParamId::KICK_DECAY,    0.38f },
                    { ParamId::KICK_PUNCH,    0.48f },
                    { ParamId::SNARE_TUNE,    210.0f },
                    { ParamId::SNARE_SNAP,    0.72f },
                    { ParamId::HATS_DECAY,    0.09f },
                    { ParamId::BASS2_CUTOFF,  380.0f },
                    { ParamId::BASS2_RESO,    0.86f },
                    { ParamId::BASS2_ENVMOD,  0.78f },
                    { ParamId::BASS2_DECAY,   0.18f },
                    { ParamId::BASS2_ACCENT,  0.80f },
                    { ParamId::SWING,         0.20f },
                    { ParamId::MASTER_DRIVE,  0.22f },
                    { ParamId::DELAY_SYNC,    3.0f  }, // 1/8d
                    { ParamId::DELAY_FB,      0.48f },
                    { ParamId::DELAY_MIX,     0.24f },
                    { ParamId::COMP_AMOUNT,   0.25f },
                }
            },
            Preset{
                "UK Garage",
                ukGarage(),
                {
                    { ParamId::KICK_TUNE,     62.0f },
                    { ParamId::KICK_DECAY,    0.26f },
                    { ParamId::KICK_PUNCH,    0.65f },
                    { ParamId::SNARE_TUNE,    240.0f },
                    { ParamId::SNARE_DECAY,   0.14f },
                    { ParamId::HATS_DECAY,    0.05f },
                    { ParamId::HATS_TONE,     0.72f },
                    { ParamId::BASS1_CUTOFF,  280.0f },
                    { ParamId::BASS1_RESO,    0.38f },
                    { ParamId::BASS1_ENVMOD,  0.50f },
                    { ParamId::BASS1_DECAY,   0.28f },
                    { ParamId::SWING,         0.55f },
                    { ParamId::REVERB_SIZE,   0.45f },
                    { ParamId::REVERB_MIX,    0.16f },
                    { ParamId::COMP_AMOUNT,   0.35f },
                    { ParamId::TIGHT,         1.0f  },
                }
            },
            Preset{
                "Industrial",
                industrial(),
                {
                    { ParamId::KICK_TUNE,     48.0f },
                    { ParamId::KICK_DECAY,    0.58f },
                    { ParamId::KICK_PUNCH,    0.75f },
                    { ParamId::KICK_DRIVE,    0.85f },
                    { ParamId::KICK_SUB,      0.70f },
                    { ParamId::SNARE_NOISE,   0.85f },
                    { ParamId::PERC1_DRIVE,   0.80f },
                    { ParamId::BASS2_CUTOFF,  650.0f },
                    { ParamId::BASS2_RESO,    0.78f },
                    { ParamId::BASS2_ACCENT,  0.85f },
                    { ParamId::MASTER_DRIVE,  0.48f },
                    { ParamId::COMP_AMOUNT,   0.58f },
                    { ParamId::REVERB_SIZE,   0.82f },
                    { ParamId::REVERB_MIX,    0.26f },
                    { ParamId::TIGHT,         1.0f  },
                }
            },
            Preset{
                "Minimal",
                minimal(),
                {
                    { ParamId::KICK_TUNE,     52.0f },
                    { ParamId::KICK_DECAY,    0.30f },
                    { ParamId::KICK_PUNCH,    0.35f },
                    { ParamId::KICK_DRIVE,    0.15f },
                    { ParamId::HATS_DECAY,    0.035f },
                    { ParamId::HATS_TONE,     0.78f },
                    { ParamId::BASS1_CUTOFF,  260.0f },
                    { ParamId::BASS1_RESO,    0.35f },
                    { ParamId::BASS2_CUTOFF,  420.0f },
                    { ParamId::BASS2_RESO,    0.55f },
                    { ParamId::SWING,         0.18f },
                    { ParamId::DELAY_SYNC,    4.0f  }, // 1/16
                    { ParamId::DELAY_MIX,     0.15f },
                    { ParamId::REVERB_MIX,    0.10f },
                    { ParamId::TIGHT,         1.0f  },
                }
            },
        };
        return presets;
    }
}
