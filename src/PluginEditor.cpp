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
        g.setColour(SpaceColours::neonCyan);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("OLDSCHOOLBOX MKII — DJ & LIVE GESTURES", 10, 8, getWidth() - 20, 18,
                   juce::Justification::centredLeft);

        static const char* rows[][2] = {
            { "Swipe Steps (L / R-drag)", "Paint steps ON/OFF or Erase across row" },
            { "Step (Shift + L-drag)",    "Draw Velocity directly on pad" },
            { "Step (Alt/Cmd + R-drag)",  "Probability / Micro-timing" },
            { "LEN (Wheel / Shift / Alt)","Odd polyrhythmic lengths (1..64 steps)" },
            { "Knobs (Alt + Drag)",       "Spring-Back Tweaking (snaps back on release)" },
            { "Knobs (Shift / Wheel)",    "Surgical fine precision / micro-adjust" },
            { "Knobs (Right-click)",      "1-Click MIDI Learn (Shift+R: clear CC)" },
            { "Scene A-D (Shift / Alt)",  "Save scene+params / Snap-Back REVERT" },
            { "QNT:1B / 1/4 / OFF",       "Quantize Scene switches & Mutes" },
            { "QWERTY 1-8 / Space / F",   "Quantized Mute 1-8 / Play / Fill" },
            { "QWERTY Q-R / C / K / V",   "Scenes A-D / Chaos / Kill Sub / Revert" },
            { "Drag WAV> / MID>",         "Drop 4-bar audio/MIDI straight into DAW" },
            { "Bass NOTE / ACC",          "Drag/wheel pitch | Shift/Cmd: +/-1 Oct" },
            { "Channel name (L / R)",     "Audition voice / Euclidean generator" },
            { "Cmd+Z / Shift+Cmd+Z",      "Undo / Redo sequencer edits" },
        };

        int y = 32;
        for (const auto& r : rows)
        {
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.setColour(SpaceColours::neonGreen.withAlpha(0.85f));
            g.drawText(r[0], 10, y, 168, 16, juce::Justification::centredLeft);
            g.setFont(juce::FontOptions(9.5f, juce::Font::plain));
            g.setColour(SpaceColours::textBright.withAlpha(0.88f));
            g.drawText(r[1], 182, y, getWidth() - 190, 16, juce::Justification::centredLeft);
            y += 17;
        }
    }
};
} // namespace

