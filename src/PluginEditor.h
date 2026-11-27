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
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    static int baseWidth()  { return StepGrid::preferredWidth(); }
    static int baseHeight() { return TITLE_H + StepGrid::preferredHeight() + MASTER_H; }
    void skipBootAnimation() { bootDone = true; }

private:
    static std::size_t idx(int i) { return static_cast<std::size_t>(i); }

    static constexpr int TITLE_H    = 26;
    static constexpr int MASTER_H   = 130;
    static constexpr int MK_SZ      = 48;  // Big uniform Master FX knobs
    static constexpr int CK_SZ      = 46;  // Big Chaos Sampler knobs inside Center Screen

    OldSchoolBoxProcessor& processorRef;
    SpaceLookAndFeel spaceLnf;
    StepGrid stepGrid;
    juce::String activeTopHint;
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
    std::unique_ptr<juce::Slider> masterVolFader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolAttachment;

    // Chaos Sample Bus controls (live inside Center Screen when CHAOS tab is active + quick toggle in bottom rack!)
    juce::TextButton chaosEnableButton;
    juce::TextButton chaosOpenButton;
    juce::TextButton chaosTrigButton;
    juce::TextButton chaosLoopButton;
    juce::TextButton chaosRevButton;
    juce::TextButton chaosStutButton;
    juce::TextButton chaosSpiralButton;
    juce::TextButton chaosBankButton;
    juce::TextButton chaosWavButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chaosEnableAttachment;
    std::array<MasterKnob, 5> chaosKnobs; // DENS, GATE, GRIT, PTCH, VOL

    // Master FX extra controls + Top-bar THEME switch
    juce::TextButton dubThrowButton;
    juce::TextButton spiralButton;
    std::unique_ptr<DjKnob> spiralDriveKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spiralDriveAttachment;
    juce::TextButton monoSubButton;
    juce::TextButton killSubButton;
    juce::TextButton themeButton;
    juce::TextButton vizButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoSubAttachment;

    // Live performance controls
    std::array<juce::TextButton, Scenes::COUNT> sceneButtons;
    juce::TextButton revertButton;
    juce::TextButton qntButton;
    DragExportButton dragWavButton;
    DragExportButton dragMidiButton;
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
    float masterMeterL = 0.0f, masterMeterR = 0.0f;
    float masterPeakHoldL = 0.0f, masterPeakHoldR = 0.0f;
    int   masterPeakTicksL = 0, masterPeakTicksR = 0;
    double bootStartMs = 0.0;
    bool   bootDone    = false;

    juce::Rectangle<int> patLenBoxRect() const;
    void layoutChaosControls();
    void updateSyncLabel();
    void updateChaosBankLabel();
    void refreshSceneVisuals();
    void syncAllGhosts();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OldSchoolBoxView)
};

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
