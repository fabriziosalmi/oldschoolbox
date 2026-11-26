#pragma once
#include <JuceHeader.h>
#include <cstddef>
#include "PluginProcessor.h"
#include "ui/StepGrid.h"
#include "ui/SpaceLookAndFeel.h"
#include "ui/DjKnob.h"

class DragExportButton : public juce::TextButton
{
public:
    std::function<juce::File()> onGenerateFile;

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragStarted && e.getDistanceFromDragStart() > 4 && onGenerateFile)
        {
            dragStarted = true;
            const auto f = onGenerateFile();
            if (f.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        dragStarted = false;
        juce::TextButton::mouseUp(e);
    }
private:
    bool dragStarted = false;
};

class OldSchoolBoxView : public juce::Component,
                         public juce::Timer,
                         public juce::FileDragAndDropTarget
{
public:
    explicit OldSchoolBoxView(OldSchoolBoxProcessor&);
    ~OldSchoolBoxView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Drag & drop WAV/AIFF directly onto the plugin to load into Chaos Sample Bus
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    static int baseWidth()  { return StepGrid::preferredWidth(); }
    static int baseHeight() { return TITLE_H + StepGrid::preferredHeight() + MASTER_H; }
    void skipBootAnimation() { bootDone = true; }

private:
    static std::size_t idx(int i) { return static_cast<std::size_t>(i); }

    static constexpr int TITLE_H    = 54;
    static constexpr int MASTER_H   = 146; // LIVE row (30) + 3-module Lower Rack (116)
    static constexpr int LIVE_ROW_H = 30;
    static constexpr int MK_SZ      = 36;
    static constexpr int CK_SZ      = 30;  // Chaos micro-knobs
    static constexpr int SCENE_BTN_W = 32;
    static constexpr int FILL_BTN_W  = 66;
    static constexpr int METER_W     = 48;

    OldSchoolBoxProcessor& processorRef;
    SpaceLookAndFeel spaceLnf;
    StepGrid stepGrid;
    juce::TooltipWindow tooltipWindow { this, 400 };
    juce::Image spaceLogo;
    juce::Image noiseTile;
    std::unique_ptr<juce::FileChooser> wavChooser;

    // Master FX knobs (0=DRIVE, 1=SIZE, 2=VERB, 3=COMP, 4=TIME, 5=FDBK, 6=MIX, 7=WASH)
    struct MasterKnob {
        std::unique_ptr<DjKnob> knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String label;
    };
    std::array<MasterKnob, 8> masterKnobs;

    // Chaos Sample Bus controls (Module 1)
    juce::TextButton chaosEnableButton;
    juce::TextButton chaosBankButton;
    juce::TextButton chaosWavButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chaosEnableAttachment;
    std::array<MasterKnob, 5> chaosKnobs; // DENS, GATE, GRIT, PTCH, VOL

    // Module 2 & 3 extra controls + Top-bar THEME switch
    juce::TextButton dubThrowButton;
    juce::TextButton monoSubButton;
    juce::TextButton killSubButton;
    juce::TextButton themeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoSubAttachment;

    // Live performance controls
    std::array<juce::TextButton, Scenes::COUNT> sceneButtons;  // A/B/C/D
    juce::TextButton revertButton;   // Snap-Back / Panic Reload (Elektron-style)
    juce::TextButton qntButton;      // Quantize mode: QNT:1B / QNT:1/4 / QNT:OFF
    DragExportButton dragWavButton;  // Drag & drop 4-bar WAV straight into DAW
    DragExportButton dragMidiButton; // Drag & drop 4-bar MIDI straight into DAW
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    juce::TextButton helpButton;
    juce::TextButton fillButton;
    juce::TextButton syncButton;
    juce::TextButton initButton;
    juce::TextButton midiOutButton;
    juce::TextButton tightButton;
    juce::TextButton autoButton;
    std::unique_ptr<DjKnob> autoMutKnob, autoDriftKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiOutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tightAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> autoMutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> autoDriftAttachment;

    int lastDrawnActive = -1;
    int lastDrawnQueued = -2;
    int64_t lastDrawnFillBar = -2;
    int lastDrawnSyncDiv = -1;
    int lastDrawnChaosBank = -1;
    float pressurePeakHold = 0.0f;
    int   pressurePeakTicks = 0;
    float meterSmoothL = 0.0f, meterSmoothR = 0.0f;
    double bootStartMs = 0.0;
    bool   bootDone    = false;

    void updateSyncLabel();
    void updateChaosBankLabel();
    void refreshSceneVisuals();
    void syncAllGhosts();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OldSchoolBoxView)
};

// Resizable wrapper editor: scales OldSchoolBoxView proportionally (65%..150%)
// so the plugin fits comfortably on 13"/14" laptops and 4K monitors alike.
class OldSchoolBoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit OldSchoolBoxEditor(OldSchoolBoxProcessor&);
    ~OldSchoolBoxEditor() override = default;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void skipBootAnimation() { view.skipBootAnimation(); }

private:
    OldSchoolBoxView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OldSchoolBoxEditor)
};
