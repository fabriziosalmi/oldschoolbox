#include "PluginEditor.h"
#include "BinaryData.h"
#include "core/ParamIds.h"
#include "core/Scenes.h"
#include "core/OutputAnalyzer.h"
#include "core/FactoryPresets.h"
#include "core/LoopExporter.h"
#include "ui/SpaceArt.h"

namespace
{
class HelpCheatsheet : public juce::Component
{
public:
    HelpCheatsheet() { setSize(420, 310); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(SpaceColours::bg.brighter(0.05f));
        g.setColour(SpaceColours::neonGreen);
        g.setFont(SpaceArt::monoFont(11.5f, true));
        g.drawText("OLDSCHOOLBOX MKII — DJ & LIVE GESTURES", 10, 8, getWidth() - 20, 18,
                   juce::Justification::centredLeft);

        static const char* rows[][2] = {
            { "Tap Top CRT / Tabs",       "Cycle SEQ / MATRIX / MIX / CHAOS / SCOPE" },
            { "SEQ Bars (Click / Drag)",  "Toggle step & draw Velocity / Nudge / Prob%" },
            { "Channel VOL Fader",        "Direct channel level (Double-click: reset)" },
            { "Strip Header (L / R)",     "Select & Audition / Euclidean generator" },
            { "LEN / PATTERN LENGTH",     "1B-4B bars or Wheel/Shift/Alt: 1..64 steps" },
            { "Knobs (Alt + Drag)",       "Spring-Back Tweaking (snaps back on release)" },
            { "Knobs (Shift / Wheel)",    "Surgical fine precision / micro-adjust" },
            { "Knobs (Right-click)",      "1-Click MIDI Learn (Shift+R: clear CC)" },
            { "Scene A-D (Shift / Alt)",  "Save scene+params / Snap-Back REVERT" },
            { "QNT:1B / 1/4 / OFF",       "Quantize Scene switches & Mutes" },
            { "QWERTY 1-8 / Space / F",   "Quantized Mute 1-8 / Play / Fill" },
            { "QWERTY Q-R / C / K / V",   "Scenes A-D / Chaos / Kill Sub / Revert" },
            { "Drag WAV> / MID>",         "Drop 4-bar audio/MIDI straight into DAW" },
            { "Cmd+Z / Shift+Cmd+Z",      "Undo / Redo sequencer edits" },
        };

        int y = 32;
        for (const auto& r : rows)
        {
            g.setFont(SpaceArt::monoFont(9.0f, true));
            g.setColour(SpaceColours::neonGreen.withAlpha(0.88f));
            g.drawText(r[0], 10, y, 168, 16, juce::Justification::centredLeft);
            g.setFont(SpaceArt::monoFont(9.0f, false));
            g.setColour(SpaceColours::textBright.withAlpha(0.88f));
            g.drawText(r[1], 182, y, getWidth() - 190, 16, juce::Justification::centredLeft);
            y += 18;
        }
    }
};
} // namespace