OldSchoolBoxView::OldSchoolBoxView(OldSchoolBoxProcessor& p)
    : processorRef(p),
      stepGrid(p.sequencer, p.voiceBank, p.apvts, p.currentBpm, p.internalPlay, p.hostPlaying,
               p.visualState, p.analyzer, p.sequencerUndo, &p.midiLearn)
{
    setLookAndFeel(&spaceLnf);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    spaceLogo = juce::ImageCache::getFromMemory(BinaryData::space_invaders_logo_png,
                                                BinaryData::space_invaders_logo_pngSize);
    noiseTile = SpaceArt::makeNoiseTile(128);
    addAndMakeVisible(stepGrid);

    // ── Master FX knobs (DjKnob: Alt spring-back, Shift fine, Wheel, Right-click MIDI Learn + HUD) ──
    auto makeMK = [&](int idx, const char* paramId, const juce::String& label, juce::Colour col) {
        auto& mk = masterKnobs[static_cast<size_t>(idx)];
        mk.label = label;
        mk.knob = std::make_unique<DjKnob>(paramId, &p.apvts, &p.midiLearn, "MASTER " + label);
        mk.knob->setTooltip(label + " (Alt+drag: spring-back | Right-click: MIDI Learn)");
        mk.knob->setColour(juce::Slider::rotarySliderFillColourId, col);
        mk.knob->setPopupDisplayEnabled(true, false, this);
        mk.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            p.apvts, paramId, *mk.knob);
        addAndMakeVisible(*mk.knob);
    };

    // Module 2 (MASTER FX // DUB & SPACE) = coherent Ice Cyan; Module 3 (PRESSURE) = Nuclear Amber + Cyan WASH
    makeMK(0, ParamId::MASTER_DRIVE, "DRIVE", SpaceColours::neonOrange);
    makeMK(1, ParamId::REVERB_SIZE,  "SIZE",  SpaceColours::neonCyan);
    makeMK(2, ParamId::REVERB_MIX,   "VERB",  SpaceColours::neonCyan);
    makeMK(3, ParamId::COMP_AMOUNT,  "COMP",  SpaceColours::neonOrange);
    makeMK(4, ParamId::DELAY_TIME,   "TIME",  SpaceColours::neonCyan);
    makeMK(5, ParamId::DELAY_FB,     "FDBK",  SpaceColours::neonCyan);
    makeMK(6, ParamId::DELAY_MIX,    "MIX",   SpaceColours::neonCyan);
    makeMK(7, ParamId::DJ_FILTER,    "WASH",  SpaceColours::neonCyan);

    // ── Module 1: CHAOS // SAMPLE BUS (Rave Magenta coherent controls) ──
    chaosEnableButton.setButtonText("CHAOS");
    chaosEnableButton.setTooltip("Chaos Sample Bus ON/OFF — intelligent Tekno DNA mid-range sample generator");
    chaosEnableButton.setClickingTogglesState(true);
    chaosEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::CHAOS_ENABLE, chaosEnableButton);
    addAndMakeVisible(chaosEnableButton);

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

    chaosWavButton.setButtonText("WAV");
    chaosWavButton.setTooltip("Load custom WAV/AIFF (max 8s) — or drag & drop a WAV file anywhere on the window");
    chaosWavButton.onClick = [this] {
        wavChooser = std::make_unique<juce::FileChooser>("Load Underground Sample (WAV/AIFF, max 8s)",
                                                         juce::File{}, "*.wav;*.aif;*.aiff");
        wavChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile() && processorRef.chaosBus.loadCustomFile(f))
                {
                    processorRef.apvts.getParameter(ParamId::CHAOS_BANK)->setValueNotifyingHost(1.0f);
                    updateChaosBankLabel();
                    repaint();
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
        ck.knob->setPopupDisplayEnabled(true, false, this);
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

    // ── Module 2: DUB THROW momentary button ──
    dubThrowButton.setButtonText("DUB THROW");
    dubThrowButton.setTooltip("Hold for live Dub Delay throw (82% feedback spike)");
    dubThrowButton.onStateChange = [this] {
        processorRef.dubThrowHeld.store(dubThrowButton.isDown(), std::memory_order_release);
    };
    addAndMakeVisible(dubThrowButton);

    // ── Module 3: MONO <120 Hz M/S toggle + Momentary KILL SUB (<150 Hz) ──
    monoSubButton.setButtonText("MONO <120");
    monoSubButton.setTooltip("Elliptical M/S Mono-Maker (< 120 Hz Side HPF for Sound System sub tightness)");
    monoSubButton.setClickingTogglesState(true);
    monoSubAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::MONO_SUB, monoSubButton);
    addAndMakeVisible(monoSubButton);

    killSubButton.setButtonText("KILL SUB");
    killSubButton.setTooltip("Hold (or press K) to cut everything below 150 Hz for instant live drop builds!");
    killSubButton.onStateChange = [this] {
        processorRef.killSubHeld.store(killSubButton.isDown(), std::memory_order_release);
    };
    addAndMakeVisible(killSubButton);

    // ── Top-right THEME switcher (RAVE / CRT-GREEN / AMBER-98) ──
    themeButton.setButtonText("RAVE");
    themeButton.setTooltip("Switch UI Theme: RAVE (4-family) / CRT (1978 phosphor green) / AMB (industrial amber)");
    themeButton.onClick = [this] {
        int& t = SpaceColours::activeTheme();
        t = (t + 1) % 3;
        static const char* kThemeNames[3] = { "RAVE", "CRT", "AMB" };
        themeButton.setButtonText(kThemeNames[t]);
        repaint();
        stepGrid.repaint();
    };
    addAndMakeVisible(themeButton);

    // ── Delay sync button ──
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

    // ── Scene buttons A/B/C/D + Snap-Back REVERT + Quantize QNT + DAW Drag-Export WAV>/MID> ──
    static const char* sceneLabels[] = { "A", "B", "C", "D" };
    for (int i = 0; i < Scenes::COUNT; ++i)
    {
        sceneButtons[idx(i)].setButtonText(sceneLabels[i]);
        sceneButtons[idx(i)].setTooltip(
            juce::String("Scene ") + sceneLabels[i]
          + " (Q/W/E/R) — Click: quantized switch | Shift+Click: save patterns+knobs | Alt+Click: Snap-Back REVERT");
        sceneButtons[idx(i)].onClick = [this, i] {
            const auto mods = juce::ModifierKeys::currentModifiers;
            if (mods.isShiftDown() || mods.isCommandDown())
            {
                // SAVE: snapshot current sequencer state + ALL synth/FX APVTS parameters
                Scenes::capture(processorRef.scenes[idx(i)], processorRef.sequencer);
                processorRef.captureSceneParams(i);
                refreshSceneVisuals();
            }
            else if (mods.isAltDown())
            {
                // SNAP-BACK / PANIC RELOAD of scene i on next bar (or immediately if stopped)
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
                {
                    processorRef.sequencer.queuedScene.store(-1, std::memory_order_release);
                }
                else if (playing && qnt > 0)
                {
                    processorRef.sequencer.queuedScene.store(i, std::memory_order_release);
                }
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
    revertButton.setTooltip("Snap-Back / Panic Reload (V): reloads saved Scene patterns + ALL knobs on next beat/bar!");
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
        static const char* kQnt[3] = { "QNT:OFF", "QNT:1/4", "QNT:1B" };
        const int m = std::clamp(processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed), 0, 2);
        qntButton.setButtonText(kQnt[m]);
    };
    qntButton.setTooltip("Live Quantize for Scene switches & Mutes: 1B (bar) / 1/4 (beat) / OFF (instant)");
    qntButton.onClick = [this, updateQntLabel] {
        const int cur = processorRef.sequencer.quantizeMode.load(std::memory_order_relaxed);
        processorRef.sequencer.quantizeMode.store((cur + 1) % 3, std::memory_order_release);
        updateQntLabel();
    };
    updateQntLabel();
    addAndMakeVisible(qntButton);

    dragWavButton.setButtonText("WAV>");
    dragWavButton.setTooltip("Drag & drop directly into Ableton/FL/Bitwig to render 4-bar 24-bit WAV!");
    dragWavButton.onGenerateFile = [this] {
        return LoopExporter::exportWav4Bars(processorRef.sequencer, processorRef.apvts,
                                            processorRef.currentBpm.load(std::memory_order_relaxed),
                                            processorRef.getSampleRateSafe());
    };
    addAndMakeVisible(dragWavButton);

    dragMidiButton.setButtonText("MID>");
    dragMidiButton.setTooltip("Drag & drop directly into DAW to export 4-bar Drums + 303 Acid MIDI!");
    dragMidiButton.onGenerateFile = [this] {
        return LoopExporter::exportMidi4Bars(processorRef.sequencer,
                                             processorRef.currentBpm.load(std::memory_order_relaxed));
    };
    addAndMakeVisible(dragMidiButton);

    // ── Undo / Redo / Help (?) ──
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

    // ── FILL button ──
    fillButton.setButtonText("FILL");
    fillButton.setTooltip("Fill — plays a 1-bar fill on the next bar, then restores the pattern.");
    fillButton.onClick = [this] {
        auto& seq = processorRef.sequencer;
        // Snapshot pre-fill state so non-autoVar channels can be restored
        Scenes::capture(processorRef.preFillSnapshot, seq);
        const int64_t cb = seq.currentBar.load(std::memory_order_acquire);
        seq.fillStartBar.store(cb + 1, std::memory_order_release);
    };
    addAndMakeVisible(fillButton);

    // ── Init / Factory Presets popup ──
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
                    // Factory preset: apply both sequencer pattern AND synth/FX sound parameters
                    const auto& preset = FactoryPresets::all()[static_cast<size_t>(result - 2)];
                    Scenes::apply(preset.snapshot, seq);
                    FactoryPresets::applyParams(preset, processorRef.apvts);
                }
                // Snapshot the new state into all 4 scenes
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

    // ── MIDI OUT toggle ──
    midiOutButton.setButtonText("MIDI");
    midiOutButton.setTooltip("MIDI Out - sequence external gear (drums on ch10, basses on ch1/ch2)");
    midiOutButton.setClickingTogglesState(true);
    midiOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::MIDI_OUT, midiOutButton);
    addAndMakeVisible(midiOutButton);

    // ── TIGHT bass toggle ──
    tightButton.setButtonText("TIGHT");
    tightButton.setTooltip("Tight Bass - lower mono fold (150 Hz) + master low-cut HP @ 40 Hz");
    tightButton.setClickingTogglesState(true);
    tightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::TIGHT, tightButton);
    addAndMakeVisible(tightButton);

    // ── AUTO mode toggle + mini knobs (MUT / DRIFT) ──
    autoButton.setButtonText("AUTO");
    autoButton.setTooltip("Auto Mode - cruise control. Mutates patterns every bar around a captured anchor. MUT = mutation rate, DRIFT = pull back to anchor.");
    autoButton.setClickingTogglesState(true);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, ParamId::AUTO_ENABLE, autoButton);
    addAndMakeVisible(autoButton);

    autoMutKnob = std::make_unique<DjKnob>(ParamId::AUTO_MUTATION, &p.apvts, &p.midiLearn);
    autoMutKnob->setTooltip("Auto Mutation (Alt+drag: spring-back | Right-click: MIDI Learn)");
    autoMutKnob->setPopupDisplayEnabled(true, false, this);
    autoMutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, ParamId::AUTO_MUTATION, *autoMutKnob);
    addAndMakeVisible(*autoMutKnob);

    autoDriftKnob = std::make_unique<DjKnob>(ParamId::AUTO_DRIFT, &p.apvts, &p.midiLearn);
    autoDriftKnob->setTooltip("Auto Drift (Alt+drag: spring-back | Right-click: MIDI Learn)");
    autoDriftKnob->setPopupDisplayEnabled(true, false, this);
    autoDriftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, ParamId::AUTO_DRIFT, *autoDriftKnob);
    addAndMakeVisible(*autoDriftKnob);

    refreshSceneVisuals();
    syncAllGhosts();

    bootStartMs = juce::Time::getMillisecondCounterHiRes();
    setSize(baseWidth(), baseHeight());
    startTimerHz(60);  // 60 FPS buttery optical LEDs, CRT power-on & HUD
}

