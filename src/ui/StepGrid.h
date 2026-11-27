#pragma once
#include <JuceHeader.h>
#include <cstddef>
#include <functional>
#include "../Sequencer.h"
#include "../core/VoiceBank.h"
#include "../core/VisualState.h"
#include "../core/OutputAnalyzer.h"
#include "../core/SequencerUndo.h"
#include "../core/MidiLearn.h"
#include "../core/ChaosSampleBus.h"
#include "../core/BeatLfo.h"
#include "ParamPanel.h"
#include "DjKnob.h"

class StepGrid : public juce::Component,
                 public juce::SettableTooltipClient,
                 public juce::Timer
{
public:
    enum class CoreScreen { Seq = 0, Matrix = 1, Mix = 2, Lfo = 3, Chaos = 4, Scope = 5 };

    StepGrid(Sequencer& seq, VoiceBank& voices, juce::AudioProcessorValueTreeState& apvts,
             std::atomic<float>& bpm, std::atomic<bool>& internalPlay,
             std::atomic<bool>& hostPlaying, VisualState& visState,
             OutputAnalyzer& analyzer, SequencerUndo& undo,
             ChaosSampleBus& chaosBus,
             BeatLfoBank& lfoBank,
             MidiLearnManager* midiLearn = nullptr);
    ~StepGrid() override;

    void triggerMuteToggle(int ch);
    void syncGhosts(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& savedState);

    int  getSelectedChannel() const { return selectedChannel; }
    void setSelectedChannel(int ch);
    void setCoreScreen(CoreScreen s);
    CoreScreen getCoreScreen() const { return coreScreen; }

    std::function<void(int)> onSelectedChannelChanged;
    std::function<void(CoreScreen)> onCoreScreenChanged;

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

    // ── MKII Mirror-Symmetric Cockpit Dimensions (1280 x 584) ──
    static constexpr int TOTAL_W       = 1280;
    static constexpr int TOTAL_H       = 584;  // 4 strips x 146 px
    static constexpr int STRIP_W       = 424;  // Expanded! Meter + Fader + M/S + 4x 54px Knobs!
    static constexpr int STRIP_H       = 146;
    static constexpr int CORE_X        = 432;
    static constexpr int CORE_W        = 416;
    static constexpr int KNOB_SZ       = 52;   // Uniform 52px knobs below taller 30px header!

    static int preferredWidth()  { return TOTAL_W; }
    static int preferredHeight() { return TOTAL_H; }

    // Exposed so OldSchoolBoxView can overlay Chaos controls inside the CHAOS screen cleanly
    juce::Rectangle<int> mainScreenRect() const { return { CORE_X + 10, 206, CORE_W - 20, 352 }; }
    void refreshThemeColors();

private:
    static std::size_t idx(int i) { return static_cast<std::size_t>(i); }

    Sequencer& sequencer;
    VoiceBank& voiceBank;
    juce::AudioProcessorValueTreeState& apvtsRef;
    std::atomic<float>& bpmRef;
    std::atomic<bool>&  internalPlayRef;
    std::atomic<bool>&  hostPlayingRef;
    VisualState& visualState;
    OutputAnalyzer& analyzerRef;
    SequencerUndo& undoRef;
    ChaosSampleBus& chaosBusRef;
    BeatLfoBank& lfoBankRef;
    MidiLearnManager* midiLearnRef = nullptr;

    // Cockpit state
    int        selectedChannel = 0;
    CoreScreen coreScreen      = CoreScreen::Seq;

    // Core Screen Tabs (SEQ | MATRIX | MIX | LFO | CHAOS | SCOPE) + Page + Copy/Paste + Acid
    juce::TextButton tabSeqBtn, tabMatrixBtn, tabMixBtn, tabLfoBtn, tabChaosBtn, tabScopeBtn;
    juce::TextButton hubVarBtn, hubRndBtn, hubClrBtn;
    juce::TextButton corePrevPageBtn, coreNextPageBtn;
    juce::TextButton coreCopyBtn, corePasteBtn;
    juce::TextButton acidRndBtn, acidScaleBtn;

    // Transport Play/Stop & Swing
    juce::TextButton playButton;
    std::unique_ptr<DjKnob> swingKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment;

    // Per-channel strip controls: Vertical VOL Fader + M/S + VAR/H/LEN/SELECT + 4x 54px Knobs!
    std::array<juce::TextButton, NUM_CH> selectButtons;
    std::array<juce::TextButton, NUM_CH> muteButtons;
    std::array<juce::TextButton, NUM_CH> soloButtons;
    std::array<juce::TextButton, NUM_CH> varButtons;
    std::array<juce::TextButton, NUM_CH> humButtons;
    std::array<juce::TextButton, NUM_CH> lenButtons;
    std::array<juce::TextButton, NUM_CH> lfoButtons;
    std::array<juce::TextButton, NUM_CH> flipButtons;
    std::array<bool, NUM_CH>             isFlipped {};
    std::array<std::unique_ptr<juce::Slider>, NUM_CH>                                         volFaders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NUM_CH> volAttachments;
    std::array<std::unique_ptr<DjKnob>, NUM_CH>                                               panKnobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NUM_CH> panAttachments;
    // Synced mirror knob for HATS PAN inside the 4x2 MIX console (stays 1:1 with Strip 3 via APVTS)
    std::unique_ptr<DjKnob>                                                                   mixHatsPanKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>                     mixHatsPanAttachment;
    std::array<ParamKnobGroup, NUM_CH>                                                        channelParams;

    // Per-channel view page
    std::array<int, NUM_CH> viewPage {};

    // CRT Scope phosphor persistence
    static constexpr int SCOPE_HIST = 4;
    std::array<std::array<float, OutputAnalyzer::SCOPE_SIZE>, SCOPE_HIST> scopeHistory {};
    int scopeHistWrite = 0;

    // Per-channel optical trigger meter + 500ms Peak-Hold (#16)
    std::array<std::array<float, VISIBLE_STEPS>, NUM_CH> stepLedGlow {};
    std::array<float, NUM_CH> channelTriggerGlow {};
    std::array<float, NUM_CH> voicePeakHold {};
    std::array<int,   NUM_CH> voicePeakTicks {};

    // Cached weathered metal tile & static chassis backdrop
    juce::Image noiseTile;
    juce::Image staticBackdrop;
    int lastBackdropTheme = -1;
    void rebuildStaticBackdrop();

    // Drag interaction state
    enum class DragMode { None, PaintOn, PaintOff, Velocity, Probability, MicroTiming, BassNote, BpmDrag };
    DragMode dragMode     = DragMode::None;
    int      dragCh       = -1;
    int      dragStep     = -1;
    int      dragStartY   = 0;
    int      dragStartVal = 0;
    float    dragStartBpm = 155.0f;

    // Layout rects
    juce::Rectangle<int> stripRect(int ch) const;
    juce::Rectangle<int> statusCrtRect() const;
    juce::Rectangle<int> bpmBoxRect() const;
    juce::Rectangle<float> stripFlipLfoRect(int ch) const;

    // Hit testing
    std::pair<int,int> hitMatrixCell(int x, int y) const;

    // Painting helpers
    void paintChannelStrips(juce::Graphics& g);
    void paintCoreFrameAndStatus(juce::Graphics& g);
    void paintMainScreen(juce::Graphics& g);
    void paintSeqScreen(juce::Graphics& g, juce::Rectangle<float> oled);
    void paintMatrixScreen(juce::Graphics& g, juce::Rectangle<float> oled);
    void paintMixScreen(juce::Graphics& g, juce::Rectangle<float> oled);
    void paintLfoScreen(juce::Graphics& g, juce::Rectangle<float> oled);
    void paintChaosScreen(juce::Graphics& g, juce::Rectangle<float> oled);
    void paintScopeScreen(juce::Graphics& g, juce::Rectangle<float> oled);

    void updateLenButton(int ch);
    void updateCoreButtons();
    void syncFromSequencer();

    // Helper: returns the 4 visible 54px knobs + labels for each channel strip
    struct StripKnobSlot {
        DjKnob* knob = nullptr;
        juce::String label;
    };
    std::array<StripKnobSlot, 4> getStripKnobSlots(int ch) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGrid)
};