OldSchoolBoxView::OldSchoolBoxView(OldSchoolBoxProcessor& p)
    : processorRef(p),
      stepGrid(p.sequencer, p.voiceBank, p.apvts, p.currentBpm, p.internalPlay, p.hostPlaying,
               p.visualState, p.analyzer, p.sequencerUndo, p.chaosBus, p.beatLfo, &p.midiLearn)
{
    setLookAndFeel(&spaceLnf);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    spaceLogo = juce::ImageCache::getFromMemory(BinaryData::space_invaders_logo_png,
                                                BinaryData::space_invaders_logo_pngSize);
    noiseTile = SpaceArt::makeNoiseTile(128);
    addAndMakeVisible(stepGrid);

    stepGrid.onSelectedChannelChanged = [this](int) { repaint(0, getHeight() - MASTER_H, getWidth(), MASTER_H); };
    stepGrid.onCoreScreenChanged = [this](StepGrid::CoreScreen) { layoutChaosControls(); repaint(); };

    // ── Master FX knobs ──
    auto makeMK = [&](int idx, const char* paramId, const juce::String& label, juce::Colour col) {
        auto& mk = masterKnobs[static_cast<size_t>(idx)];
        mk.label = label;
        mk.knob = std::make_unique<DjKnob>(paramId, &p.apvts, &p.midiLearn, "MASTER " + label);
        mk.knob->setTooltip(label + " (Alt+drag: spring-back | Right-click: MIDI Learn)");
        mk.knob->setColour(juce::Slider::rotarySliderFillColourId, col);
        mk.knob->getProperties().set("knobStyle", "mackie");
        mk.knob->setPopupDisplayEnabled(false, false, nullptr);
        mk.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            p.apvts, paramId, *mk.knob);
        addAndMakeVisible(*mk.knob);
    };

    // Cohesive warm master-bus accent (eliminates bottom-rack rainbow clutter!)
    const auto masterGold = SpaceColours::neonYellow;
    makeMK(0, ParamId::MASTER_DRIVE, "SAT",  masterGold);
    makeMK(1, ParamId::REVERB_SIZE,  "SIZE", masterGold);
    makeMK(2, ParamId::REVERB_MIX,   "VERB", masterGold);
    makeMK(3, ParamId::COMP_AMOUNT,  "GLUE", masterGold);
    makeMK(4, ParamId::DELAY_TIME,   "TIME", masterGold);
    makeMK(5, ParamId::DELAY_FB,     "FDBK", masterGold);
    makeMK(6, ParamId::DELAY_MIX,    "MIX",  masterGold);
    makeMK(7, ParamId::DJ_FILTER,    "ISOL", masterGold);
    if (masterKnobs[7].knob) masterKnobs[7].knob->setBipolar(true);

    // Master Output Volume Fader (SSL vertical fader right next to OUT L/R meter!)
    masterVolFader = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    masterVolFader->setTooltip("Master Output Level (Double-click: 0.0 dB unity)");
    masterVolFader->setColour(juce::Slider::trackColourId, masterGold);
    masterVolFader->setDoubleClickReturnValue(true, 1.0);
    masterVolFader->onValueChange = [this] {
        MusicalUnits::publishHud("MASTER OUT", ParamId::MASTER_VOL, masterVolFader->getValue());
    };
    masterVolAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, ParamId::MASTER_VOL, *masterVolFader);
    addAndMakeVisible(*masterVolFader);

    // ── Chaos Sample Bus controls (quick toggle in bottom rack + full controls inside CHAOS Center Screen!) ──
    chaosEnableButton.setButtonText("CHAOS");
    chaosEnableButton.setTooltip("Chaos Sample Bus ON/OFF (C)");
    chaosEnableButton.setClickingTogglesState(true);
    chaosEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::CHAOS_ENABLE, chaosEnableButton);
    addAndMakeVisible(chaosEnableButton);

    chaosOpenButton.setButtonText("EDIT");
    chaosOpenButton.setTooltip("Open Chaos Sampler inside Center Screen");
    chaosOpenButton.onClick = [this] {
        stepGrid.setCoreScreen(stepGrid.getCoreScreen() == StepGrid::CoreScreen::Chaos
            ? StepGrid::CoreScreen::Seq : StepGrid::CoreScreen::Chaos);
    };
    addAndMakeVisible(chaosOpenButton);

    chaosTrigButton.setButtonText("TRIG");
    chaosTrigButton.setTooltip("Audition / manually trigger Chaos sample one-shot now");
    chaosTrigButton.onClick = [this] {
        processorRef.chaosBus.requestManualTrigger();
    };
    addAndMakeVisible(chaosTrigButton);

    chaosLoopButton.setButtonText("LOOP");
    chaosLoopButton.setTooltip("Beat-Synced Auto-Loop: OFF / 1/4 / 1/2 / 1B / 2B");
    chaosLoopButton.onClick = [this] {
        const int next = (processorRef.chaosBus.loopMode.load(std::memory_order_relaxed) + 1) % 5;
        processorRef.chaosBus.loopMode.store(next, std::memory_order_relaxed);
        chaosLoopButton.setButtonText(ChaosSampleBus::loopModeName(next));
        chaosLoopButton.setToggleState(next > 0, juce::dontSendNotification);
    };
    addAndMakeVisible(chaosLoopButton);

    chaosRevButton.setButtonText("REV");
    chaosRevButton.setTooltip("Instant Reverse playback on Chaos Sampler");
    chaosRevButton.setClickingTogglesState(true);
    chaosRevButton.onClick = [this] {
        processorRef.chaosBus.userReverse.store(chaosRevButton.getToggleState(), std::memory_order_relaxed);
    };
    addAndMakeVisible(chaosRevButton);

    chaosStutButton.setButtonText("STUT");
    chaosStutButton.setTooltip("Hold for live 1/16 beat-repeat stutter roll on current slice");
    chaosStutButton.onStateChange = [this] {
        processorRef.chaosBus.stutterHeld.store(chaosStutButton.isDown(), std::memory_order_relaxed);
    };
    addAndMakeVisible(chaosStutButton);

    chaosBankButton.setTooltip("Cycle built-in Tekno DNA bank (8 sounds + Custom WAV)");
    chaosBankButton.onClick = [this] {
        auto* param = processorRef.apvts.getRawParameterValue(ParamId::CHAOS_BANK);
        const int maxB = processorRef.chaosBus.hasCustomSample() ? 8 : 7;
        const int next = (static_cast<int>(param->load()) + 1) % (maxB + 1);
        processorRef.apvts.getParameter(ParamId::CHAOS_BANK)
            ->setValueNotifyingHost(static_cast<float>(next) / 8.0f);
        processorRef.chaosBus.syncPreviewBank(next);
        updateChaosBankLabel();
    };
    addAndMakeVisible(chaosBankButton);

    chaosWavButton.setButtonText("DROP WAV");
    chaosWavButton.setTooltip("Load custom WAV/AIFF (max 8s) — or drag & drop WAV anywhere");
    chaosWavButton.onClick = [this] {
        wavChooser = std::make_unique<juce::FileChooser>("Load Underground Sample (WAV/AIFF, max 8s)",
                                                         juce::File{}, "*.wav;*.aif;*.aiff");
        wavChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    processorRef.chaosBus.loadCustomFileAsync(f, [this](bool ok) {
                        if (ok)
                        {
                            processorRef.apvts.getParameter(ParamId::CHAOS_BANK)->setValueNotifyingHost(1.0f);
                            updateChaosBankLabel();
                            repaint();
                        }
                    });
                }
            });
    };
    addAndMakeVisible(chaosWavButton);

    auto makeCK = [&](int idx, const char* paramId, const juce::String& label) {
        auto& ck = chaosKnobs[static_cast<size_t>(idx)];
        ck.label = label;
        ck.knob = std::make_unique<DjKnob>(paramId, &p.apvts, &p.midiLearn, "CHAOS " + label);
        ck.knob->setTooltip("Chaos " + label + " (Alt+drag: spring-back | Right-click: MIDI Learn)");
        ck.knob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonMagenta);
        ck.knob->setPopupDisplayEnabled(false, false, nullptr);
        ck.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            p.apvts, paramId, *ck.knob);
        addAndMakeVisible(*ck.knob);
    };
    makeCK(0, ParamId::CHAOS_DENSITY, "DENS");
    makeCK(1, ParamId::CHAOS_GATE,    "GATE");
    makeCK(2, ParamId::CHAOS_GRIT,    "GRIT");
    makeCK(3, ParamId::CHAOS_PITCH,   "PTCH");
    makeCK(4, ParamId::CHAOS_VOL,     "VOL");
    updateChaosBankLabel();

    // ── DUB THROW / SPIRAL FX / MONO <120 / KILL SUB / THEME ──
    dubThrowButton.setButtonText("DUB");
    dubThrowButton.setTooltip("Hold for live Dub Delay throw (82% feedback spike)");
    dubThrowButton.onStateChange = [this] {
        processorRef.dubThrowHeld.store(dubThrowButton.isDown(), std::memory_order_release);
    };
    addAndMakeVisible(dubThrowButton);

    auto wireSpiralBtn = [this](juce::TextButton& btn) {
        btn.setButtonText("SPIRAL");
        btn.setTooltip("Spiral FX (Percs + Samples): Jet Flanger + 3/16 Gated Decaying Delay + Drive — Click: Throw | Shift+Click: Latch");
        btn.setClickingTogglesState(false);
        btn.onClick = [this] {
            if (juce::ModifierKeys::currentModifiers.isShiftDown())
                processorRef.spiralFx.toggleLatch();
            else
                processorRef.spiralFx.triggerThrow();
        };
        addAndMakeVisible(btn);
    };
    wireSpiralBtn(spiralButton);
    wireSpiralBtn(chaosSpiralButton);

    spiralDriveKnob = std::make_unique<DjKnob>(ParamId::SPIRAL_DRIVE, &p.apvts, &p.midiLearn, "SPIRAL DRV");
    spiralDriveKnob->setTooltip("Spiral FX Distortion / Grit range (0% clean dub flange .. 100% Spiral scream)");
    spiralDriveKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonPurple);
    spiralDriveKnob->setPopupDisplayEnabled(false, false, nullptr);
    spiralDriveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, ParamId::SPIRAL_DRIVE, *spiralDriveKnob);
    addAndMakeVisible(*spiralDriveKnob);

    monoSubButton.setButtonText("MONO <120");
    monoSubButton.setTooltip("Elliptical M/S Mono-Maker (< 120 Hz Side HPF)");
    monoSubButton.setClickingTogglesState(true);
    monoSubAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::MONO_SUB, monoSubButton);
    addAndMakeVisible(monoSubButton);

    killSubButton.setButtonText("KILL SUB");
    killSubButton.setTooltip("Hold (or press K) to cut <150 Hz for live drop builds");
    killSubButton.onStateChange = [this] {
        processorRef.killSubHeld.store(killSubButton.isDown(), std::memory_order_release);
    };
    addAndMakeVisible(killSubButton);

    themeButton.setComponentID("themeBtn");
    themeButton.setButtonText(SpaceColours::theme().name);
    themeButton.setTooltip("Switch Hardware Theme (13 skins) — Click: Next | Shift+Click: Pick from Menu");
    themeButton.onClick = [this] {
        auto applyThemeIdx = [this](int idx) {
            SpaceColours::activeTheme() = std::clamp(idx, 0, SpaceColours::NUM_THEMES - 1);
            themeButton.setButtonText(SpaceColours::theme().name);
            const auto mCol = SpaceColours::theme().masterAccent;
            for (auto& mk : masterKnobs)
                if (mk.knob) mk.knob->setColour(juce::Slider::rotarySliderFillColourId, mCol);
            if (masterVolFader) masterVolFader->setColour(juce::Slider::trackColourId, mCol);
            stepGrid.refreshThemeColors();
            repaint();
        };
        if (juce::ModifierKeys::currentModifiers.isShiftDown())
        {
            juce::PopupMenu menu;
            const int cur = SpaceColours::activeTheme();
            for (int i = 0; i < SpaceColours::NUM_THEMES; ++i)
            {
                SpaceColours::activeTheme() = i;
                const juce::String nm = SpaceColours::theme().name;
                menu.addItem(i + 1, nm, true, i == cur);
            }
            SpaceColours::activeTheme() = cur;
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&themeButton),
                [applyThemeIdx](int res) { if (res > 0) applyThemeIdx(res - 1); });
        }
        else
        {
            applyThemeIdx((SpaceColours::activeTheme() + 1) % SpaceColours::NUM_THEMES);
        }
    };
    addAndMakeVisible(themeButton);

    vizButton.setComponentID("vizBtn");
    vizButton.setButtonText(SpaceColours::vizModeName(SpaceColours::vizMode()));
    vizButton.setTooltip("Beat-Synced Visual FX: OFF / SUBTLE / RAVE (100% UI thread, zero audio cost)");
    vizButton.onClick = [this] {
        int& m = SpaceColours::vizMode();
        m = (m + 1) % 3;
        vizButton.setButtonText(SpaceColours::vizModeName(m));
        stepGrid.repaint();
        repaint();
    };
    addAndMakeVisible(vizButton);

    syncButton.onClick = [this] {
        auto* param = processorRef.apvts.getRawParameterValue(ParamId::DELAY_SYNC);
        const int cur = static_cast<int>(param->load());
        const int next = (cur + 1) % 7;
        processorRef.apvts.getParameter(ParamId::DELAY_SYNC)
            ->setValueNotifyingHost(static_cast<float>(next) / 6.0f);
        updateSyncLabel();
    };
    addAndMakeVisible(syncButton);
    updateSyncLabel();

    // ── Scenes A/B/C/D + REVERT + QNT ──
    static const char* sceneLabels[] = { "A", "B", "C", "D" };
    for (int i = 0; i < Scenes::COUNT; ++i)
    {
        sceneButtons[idx(i)].setButtonText(sceneLabels[i]);
        sceneButtons[idx(i)].setTooltip(
            juce::String("Scene ") + sceneLabels[i]
          + " (Q/W/E/R) — Click: quantized switch | Shift+Click: save | Alt+Click: REVERT");
        sceneButtons[idx(i)].onClick = [this, i] {
            const auto mods = juce::ModifierKeys::currentModifiers;
            if (mods.isShiftDown() || mods.isCommandDown())
            {
                Scenes::capture(processorRef.scenes[idx(i)], processorRef.sequencer);
                processorRef.captureSceneParams(i);
                refreshSceneVisuals();
            }
            else if (mods.isAltDown())
            {
                if (processorRef.sequencer.isPlaying.load(std::memory_order_acquire)
                    && processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed) > 0)
                {
                    processorRef.sequencer.queuedScene.store(i, std::memory_order_release);
                    processorRef.sequencer.revertQueued.store(true, std::memory_order_release);
                }
                else
                {
                    Scenes::apply(processorRef.scenes[idx(i)], processorRef.sequencer);
                    processorRef.sequencer.activeScene.store(i, std::memory_order_release);
                    processorRef.applySceneParams(i);
                    stepGrid.repaint();
                }
                refreshSceneVisuals();
            }
            else
            {
                const int active = processorRef.sequencer.activeScene.load(std::memory_order_acquire);
                const bool playing = processorRef.sequencer.isPlaying.load(std::memory_order_acquire);
                const int  qnt     = processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed);
                if (i == active)
                    processorRef.sequencer.queuedScene.store(-1, std::memory_order_release);
                else if (playing && qnt > 0)
                    processorRef.sequencer.queuedScene.store(i, std::memory_order_release);
                else
                {
                    Scenes::apply(processorRef.scenes[idx(i)], processorRef.sequencer);
                    processorRef.sequencer.activeScene.store(i, std::memory_order_release);
                    processorRef.sequencer.queuedScene.store(-1, std::memory_order_release);
                    processorRef.applySceneParams(i);
                    stepGrid.repaint();
                }
                refreshSceneVisuals();
            }
        };
        addAndMakeVisible(sceneButtons[idx(i)]);
    }

    revertButton.setButtonText("REVERT");
    revertButton.setTooltip("Snap-Back / Panic Reload (V)");
    revertButton.onClick = [this] {
        const int active = processorRef.sequencer.activeScene.load(std::memory_order_acquire);
        if (processorRef.sequencer.isPlaying.load(std::memory_order_acquire)
            && processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed) > 0)
        {
            processorRef.sequencer.revertQueued.store(true, std::memory_order_release);
        }
        else
        {
            Scenes::apply(processorRef.scenes[idx(active)], processorRef.sequencer);
            processorRef.applySceneParams(active);
            stepGrid.repaint();
        }
        refreshSceneVisuals();
    };
    addAndMakeVisible(revertButton);

    auto updateQntLabel = [this] {
        static const char* kQnt[3] = { "OFF", "1/4", "1B" };
        const int m = std::clamp(processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed), 0, 2);
        qntButton.setButtonText(kQnt[m]);
    };
    qntButton.setTooltip("Live Quantize for Scene switches & Mutes: 1B / 1/4 / OFF");
    qntButton.onClick = [this, updateQntLabel] {
        const int cur = processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed);
        processorRef.sequencer.quantizeMode.store((cur + 1) % 3, std::memory_order_release);
        updateQntLabel();
    };
    updateQntLabel();
    addAndMakeVisible(qntButton);

    dragWavButton.setButtonText("WAV>");
    dragWavButton.setTooltip("Drag & drop 4-bar 24-bit WAV into DAW");
    dragWavButton.onGenerateFile = [this] {
        return LoopExporter::exportWav4Bars(processorRef.sequencer, processorRef.apvts,
                                            processorRef.currentBpm.load(std::memory_order_relaxed),
                                            processorRef.getSampleRateSafe());
    };
    addAndMakeVisible(dragWavButton);

    dragMidiButton.setButtonText("MID>");
    dragMidiButton.setTooltip("Drag & drop 4-bar Drums + 303 MIDI into DAW");
    dragMidiButton.onGenerateFile = [this] {
        return LoopExporter::exportMidi4Bars(processorRef.sequencer,
                                             processorRef.currentBpm.load(std::memory_order_relaxed));
    };
    addAndMakeVisible(dragMidiButton);

    undoButton.setButtonText("UNDO");
    undoButton.setTooltip("Undo last sequencer edit (Cmd+Z)");
    undoButton.onClick = [this] {
        if (processorRef.sequencerUndo.undo(processorRef.sequencer))
        {
            stepGrid.repaint();
            refreshSceneVisuals();
        }
    };
    addAndMakeVisible(undoButton);

    redoButton.setButtonText("REDO");
    redoButton.setTooltip("Redo sequencer edit (Shift+Cmd+Z)");
    redoButton.onClick = [this] {
        if (processorRef.sequencerUndo.redo(processorRef.sequencer))
        {
            stepGrid.repaint();
            refreshSceneVisuals();
        }
    };
    addAndMakeVisible(redoButton);

    helpButton.setButtonText("?");
    helpButton.setTooltip("Gestures & keyboard shortcuts cheatsheet");
    helpButton.onClick = [this] {
        auto sheet = std::make_unique<HelpCheatsheet>();
        juce::CallOutBox::launchAsynchronously(std::move(sheet),
            helpButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible(helpButton);

    fillButton.setButtonText("FILL");
    fillButton.setTooltip("Fill — 1-bar fill on next bar, then restores pattern (F)");
    fillButton.onClick = [this] {
        auto& seq = processorRef.sequencer;
        Scenes::capture(processorRef.preFillSnapshot, seq);
        const int64_t cb = seq.currentBar.load(std::memory_order_acquire);
        seq.fillStartBar.store(cb + 1, std::memory_order_release);
    };
    addAndMakeVisible(fillButton);

    initButton.setButtonText("INIT");
    initButton.setTooltip("Init / Factory presets");
    initButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Init (clear all)");
        menu.addSeparator();
        menu.addSectionHeader("Factory Presets");
        const auto& presets = FactoryPresets::all();
        for (size_t i = 0; i < presets.size(); ++i)
            menu.addItem(static_cast<int>(i + 2), presets[i].name);

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&initButton),
            [this](int result)
            {
                if (result == 0) return;
                auto& seq = processorRef.sequencer;
                processorRef.sequencerUndo.push(seq);
                if (result == 1)
                {
                    for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
                    {
                        seq.clearChannel(ch);
                        seq.setPatternLength(ch, 16);
                        seq.autoVar[idx(ch)].store(false, std::memory_order_release);
                        seq.mute[idx(ch)].store(false, std::memory_order_release);
                        seq.solo[idx(ch)].store(false, std::memory_order_release);
                        seq.humanize[idx(ch)].store(false, std::memory_order_release);
                        for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                        {
                            seq.setVelocity(ch, s, 100);
                            seq.setProbability(ch, s, 100);
                            seq.setMicroTiming(ch, s, 0);
                        }
                    }
                    for (int bi = 0; bi < 2; ++bi)
                    {
                        seq.setBassSlidePattern(bi, 0);
                        seq.setBassAccentPattern(bi, 0);
                        seq.setBassOctUpPattern(bi, 0);
                        seq.setBassOctDownPattern(bi, 0);
                    }
                }
                else
                {
                    const auto& preset = FactoryPresets::all()[static_cast<size_t>(result - 2)];
                    Scenes::apply(preset.snapshot, seq);
                    FactoryPresets::applyParams(preset, processorRef.apvts);
                }
                for (auto& sc : processorRef.scenes)
                    Scenes::capture(sc, seq);
                seq.activeScene.store(0, std::memory_order_release);
                seq.queuedScene.store(-1, std::memory_order_release);
                seq.fillStartBar.store(-1, std::memory_order_release);
                stepGrid.repaint();
                refreshSceneVisuals();
            });
    };
    addAndMakeVisible(initButton);

    midiOutButton.setButtonText("MIDI");
    midiOutButton.setTooltip("MIDI Out — sequence external gear (drums ch10, basses ch1/ch2)");
    midiOutButton.setClickingTogglesState(true);
    midiOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::MIDI_OUT, midiOutButton);
    addAndMakeVisible(midiOutButton);

    tightButton.setButtonText("TIGHT");
    tightButton.setTooltip("Tight Bass — lower mono fold (150 Hz) + master low-cut HP @ 40 Hz");
    tightButton.setClickingTogglesState(true);
    tightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::TIGHT, tightButton);
    addAndMakeVisible(tightButton);

    autoButton.setButtonText("AUTO");
    autoButton.setTooltip("Auto Mode — cruise control pattern evolution");
    autoButton.setClickingTogglesState(true);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::AUTO_ENABLE, autoButton);
    addAndMakeVisible(autoButton);

    autoMutKnob = std::make_unique<DjKnob>(ParamId::AUTO_MUTATION, &p.apvts, &p.midiLearn, "AUTO MUT");
    autoMutKnob->setTooltip("Auto Mutation rate");
    autoMutKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonYellow);
    autoMutKnob->setPopupDisplayEnabled(false, false, nullptr);
    autoMutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, ParamId::AUTO_MUTATION, *autoMutKnob);
    addAndMakeVisible(*autoMutKnob);

    autoDriftKnob = std::make_unique<DjKnob>(ParamId::AUTO_DRIFT, &p.apvts, &p.midiLearn, "AUTO DRF");
    autoDriftKnob->setTooltip("Auto Drift pull-back to anchor");
    autoDriftKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonYellow);
    autoDriftKnob->setPopupDisplayEnabled(false, false, nullptr);
    autoDriftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, ParamId::AUTO_DRIFT, *autoDriftKnob);
    addAndMakeVisible(*autoDriftKnob);

    refreshSceneVisuals();
    syncAllGhosts();

    bootStartMs = juce::Time::getMillisecondCounterHiRes();
    setSize(baseWidth(), baseHeight());
    startTimerHz(60);
}

