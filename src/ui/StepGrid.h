#pragma once
#include <JuceHeader.h>
#include <cstddef>
#include "../Sequencer.h"
#include "../core/VoiceBank.h"
#include "../core/VisualState.h"
#include "../core/OutputAnalyzer.h"
#include "../core/SequencerUndo.h"
#include "../core/MidiLearn.h"
#include "ParamPanel.h"
#include "DjKnob.h"

class StepGrid : public juce::Component,
                 public juce::SettableTooltipClient,
                 public juce::Timer
{
public:
    StepGrid(Sequencer& seq, VoiceBank& voices, juce::AudioProcessorValueTreeState& apvts,
             std::atomic<float>& bpm, std::atomic<bool>& internalPlay,
             std::atomic<bool>& hostPlaying, VisualState& visState,
             OutputAnalyzer& analyzer, SequencerUndo& undo,
             MidiLearnManager* midiLearn = nullptr);
    ~StepGrid() override;

    // Trigger quantized (or immediate if stopped/QNT=OFF) mute toggle for channel 0..7
    void triggerMuteToggle(int ch);

    // Sync Ghost Dot positions on all track knobs from a saved Scene ValueTree
    void syncGhosts(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& savedState);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;

    static constexpr int NUM_CH        = Sequencer::NUM_CHANNELS;
    static constexpr int VISIBLE_STEPS = 16;

    // ── Single-row layout constants ──
    static constexpr int LABEL_W       = 60;
    static constexpr int STEP_W        = 37;
    static constexpr int BEAT_GAP      = 4;    // TR-909 4-step visual grouping gap
    static constexpr int STEPS_AREA_W  = VISIBLE_STEPS * STEP_W + 3 * BEAT_GAP; // 604
    static constexpr int STEP_H        = 32;
    static constexpr int ROW_H         = 62;   // single row height for ALL channels
    static constexpr int PKNOB_SZ      = 34;   // all knobs same size
    static constexpr int MAX_PKNOBS    = 6;
    static constexpr int PKNOB_GAP     = 3;
    static constexpr int PARAM_AREA_W  = MAX_PKNOBS * (PKNOB_SZ + PKNOB_GAP); // 222
    static constexpr int KNOB_W        = 34;   // vol knob (unified)
    static constexpr int PAN_KSZ       = 34;   // pan knob (unified)
    static constexpr int BTN_W         = 34;
    static constexpr int BTN_H         = 17;
    static constexpr int HEADER_H      = 32;
    static constexpr int PAD           = 4;
    static constexpr int LEN_BTN_W     = 30;
    static constexpr int PAGE_BTN_W    = 16;
    static constexpr int SWING_KSZ     = 20;
    static constexpr int VAR_BTN_W     = 30;
    static constexpr int SUB_ROW_H     = 18;   // 303 attribute sub-rows (note/slide/accent)
    static constexpr int BASS_EXTRA    = SUB_ROW_H * 3; // 3 sub-rows per bass channel

    static int stepX(int visStep)
    {
        return PAD + LABEL_W + visStep * STEP_W + (visStep / 4) * BEAT_GAP;
    }

    static int rowY(int ch)
    {
        // Bass channels (6,7) have extra sub-rows below previous bass channels
        int y = HEADER_H;
        for (int i = 0; i < ch; ++i)
        {
            y += ROW_H;
            if (i >= 6) y += BASS_EXTRA;  // bass channels are taller
        }
        return y;
    }

    static constexpr int MS_BTN_W = 20;  // mute/solo button width

    static int preferredWidth()
    {
        return PAD + LABEL_W + STEPS_AREA_W
             + PAD + PARAM_AREA_W
             + PAD + KNOB_W + PAD + PAN_KSZ
             + PAD + MS_BTN_W         // M/S stacked
             + PAD + BTN_W            // CLR/RND
             + PAD + BTN_W            // CPY/PST
             + PAD + VAR_BTN_W
             + PAD + LEN_BTN_W
             + PAD + PAGE_BTN_W * 2 + 2
             + PAD;
    }
    static int preferredHeight()
    {
        // 6 drum rows + 2 bass rows (each with BASS_EXTRA)
        return HEADER_H + NUM_CH * ROW_H + 2 * BASS_EXTRA + PAD;
    }

private:
    static std::size_t idx(int i) { return static_cast<std::size_t>(i); }

    Sequencer& sequencer;
    VoiceBank& voiceBank;
    std::atomic<float>& bpmRef;
    std::atomic<bool>&  internalPlayRef;
    std::atomic<bool>&  hostPlayingRef;
    VisualState& visualState;
    OutputAnalyzer& analyzerRef;
    SequencerUndo& undoRef;

    // Header internal transport button + 303 Acid RND + Scale Lock buttons
    juce::TextButton playButton;
    std::array<juce::TextButton, 2> acidRndButtons;
    juce::TextButton acidScaleButton;
    MidiLearnManager* midiLearnRef = nullptr;

    // CRT Scope phosphor persistence history (4 fading frames)
    static constexpr int SCOPE_HIST = 4;
    std::array<std::array<float, OutputAnalyzer::SCOPE_SIZE>, SCOPE_HIST> scopeHistory {};
    int scopeHistWrite = 0;

    // Cached weathered-metal industrial grain tile + static backdrop image (60 FPS optimization)
    juce::Image noiseTile;
    juce::Image staticBackdrop;
    int lastBackdropTheme = -1;
    void rebuildStaticBackdrop();

    // Micro-physics optical LED persistence (60-120 ms smooth decay @ 60 FPS)
    std::array<std::array<float, VISIBLE_STEPS>, NUM_CH> stepLedGlow {};
    int hoveredChannel = -1;

    // Per-channel view state (UI thread only)
    std::array<int, NUM_CH> viewPage {};

    // Per-channel widgets
    std::array<std::unique_ptr<DjKnob>, NUM_CH>                                                 volKnobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NUM_CH>   volAttachments;
    std::array<std::unique_ptr<DjKnob>, NUM_CH>                                                 panKnobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NUM_CH>   panAttachments;
    std::array<juce::TextButton, NUM_CH> clrButtons;
    std::array<juce::TextButton, NUM_CH> rndButtons;
    std::array<juce::TextButton, NUM_CH> varButtons;
    std::array<juce::TextButton, NUM_CH> lenButtons;
    std::array<juce::TextButton, NUM_CH> prevBtns;
    std::array<juce::TextButton, NUM_CH> nextBtns;
    std::array<juce::TextButton, NUM_CH> muteButtons;
    std::array<juce::TextButton, NUM_CH> soloButtons;
    std::array<juce::TextButton, NUM_CH> copyButtons;
    std::array<juce::TextButton, NUM_CH> pasteButtons;
    std::array<juce::TextButton, NUM_CH> humButtons;   // Humanize toggle (stacked with VAR)
    std::array<ParamKnobGroup, NUM_CH>   channelParams;

    // Global swing knob in header
    std::unique_ptr<DjKnob>                                                 swingKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   swingAttachment;

    // Step drag state — Swipe-to-Paint + Velocity / Probability / Micro-timing / Bass note
    enum class DragMode { None, PaintOn, PaintOff, Erase, Velocity, Probability, MicroTiming, BassNote };
    DragMode dragMode       = DragMode::None;
    int      dragCh         = -1;
    int      dragStep       = -1;
    int      dragStartX     = 0;
    int      dragStartY     = 0;
    int      dragStartVal   = 0;

    // Timer repaint guard
    std::array<int, NUM_CH> lastDisplayedStep {};
    float lastDisplayedBpm = 0.0f;  // detect BPM changes for header repaint

    // Returns the visible-step rectangle for a given channel and visible-step index (0-15)
    juce::Rectangle<int> stepRect(int ch, int visStep) const;

    // Returns {ch, absStep} if point hits a step cell, else {-1,-1}
    std::pair<int,int> hitStep(int x, int y) const;

    void updateLenButton(int ch);
    void updatePageButtons(int ch);
    void syncFromSequencer();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGrid)
};