OldSchoolBoxView::~OldSchoolBoxView()
{
    stopTimer();
    setLookAndFeel(nullptr);
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

    // Update PRESSURE // WALL peak-hold (~500 ms @ 20 Hz = 10 ticks)
    const float rmsAvg = 0.5f * (processorRef.analyzer.getRmsL() + processorRef.analyzer.getRmsR());
    const float pkMax  = std::max(processorRef.analyzer.getPeakL(), processorRef.analyzer.getPeakR());
    const float sustained = processorRef.visualState.sustainedIntensity.load(std::memory_order_relaxed);
    const float pNow = std::clamp(rmsAvg * 1.45f + pkMax * 0.25f + sustained * 0.25f, 0.0f, 1.0f);
    if (pNow >= pressurePeakHold)
    {
        pressurePeakHold = pNow;
        pressurePeakTicks = 10;
    }
    else if (pressurePeakTicks > 0)
    {
        --pressurePeakTicks;
    }
    else
    {
        pressurePeakHold = std::max(pNow, pressurePeakHold - 0.05f);
    }

    // Flush MIDI Learn CC updates & apply any quantized Scene/Revert APVTS snapshot on message thread
    processorRef.midiLearn.flushToParameters();
    const int pendingParamScene = processorRef.sequencer.pendingParamSceneApply.exchange(-1, std::memory_order_acq_rel);
    if (pendingParamScene >= 0 && pendingParamScene < Scenes::COUNT)
    {
        processorRef.applySceneParams(pendingParamScene);
        syncAllGhosts();
        stepGrid.repaint();
    }

    // Optical LED meter smoothing (~95 ms release @ 60 FPS)
    const float rawL = std::min(1.0f, processorRef.analyzer.getPeakL());
    const float rawR = std::min(1.0f, processorRef.analyzer.getPeakR());
    meterSmoothL = (rawL > meterSmoothL) ? (meterSmoothL + 0.65f * (rawL - meterSmoothL)) : (meterSmoothL * 0.88f);
    meterSmoothR = (rawR > meterSmoothR) ? (meterSmoothR + 0.65f * (rawR - meterSmoothR)) : (meterSmoothR * 0.88f);

    // Live Modulation Rings on DRIVE, COMP & CHAOS DENS when AUTO / CHAOS are active
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
    if (autoMutKnob)   autoMutKnob->refreshLearnVisual();
    if (autoDriftKnob) autoDriftKnob->refreshLearnVisual();

    undoButton.setEnabled(processorRef.sequencerUndo.canUndo());
    redoButton.setEnabled(processorRef.sequencerUndo.canRedo());

    // During 350 ms CRT Power-On sequence, repaint full frame; afterwards only dynamic zones
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
    // Easter Egg: clicking the top-right red Space Invader randomizes all channels (with Undo!)
    if (e.y < TITLE_H && e.x > getWidth() - 56)
    {
        processorRef.sequencerUndo.push(processorRef.sequencer);
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
            processorRef.sequencer.smartRandomize(ch);
        stepGrid.repaint();
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
        if (f.existsAsFile() && processorRef.chaosBus.loadCustomFile(f))
        {
            processorRef.apvts.getParameter(ParamId::CHAOS_BANK)->setValueNotifyingHost(1.0f);
            processorRef.apvts.getParameter(ParamId::CHAOS_ENABLE)->setValueNotifyingHost(1.0f);
            updateChaosBankLabel();
            repaint();
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

    // 1..8: quantized Mute toggles for tracks 1..8
    if (code >= '1' && code <= '8')
    {
        stepGrid.triggerMuteToggle(code - '1');
        return true;
    }
    // Space: Play / Stop internal clock
    if (code == juce::KeyPress::spaceKey)
    {
        const bool cur = processorRef.internalPlay.load(std::memory_order_acquire);
        processorRef.internalPlay.store(!cur, std::memory_order_release);
        return true;
    }
    // F: trigger 1-bar FILL
    if (code == 'f' || code == 'F')
    {
        fillButton.triggerClick();
        return true;
    }
    // Q / W / E / R: switch Scenes A / B / C / D
    if (code == 'q' || code == 'Q') { sceneButtons[0].triggerClick(); return true; }
    if (code == 'w' || code == 'W') { sceneButtons[1].triggerClick(); return true; }
    if (code == 'e' || code == 'E') { sceneButtons[2].triggerClick(); return true; }
    if (code == 'r' || code == 'R') { sceneButtons[3].triggerClick(); return true; }
    // C: toggle CHAOS Sample Bus ON/OFF
    if (code == 'c' || code == 'C')
    {
        chaosEnableButton.triggerClick();
        return true;
    }
    // V: Snap-Back / Panic Reload (REVERT)
    if (code == 'v' || code == 'V')
    {
        revertButton.triggerClick();
        return true;
    }
    // K: toggle KILL SUB (< 150 Hz)
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
        if (i == active)       bg = SpaceColours::neonGreen.withAlpha(0.85f);
        else if (i == queued)  bg = SpaceColours::neonCyan.withAlpha(0.55f);
        else                   bg = SpaceColours::bg.brighter(0.10f);
        sceneButtons[idx(i)].setColour(juce::TextButton::buttonColourId, bg);
        sceneButtons[idx(i)].setColour(juce::TextButton::textColourOffId,
            i == active ? juce::Colours::black : SpaceColours::textMid);
    }

    revertButton.setColour(juce::TextButton::buttonColourId,
        revArmed ? SpaceColours::neonRed.withAlpha(0.85f) : SpaceColours::bg.brighter(0.10f));

    // FILL armed state
    const int64_t fb = processorRef.sequencer.fillStartBar.load(std::memory_order_acquire);
    const bool armed = (fb >= 0);
    fillButton.setColour(juce::TextButton::buttonColourId,
        armed ? SpaceColours::neonGreen.withAlpha(0.80f) : SpaceColours::bg.brighter(0.10f));
    fillButton.setColour(juce::TextButton::textColourOffId,
        armed ? juce::Colours::black : SpaceColours::textMid);
}

void OldSchoolBoxView::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // ── Background pulse driven by VisualState ──
    const float kickPulse = processorRef.visualState.kickPulse.load(std::memory_order_relaxed);
    const float sustained = processorRef.visualState.sustainedIntensity.load(std::memory_order_relaxed);

    g.fillAll(SpaceColours::bg);

    // Low-contrast arcade pixel field: visible as texture, never as foreground.
    {
        const int tick = static_cast<int>(juce::Time::getMillisecondCounter() / 180);
        for (int y = TITLE_H + 10; y < getHeight() - MASTER_H; y += 24)
        {
            for (int x = 18; x < getWidth(); x += 32)
            {
                const int cell = (x / 32 + y / 24 + tick) % 7;
                if (cell == 0 || cell == 3)
                {
                    const auto c = (cell == 0 ? SpaceColours::neonCyan : SpaceColours::neonMagenta);
                    g.setColour(c.withAlpha(0.012f + sustained * 0.010f));
                    g.fillRect(x, y, 2, 2);
                }
            }
        }

        const float spin = static_cast<float>(juce::Time::getMillisecondCounter() % 9000) / 9000.0f
                         * juce::MathConstants<float>::twoPi;
        const float cx = w * 0.78f;
        const float cy = static_cast<float>(TITLE_H) + (h - static_cast<float>(TITLE_H + MASTER_H)) * 0.42f;
        for (int i = 0; i < 5; ++i)
        {
            juce::Path spiral;
            const float rx = 48.0f + static_cast<float>(i) * 46.0f;
            const float ry = 24.0f + static_cast<float>(i) * 27.0f;
            spiral.addCentredArc(cx, cy, rx, ry, 0.0f,
                                 -2.4f + spin * 0.22f + static_cast<float>(i) * 0.32f,
                                 -0.45f + spin * 0.22f + static_cast<float>(i) * 0.45f,
                                 true);
            g.setColour((i % 2 == 0 ? SpaceColours::neonCyan : SpaceColours::neonMagenta)
                            .withAlpha(0.018f + sustained * 0.020f));
            g.strokePath(spiral, juce::PathStrokeType(1.0f));
        }
    }

    // Energy-driven ambient glow — a soft radial brightening from centre when
    // sustained kicks build up. The room "lights up" the longer you pound.
    if (sustained > 0.05f)
    {
        juce::ColourGradient amb(
            SpaceColours::neonMagenta.withAlpha(sustained * 0.08f),
            w * 0.5f, h * 0.5f,
            SpaceColours::bg.withAlpha(0.0f),
            0.0f, h, true);
        amb.addColour(0.6, SpaceColours::neonCyan.withAlpha(sustained * 0.04f));
        g.setGradientFill(amb);
        g.fillRect(getLocalBounds());
    }

    // Kick pulse — quick full-window flash on every kick, scaled by sustained intensity
    if (kickPulse > 0.02f)
    {
        const float a = kickPulse * (0.05f + sustained * 0.12f);
        g.setColour(juce::Colour(0xff4a9eff).withAlpha(a));
        g.fillRect(getLocalBounds());
    }

    // ── Top Bar Marquee: FREE TEKNO ARCADE CHASSIS ──
    const bool isClipping = processorRef.masterClipping.load(std::memory_order_relaxed);
    const bool isFillArmed = (processorRef.sequencer.fillStartBar.load(std::memory_order_relaxed) >= 0);
    {
        const int animFrame = processorRef.visualState.kickCount.load(std::memory_order_relaxed) & 1;

        juce::ColourGradient grad(juce::Colour(0xff15181f), 0.0f, 0.0f,
                                  SpaceColours::bg,         0.0f, static_cast<float>(TITLE_H), false);
        g.setGradientFill(grad);
        g.fillRect(0, 0, getWidth(), TITLE_H);

        // Industrial hazard stripes (///) behind PRESSURE // WALL left bay
        SpaceArt::drawHazardStripes(g, { 8.0f, 5.0f, 224.0f, 5.0f }, SpaceColours::themed(SpaceColours::neonOrange), 0.30f);

        // Left: Space Invader (turns RED & shakes 1px when master clips!) + Peak-Hold PRESSURE // WALL
        {
            const int shakeX = isClipping ? ((static_cast<int>(juce::Time::getMillisecondCounter() / 40) & 1) ? 1 : -1) : 0;
            const auto invCol = isClipping
                ? SpaceColours::neonRed
                : SpaceColours::themed(SpaceColours::neonGreen).interpolatedWith(SpaceColours::themed(SpaceColours::neonCyan), kickPulse * 0.40f);
            SpaceArt::drawInvader(g, 0, animFrame, 14.0f + static_cast<float>(shakeX), 15.0f, 3, invCol,
                                  isClipping ? 0.40f : (0.16f + kickPulse * 0.25f));

            const float pX = 58.0f, pY = 14.0f, pW = 166.0f, pH = 14.0f;
            const float rmsAvg = 0.5f * (processorRef.analyzer.getRmsL() + processorRef.analyzer.getRmsR());
            const float pkMax  = std::max(processorRef.analyzer.getPeakL(), processorRef.analyzer.getPeakR());
            const float pressureVal = std::clamp(rmsAvg * 1.45f + pkMax * 0.25f + sustained * 0.25f, 0.0f, 1.0f);

            g.setColour(juce::Colours::black.withAlpha(0.85f));
            g.fillRoundedRectangle(pX - 2.0f, pY - 2.0f, pW + 4.0f, pH + 4.0f, 2.0f);
            SpaceArt::drawLedBar(g, { pX, pY, pW, pH }, pressureVal, 16, false);

            // Peak-hold marker LED
            if (pressurePeakHold > 0.05f)
            {
                const int pkSeg = std::clamp(static_cast<int>(std::round(pressurePeakHold * 15.0f)), 0, 15);
                const float segW = (pW - 15.0f * 1.5f) / 16.0f;
                const float pkX = pX + static_cast<float>(pkSeg) * (segW + 1.5f);
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                g.fillRect(pkX, pY, std::max(2.0f, segW), pH);
            }

            g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.32f));
            g.drawRoundedRectangle(pX - 2.0f, pY - 2.0f, pW + 4.0f, pH + 4.0f, 2.0f, 0.8f);

            g.setFont(SpaceArt::monoFont(8.5f, true));
            g.setColour(SpaceColours::themed(SpaceColours::neonGreen).withAlpha(0.88f));
            g.drawText("PRESSURE // WALL", static_cast<int>(pX), 33, 166, 11, juce::Justification::centredLeft);
        }

        // Centre: Recessed Arcade CRT Display (no geometric ovals — crisp perspective wireframe + razor pixel title)
        {
            const auto titleZone = juce::Rectangle<float>(w * 0.5f - 235.0f, 4.0f, 470.0f, static_cast<float>(TITLE_H - 9));
            const auto crtAccent = SpaceColours::themed(isFillArmed ? SpaceColours::neonRed : SpaceColours::neonGreen);
            SpaceArt::drawCrtBezel(g, titleZone, crtAccent, true);

            // Subtle synth perspective wireframe grid (10% alpha)
            g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.08f));
            for (int gx = static_cast<int>(titleZone.getX()) + 28; gx < static_cast<int>(titleZone.getRight()); gx += 34)
                g.drawLine(static_cast<float>(gx), titleZone.getY() + 2.0f,
                           titleZone.getCentreX() + (static_cast<float>(gx) - titleZone.getCentreX()) * 1.18f,
                           titleZone.getBottom() - 2.0f, 0.7f);

            const int titleCx = getWidth() / 2;
            const int titleY  = 9;
            // Tight 1px RGB glitch ONLY when master clips or FILL is armed (keeps logo razor sharp!)
            if (isClipping || isFillArmed)
            {
                SpaceArt::drawPixelTitle(g, "OLDSCHOOLBOX", titleCx - 1, titleY, 3, SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.75f));
                SpaceArt::drawPixelTitle(g, "OLDSCHOOLBOX", titleCx + 1, titleY, 3, SpaceColours::themed(SpaceColours::neonMagenta).withAlpha(0.75f));
            }
            SpaceArt::drawPixelTitle(g, "OLDSCHOOLBOX", titleCx, titleY, 3,
                                     crtAccent.brighter(kickPulse * 0.25f));

            // Micro-Terminal Engineering HUD: live Hz / ms / kHz / +dB [MACKIE SAT] when hovering/dragging any knob!
            const auto& hudState = MusicalUnits::hud();
            const bool hudLive = hudState.text.isNotEmpty()
                              && (juce::Time::getMillisecondCounterHiRes() - hudState.lastUpdateMs < 1600.0);
            g.setFont(SpaceArt::monoFont(8.5f, true));
            g.setColour(hudLive ? juce::Colours::white.withAlpha(0.95f)
                                : SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.90f));
            g.drawText(hudLive ? hudState.text : "SPACE INVADERS TEKNO SOUND  //  23  //  EST. 1998",
                       static_cast<int>(titleZone.getX()), 33, static_cast<int>(titleZone.getWidth()), 12,
                       juce::Justification::centred);
        }

        // Right: HUD info + THEME switch + clickable red Easter-Egg Space Invader (RND ALL)
        {
            const auto invColR = SpaceColours::themed(SpaceColours::neonRed);
            SpaceArt::drawInvader(g, 1, animFrame ^ 1, w - 50.0f, 15.0f, 3, invColR, 0.20f + kickPulse * 0.25f);

            g.setFont(SpaceArt::monoFont(8.5f, true));
            g.setColour(SpaceColours::themed(SpaceColours::neonOrange).withAlpha(0.92f));
            g.drawText("ACID 303 // DIODE", getWidth() - 220, 10, 158, 12, juce::Justification::centredRight);
            g.setFont(SpaceArt::monoFont(7.5f, true));
            g.setColour(SpaceColours::textDim);
            g.drawText("MKII // CLICK ALIEN: RND", getWidth() - 220, 36, 158, 10, juce::Justification::centredRight);
        }

        // Industrial Torx screws in top corners
        SpaceArt::drawTorxScrew(g, 7.0f, 7.0f, 3.5f);
        SpaceArt::drawTorxScrew(g, w - 7.0f, 7.0f, 3.5f);

        g.setColour(SpaceColours::sectionLine.brighter(0.25f));
        g.drawHorizontalLine(TITLE_H - 1, 0.0f, w);
    }

    // ── Lower Rack: LIVE Row + 3 Modular Hardware Bays ──
    const int masterY  = getHeight() - MASTER_H;
    const int liveRowY = masterY;
    const int modY     = masterY + LIVE_ROW_H + 4;
    const int modH     = MASTER_H - LIVE_ROW_H - 8;
    {
        g.setColour(SpaceColours::bg);
        g.fillRect(0, masterY, getWidth(), MASTER_H);

        g.setColour(SpaceColours::sectionLine.brighter(0.25f));
        g.drawHorizontalLine(masterY, 0.0f, w);
        g.setColour(SpaceColours::sectionLine.withAlpha(0.55f));
        g.drawHorizontalLine(masterY + LIVE_ROW_H, 0.0f, w);

        // LIVE row labels
        g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.80f));
        g.setFont(SpaceArt::monoFont(8.5f, true));
        g.drawText("LIVE", 8, liveRowY + 10, 38, 10, juce::Justification::centredLeft);

        if (!sceneButtons.empty() && sceneButtons[0].getBounds().getX() > 0)
        {
            g.setColour(SpaceColours::themed(SpaceColours::neonGreen).withAlpha(0.70f));
            g.setFont(SpaceArt::monoFont(8.0f, true));
            g.drawText("SCENE", sceneButtons[0].getBounds().getX() - 44, liveRowY + 10, 40, 10,
                       juce::Justification::centredRight);
        }
        if (autoMutKnob && autoDriftKnob)
        {
            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(SpaceColours::themed(SpaceColours::neonYellow).withAlpha(0.85f));
            const auto mr = autoMutKnob->getBounds();
            const auto dr = autoDriftKnob->getBounds();
            g.drawText("MUT", mr.getX() - 4, mr.getBottom() - 2, mr.getWidth() + 8, 8, juce::Justification::centred);
            g.drawText("DRF", dr.getX() - 4, dr.getBottom() - 2, dr.getWidth() + 8, 8, juce::Justification::centred);
        }

        // ── 3-Module Bay Geometry ──
        const int m1X = 6,   m1W = 452;
        const int m2X = 464, m2W = 426;
        const int m3X = 896, m3W = getWidth() - m3X - 6;

        auto drawBay = [&](int bx, int bw, const char* title, juce::Colour accent, bool hazard)
        {
            const auto r = juce::Rectangle<float>(static_cast<float>(bx), static_cast<float>(modY),
                                                  static_cast<float>(bw), static_cast<float>(modH));
            g.setColour(juce::Colour(0xff0b0d12));
            g.fillRoundedRectangle(r, 4.0f);
            if (hazard)
                SpaceArt::drawHazardStripes(g, { r.getX() + 2.0f, r.getY() + 2.0f, r.getWidth() - 4.0f, 13.0f }, accent, 0.22f);
            g.setColour(accent.withAlpha(0.32f));
            g.drawRoundedRectangle(r, 4.0f, 1.0f);
            g.setFont(SpaceArt::monoFont(8.5f, true));
            g.setColour(accent.withAlpha(0.95f));
            g.drawText(title, bx + 8, modY + 3, bw - 16, 12, juce::Justification::centredLeft);
        };

        const auto chaosCol = SpaceColours::themed(SpaceColours::neonMagenta);
        const auto fxCol    = SpaceColours::themed(SpaceColours::neonCyan);
        const auto pressCol = SpaceColours::themed(SpaceColours::neonOrange);

        drawBay(m1X, m1W, "/// CHAOS // SAMPLE BUS", chaosCol, true);
        drawBay(m2X, m2W, "MASTER FX // DUB & SPACE", fxCol, false);
        drawBay(m3X, m3W, "/// PRESSURE // WALL", pressCol, true);

        // ── MODULE 1: CHAOS CRT Waveform + Gate Slicer Overlay + Knob Labels ──
        {
            const auto crtR = juce::Rectangle<float>(static_cast<float>(m1X + 8), static_cast<float>(modY + 19),
                                                     214.0f, 40.0f);
            const bool chaosOn = processorRef.apvts.getRawParameterValue(ParamId::CHAOS_ENABLE)->load() > 0.5f;
            SpaceArt::drawCrtBezel(g, crtR, chaosOn ? chaosCol : SpaceColours::textDim, true);

            // Draw sample waveform preview
            constexpr int NP = ChaosSampleBus::PREVIEW_POINTS;
            const float midY = crtR.getCentreY();
            const float ampH = crtR.getHeight() * 0.40f;
            juce::Path wPath;
            for (int i = 0; i < NP; ++i)
            {
                const float s = processorRef.chaosBus.getPreviewSample(i);
                const float px = crtR.getX() + 4.0f + (static_cast<float>(i) / static_cast<float>(NP - 1)) * (crtR.getWidth() - 8.0f);
                const float py = midY - s * ampH;
                if (i == 0) wPath.startNewSubPath(px, py);
                else        wPath.lineTo(px, py);
            }
            g.setColour((chaosOn ? chaosCol : SpaceColours::textDim).withAlpha(chaosOn ? 0.35f : 0.15f));
            g.strokePath(wPath, juce::PathStrokeType(3.0f));
            g.setColour((chaosOn ? chaosCol : SpaceColours::textMid).withAlpha(chaosOn ? 0.95f : 0.50f));
            g.strokePath(wPath, juce::PathStrokeType(1.2f));

            // Animated vertical gate/playhead slice bar
            const float ph = processorRef.chaosBus.getPlayheadNorm();
            if (chaosOn && ph >= 0.0f)
            {
                const float sx = crtR.getX() + 4.0f + ph * (crtR.getWidth() - 8.0f);
                g.setColour(juce::Colours::white.withAlpha(0.90f));
                g.drawVerticalLine(static_cast<int>(sx), crtR.getY() + 2.0f, crtR.getBottom() - 2.0f);
            }
            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(chaosCol.withAlpha(0.75f));
            g.drawText("DROP WAV // 8S MAX", static_cast<int>(crtR.getX()) + 4, static_cast<int>(crtR.getBottom()) - 10, 120, 9, juce::Justification::centredLeft);

            // Chaos 5 knob labels
            g.setFont(SpaceArt::monoFont(7.5f, true));
            g.setColour(SpaceColours::textMid);
            for (const auto& ck : chaosKnobs)
                if (ck.knob)
                    g.drawText(ck.label, ck.knob->getX() - 2, ck.knob->getBottom() + 1, ck.knob->getWidth() + 4, 10, juce::Justification::centred);
        }

        // ── MODULE 2: MASTER FX // DUB & SPACE (Cyan) + Official Space Invaders Logo Emblem ──
        {
            if (spaceLogo.isValid())
            {
                const int logoSz = 66;
                const int lx = m2X + 10;
                const int ly = modY + 32;
                g.setColour(SpaceColours::themed(SpaceColours::neonGreen).withAlpha(0.20f + kickPulse * 0.25f));
                g.fillEllipse(static_cast<float>(lx - 3), static_cast<float>(ly - 3),
                              static_cast<float>(logoSz + 6), static_cast<float>(logoSz + 6));
                g.drawImageWithin(spaceLogo, lx, ly, logoSz, logoSz, juce::RectanglePlacement::centred);
            }

            g.setFont(SpaceArt::monoFont(7.5f, true));
            g.setColour(fxCol.withAlpha(0.75f));
            if (masterKnobs[1].knob && masterKnobs[2].knob)
                g.drawText("REVERB", masterKnobs[1].knob->getX(), modY + 19,
                           masterKnobs[2].knob->getRight() - masterKnobs[1].knob->getX(), 10, juce::Justification::centred);
            if (masterKnobs[4].knob && masterKnobs[6].knob)
                g.drawText("DELAY", masterKnobs[4].knob->getX(), modY + 19,
                           masterKnobs[6].knob->getRight() - masterKnobs[4].knob->getX(), 10, juce::Justification::centred);

            g.setColour(SpaceColours::textMid);
            for (int i : { 1, 2, 4, 5, 6 })
                if (masterKnobs[static_cast<size_t>(i)].knob)
                {
                    const auto r = masterKnobs[static_cast<size_t>(i)].knob->getBounds();
                    g.drawText(masterKnobs[static_cast<size_t>(i)].label, r.getX(), r.getBottom() + 1, r.getWidth(), 10, juce::Justification::centred);
                }
        }

        // ── MODULE 3: PRESSURE // WALL (Mackie Drive, Comp, WASH DJ Isolator, CLIP LED, Mono <120, KILL SUB, OUT L/R) ──
        {
            g.setFont(SpaceArt::monoFont(7.5f, true));
            g.setColour(SpaceColours::textMid);
            for (int i : { 0, 3, 7 })
                if (masterKnobs[static_cast<size_t>(i)].knob)
                {
                    const auto r = masterKnobs[static_cast<size_t>(i)].knob->getBounds();
                    g.drawText(masterKnobs[static_cast<size_t>(i)].label, r.getX(), r.getBottom() + 1, r.getWidth(), 10, juce::Justification::centred);
                }

            // Fiery red CLIP LED indicator + Ambient Light Bleed on Module 3 gunmetal bezel
            const int clipX = m3X + 10;
            const int clipY = modY + 86;
            if (isClipping)
            {
                g.setColour(SpaceColours::neonRed.withAlpha(0.045f));
                g.fillRoundedRectangle(static_cast<float>(m3X + 2), static_cast<float>(modY + 2),
                                       static_cast<float>(m3W - 4), static_cast<float>(modH - 4), 4.0f);
            }
            g.setColour(isClipping ? SpaceColours::neonRed : SpaceColours::neonRed.withAlpha(0.18f));
            g.fillEllipse(static_cast<float>(clipX), static_cast<float>(clipY), 8.0f, 8.0f);
            if (isClipping)
            {
                g.setColour(SpaceColours::neonRed.withAlpha(0.35f));
                g.fillEllipse(static_cast<float>(clipX - 3), static_cast<float>(clipY - 3), 14.0f, 14.0f);
            }
            g.setFont(SpaceArt::monoFont(7.0f, true));
            g.setColour(isClipping ? SpaceColours::neonRed : SpaceColours::textDim);
            g.drawText("CLIP", clipX + 10, clipY - 1, 28, 10, juce::Justification::centredLeft);

            // Stereo OUT L/R LED Meter with dB scale
            const int meterX = m3X + m3W - METER_W - 8;
            const int meterY = modY + 29;
            const int meterH = modH - 36;
            const int halfW  = 13;
            const int barLX  = meterX;
            const int barRX  = meterX + halfW + 4;

            g.setFont(SpaceArt::monoFont(7.0f, true));
            g.setColour(pressCol.withAlpha(0.85f));
            g.drawText("OUT L/R", meterX - 4, modY + 15, METER_W + 8, 10, juce::Justification::centred);

            auto drawMeter = [&](int bx, float peak, float rms)
            {
                const auto rArea = juce::Rectangle<float>(static_cast<float>(bx), static_cast<float>(meterY),
                                                          static_cast<float>(halfW), static_cast<float>(meterH));
                g.setColour(juce::Colours::black.withAlpha(0.85f));
                g.fillRoundedRectangle(rArea, 1.5f);
                SpaceArt::drawLedBar(g, rArea.reduced(1.0f), std::min(1.0f, peak), 14, true);
                const int rmsH = static_cast<int>(static_cast<float>(meterH) * std::min(1.0f, rms));
                if (rmsH > 0)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    g.fillRect(bx + 1, meterY + meterH - rmsH, halfW - 2, 1);
                }
                g.setColour(pressCol.withAlpha(0.28f));
                g.drawRoundedRectangle(rArea, 1.5f, 0.8f);
            };
            drawMeter(barLX, std::max(meterSmoothL, processorRef.analyzer.getPeakL()), processorRef.analyzer.getRmsL());
            drawMeter(barRX, std::max(meterSmoothR, processorRef.analyzer.getPeakR()), processorRef.analyzer.getRmsR());

            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(SpaceColours::textDim.withAlpha(0.80f));
            g.drawText("0",   barRX + halfW + 2, meterY - 2,                  16, 8, juce::Justification::centredLeft);
            g.drawText("-6",  barRX + halfW + 2, meterY + meterH / 3 - 4,     16, 8, juce::Justification::centredLeft);
            g.drawText("-18", barRX + halfW + 2, meterY + (meterH * 2)/3 - 4, 18, 8, juce::Justification::centredLeft);
        }

        // Bottom Torx corner screws + weathered metal noise overlay
        SpaceArt::drawTorxScrew(g, 7.0f, h - 7.0f, 3.5f);
        SpaceArt::drawTorxScrew(g, w - 7.0f, h - 7.0f, 3.5f);
        if (noiseTile.isValid())
        {
            g.setTiledImageFill(noiseTile, 0, 0, 0.24f);
            g.fillRect(0, masterY, getWidth(), MASTER_H);
        }
    }

    // ── Window-level border glow — full-power mode ──
    if (sustained > 0.05f)
    {
        const float a = sustained * 0.55f;
        g.setColour(SpaceColours::neonMagenta.withAlpha(a));
        g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 4.0f, 1.5f);
        g.setColour(SpaceColours::neonCyan.withAlpha(a * 0.45f));
        g.drawRoundedRectangle(1.5f, 1.5f, w - 3.0f, h - 3.0f, 3.5f, 1.0f);
    }

    // ── CRT Power-On Sequence (350 ms): expanding white horizontal beam + L->R LED check-sweep ──
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
        else if (elapsed < 350.0f)
        {
            const float t = (elapsed - 200.0f) / 150.0f;
            const float sweepX = t * w;
            juce::ColourGradient beam(SpaceColours::neonGreen.withAlpha(0.0f), sweepX - 60.0f, 0.0f,
                                      SpaceColours::neonCyan.withAlpha(0.28f * (1.0f - t)), sweepX, 0.0f, false);
            g.setGradientFill(beam);
            g.fillRect(sweepX - 60.0f, 0.0f, 68.0f, h);
        }
    }
}