OldSchoolBoxView::~OldSchoolBoxView()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

juce::Rectangle<int> OldSchoolBoxView::patLenBoxRect() const
{
    const int modY = getHeight() - MASTER_H + 6;
    return { 28, modY + 46, 200, 34 };
}

void OldSchoolBoxView::layoutChaosControls()
{
    const bool inChaos = (stepGrid.getCoreScreen() == StepGrid::CoreScreen::Chaos);
    const auto scr = stepGrid.mainScreenRect().translated(0, TITLE_H);

    chaosBankButton.setVisible(inChaos);
    chaosTrigButton.setVisible(inChaos);
    chaosLoopButton.setVisible(inChaos);
    chaosRevButton.setVisible(inChaos);
    chaosStutButton.setVisible(inChaos);
    chaosSpiralButton.setVisible(inChaos);
    chaosWavButton.setVisible(inChaos);
    for (auto& ck : chaosKnobs)
        if (ck.knob) ck.knob->setVisible(inChaos);

    if (inChaos)
    {
        chaosBankButton.setBounds(scr.getX() + 14,       scr.getY() + 30, 126, 24);
        chaosTrigButton.setBounds(scr.getX() + 146,      scr.getY() + 30, 56,  24);
        chaosWavButton.setBounds (scr.getRight() - 124,  scr.getY() + 30, 110, 24);

        // DJ Vibe strip right below taller 146px waveform (y = +214): LOOP | REV | STUT | SPIRAL
        const int vibeY = scr.getY() + 214;
        chaosLoopButton.setBounds  (scr.getX() + 14,  vibeY, 92, 22);
        chaosRevButton.setBounds   (scr.getX() + 112, vibeY, 54, 22);
        chaosStutButton.setBounds  (scr.getX() + 172, vibeY, 64, 22);
        chaosSpiralButton.setBounds(scr.getX() + 242, vibeY, 96, 22);

        const int kx0 = scr.getX() + 16;
        const int kw  = scr.getWidth() - 32;
        const float pitch = static_cast<float>(kw) / 5.0f;
        const int ky = scr.getY() + 248;
        for (size_t i = 0; i < chaosKnobs.size(); ++i)
            if (chaosKnobs[i].knob)
            {
                const int kx = kx0 + static_cast<int>(std::round((static_cast<float>(i) + 0.5f) * pitch)) - CK_SZ / 2;
                chaosKnobs[i].knob->setBounds(kx, ky, CK_SZ, CK_SZ);
            }
    }
}

