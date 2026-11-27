#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstddef>
#include "core/BusLayout.h"
#include "core/VoiceBank.h"
#include "core/ParamLayout.h"
#include "core/Scenes.h"
#include "core/PlateReverb.h"
#include "core/GlueCompressor.h"
#include "core/MasterDrive.h"
#include "core/MasterLimiter.h"
#include "core/OutputAnalyzer.h"
#include "core/VisualState.h"
#include "core/AutoMode.h"
#include "core/SequencerUndo.h"
#include "core/ChaosSampleBus.h"
#include "core/SpiralFx.h"
#include "core/BeatLfo.h"
#include "core/MidiLearn.h"
#include "Sequencer.h"

class OldSchoolBoxProcessor : public juce::AudioProcessor
{
public:
    OldSchoolBoxProcessor();
    ~OldSchoolBoxProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }   // optional output for sequencing external gear
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    Sequencer   sequencer;
    VoiceBank   voiceBank;
    std::atomic<float> currentBpm { 120.0f };  // host BPM or internal clock BPM
    std::atomic<bool>  internalPlay { false }; // free-run clock when DAW transport is stopped
    std::atomic<bool>  hostPlaying { false };  // true when DAW transport is driving playback

    // Scene snapshots (4 slots) + companion APVTS parameter snapshots (for Snap-Back / REVERT)
    std::array<Scenes::Snapshot, Scenes::COUNT> scenes;
    std::array<juce::ValueTree, Scenes::COUNT>  sceneParams;
    Scenes::Snapshot                            preFillSnapshot;
    SequencerUndo                               sequencerUndo;

    // 1-click MIDI CC Learn manager
    MidiLearnManager midiLearn;

    void captureSceneParams(int slot)
    {
        if (slot >= 0 && slot < Scenes::COUNT)
            sceneParams[idx(slot)] = apvts.copyState();
    }

    void applySceneParams(int slot)
    {
        if (slot >= 0 && slot < Scenes::COUNT && sceneParams[idx(slot)].isValid())
            apvts.replaceState(sceneParams[idx(slot)].createCopy());
    }

    double getSampleRateSafe() const { return currentSampleRate > 1000.0 ? currentSampleRate : 44100.0; }

    // Master FX + analyzer (audio thread only, except analyzer which exposes atomic state)
    PlateReverb     plateReverb;
    MasterDrive     masterDrive;
    GlueCompressor  glueComp;
    MasterLimiter   masterLimiter;
    OutputAnalyzer  analyzer;
    VisualState     visualState;
    AutoMode        autoMode;
    ChaosSampleBus  chaosBus;
    SpiralFx        spiralFx;
    BeatLfoBank     beatLfo;
    bool            autoPrevEnabled = false;
    std::atomic<bool> dubThrowHeld { false };
    std::atomic<bool> killSubHeld  { false };
    std::atomic<bool> masterClipping { false };

private:
    static std::size_t idx(int i) { return static_cast<std::size_t>(i); }

    std::array<int, Sequencer::NUM_CHANNELS> lastSteps;
    int64_t prevBar = -1;
    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;
    float  lastHostBpm = -1.0f;
    uint32_t audioRng = 0xC0FFEE42u;  // audio-thread RNG for probability/humanise
    std::array<int, Sequencer::NUM_CHANNELS> pendingMidiNotes;  // currently-on out-MIDI notes per channel

    // Mono crossover: LP filter states per bus, per channel (L/R)
    // Below ~300Hz → mono, above → stereo. TIGHT toggle uses a tighter
    // 150Hz fold + master low-shelf cut.
    struct CrossoverState {
        float lpL = 0.0f, lpR = 0.0f;
    };
    std::array<CrossoverState, OsbBus::COUNT> crossover {};
    float crossoverAlpha     = 0.04f;   // normal — 300 Hz
    float tightCrossoverAlpha = 0.02f;  // tight — 150 Hz

    // Master low-cut HP for TIGHT mode (one-pole HP @ 40 Hz)
    float tightHpL = 0.0f, tightHpR = 0.0f;
    float tightHpPrevL = 0.0f, tightHpPrevR = 0.0f;
    float tightHpAlpha = 0.99f;

    // Master 8 Hz DC Blocker (pre-Glue/Limiter/Phase meter — preserves headroom & prevents DC bias)
    float masterDcAlpha = 0.9988f;
    float masterDcPrevL = 0.0f, masterDcOutL = 0.0f;
    float masterDcPrevR = 0.0f, masterDcOutR = 0.0f;

    // Master Linkwitz-Riley LR4 (24 dB/oct) Elliptical M/S Mono-Maker @ 120 Hz on Side channel
    struct BiquadState { float z1 = 0.0f, z2 = 0.0f; };
    float lr4B0 = 1.0f, lr4B1 = 0.0f, lr4B2 = 0.0f, lr4A1 = 0.0f, lr4A2 = 0.0f;
    BiquadState sideLr4A {}, sideLr4B {};

    // Master Reverb wet HPF @ 250 Hz + Master Delay smooth 12 dB/oct HPF @ 300 Hz & 6 kHz damping
    float delHp1L = 0.0f, delHp1PrevL = 0.0f, delHp2L = 0.0f, delHp2PrevL = 0.0f;
    float delHp1R = 0.0f, delHp1PrevR = 0.0f, delHp2R = 0.0f, delHp2PrevR = 0.0f;
    float delLpL  = 0.0f, delLpR      = 0.0f;
    float delHpAlpha = 0.95f, delLpAlpha = 0.45f;
    float revWetHpL = 0.0f, revWetHpR = 0.0f, revWetPrevL = 0.0f, revWetPrevR = 0.0f;
    float fxWetHpAlpha = 0.96f;

    // Per-sample parameter smoothing (~10 ms, eliminates zipper noise on live automation, #28)
    float paramSmoothAlpha = 0.002f;
    float smDrive = 0.0f, smDelayMix = 0.0f, smReverbMix = 0.0f, smCompAmt = 0.0f;
    float smDjFilter = 0.0f;
    float smMasterVol = 1.0f;
    std::array<float, OsbBus::COUNT> smVol {};
    std::array<float, OsbBus::COUNT> smPan {};

    // Master Bipolar DJ Isolator ("WASH") — 2-pole SVF per channel (LP < 0, bypass 0, HP > 0)
    struct DjSvf { float ic1 = 0.0f, ic2 = 0.0f; };
    DjSvf djSvfL {}, djSvfR {};
    // Momentary KILL SUB (24 dB/oct HPF @ 120 Hz with 12 ms clickless wet/dry crossfade!)
    float killHp1L = 0.0f, killHp1PrevL = 0.0f, killHp2L = 0.0f, killHp2PrevL = 0.0f;
    float killHp1R = 0.0f, killHp1PrevR = 0.0f, killHp2R = 0.0f, killHp2PrevR = 0.0f;
    float killHpAlpha = 0.97f;
    float smKillSub = 0.0f, killFadeAlpha = 0.002f;

    // Master stereo delay line, allocated in prepareToPlay for 2 seconds at
    // the host sample rate (4-point Hermite cubic interpolation + authentic Dub Throw send gate).
    struct DelayState {
        std::vector<float> bufL, bufR;
        int writePos = 0;
        int size = 1;
        float smoothedDelay = 0.0f;   // 55 ms tape-style smoothed delay time in samples
        float throwSendEnv  = 0.0f;   // 5 ms clickless Dub Throw input send gate
        float tailEnergy    = 0.0f;   // keeps Dub Throw wet tail audible after button release
    };
    DelayState delay {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OldSchoolBoxProcessor)
};