void OldSchoolBoxView::resized()
{
    const int masterY  = getHeight() - MASTER_H;
    const int liveRowY = masterY;
    const int modY     = masterY + LIVE_ROW_H + 4;
    stepGrid.setBounds(0, TITLE_H, getWidth(), masterY - TITLE_H);

    // Top-right THEME switcher inside header
    themeButton.setBounds(getWidth() - 106, 22, 46, 14);

    // ── Live row: UNDO/REDO/?/QNT | SCENE A-D + REVERT | FILL | AUTO + MUT/DRF | WAV>/MID>/TIGHT/MIDI/INIT ──
    const int liveBtnH = 22;
    const int liveBtnY = liveRowY + (LIVE_ROW_H - liveBtnH) / 2;
    const int sceneGap = 4;
    const int totalSceneW = Scenes::COUNT * SCENE_BTN_W + (Scenes::COUNT - 1) * sceneGap;
    const int sceneGroupX = 354;

    undoButton.setBounds(50,  liveBtnY + 2, 42, liveBtnH - 4);
    redoButton.setBounds(95,  liveBtnY + 2, 42, liveBtnH - 4);
    helpButton.setBounds(140, liveBtnY + 2, 20, liveBtnH - 4);
    qntButton.setBounds (166, liveBtnY + 2, 54, liveBtnH - 4);

    for (int i = 0; i < Scenes::COUNT; ++i)
        sceneButtons[idx(i)].setBounds(sceneGroupX + i * (SCENE_BTN_W + sceneGap),
                                  liveBtnY, SCENE_BTN_W, liveBtnH);

    revertButton.setBounds(sceneGroupX + totalSceneW + 6, liveBtnY + 1, 52, liveBtnH - 2);
    fillButton.setBounds(sceneGroupX + totalSceneW + 64, liveBtnY - 1, FILL_BTN_W, liveBtnH + 2);

    const int autoX = sceneGroupX + totalSceneW + 64 + FILL_BTN_W + 10;
    autoButton.setBounds(autoX, liveBtnY, 54, liveBtnH);
    autoMutKnob->setBounds(autoX + 58, liveBtnY, 20, 20);
    autoDriftKnob->setBounds(autoX + 82, liveBtnY, 20, 20);

    dragWavButton.setBounds (getWidth() - 256, liveBtnY + 2, 46, liveBtnH - 4);
    dragMidiButton.setBounds(getWidth() - 206, liveBtnY + 2, 46, liveBtnH - 4);
    tightButton.setBounds   (getWidth() - 154, liveBtnY + 2, 48, liveBtnH - 4);
    midiOutButton.setBounds (getWidth() - 101, liveBtnY + 2, 43, liveBtnH - 4);
    initButton.setBounds    (getWidth() - 52,  liveBtnY + 2, 44, liveBtnH - 4);

    // ── MODULE 1 (Left, x=6..458): /// CHAOS // SAMPLE BUS ──
    const int m1X = 6;
    chaosEnableButton.setBounds(m1X + 230, modY + 20, 62, 20);
    chaosWavButton.setBounds   (m1X + 298, modY + 20, 44, 20);
    chaosBankButton.setBounds  (m1X + 348, modY + 20, 96, 20);

    for (size_t i = 0; i < chaosKnobs.size(); ++i)
        if (chaosKnobs[i].knob)
            chaosKnobs[i].knob->setBounds(m1X + 26 + static_cast<int>(i) * 82, modY + 65, CK_SZ, CK_SZ);

    // ── MODULE 2 (Centre, x=464..890): MASTER FX // DUB & SPACE ──
    const int m2X = 464;
    const int fxKy = modY + 32;
    if (masterKnobs[1].knob) masterKnobs[1].knob->setBounds(m2X + 88,  fxKy, MK_SZ, MK_SZ); // SIZE
    if (masterKnobs[2].knob) masterKnobs[2].knob->setBounds(m2X + 134, fxKy, MK_SZ, MK_SZ); // VERB
    if (masterKnobs[4].knob) masterKnobs[4].knob->setBounds(m2X + 194, fxKy, MK_SZ, MK_SZ); // TIME
    if (masterKnobs[5].knob) masterKnobs[5].knob->setBounds(m2X + 240, fxKy, MK_SZ, MK_SZ); // FDBK
    if (masterKnobs[6].knob) masterKnobs[6].knob->setBounds(m2X + 286, fxKy, MK_SZ, MK_SZ); // MIX
    syncButton.setBounds    (m2X + 336, fxKy + 6,  78, 20);
    dubThrowButton.setBounds(m2X + 194, modY + 86, 128, 22);

    // ── MODULE 3 (Right, x=896..end): /// PRESSURE // WALL ──
    const int m3X = 896;
    if (masterKnobs[0].knob) masterKnobs[0].knob->setBounds(m3X + 14,  modY + 28, MK_SZ, MK_SZ); // DRIVE
    if (masterKnobs[3].knob) masterKnobs[3].knob->setBounds(m3X + 70,  modY + 28, MK_SZ, MK_SZ); // COMP
    if (masterKnobs[7].knob) masterKnobs[7].knob->setBounds(m3X + 126, modY + 28, MK_SZ, MK_SZ); // WASH
    monoSubButton.setBounds(m3X + 48,  modY + 81, 66, 20);
    killSubButton.setBounds(m3X + 120, modY + 81, 66, 20);
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
    setResizeLimits(static_cast<int>(std::round(bw * 0.70)),
                    static_cast<int>(std::round(bh * 0.70)),
                    static_cast<int>(std::round(bw * 2.00)),
                    static_cast<int>(std::round(bh * 2.00)));

    // Fit cleanly on smaller laptop screens (e.g. 13"/14") while staying crisp at 1.0x on large displays
    float initScale = 1.0f;
    if (const auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = disp->userArea;
        const float maxW = static_cast<float>(area.getWidth()) * 0.92f;
        const float maxH = static_cast<float>(area.getHeight()) * 0.88f;
        initScale = std::min({ 1.0f, maxW / static_cast<float>(bw), maxH / static_cast<float>(bh) });
        initScale = std::clamp(initScale, 0.65f, 1.0f);
    }

    setSize(static_cast<int>(std::round(bw * initScale)),
            static_cast<int>(std::round(bh * initScale)));
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