void OldSchoolBoxView::syncAllGhosts()
{
    const int active = std::clamp(processorRef.sequencer.activeScene.load(std::memory_order_acquire), 0, Scenes::COUNT - 1);
    const auto& saved = processorRef.sceneParams[idx(active)];
    if (!saved.isValid()) return;

    stepGrid.syncGhosts(processorRef.apvts, saved);
    auto syncOne = [&](DjKnob* k) {
        if (!k) return;
        const auto pid = k->getParamId();
        if (auto* p = processorRef.apvts.getParameter(pid))
        {
            float norm = p->getDefaultValue();
            for (int c = 0; c < saved.getNumChildren(); ++c)
            {
                const auto child = saved.getChild(c);
                if (child.getProperty("id").toString() == pid)
                {
                    norm = p->convertTo0to1(static_cast<float>(child.getProperty("value")));
                    break;
                }
            }
            k->setGhostNormalized(norm);
        }
    };
    for (auto& mk : masterKnobs) syncOne(mk.knob.get());
    for (auto& ck : chaosKnobs)  syncOne(ck.knob.get());
    syncOne(autoMutKnob.get());
    syncOne(autoDriftKnob.get());
}

void OldSchoolBoxView::timerCallback()
{
    auto& seq = processorRef.sequencer;
    const int active = seq.activeScene.load(std::memory_order_acquire);
    const int queued = seq.queuedScene.load(std::memory_order_acquire);
    const int64_t fb = seq.fillStartBar.load(std::memory_order_acquire);

    if (active != lastDrawnActive || queued != lastDrawnQueued || fb != lastDrawnFillBar)
    {
        lastDrawnActive = active;
        lastDrawnQueued = queued;
        lastDrawnFillBar = fb;
        refreshSceneVisuals();
    }

    const int syncDiv = static_cast<int>(processorRef.apvts.getRawParameterValue(ParamId::DELAY_SYNC)->load());
    if (syncDiv != lastDrawnSyncDiv)
    {
        lastDrawnSyncDiv = syncDiv;
        updateSyncLabel();
    }

    const int cBank = static_cast<int>(processorRef.apvts.getRawParameterValue(ParamId::CHAOS_BANK)->load());
    if (cBank != lastDrawnChaosBank)
    {
        lastDrawnChaosBank = cBank;
        processorRef.chaosBus.syncPreviewBank(cBank);
        updateChaosBankLabel();
    }

    const float rawL = std::min(1.0f, processorRef.analyzer.getPeakL());
    const float rawR = std::min(1.0f, processorRef.analyzer.getPeakR());
    masterMeterL = (rawL > masterMeterL) ? (masterMeterL + 0.65f * (rawL - masterMeterL)) : (masterMeterL * 0.88f);
    masterMeterR = (rawR > masterMeterR) ? (masterMeterR + 0.65f * (rawR - masterMeterR)) : (masterMeterR * 0.88f);
    if (masterMeterL >= masterPeakHoldL) { masterPeakHoldL = masterMeterL; masterPeakTicksL = 30; }
    else if (masterPeakTicksL > 0) --masterPeakTicksL;
    else masterPeakHoldL = std::max(masterMeterL, masterPeakHoldL - 0.020f);

    if (masterMeterR >= masterPeakHoldR) { masterPeakHoldR = masterMeterR; masterPeakTicksR = 30; }
    else if (masterPeakTicksR > 0) --masterPeakTicksR;
    else masterPeakHoldR = std::max(masterMeterR, masterPeakHoldR - 0.020f);

    processorRef.midiLearn.flushToParameters();
    const int pendingParamScene = processorRef.sequencer.pendingParamSceneApply.exchange(-1, std::memory_order_acq_rel);
    if (pendingParamScene >= 0 && pendingParamScene < Scenes::COUNT)
    {
        processorRef.applySceneParams(pendingParamScene);
        syncAllGhosts();
        stepGrid.repaint();
    }

    {
        const bool autoOn  = processorRef.apvts.getRawParameterValue(ParamId::AUTO_ENABLE)->load() > 0.5f;
        const bool chaosOn = processorRef.apvts.getRawParameterValue(ParamId::CHAOS_ENABLE)->load() > 0.5f;
        const float kp = processorRef.visualState.kickPulse.load(std::memory_order_relaxed);
        const float si = processorRef.visualState.sustainedIntensity.load(std::memory_order_relaxed);
        if (masterKnobs[0].knob) masterKnobs[0].knob->setModOffset(autoOn ? (si * 0.14f) : 0.0f);
        if (masterKnobs[3].knob) masterKnobs[3].knob->setModOffset(kp * 0.16f);
        if (chaosKnobs[0].knob)  chaosKnobs[0].knob->setModOffset(chaosOn ? (si * 0.18f) : 0.0f);
    }

    for (auto& mk : masterKnobs) if (mk.knob) mk.knob->refreshLearnVisual();
    for (auto& ck : chaosKnobs)  if (ck.knob) ck.knob->refreshLearnVisual();
    if (autoMutKnob)     autoMutKnob->refreshLearnVisual();
    if (autoDriftKnob)   autoDriftKnob->refreshLearnVisual();
    if (spiralDriveKnob) spiralDriveKnob->refreshLearnVisual();

    // Poll component under mouse for non-intrusive Top-Bar Info View (replaces floating TooltipWindow!)
    {
        juce::String newHint;
        if (auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
        {
            for (auto* c = under; c != nullptr && c != this; c = c->getParentComponent())
            {
                if (auto* tc = dynamic_cast<juce::TooltipClient*>(c))
                {
                    const auto tip = tc->getTooltip();
                    if (tip.isNotEmpty()) { newHint = tip; break; }
                }
            }
        }
        if (newHint != activeTopHint)
        {
            activeTopHint = newHint;
            repaint(StepGrid::CORE_X, 0, StepGrid::CORE_W, TITLE_H);
        }
    }

    // UI-thread garbage collector for retired Chaos WAV buffers (zero delete on audio thread!)
    processorRef.chaosBus.collectGarbage();

    const bool spiralOn = processorRef.spiralFx.isActive();
    if (spiralButton.getToggleState() != spiralOn)
    {
        spiralButton.setToggleState(spiralOn, juce::dontSendNotification);
        chaosSpiralButton.setToggleState(spiralOn, juce::dontSendNotification);
        spiralButton.repaint();
        chaosSpiralButton.repaint();
    }

    undoButton.setEnabled(processorRef.sequencerUndo.canUndo());
    redoButton.setEnabled(processorRef.sequencerUndo.canRedo());

    if (!bootDone)
    {
        if (juce::Time::getMillisecondCounterHiRes() - bootStartMs >= 360.0)
            bootDone = true;
        repaint();
    }
    else
    {
        repaint(0, 0, getWidth(), TITLE_H);
        repaint(0, getHeight() - MASTER_H, getWidth(), MASTER_H);
    }
}

void OldSchoolBoxView::mouseDown(const juce::MouseEvent& e)
{
    if (e.y < TITLE_H && e.x > getWidth() - 34)
    {
        processorRef.sequencerUndo.push(processorRef.sequencer);
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
            processorRef.sequencer.smartRandomize(ch);
        stepGrid.repaint();
        return;
    }

    const auto pBox = patLenBoxRect();
    if (pBox.contains(e.x, e.y))
    {
        const int segW = pBox.getWidth() / 4;
        const int barIdx = std::clamp((e.x - pBox.getX()) / std::max(1, segW), 0, 3);
        const int ch = stepGrid.getSelectedChannel();
        processorRef.sequencerUndo.push(processorRef.sequencer);
        processorRef.sequencer.setPatternLength(ch, (barIdx + 1) * 16);
        stepGrid.repaint();
        repaint(pBox.expanded(4));
        return;
    }
}

void OldSchoolBoxView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) < 1.0e-5f) return;
    if (patLenBoxRect().contains(e.x, e.y))
    {
        const int dir = (wheel.deltaY > 0.0f) ? 1 : -1;
        const int ch = stepGrid.getSelectedChannel();
        const int cur = processorRef.sequencer.getPatternLength(ch);
        processorRef.sequencer.setPatternLength(ch, std::clamp(cur + dir, 1, Sequencer::MAX_STEPS));
        stepGrid.repaint();
        repaint(patLenBoxRect().expanded(8));
    }
}

