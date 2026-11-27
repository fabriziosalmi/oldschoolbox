#include "ParamLayout.h"
#include "ParamIds.h"
#include "BusLayout.h"
#include <algorithm>
#include <cmath>

namespace ParamLayout
{

juce::AudioProcessorValueTreeState::ParameterLayout create()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&](const char* id, const char* name,
                    float lo, float hi, float step, float def)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, name,
            juce::NormalisableRange<float>(lo, hi, step), def));
    };

    // Volume (all 8 channels)
    for (int ch = 0; ch < OsbBus::COUNT; ++ch)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ ParamId::vol(ch), 1 },
            juce::String(OsbBus::NAMES[ch]) + " Volume",
            juce::NormalisableRange<float>(0.f, 1.f), 0.8f));

    // Pan (all 8 channels) — 0 = left, 0.5 = centre, 1 = right
    for (int ch = 0; ch < OsbBus::COUNT; ++ch)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ ParamId::pan(ch), 1 },
            juce::String(OsbBus::NAMES[ch]) + " Pan",
            juce::NormalisableRange<float>(0.f, 1.f), 0.5f));

    // ── Kick (6 params) ──
    addF(ParamId::KICK_TUNE,     "Kick Tune",     40.f,  120.f, 0.1f,   60.f);
    addF(ParamId::KICK_DECAY,    "Kick Decay",    0.05f, 0.80f, 0.001f, 0.45f);
    addF(ParamId::KICK_PUNCH,    "Kick Punch",    0.f,   1.f,   0.01f,  0.40f);
    addF(ParamId::KICK_PITCHENV, "Kick PitchEnv", 0.f,   1.f,   0.01f,  0.80f);
    addF(ParamId::KICK_DRIVE,    "Kick Drive",    0.f,   1.f,   0.01f,  0.30f);
    addF(ParamId::KICK_SUB,      "Kick Sub",      0.f,   1.f,   0.01f,  0.50f);

    // ── Snare ──
    addF(ParamId::SNARE_TUNE,  "Snare Tune",  100.f, 400.f, 1.f,    185.f);
    addF(ParamId::SNARE_DECAY, "Snare Decay", 0.04f, 0.40f, 0.001f, 0.18f);
    addF(ParamId::SNARE_SNAP,  "Snare Snap",  0.f,   1.f,   0.01f,  0.50f);
    addF(ParamId::SNARE_NOISE, "Snare Noise", 0.f,   1.f,   0.01f,  0.60f);

    // ── Hats ──
    addF(ParamId::HATS_DECAY, "Hats Decay", 0.01f, 0.50f, 0.001f, 0.08f);
    addF(ParamId::HATS_TONE,  "Hats Tone",  0.f,   1.f,   0.01f,  0.50f);
    addF(ParamId::HATS_BODY,  "Hats Body",  0.f,   1.f,   0.01f,  0.30f);

    // ── Tom ──
    addF(ParamId::TOM_TUNE,     "Tom Tune",     50.f,  200.f, 1.f,    90.f);
    addF(ParamId::TOM_DECAY,    "Tom Decay",    0.08f, 0.60f, 0.001f, 0.30f);
    addF(ParamId::TOM_PITCHENV, "Tom PitchEnv", 0.f,   1.f,   0.01f,  0.65f);
    addF(ParamId::TOM_ATTACK,   "Tom Attack",   0.f,   1.f,   0.01f,  0.20f);

    // ── Perc1 ──
    addF(ParamId::PERC1_TUNE,   "Perc1 Tune",   200.f, 2000.f, 1.f,    600.f);
    addF(ParamId::PERC1_DECAY,  "Perc1 Decay",  0.02f, 0.30f,  0.001f, 0.10f);
    addF(ParamId::PERC1_DETUNE, "Perc1 Detune", 0.f,   1.f,    0.01f,  0.40f);
    addF(ParamId::PERC1_DRIVE,  "Perc1 Drive",  0.f,   1.f,    0.01f,  0.30f);

    // ── Perc2 ──
    addF(ParamId::PERC2_TUNE,  "Perc2 Tune",  100.f, 800.f,  1.f,    350.f);
    addF(ParamId::PERC2_DECAY, "Perc2 Decay", 0.02f, 0.35f,  0.001f, 0.12f);
    addF(ParamId::PERC2_NOISE, "Perc2 Noise", 0.f,   1.f,    0.01f,  0.50f);
    addF(ParamId::PERC2_PITCH, "Perc2 Pitch", 0.f,   1.f,    0.01f,  0.40f);

    // True musical logarithmic (octave-per-degree) range for 303 Cutoff (20 Hz .. 20 kHz):
    // 0%=20Hz, 10%=40Hz, 20%=80Hz, 30%=160Hz, 40%=317Hz, 50%=632Hz, 60%=1.26k, 80%=5k, 100%=20k.
    // Continuous (no 1 Hz integer snap) so there is zero dead zone or stepping at low frequencies.
    auto makeLogCutoffRange = []()
    {
        return juce::NormalisableRange<float>(
            20.f, 20000.f,
            [](float lo, float hi, float norm) {
                return lo * std::pow(hi / lo, std::clamp(norm, 0.0f, 1.0f));
            },
            [](float lo, float hi, float val) {
                const float v = std::clamp(val, lo, hi);
                return std::log(v / lo) / std::log(hi / lo);
            },
            [](float lo, float hi, float val) {
                return std::clamp(val, lo, hi);
            });
    };

    // ── Bass1 (saw 303) ──
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ParamId::BASS1_CUTOFF, 1 }, "Bass1 Cutoff", makeLogCutoffRange(), 450.f));
    addF(ParamId::BASS1_RESO,   "Bass1 Reso",    0.f,  1.f,     0.01f, 0.55f);
    addF(ParamId::BASS1_ENVMOD, "Bass1 EnvMod",  0.f,  1.f,     0.01f, 0.45f);
    addF(ParamId::BASS1_DECAY,  "Bass1 Decay",   0.01f, 1.f,    0.001f, 0.22f);
    addF(ParamId::BASS1_ACCENT, "Bass1 Accent",  0.f,  1.f,     0.01f, 0.35f);

    // ── Bass2 (acid 303) ──
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ParamId::BASS2_CUTOFF, 1 }, "Bass2 Cutoff", makeLogCutoffRange(), 400.f));
    addF(ParamId::BASS2_RESO,   "Bass2 Reso",    0.f,  1.f,     0.01f, 0.65f);
    addF(ParamId::BASS2_ENVMOD, "Bass2 EnvMod",  0.f,  1.f,     0.01f, 0.55f);
    addF(ParamId::BASS2_DECAY,  "Bass2 Decay",   0.01f, 1.f,    0.001f, 0.20f);
    addF(ParamId::BASS2_ACCENT, "Bass2 Accent",  0.f,  1.f,     0.01f, 0.50f);

    // ── Sidechain (kick → bass ducking) ──
    addF(ParamId::SC_BASS1,   "SC Bass1 Amt",  0.f, 1.f,   0.01f, 0.50f);
    addF(ParamId::SC_BASS2,   "SC Bass2 Amt",  0.f, 1.f,   0.01f, 0.30f);
    addF(ParamId::SC_RELEASE, "SC Release",     0.01f, 0.5f, 0.001f, 0.15f);

    // ── Global ──
    addF(ParamId::SWING, "Swing", 0.f, 1.f, 0.01f, 0.f);

    // ── Master FX ──
    addF(ParamId::MASTER_DRIVE, "Drive",         0.f,   1.f,  0.01f, 0.f);
    addF(ParamId::DELAY_TIME,   "Delay Time",    0.01f, 2.f,  0.001f, 0.375f);
    addF(ParamId::DELAY_FB,     "Delay Feedback", 0.f,  0.90f, 0.01f, 0.40f);
    addF(ParamId::DELAY_MIX,    "Delay Mix",     0.f,   1.f,  0.01f, 0.f);

    // Delay sync: 0=off, 1=1/4, 2=1/8, 3=1/8d, 4=1/16, 5=1/16d, 6=1/32
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ ParamId::DELAY_SYNC, 1 }, "Delay Sync",
        0, 6, 0));

    // Master Reverb + Compressor
    addF(ParamId::REVERB_SIZE, "Reverb Size", 0.f, 1.f, 0.01f, 0.55f);
    addF(ParamId::REVERB_MIX,  "Reverb Mix",  0.f, 1.f, 0.01f, 0.00f);
    addF(ParamId::COMP_AMOUNT, "Comp Amount", 0.f, 1.f, 0.01f, 0.00f);

    // MIDI output (sequence external gear)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamId::MIDI_OUT, 1 }, "MIDI Out", false));

    // TIGHT toggle — bass tightener
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamId::TIGHT, 1 }, "Tight Bass", false));

    // AUTO mode + Mutation + Drift knobs
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamId::AUTO_ENABLE, 1 }, "Auto Mode", false));
    addF(ParamId::AUTO_MUTATION, "Auto Mutation", 0.f, 1.f, 0.01f, 0.40f);
    addF(ParamId::AUTO_DRIFT,    "Auto Drift",    0.f, 1.f, 0.01f, 0.35f);

    // CHAOS // UNDERGROUND SAMPLE BUS
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamId::CHAOS_ENABLE, 1 }, "Chaos Bus", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ ParamId::CHAOS_BANK, 1 }, "Chaos Bank", 0, 8, 0));
    addF(ParamId::CHAOS_DENSITY, "Chaos Density", 0.f, 1.f, 0.01f, 0.45f);
    addF(ParamId::CHAOS_GATE,    "Chaos Gate",    0.f, 1.f, 0.01f, 0.50f);
    addF(ParamId::CHAOS_GRIT,    "Chaos Grit",    0.f, 1.f, 0.01f, 0.45f);
    addF(ParamId::CHAOS_PITCH,   "Chaos Pitch",   0.f, 1.f, 0.01f, 0.40f);
    addF(ParamId::CHAOS_VOL,     "Chaos Volume",  0.f, 1.f, 0.01f, 0.65f);

    // Live Dub-Throw + Spiral FX Drive + Master Mono-Maker < 120 Hz + Bipolar DJ Isolator ("WASH")
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamId::DUB_THROW, 1 }, "Dub Throw", false));
    addF(ParamId::SPIRAL_DRIVE, "Spiral Drive", 0.f, 1.f, 0.01f, 0.35f);
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamId::MONO_SUB, 1 }, "Mono <120Hz", true));
    addF(ParamId::DJ_FILTER, "DJ Wash Filter", -1.f, 1.f, 0.005f, 0.0f);
    addF(ParamId::MASTER_VOL, "Master Volume", 0.f, 1.25f, 0.005f, 1.0f);

    return layout;
}

} // namespace ParamLayout
