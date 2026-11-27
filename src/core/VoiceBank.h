#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <cstddef>
#include "../voices/KickVoice.h"
#include "../voices/SnareVoice.h"
#include "../voices/HatsVoice.h"
#include "../voices/TomVoice.h"
#include "../voices/Perc1Voice.h"
#include "../voices/Perc2Voice.h"
#include "../voices/BassVoice.h"
#include "BusLayout.h"
#include "BeatLfo.h"

// Sidechain ducker: kick triggers a click-free 1 ms attack / exponential release envelope.
struct SidechainDucker
{
    float env          = 0.0f;
    float targetEnv    = 0.0f;
    float attackCoeff  = 0.02f;
    float releaseCoeff = 0.999f;

    void prepare(double sr, float relSec = 0.15f)
    {
        attackCoeff = 1.0f - static_cast<float>(std::exp(-1.0 / (0.001 * sr))); // 1 ms anti-click attack
        setRelease(sr, relSec);
        env = targetEnv = 0.0f;
    }

    void trigger(float amount)
    {
        targetEnv = std::clamp(amount, 0.0f, 0.98f);
    }

    void setRelease(double sr, float relSec)
    {
        releaseCoeff = static_cast<float>(std::exp(-1.0 / (std::max(0.01, static_cast<double>(relSec)) * sr)));
    }

    // Returns gain multiplier: 1.0 = no duck, 0.0 = fully ducked
    float tick()
    {
        if (targetEnv > env)
        {
            env += attackCoeff * (targetEnv - env);
            if (targetEnv - env < 0.01f)
                targetEnv = 0.0f; // peak reached → switch to release
        }
        else
        {
            env *= releaseCoeff;
        }
        return 1.0f - env;
    }
};

// Owns all 8 voices. Single point of contact for trigger, render, and UI requests.
class VoiceBank
{
public:
    explicit VoiceBank(juce::AudioProcessorValueTreeState& apvts);

    // Audio thread
    void prepare(double sampleRate);
    void consumeUiTriggers();
    void trigger(int ch, float vel = 1.0f);                      // drums 0-5
    void triggerBass(int idx, int midiNote, float vel = 1.0f);  // bass 0-1
    void triggerBass303(int idx, int midiNote, float vel, bool slide, bool accent);
    void noteOffBass(int idx);
    void renderBus(int ch, float* L, float* R, int numSamples, float vol);
    bool isActive(int ch) const;

    // UI / message thread
    void requestTrigger(int ch);
    void requestBassNote(int bassIdx, int midiNote);
    int  getBassNote(int bassIdx) const;

    // Master FX wet ducking driven by kick (audio thread)
    float tickFxDuck() { return fxDuck.tick(); }

    void setLfoBank(const BeatLfoBank* bank) { lfoBank = bank; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    const BeatLfoBank* lfoBank = nullptr;
    double storedSampleRate = 44100.0;

    KickVoice  kick;
    SnareVoice snare;
    HatsVoice  hats;
    TomVoice   tom;
    Perc1Voice perc1;
    Perc2Voice perc2;
    std::array<BassVoice, 2> bass;

    // Sidechain duckers (one per bass channel + one for Master Reverb/Delay wet)
    std::array<SidechainDucker, 2> sidechain;
    SidechainDucker fxDuck;

    std::array<std::atomic<bool>, OsbBus::COUNT> uiTriggers {};
    std::array<std::atomic<int>,  2>             uiBassNotes {};   // init in constructor
    std::array<float, OsbBus::COUNT>             channelVelocity {};

    float raw(const char* id) const;
    float modded(int ch, int targetSlot, const char* id) const;
    static bool validBus(int ch) { return ch >= 0 && ch < OsbBus::COUNT; }
    static bool validBass(int idx) { return idx == 0 || idx == 1; }
    static std::size_t busIndex(int ch) { return static_cast<std::size_t>(ch); }
    static std::size_t bassIndex(int idx) { return static_cast<std::size_t>(idx); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceBank)
};