bool OldSchoolBoxView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff"))
            return true;
    return false;
}

void OldSchoolBoxView::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        juce::File f(path);
        if (f.existsAsFile())
        {
            processorRef.chaosBus.loadCustomFileAsync(f, [this](bool ok) {
                if (ok)
                {
                    processorRef.apvts.getParameter(ParamId::CHAOS_BANK)->setValueNotifyingHost(1.0f);
                    processorRef.apvts.getParameter(ParamId::CHAOS_ENABLE)->setValueNotifyingHost(1.0f);
                    updateChaosBankLabel();
                    stepGrid.setCoreScreen(StepGrid::CoreScreen::Chaos);
                    repaint();
                }
            });
            break;
        }
    }
}

bool OldSchoolBoxView::keyPressed(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (mods.isCommandDown() && (code == 'z' || code == 'Z'))
    {
        bool changed = mods.isShiftDown()
            ? processorRef.sequencerUndo.redo(processorRef.sequencer)
            : processorRef.sequencerUndo.undo(processorRef.sequencer);
        if (changed)
        {
            stepGrid.repaint();
            refreshSceneVisuals();
        }
        return true;
    }

    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    if (code >= '1' && code <= '8') { stepGrid.triggerMuteToggle(code - '1'); return true; }
    if (code == juce::KeyPress::spaceKey)
    {
        const bool cur = processorRef.internalPlay.load(std::memory_order_acquire);
        processorRef.internalPlay.store(!cur, std::memory_order_release);
        return true;
    }
    if (code == 'f' || code == 'F') { fillButton.triggerClick(); return true; }
    if (code == 'q' || code == 'Q') { sceneButtons[0].triggerClick(); return true; }
    if (code == 'w' || code == 'W') { sceneButtons[1].triggerClick(); return true; }
    if (code == 'e' || code == 'E') { sceneButtons[2].triggerClick(); return true; }
    if (code == 'r' || code == 'R') { sceneButtons[3].triggerClick(); return true; }
    if (code == 'c' || code == 'C') { chaosEnableButton.triggerClick(); return true; }
    if (code == 'v' || code == 'V') { revertButton.triggerClick(); return true; }
    if (code == 'k' || code == 'K')
    {
        const bool cur = processorRef.killSubHeld.load(std::memory_order_acquire);
        processorRef.killSubHeld.store(!cur, std::memory_order_release);
        return true;
    }
    return false;
}

void OldSchoolBoxView::refreshSceneVisuals()
{
    const int active = processorRef.sequencer.activeScene.load(std::memory_order_acquire);
    const int queued = processorRef.sequencer.queuedScene.load(std::memory_order_acquire);
    const bool revArmed = processorRef.sequencer.revertQueued.load(std::memory_order_acquire);

    for (int i = 0; i < Scenes::COUNT; ++i)
    {
        juce::Colour bg;
        if (i == active)       bg = SpaceColours::neonGreen.withAlpha(0.88f);
        else if (i == queued)  bg = SpaceColours::neonCyan.withAlpha(0.55f);
        else                   bg = SpaceColours::bg.brighter(0.08f);
        sceneButtons[idx(i)].setColour(juce::TextButton::buttonColourId, bg);
    }

    revertButton.setColour(juce::TextButton::buttonColourId,
        revArmed ? SpaceColours::neonRed.withAlpha(0.85f) : SpaceColours::bg.brighter(0.08f));

    const int64_t fb = processorRef.sequencer.fillStartBar.load(std::memory_order_acquire);
    const bool armed = (fb >= 0);
    fillButton.setColour(juce::TextButton::buttonColourId,
        armed ? SpaceColours::neonGreen.withAlpha(0.85f) : SpaceColours::bg.brighter(0.08f));
}

void OldSchoolBoxView::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const bool isClipping = processorRef.masterClipping.load(std::memory_order_relaxed);
    const float kickPulse = processorRef.visualState.kickPulse.load(std::memory_order_relaxed);
    const int animFrame = processorRef.visualState.kickCount.load(std::memory_order_relaxed) & 1;

    const auto& th = SpaceColours::theme();
    g.fillAll(th.bg);

    // ── Symmetric 3-Pillar Top Studio Header Bar (Left 0..428 | Center 432..848 | Right 852..1280) ──
    {
        g.setColour(th.headerBg);
        g.fillRect(0, 0, getWidth(), TITLE_H);

        // Left Pillar: Brand + Edit/Help group pill + Export group pill
        g.setFont(SpaceArt::silkFont(8.5f, true));
        g.setColour(SpaceColours::txtDim());
        g.drawText("OLDSCHOOLBOX MKII", 12, 6, 130, 14, juce::Justification::centredLeft);

        auto drawTopGroupPill = [&](float x, float pw) {
            const auto r = juce::Rectangle<float>(x, 3.0f, pw, 20.0f);
            g.setColour(th.isLight ? juce::Colour(0xffdfddd6) : juce::Colour(0xff13161d));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(th.sectionLine.withAlpha(0.65f));
            g.drawRoundedRectangle(r, 3.0f, 0.7f);
        };
        drawTopGroupPill(146.0f, 80.0f);   // Edit & Help [↺ ↻ ?]
        drawTopGroupPill(234.0f, 128.0f);  // Export [↓WAV ↓MID]
        drawTopGroupPill(1082.0f, 158.0f); // Auto [AUTO MUT DRF]

        // Center Pillar (432..848 — pixel-exact alignment with Center Core CRT!)
        {
            const auto midR = juce::Rectangle<float>(static_cast<float>(StepGrid::CORE_X), 3.0f,
                                                     static_cast<float>(StepGrid::CORE_W), 20.0f);
            if (activeTopHint.isNotEmpty())
            {
                g.setColour(th.isLight ? juce::Colour(0xffdfddd6) : juce::Colour(0xff161920));
                g.fillRoundedRectangle(midR, 3.0f);
                g.setColour(th.sectionLine.withAlpha(0.75f));
                g.drawRoundedRectangle(midR, 3.0f, 0.7f);
                g.setFont(SpaceArt::uiFont(9.0f, false));
                g.setColour(SpaceColours::txtBright().withAlpha(0.94f));
                g.drawFittedText(activeTopHint, midR.reduced(8.0f, 1.0f).toNearestInt(),
                                 juce::Justification::centred, 1, 0.85f);
            }
            else
            {
                static const char* kSc[4] = { "A", "B", "C", "D" };
                const int scIdx = std::clamp(processorRef.sequencer.activeScene.load(std::memory_order_relaxed), 0, 3);
                const int bpmI  = static_cast<int>(std::round(processorRef.currentBpm.load(std::memory_order_relaxed)));
                const double srK = processorRef.getSampleRateSafe() / 1000.0;
                const juce::String status = "SCENE " + juce::String(kSc[scIdx])
                                          + "   |   " + juce::String(bpmI) + " BPM"
                                          + "   |   " + juce::String(srK, 1) + " kHz";
                g.setFont(SpaceArt::monoFont(8.5f, true));
                g.setColour(SpaceColours::txtMid());
                g.drawText(status, midR.toNearestInt(), juce::Justification::centred);
            }
        }

        // Right Pillar: Micro-labels for AUTO MUT & DRF mini-knobs inside Auto pill
        g.setFont(SpaceArt::silkFont(7.0f, true));
        g.setColour(SpaceColours::txtMid());
        g.drawText("MUT", 1126, 6, 24, 14, juce::Justification::centredRight);
        g.drawText("DRF", 1180, 6, 24, 14, juce::Justification::centredRight);

        SpaceArt::drawInvader(g, 0, animFrame, w - 26.0f, 6.0f, 2,
                              isClipping ? SpaceColours::neonRed : SpaceColours::themed(SpaceColours::neonGreen),
                              0.14f + kickPulse * 0.16f);

        g.setColour(th.sectionLine.withAlpha(0.65f));
        g.drawHorizontalLine(TITLE_H - 1, 0.0f, w);
    }

    // ── Symmetric Bottom Hardware Rack (y = getHeight() - MASTER_H) ──
    const int masterY = getHeight() - MASTER_H;
    const int modY    = masterY + 6;
    const int modH    = MASTER_H - 12;
    {
        g.setColour(th.bg);
        g.fillRect(0, masterY, getWidth(), MASTER_H);
        g.setColour(th.sectionLine.withAlpha(0.55f));
        g.drawHorizontalLine(masterY, 0.0f, w);

        // ── LEFT BAY (x = 12..424): PATTERN LENGTH + MIDI OUT + QUICK CHAOS SAMPLER ──
        {
            const auto b1 = juce::Rectangle<float>(12.0f, static_cast<float>(modY), 412.0f, static_cast<float>(modH));
            g.setColour(th.panelBg);
            g.fillRoundedRectangle(b1, 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.055f));
            g.drawHorizontalLine(static_cast<int>(b1.getY()) + 1, b1.getX() + 4.0f, b1.getRight() - 4.0f);
            g.setColour(th.sectionLine.withAlpha(0.60f));
            g.drawRoundedRectangle(b1, 4.0f, 0.8f);

            const int selCh = stepGrid.getSelectedChannel();
            const int curLen = processorRef.sequencer.getPatternLength(selCh);

            g.setFont(SpaceArt::monoFont(9.5f, true));
            g.setColour(SpaceColours::txtBright());
            g.drawText("PATTERN LENGTH", 28, modY + 22, 200, 14, juce::Justification::centred);

            const auto pBox = patLenBoxRect();
            const int segW = pBox.getWidth() / 4;
            static const char* kBarLbl[4] = { "1B", "2B", "3B", "4B" };
            for (int i = 0; i < 4; ++i)
            {
                const auto r = juce::Rectangle<float>(static_cast<float>(pBox.getX() + i * segW + 2),
                                                      static_cast<float>(pBox.getY()),
                                                      static_cast<float>(segW - 4),
                                                      static_cast<float>(pBox.getHeight()));
                const bool active = (curLen == (i + 1) * 16);
                const auto col = SpaceColours::themed(SpaceColours::neonGreen);
                g.setColour(active ? col.withAlpha(0.24f) : (th.isLight ? juce::Colour(0xffe6e5df) : juce::Colour(0xff131614)));
                g.fillRoundedRectangle(r, 3.0f);
                g.setColour(active ? col : th.sectionLine);
                g.drawRoundedRectangle(r, 3.0f, active ? 1.3f : 0.8f);
                g.setFont(SpaceArt::monoFont(10.0f, true));
                g.setColour(active ? (th.isLight ? col.darker(0.20f) : col) : SpaceColours::txtMid());
                g.drawText(kBarLbl[i], r.toNearestInt(), juce::Justification::centred);
            }

            g.setFont(SpaceArt::monoFont(7.5f, false));
            g.setColour(SpaceColours::txtDim());
            g.drawText("CH " + juce::String(selCh + 1) + " : " + juce::String(curLen) + " STEPS (WHEEL +/-1)",
                       28, pBox.getBottom() + 5, 200, 12, juce::Justification::centred);

            // Consistent grey vertical divider between PATTERN LENGTH & Quick Sampler
            g.setColour(th.sectionLine.withAlpha(0.75f));
            g.drawVerticalLine(244, static_cast<float>(modY + 8), static_cast<float>(modY + modH - 8));

            // Quick Chaos Sampler Status + Distinct Mini Magenta Meter!
            const auto magCol = SpaceColours::themed(SpaceColours::neonMagenta);

            g.setFont(SpaceArt::monoFont(8.5f, true));
            g.setColour(magCol);
            g.drawText("SAMPLER", 258, modY + 12, 124, 12, juce::Justification::centred);

            // Distinct compact 8-segment real audio sampler meter
            const auto smR = juce::Rectangle<float>(392.0f, static_cast<float>(modY + 18), 16.0f, 82.0f);
            g.setColour(juce::Colour(0xff090b0c));
            g.fillRoundedRectangle(smR, 3.0f);
            const float smpVal = std::clamp(processorRef.analyzer.getChaosPeak() * 1.35f, 0.0f, 1.0f);
            const int segs = 8;
            const int lit = static_cast<int>(std::round(smpVal * static_cast<float>(segs)));
            const float sh = (smR.getHeight() - 10.0f - static_cast<float>(segs - 1) * 2.0f) / static_cast<float>(segs);
            for (int i = 0; i < segs; ++i)
            {
                const float sy = smR.getBottom() - 5.0f - static_cast<float>(i + 1) * sh - static_cast<float>(i) * 2.0f;
                const auto c = (i >= 6) ? SpaceColours::themed(SpaceColours::neonCyan) : magCol;
                g.setColour(i < lit ? c : c.withAlpha(0.10f));
                g.fillRoundedRectangle(smR.getX() + 3.0f, sy, smR.getWidth() - 6.0f, sh, 1.5f);
            }
            g.setColour(magCol.withAlpha(0.35f));
            g.drawRoundedRectangle(smR, 3.0f, 0.8f);
        }

        // ── RIGHT BAY (x = 432..1268): SPACIOUS MASTER FX & PRESSURE + OUT L/R METER ──
        {
            const auto b2 = juce::Rectangle<float>(432.0f, static_cast<float>(modY), 836.0f, static_cast<float>(modH));
            g.setColour(th.panelBg);
            g.fillRoundedRectangle(b2, 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.055f));
            g.drawHorizontalLine(static_cast<int>(b2.getY()) + 1, b2.getX() + 4.0f, b2.getRight() - 4.0f);
            g.setColour(th.sectionLine.withAlpha(0.60f));
            g.drawRoundedRectangle(b2, 4.0f, 0.8f);

            // Unified Master Bus section titles
            g.setFont(SpaceArt::silkFont(8.8f, true));
            const auto masterHdrCol = th.isLight ? SpaceColours::txtBright() : juce::Colour(0xffcfd4df);
            g.setColour(masterHdrCol);
            g.drawText("DRIVE",  436, modY + 6, 82, 14, juce::Justification::centred);
            g.drawText("REVERB", 526, modY + 6, 140, 14, juce::Justification::centred);
            g.drawText("DELAY",  676, modY + 6, 222, 14, juce::Justification::centred);
            g.drawText("COMP",   906, modY + 6, 88, 14, juce::Justification::centred);
            g.drawText("WASH",   1004, modY + 6, 134, 14, juce::Justification::centred);
            g.setColour(SpaceColours::txtBright());
            g.drawText("MASTER", 1146, modY + 6, 118, 14, juce::Justification::centred);

            // Consistent grey vertical dividers
            g.setColour(th.sectionLine.withAlpha(0.75f));
            for (int dx : { 522, 672, 902, 1000, 1142 })
                g.drawVerticalLine(dx, static_cast<float>(modY + 8), static_cast<float>(modY + modH - 8));

            // Optional Beat-Synced Performance FX Aura (SUBTLE / RAVE: lights only the active FX module!)
            const int vizM = SpaceColours::vizMode();
            if (vizM >= SpaceColours::VIZ_SUBTLE)
            {
                const float auraAlpha = (vizM == SpaceColours::VIZ_RAVE) ? 0.18f : 0.10f;
                const float rimAlpha  = (vizM == SpaceColours::VIZ_RAVE) ? 0.65f : 0.42f;
                if (processorRef.dubThrowHeld.load(std::memory_order_relaxed))
                {
                    const auto c = SpaceColours::themed(SpaceColours::neonCyan);
                    const auto dr = juce::Rectangle<float>(674.0f, static_cast<float>(modY + 2), 226.0f, static_cast<float>(modH - 4));
                    g.setColour(c.withAlpha(auraAlpha)); g.fillRoundedRectangle(dr, 3.0f);
                    g.setColour(c.withAlpha(rimAlpha));  g.drawRoundedRectangle(dr, 3.0f, 1.1f);
                }
                if (processorRef.spiralFx.isActive())
                {
                    const auto c = SpaceColours::themed(SpaceColours::neonPurple);
                    const auto dr = juce::Rectangle<float>(674.0f, static_cast<float>(modY + 2), 226.0f, static_cast<float>(modH - 4));
                    g.setColour(c.withAlpha(auraAlpha)); g.fillRoundedRectangle(dr, 3.0f);
                    g.setColour(c.withAlpha(rimAlpha));  g.drawRoundedRectangle(dr, 3.0f, 1.1f);
                }
                if (processorRef.killSubHeld.load(std::memory_order_relaxed))
                {
                    const auto c = SpaceColours::neonRed;
                    const auto wr = juce::Rectangle<float>(1002.0f, static_cast<float>(modY + 2), 138.0f, static_cast<float>(modH - 4));
                    g.setColour(c.withAlpha(auraAlpha)); g.fillRoundedRectangle(wr, 3.0f);
                    g.setColour(c.withAlpha(rimAlpha));  g.drawRoundedRectangle(wr, 3.0f, 1.1f);
                }
            }

            // Hardware Silkscreen Labels with In-Place Value <-> Label replacement (#7 + #8 + #9)
            for (const auto& mk : masterKnobs)
                if (mk.knob)
                {
                    const auto r = juce::Rectangle<int>(mk.knob->getX() - 10, mk.knob->getBottom() + 3, mk.knob->getWidth() + 20, 12);
                    if (mk.knob->isShowingValue())
                        SpaceArt::drawValueWithUnit(g, mk.knob->getCompactValueText(), r, 8.2f, SpaceColours::txtBright());
                    else
                    {
                        g.setFont(SpaceArt::silkFont(8.2f, true));
                        g.setColour(SpaceColours::txtMid());
                        g.drawText(mk.label, r, juce::Justification::centred);
                    }
                }

            // CLIP LED under DRIVE
            const int clipX = 458, clipY = modY + 96;
            g.setColour(isClipping ? SpaceColours::neonRed : SpaceColours::neonRed.withAlpha(0.20f));
            g.fillEllipse(static_cast<float>(clipX), static_cast<float>(clipY), 8.0f, 8.0f);
            g.setFont(SpaceArt::monoFont(7.5f, true));
            g.setColour(isClipping ? SpaceColours::neonRed : SpaceColours::textDim);
            g.drawText("CLIP", clipX + 11, clipY - 1, 30, 10, juce::Justification::centredLeft);

            // Micro-label for SPIRAL DRV mini-knob in DELAY section
            g.setFont(SpaceArt::silkFont(7.0f, true));
            g.setColour(SpaceColours::themed(SpaceColours::neonPurple).brighter(0.20f));
            g.drawText("DRV", 844, modY + 96, 24, 12, juce::Justification::centredRight);

            // Vertical 1176/SSL-style Gain Reduction (GR) meter right next to COMP GLUE knob!
            {
                const float grDb = processorRef.analyzer.getCompGrDb();
                const float grNorm = std::clamp(grDb / 10.0f, 0.0f, 1.0f);
                const auto grR = juce::Rectangle<float>(972.0f, static_cast<float>(modY + 25), 14.0f, 64.0f);
                g.setColour(juce::Colour(0xff090b0c));
                g.fillRoundedRectangle(grR, 2.5f);
                g.setColour(SpaceColours::sectionLine.withAlpha(0.65f));
                g.drawRoundedRectangle(grR, 2.5f, 0.75f);

                const int segs = 10;
                const int lit = static_cast<int>(std::round(grNorm * static_cast<float>(segs)));
                const float sh = (grR.getHeight() - 6.0f - static_cast<float>(segs - 1) * 1.5f) / static_cast<float>(segs);
                for (int i = 0; i < segs; ++i)
                {
                    // Classic analog GR fills top-down from 0 dB!
                    const float sy = grR.getY() + 3.0f + static_cast<float>(i) * (sh + 1.5f);
                    const auto c = (i >= 6) ? SpaceColours::neonRed : SpaceColours::themed(SpaceColours::neonOrange);
                    g.setColour(i < lit ? c : c.withAlpha(0.12f));
                    g.fillRoundedRectangle(grR.getX() + 2.5f, sy, grR.getWidth() - 5.0f, sh, 1.0f);
                }
                g.setFont(SpaceArt::monoFont(7.2f, true));
                g.setColour(grDb > 0.2f ? SpaceColours::themed(SpaceColours::neonOrange) : SpaceColours::textMid);
                g.drawText(grDb > 0.2f ? ("-" + juce::String(grDb, 1) + "dB") : "GR",
                           958, modY + 95, 42, 12, juce::Justification::centred);
            }

            // Master Fader "OUT" label
            g.setFont(SpaceArt::silkFont(7.2f, true));
            g.setColour(SpaceColours::txtMid());
            g.drawText("OUT", 1150, modY + 102, 30, 11, juce::Justification::centred);

            // Clean Master Stereo OUT L/R Pill Meter with 500ms Peak-Hold (#16)
            const float liveL = std::max(masterMeterL, std::min(1.0f, processorRef.analyzer.getPeakL()));
            const float liveR = std::max(masterMeterR, std::min(1.0f, processorRef.analyzer.getPeakR()));
            auto drawMasterBar = [&](float bx, float val, float pkHold)
            {
                const auto r = juce::Rectangle<float>(bx, static_cast<float>(modY + 24), 14.0f, 84.0f);
                g.setColour(juce::Colour(0xff090b0a));
                g.fillRoundedRectangle(r, 2.5f);
                const auto inner = r.reduced(2.0f);
                SpaceArt::drawLedBar(g, inner, val, 14, true, false);
                if (pkHold > 0.05f)
                {
                    const float py = inner.getBottom() - std::clamp(pkHold, 0.0f, 1.0f) * inner.getHeight();
                    g.setColour(juce::Colours::white.withAlpha(0.92f));
                    g.fillRect(inner.getX(), py, inner.getWidth(), 2.0f);
                }
                g.setColour(SpaceColours::sectionLine.withAlpha(0.75f));
                g.drawRoundedRectangle(r, 2.5f, 0.8f);
            };
            drawMasterBar(1192.0f, liveL, std::max(masterPeakHoldL, liveL));
            drawMasterBar(1210.0f, liveR, std::max(masterPeakHoldR, liveR));

            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(SpaceColours::txtDim());
            g.drawText("0",   1228, modY + 22, 18, 10, juce::Justification::centredLeft);
            g.drawText("-6",  1228, modY + 52, 18, 10, juce::Justification::centredLeft);
            g.drawText("-18", 1228, modY + 82, 22, 10, juce::Justification::centredLeft);
        }

        SpaceArt::drawTorxScrew(g, 11.0f, h - 10.0f, 3.6f);
        SpaceArt::drawTorxScrew(g, w - 11.0f, h - 10.0f, 3.6f);
    }

    // ── CRT Power-On Sequence (350 ms) ──
    if (!bootDone)
    {
        const float elapsed = static_cast<float>(juce::Time::getMillisecondCounterHiRes() - bootStartMs);
        if (elapsed < 200.0f)
        {
            const float t = std::clamp(elapsed / 200.0f, 0.0f, 1.0f);
            const float openH = std::max(2.0f, h * t * t);
            const float topMask = (h - openH) * 0.5f;
            g.setColour(juce::Colours::black.withAlpha(0.95f * (1.0f - t * 0.35f)));
            g.fillRect(0.0f, 0.0f, w, topMask);
            g.fillRect(0.0f, h - topMask, w, topMask);
            g.setColour(juce::Colours::white.withAlpha((1.0f - t) * 0.90f));
            g.fillRect(0.0f, h * 0.5f - 1.5f, w, 3.0f);
        }
    }
}

void OldSchoolBoxView::paintOverChildren(juce::Graphics& g)
{
    // Draw the 5 Chaos Sampler knob labels ABOVE stepGrid's OLED background (with In-Place Label <-> Value!)
    if (stepGrid.getCoreScreen() == StepGrid::CoreScreen::Chaos)
    {
        for (const auto& ck : chaosKnobs)
        {
            if (ck.knob && ck.knob->isVisible())
            {
                const auto r = juce::Rectangle<int>(ck.knob->getX() - 8, ck.knob->getBottom() + 4,
                                                    ck.knob->getWidth() + 16, 13);
                if (ck.knob->isShowingValue())
                    SpaceArt::drawValueWithUnit(g, ck.knob->getCompactValueText(), r, 8.2f, juce::Colours::white);
                else
                {
                    g.setFont(SpaceArt::silkFont(8.2f, true));
                    g.setColour(SpaceColours::themed(SpaceColours::neonMagenta).brighter(0.28f));
                    g.drawText(ck.label, r, juce::Justification::centred);
                }
            }
        }
    }
}

void OldSchoolBoxView::resized()
{
    const int masterY = getHeight() - MASTER_H;
    const int modY    = masterY + 6;

    stepGrid.setBounds(0, TITLE_H, getWidth(), masterY - TITLE_H);

    // ── Symmetric 3-Pillar Top Header Utility Controls ──
    // Left Pillar (0..428): Edit/Help + Export + Preset/Init
    undoButton.setBounds    (149, 4, 24, 18);
    redoButton.setBounds    (175, 4, 24, 18);
    helpButton.setBounds    (201, 4, 22, 18);

    dragWavButton.setBounds (237, 5, 60, 16);
    dragMidiButton.setBounds(299, 5, 60, 16);
    initButton.setBounds    (370, 4, 54, 18);

    // Right Pillar (852..1280): MIDI + Theme + Viz + Auto Group
    midiOutButton.setBounds (856, 4, 62, 18);
    themeButton.setBounds   (924, 4, 84, 18);
    vizButton.setBounds     (1014, 4, 60, 18);

    autoButton.setBounds    (1085, 5, 40, 16);
    autoMutKnob->setBounds  (1152, 3, 20, 20);
    autoDriftKnob->setBounds(1206, 3, 20, 20);

    // ── Center Core Transport Strip Overlay ──
    const int ty = TITLE_H + 134;
    const int cx = StepGrid::CORE_X;
    qntButton.setBounds   (cx + 6,   ty + 1, 36, 26);
    revertButton.setBounds(cx + 138, ty,     52, 28);

    for (int i = 0; i < Scenes::COUNT; ++i)
        sceneButtons[idx(i)].setBounds(cx + 196 + i * 30, ty + 1, 26, 26);

    fillButton.setBounds (cx + 324, TITLE_H + 118, 82, 22);
    tightButton.setBounds(cx + 324, TITLE_H + 144, 82, 22);

    // ── Bottom Left Bay: Quick CHAOS ON / EDIT (MIDI moved to Top Bar!) ──
    chaosEnableButton.setBounds(268, modY + 34, 104, 26);
    chaosOpenButton.setBounds  (268, modY + 68, 104, 24);

    // ── Bottom Right Bay: Spacious MASTER FX + Master Output Fader + OUT L/R ──
    const int fxKy = modY + 26;
    if (masterKnobs[0].knob) masterKnobs[0].knob->setBounds(453,  fxKy, MK_SZ, MK_SZ); // DRIVE
    if (masterKnobs[1].knob) masterKnobs[1].knob->setBounds(538,  fxKy, 46, 46);       // SIZE
    if (masterKnobs[2].knob) masterKnobs[2].knob->setBounds(608,  fxKy, 46, 46);       // VERB
    if (masterKnobs[4].knob) masterKnobs[4].knob->setBounds(686,  fxKy, 46, 46);       // TIME
    if (masterKnobs[5].knob) masterKnobs[5].knob->setBounds(762,  fxKy, 46, 46);       // FDBK
    if (masterKnobs[6].knob) masterKnobs[6].knob->setBounds(838,  fxKy, 46, 46);       // MIX
    syncButton.setBounds     (682, modY + 92, 44, 19);
    dubThrowButton.setBounds (730, modY + 92, 48, 19);
    spiralButton.setBounds   (782, modY + 92, 58, 19);
    if (spiralDriveKnob) spiralDriveKnob->setBounds(871, modY + 90, 22, 22);

    if (masterKnobs[3].knob) masterKnobs[3].knob->setBounds(914,  fxKy, 46, 46);       // COMP GLUE (vertical GR meter on right)
    if (masterKnobs[7].knob) masterKnobs[7].knob->setBounds(1047, fxKy, MK_SZ, MK_SZ); // WASH
    monoSubButton.setBounds(1008, modY + 92, 64, 19);
    killSubButton.setBounds(1076, modY + 92, 58, 19);

    if (masterVolFader) masterVolFader->setBounds(1152, modY + 22, 26, 78);            // MASTER OUT Fader next to OUT L/R!

    layoutChaosControls();
}

void OldSchoolBoxView::updateSyncLabel()
{
    static const char* labels[] = { "FREE", "1/4", "1/8", "1/8d", "1/16", "1/16d", "1/32" };
    const int val = static_cast<int>(processorRef.apvts.getRawParameterValue(ParamId::DELAY_SYNC)->load());
    syncButton.setButtonText(labels[std::clamp(val, 0, 6)]);
}

void OldSchoolBoxView::updateChaosBankLabel()
{
    const int val = static_cast<int>(processorRef.apvts.getRawParameterValue(ParamId::CHAOS_BANK)->load());
    chaosBankButton.setButtonText(ChaosSampleBus::bankName(val));
}

// ── Resizable wrapper editor ──────────────────────────────────────────────────

OldSchoolBoxEditor::OldSchoolBoxEditor(OldSchoolBoxProcessor& p)
    : AudioProcessorEditor(&p), view(p)
{
    addAndMakeVisible(view);
    setWantsKeyboardFocus(true);

    const int bw = OldSchoolBoxView::baseWidth();
    const int bh = OldSchoolBoxView::baseHeight();
    const double aspect = static_cast<double>(bw) / static_cast<double>(bh);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(aspect);
    setResizeLimits(static_cast<int>(std::round(bw * 0.65)),
                    static_cast<int>(std::round(bh * 0.65)),
                    static_cast<int>(std::round(bw * 2.00)),
                    static_cast<int>(std::round(bh * 2.00)));

    float initScale = 1.0f;
    if (const auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = disp->userArea;
        const float maxW = static_cast<float>(area.getWidth()) * 0.92f;
        const float maxH = static_cast<float>(area.getHeight()) * 0.88f;
        initScale = std::min({ 1.0f, maxW / static_cast<float>(bw), maxH / static_cast<float>(bh) });
        initScale = std::clamp(initScale, 0.65f, 1.0f);
    }

    setSize(static_cast<int>(std::round(static_cast<float>(bw) * initScale)),
            static_cast<int>(std::round(static_cast<float>(bh) * initScale)));
}

void OldSchoolBoxEditor::resized()
{
    const float bw = static_cast<float>(OldSchoolBoxView::baseWidth());
    const float bh = static_cast<float>(OldSchoolBoxView::baseHeight());
    const float scale = std::min(static_cast<float>(getWidth()) / bw,
                                 static_cast<float>(getHeight()) / bh);
    view.setBounds(0, 0, OldSchoolBoxView::baseWidth(), OldSchoolBoxView::baseHeight());
    view.setTransform(juce::AffineTransform::scale(scale));
}

bool OldSchoolBoxEditor::keyPressed(const juce::KeyPress& key)
{
    return view.keyPressed(key);
}
