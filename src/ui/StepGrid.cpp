#include "StepGrid.h"
#include "../core/ParamIds.h"
#include "SpaceLookAndFeel.h"
#include "SpaceArt.h"
#include "EuclideanPopup.h"

static const char* kChannelNames[] = {
    "KICK", "SNARE", "HATS", "TOM", "PERC1", "PERC2", "BASS1", "BASS2"
};

StepGrid::StepGrid(Sequencer& seq, VoiceBank& voices, juce::AudioProcessorValueTreeState& apvts,
                   std::atomic<float>& bpm, std::atomic<bool>& internalPlay,
                   std::atomic<bool>& hostPlaying, VisualState& visState,
                   OutputAnalyzer& analyzer, SequencerUndo& undo,
                   MidiLearnManager* midiLearn)
    : sequencer(seq), voiceBank(voices), bpmRef(bpm),
      internalPlayRef(internalPlay), hostPlayingRef(hostPlaying),
      visualState(visState), analyzerRef(analyzer), undoRef(undo),
      midiLearnRef(midiLearn)
{
    lastDisplayedStep.fill(-1);
    noiseTile = SpaceArt::makeNoiseTile(128);

    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto chCol = SpaceColours::channelColor(ch);

        // Volume knob (DjKnob: Alt+drag spring-back, Shift fine, Right-click MIDI Learn)
        const juce::String volId = "vol_" + juce::String(ch);
        auto knob = std::make_unique<DjKnob>(volId, &apvts, midiLearnRef, juce::String(kChannelNames[ch]) + " VOL");
        knob->setTooltip(juce::String(kChannelNames[ch]) + " Volume (Alt+drag: spring-back | Right-click: MIDI Learn)");
        knob->setColour(juce::Slider::rotarySliderFillColourId, chCol);
        knob->setPopupDisplayEnabled(true, false, this);
        auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, volId, *knob);
        addAndMakeVisible(*knob);
        volAttachments[idx(ch)] = std::move(att);
        volKnobs[idx(ch)]       = std::move(knob);

        // Pan knob
        const juce::String panId = ParamId::pan(ch);
        auto pk = std::make_unique<DjKnob>(panId, &apvts, midiLearnRef, juce::String(kChannelNames[ch]) + " PAN");
        pk->setTooltip(juce::String(kChannelNames[ch]) + " Pan (double-click: centre | Right-click: MIDI Learn)");
        pk->setColour(juce::Slider::rotarySliderFillColourId, chCol.withMultipliedSaturation(0.75f));
        pk->setPopupDisplayEnabled(true, false, this);
        panAttachments[idx(ch)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, panId, *pk);
        addAndMakeVisible(*pk);
        panKnobs[idx(ch)] = std::move(pk);

        // CLR / RND
        clrButtons[idx(ch)].setButtonText("CLR");
        clrButtons[idx(ch)].setTooltip("Clear all steps on " + juce::String(kChannelNames[ch]));
        clrButtons[idx(ch)].onClick = [this, ch] { undoRef.push(sequencer); sequencer.clearChannel(ch); repaint(); };
        addAndMakeVisible(clrButtons[idx(ch)]);

        rndButtons[idx(ch)].setButtonText("RND");
        rndButtons[idx(ch)].setTooltip("Smart-randomize pattern for " + juce::String(kChannelNames[ch]));
        rndButtons[idx(ch)].onClick = [this, ch] { undoRef.push(sequencer); sequencer.smartRandomize(ch); repaint(); };
        addAndMakeVisible(rndButtons[idx(ch)]);

        // Auto-variation toggle
        varButtons[idx(ch)].setButtonText("VAR");
        varButtons[idx(ch)].setTooltip("Auto-Variation — cycles fills & variations over 64-bar phrase");
        varButtons[idx(ch)].setClickingTogglesState(true);
        varButtons[idx(ch)].setToggleState(false, juce::dontSendNotification);
        varButtons[idx(ch)].onClick = [this, ch] {
            const bool on = varButtons[idx(ch)].getToggleState();
            sequencer.autoVar[idx(ch)].store(on, std::memory_order_release);
        };
        addAndMakeVisible(varButtons[idx(ch)]);

        // Humanize toggle (±15% velocity per trigger)
        humButtons[idx(ch)].setButtonText("H");
        humButtons[idx(ch)].setTooltip("Humanize - randomises velocity +/-15% on each trigger");
        humButtons[idx(ch)].setClickingTogglesState(true);
        humButtons[idx(ch)].setToggleState(false, juce::dontSendNotification);
        humButtons[idx(ch)].onClick = [this, ch] {
            sequencer.humanize[idx(ch)].store(humButtons[idx(ch)].getToggleState(), std::memory_order_release);
        };
        addAndMakeVisible(humButtons[idx(ch)]);

        // Pattern length cycle button (Click: 16/32/48/64 | Shift+Click: -1 step | Alt+Click: +1 step | Wheel: ±1 polyrhythm!)
        lenButtons[idx(ch)].onClick = [this, ch] {
            undoRef.push(sequencer);
            const auto mods = juce::ModifierKeys::currentModifiers;
            const int cur = sequencer.getPatternLength(ch);
            int next = cur;
            if (mods.isShiftDown())
                next = std::max(1, cur - 1);
            else if (mods.isAltDown())
                next = std::min(Sequencer::MAX_STEPS, cur + 1);
            else
                next = (cur >= 64) ? 16 : ((cur / 16) + 1) * 16;
            sequencer.setPatternLength(ch, next);
            const int maxPage = std::max(0, (next - 1) / VISIBLE_STEPS);
            viewPage[idx(ch)] = std::min(viewPage[idx(ch)], maxPage);
            updateLenButton(ch);
            updatePageButtons(ch);
            repaint();
        };
        addAndMakeVisible(lenButtons[idx(ch)]);
        updateLenButton(ch);

        // Page navigation
        prevBtns[idx(ch)].setButtonText("<");
        prevBtns[idx(ch)].setTooltip("Previous 16-step page");
        prevBtns[idx(ch)].onClick = [this, ch] {
            if (viewPage[idx(ch)] > 0) { --viewPage[idx(ch)]; updatePageButtons(ch); repaint(); }
        };
        addAndMakeVisible(prevBtns[idx(ch)]);

        nextBtns[idx(ch)].setButtonText(">");
        nextBtns[idx(ch)].setTooltip("Next 16-step page");
        nextBtns[idx(ch)].onClick = [this, ch] {
            const int maxPage = std::max(0, (sequencer.getPatternLength(ch) - 1) / VISIBLE_STEPS);
            if (viewPage[idx(ch)] < maxPage) { ++viewPage[idx(ch)]; updatePageButtons(ch); repaint(); }
        };
        addAndMakeVisible(nextBtns[idx(ch)]);
        updatePageButtons(ch);

        // Mute toggle (quantized when playing & QNT != OFF; flashes until beat/bar boundary!)
        muteButtons[idx(ch)].setButtonText("M");
        muteButtons[idx(ch)].setTooltip("Mute " + juce::String(kChannelNames[ch]) + " (quantized to QNT when playing; Shift+Click: instant)");
        muteButtons[idx(ch)].setClickingTogglesState(false);
        muteButtons[idx(ch)].setToggleState(false, juce::dontSendNotification);
        muteButtons[idx(ch)].onClick = [this, ch] {
            if (juce::ModifierKeys::currentModifiers.isShiftDown())
            {
                const bool cur = sequencer.mute[idx(ch)].load(std::memory_order_acquire);
                sequencer.mute[idx(ch)].store(!cur, std::memory_order_release);
                sequencer.queuedMuteMask.fetch_and(static_cast<uint8_t>(~(1u << ch)), std::memory_order_acq_rel);
                syncFromSequencer();
            }
            else
            {
                triggerMuteToggle(ch);
            }
        };
        addAndMakeVisible(muteButtons[idx(ch)]);

        // Solo toggle
        soloButtons[idx(ch)].setButtonText("S");
        soloButtons[idx(ch)].setTooltip("Solo " + juce::String(kChannelNames[ch]));
        soloButtons[idx(ch)].setClickingTogglesState(true);
        soloButtons[idx(ch)].setToggleState(false, juce::dontSendNotification);
        soloButtons[idx(ch)].onClick = [this, ch] {
            sequencer.solo[idx(ch)].store(soloButtons[idx(ch)].getToggleState(), std::memory_order_release);
        };
        addAndMakeVisible(soloButtons[idx(ch)]);

        // Copy
        copyButtons[idx(ch)].setButtonText("C");
        copyButtons[idx(ch)].setTooltip("Copy pattern" + juce::String(ch >= 6 ? " + 303 notes/attrs" : ""));
        copyButtons[idx(ch)].onClick = [this, ch] {
            sequencer.clipboardCh = ch;
            sequencer.clipboardPattern = sequencer.getPattern(ch);
            if (ch >= 6)
            {
                const int bi = ch - 6;
                sequencer.clipboardBassSlide   = sequencer.getBassSlidePattern(bi);
                sequencer.clipboardBassAccent  = sequencer.getBassAccentPattern(bi);
                sequencer.clipboardBassOctUp   = sequencer.getBassOctUpPattern(bi);
                sequencer.clipboardBassOctDown = sequencer.getBassOctDownPattern(bi);
                for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                    sequencer.clipboardBassNotes[static_cast<size_t>(s)] = sequencer.getBassNoteOffset(bi, s);
            }
        };
        addAndMakeVisible(copyButtons[idx(ch)]);

        // Paste
        pasteButtons[idx(ch)].setButtonText("P");
        pasteButtons[idx(ch)].setTooltip("Paste copied pattern into " + juce::String(kChannelNames[ch]));
        pasteButtons[idx(ch)].onClick = [this, ch] {
            if (sequencer.clipboardCh >= 0)
            {
                undoRef.push(sequencer);
                sequencer.setPattern(ch, sequencer.clipboardPattern);
                if (ch >= 6 && sequencer.clipboardCh >= 6)
                {
                    const int bi = ch - 6;
                    sequencer.setBassSlidePattern(bi, sequencer.clipboardBassSlide);
                    sequencer.setBassAccentPattern(bi, sequencer.clipboardBassAccent);
                    sequencer.setBassOctUpPattern(bi, sequencer.clipboardBassOctUp);
                    sequencer.setBassOctDownPattern(bi, sequencer.clipboardBassOctDown);
                    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                        sequencer.setBassNoteOffset(bi, s, sequencer.clipboardBassNotes[static_cast<size_t>(s)]);
                }
                repaint();
            }
        };
        addAndMakeVisible(pasteButtons[idx(ch)]);
    }

    // Per-channel synth params — every knob inherits its track colour + DjKnob + Engineering Units HUD
    channelParams[0].add(apvts, ParamId::KICK_TUNE,     "Tune",  *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_DECAY,    "Decay", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_PUNCH,    "Punch", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_PITCHENV, "PtchE", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_DRIVE,    "Drive", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_SUB,      "Sub",   *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");

    channelParams[1].add(apvts, ParamId::SNARE_TUNE,  "Tune",  *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");
    channelParams[1].add(apvts, ParamId::SNARE_DECAY, "Decay", *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");
    channelParams[1].add(apvts, ParamId::SNARE_SNAP,  "Snap",  *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");
    channelParams[1].add(apvts, ParamId::SNARE_NOISE, "Noise", *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");

    channelParams[2].add(apvts, ParamId::HATS_DECAY, "Decay", *this, SpaceColours::channelColor(2), midiLearnRef, "HATS");
    channelParams[2].add(apvts, ParamId::HATS_TONE,  "Tone",  *this, SpaceColours::channelColor(2), midiLearnRef, "HATS");
    channelParams[2].add(apvts, ParamId::HATS_BODY,  "Body",  *this, SpaceColours::channelColor(2), midiLearnRef, "HATS");

    channelParams[3].add(apvts, ParamId::TOM_TUNE,     "Tune",  *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");
    channelParams[3].add(apvts, ParamId::TOM_DECAY,    "Decay", *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");
    channelParams[3].add(apvts, ParamId::TOM_PITCHENV, "PtchE", *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");
    channelParams[3].add(apvts, ParamId::TOM_ATTACK,   "Attk",  *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");

    channelParams[4].add(apvts, ParamId::PERC1_TUNE,   "Tune",  *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");
    channelParams[4].add(apvts, ParamId::PERC1_DECAY,  "Decay", *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");
    channelParams[4].add(apvts, ParamId::PERC1_DETUNE, "Detn",  *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");
    channelParams[4].add(apvts, ParamId::PERC1_DRIVE,  "Drive", *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");

    channelParams[5].add(apvts, ParamId::PERC2_TUNE,  "Tune",  *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");
    channelParams[5].add(apvts, ParamId::PERC2_DECAY, "Decay", *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");
    channelParams[5].add(apvts, ParamId::PERC2_NOISE, "Noise", *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");
    channelParams[5].add(apvts, ParamId::PERC2_PITCH, "Pitch", *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");

    channelParams[6].add(apvts, ParamId::BASS1_CUTOFF, "Cut",  *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_RESO,   "Res",  *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_ENVMOD, "Env",  *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_DECAY,  "Dec",  *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_ACCENT, "Acc",  *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::SC_BASS1,     "SC",   *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");

    channelParams[7].add(apvts, ParamId::BASS2_CUTOFF, "Cut",  *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_RESO,   "Res",  *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_ENVMOD, "Env",  *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_DECAY,  "Dec",  *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_ACCENT, "Acc",  *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::SC_BASS2,     "SC",   *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");

    // Dedicated RND ACID buttons for BASS1 & BASS2 (locked to active underground scale)
    for (int bi = 0; bi < 2; ++bi)
    {
        acidRndButtons[static_cast<size_t>(bi)].setButtonText("ACID");
        acidRndButtons[static_cast<size_t>(bi)].setTooltip("Randomize 303 Notes, Slides, Accents & Octaves locked to active Underground Scale");
        acidRndButtons[static_cast<size_t>(bi)].onClick = [this, bi] {
            undoRef.push(sequencer);
            sequencer.randomizeBassAcid(bi);
            repaint();
        };
        addAndMakeVisible(acidRndButtons[static_cast<size_t>(bi)]);
    }

    // Compact Underground Acid Scale Lock pill (PHR23 / DMIN / HYP2 / DIM)
    acidScaleButton.setButtonText(Sequencer::acidScaleName(sequencer.acidScale.load(std::memory_order_relaxed)));
    acidScaleButton.setTooltip("Acid Scale Lock for RND & ACID: PHR23 (Phrygian 23) / DMIN (Dark Minor) / HYP2 (Hypno 2-Note) / DIM (Diminished)");
    acidScaleButton.onClick = [this] {
        const int next = (sequencer.acidScale.load(std::memory_order_relaxed) + 1) % static_cast<int>(Sequencer::AcidScale::Count);
        sequencer.acidScale.store(next, std::memory_order_release);
        acidScaleButton.setButtonText(Sequencer::acidScaleName(next));
        repaint();
    };
    addAndMakeVisible(acidScaleButton);

    // Internal clock Play/Stop toggle in header
    playButton.setButtonText("PLAY");
    playButton.setTooltip("Internal Clock Play/Stop (Spacebar; runs when DAW transport is stopped)");
    playButton.setClickingTogglesState(true);
    playButton.setToggleState(internalPlayRef.load(std::memory_order_acquire), juce::dontSendNotification);
    playButton.onClick = [this] {
        const bool on = playButton.getToggleState();
        internalPlayRef.store(on, std::memory_order_release);
        playButton.setButtonText(on ? "STOP" : "PLAY");
    };
    addAndMakeVisible(playButton);

    // Swing knob in header
    swingKnob = std::make_unique<DjKnob>(ParamId::SWING, &apvts, midiLearnRef, "SWING");
    swingKnob->setTooltip("Swing (0 = straight, 1 = max shuffle; Right-click: MIDI Learn)");
    swingKnob->setPopupDisplayEnabled(true, false, this);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamId::SWING, *swingKnob);
    addAndMakeVisible(*swingKnob);

    syncFromSequencer();
    startTimerHz(60); // 60 FPS buttery optical LED decay & phase-locked CRT scope
}

StepGrid::~StepGrid() { stopTimer(); }

void StepGrid::syncGhosts(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& savedState)
{
    auto syncOne = [&](DjKnob* k) {
        if (!k) return;
        const auto pid = k->getParamId();
        if (auto* p = apvts.getParameter(pid))
        {
            float norm = p->getDefaultValue();
            for (int c = 0; c < savedState.getNumChildren(); ++c)
            {
                const auto child = savedState.getChild(c);
                if (child.getProperty("id").toString() == pid)
                {
                    norm = p->convertTo0to1(static_cast<float>(child.getProperty("value")));
                    break;
                }
            }
            k->setGhostNormalized(norm);
        }
    };
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        syncOne(volKnobs[idx(ch)].get());
        syncOne(panKnobs[idx(ch)].get());
        channelParams[idx(ch)].syncGhosts(apvts, savedState);
    }
    syncOne(swingKnob.get());
}

void StepGrid::rebuildStaticBackdrop()
{
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) return;
    lastBackdropTheme = SpaceColours::activeTheme();
    staticBackdrop = juce::Image(juce::Image::RGB, w, h, true);
    juce::Graphics g(staticBackdrop);
    g.fillAll(SpaceColours::bg);
    if (noiseTile.isValid())
    {
        g.setTiledImageFill(noiseTile, 0, 0, 0.22f);
        g.fillAll();
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void StepGrid::triggerMuteToggle(int ch)
{
    if (ch < 0 || ch >= NUM_CH) return;
    const bool playing = sequencer.isPlaying.load(std::memory_order_acquire);
    const int  qnt     = sequencer.quantizeMode.load(std::memory_order_relaxed);
    if (playing && qnt > 0)
    {
        sequencer.queuedMuteMask.fetch_xor(static_cast<uint8_t>(1u << ch), std::memory_order_acq_rel);
    }
    else
    {
        const bool cur = sequencer.mute[idx(ch)].load(std::memory_order_acquire);
        sequencer.mute[idx(ch)].store(!cur, std::memory_order_release);
    }
    syncFromSequencer();
}

void StepGrid::updateLenButton(int ch)
{
    const int len = sequencer.getPatternLength(ch);
    const juce::String lbl = (len % 16 == 0) ? (juce::String(len / 16) + "B")
                                             : (juce::String(len) + "S");
    lenButtons[idx(ch)].setButtonText(lbl);
    lenButtons[idx(ch)].setTooltip("Polyrhythmic length: " + juce::String(len) + " steps (Click: 1B-4B | Shift/Alt+Click or Wheel: +/-1 step)");
}

void StepGrid::updatePageButtons(int ch)
{
    const int maxPage = std::max(0, (sequencer.getPatternLength(ch) - 1) / VISIBLE_STEPS);
    prevBtns[idx(ch)].setEnabled(viewPage[idx(ch)] > 0);
    nextBtns[idx(ch)].setEnabled(viewPage[idx(ch)] < maxPage);
}

void StepGrid::syncFromSequencer()
{
    bool changed = false;

    const bool intPlay = internalPlayRef.load(std::memory_order_acquire);
    if (playButton.getToggleState() != intPlay)
    {
        playButton.setToggleState(intPlay, juce::dontSendNotification);
        playButton.setButtonText(intPlay ? "STOP" : "PLAY");
        changed = true;
    }

    const juce::String scLbl = Sequencer::acidScaleName(sequencer.acidScale.load(std::memory_order_relaxed));
    if (acidScaleButton.getButtonText() != scLbl)
    {
        acidScaleButton.setButtonText(scLbl);
        changed = true;
    }

    const uint8_t qMask = sequencer.queuedMuteMask.load(std::memory_order_acquire);

    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const bool m = sequencer.mute[idx(ch)].load(std::memory_order_acquire);
        if (muteButtons[idx(ch)].getToggleState() != m)
        {
            muteButtons[idx(ch)].setToggleState(m, juce::dontSendNotification);
            changed = true;
        }
        const bool qm = (qMask & (1u << ch)) != 0;
        if (static_cast<bool>(muteButtons[idx(ch)].getProperties()["queuedMute"]) != qm)
        {
            muteButtons[idx(ch)].getProperties().set("queuedMute", qm);
            muteButtons[idx(ch)].repaint();
            changed = true;
        }
        else if (qm)
        {
            muteButtons[idx(ch)].repaint(); // keep pulsing while queued
        }

        const bool s = sequencer.solo[idx(ch)].load(std::memory_order_acquire);
        if (soloButtons[idx(ch)].getToggleState() != s)
        {
            soloButtons[idx(ch)].setToggleState(s, juce::dontSendNotification);
            changed = true;
        }

        const bool v = sequencer.autoVar[idx(ch)].load(std::memory_order_acquire);
        if (varButtons[idx(ch)].getToggleState() != v)
        {
            varButtons[idx(ch)].setToggleState(v, juce::dontSendNotification);
            changed = true;
        }

        const bool h = sequencer.humanize[idx(ch)].load(std::memory_order_acquire);
        if (humButtons[idx(ch)].getToggleState() != h)
        {
            humButtons[idx(ch)].setToggleState(h, juce::dontSendNotification);
            changed = true;
        }

        const int len = sequencer.getPatternLength(ch);
        const juce::String expectedLen = (len % 16 == 0) ? (juce::String(len / 16) + "B")
                                                         : (juce::String(len) + "S");
        if (lenButtons[idx(ch)].getButtonText() != expectedLen)
        {
            const int maxPage = std::max(0, (len - 1) / VISIBLE_STEPS);
            viewPage[idx(ch)] = std::min(viewPage[idx(ch)], maxPage);
            updateLenButton(ch);
            updatePageButtons(ch);
            changed = true;
        }
    }

    if (changed) repaint();
}

juce::Rectangle<int> StepGrid::stepRect(int ch, int visStep) const
{
    const int x = stepX(visStep);
    const int y = rowY(ch) + (ROW_H - STEP_H) / 2;
    return { x, y, STEP_W - 2, STEP_H - 2 };
}

std::pair<int,int> StepGrid::hitStep(int mx, int my) const
{
    for (int ch = 0; ch < NUM_CH; ++ch)
        for (int vs = 0; vs < VISIBLE_STEPS; ++vs)
            if (stepRect(ch, vs).contains(mx, my))
                return { ch, viewPage[idx(ch)] * VISIBLE_STEPS + vs };
    return { -1, -1 };
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(SpaceColours::bg);

    const bool playing = sequencer.isPlaying.load(std::memory_order_acquire);
    const int64_t nowMs = juce::Time::getMillisecondCounter();
    const float phase = static_cast<float>(nowMs % 4096) / 4096.0f;
    const int64_t visNow = visualState.sampleCounter.load(std::memory_order_acquire);
    const float   energy = visualState.totalEnergy.load(std::memory_order_relaxed);
    const float   sustained = visualState.sustainedIntensity.load(std::memory_order_relaxed);

    // Pixel-arcade substrate: static circuit dots, invader ticks, and a slow radar shimmer.
    {
        const int gridX = PAD + LABEL_W;
        const int gridW = STEPS_AREA_W;
        const int gridBottom = rowY(NUM_CH - 1) + ROW_H;

        g.setColour(juce::Colours::white.withAlpha(0.020f));
        for (int y = HEADER_H + 8; y < gridBottom; y += 18)
            for (int x = gridX + 10; x < gridX + gridW; x += 38)
                if (((x / 38) + (y / 18)) % 3 == 0)
                    g.fillRect(x, y, 1, 1);

        for (int y = HEADER_H + 16; y < gridBottom; y += ROW_H)
        {
            const int invX = gridX + 9 + ((y / ROW_H) % 4) * STEP_W;
            const auto invader = juce::Rectangle<int>(invX, y, 13, 7);
            g.setColour(((y / ROW_H) % 2 == 0 ? SpaceColours::neonCyan : SpaceColours::neonMagenta)
                            .withAlpha(0.030f + sustained * 0.018f));
            g.fillRect(invader.getX() + 2, invader.getY(),     2, 2);
            g.fillRect(invader.getX() + 9, invader.getY(),     2, 2);
            g.fillRect(invader.getX(),     invader.getY() + 2, 13, 2);
            g.fillRect(invader.getX() + 2, invader.getY() + 4, 2, 2);
            g.fillRect(invader.getX() + 9, invader.getY() + 4, 2, 2);
            g.fillRect(invader.getX() + 5, invader.getY() + 5, 3, 2);
        }

        const float radarX = static_cast<float>(gridX)
                           + std::fmod(phase * static_cast<float>(gridW * 2),
                                       static_cast<float>(gridW));
        juce::ColourGradient sweep(SpaceColours::neonCyan.withAlpha(0.000f), radarX - 48.0f, 0.0f,
                                   SpaceColours::neonCyan.withAlpha(0.060f + sustained * 0.030f), radarX, 0.0f, false);
        sweep.addColour(1.0, SpaceColours::neonMagenta.withAlpha(0.000f));
        g.setGradientFill(sweep);
        g.fillRect(gridX, HEADER_H, gridW, gridBottom - HEADER_H);

        const float traceCx = static_cast<float>(gridX + gridW / 2);
        const float traceCy = static_cast<float>(HEADER_H + (gridBottom - HEADER_H) / 2);
        const float spin = phase * juce::MathConstants<float>::twoPi;
        for (int i = 0; i < 4; ++i)
        {
            juce::Path arc;
            const float rx = 112.0f + static_cast<float>(i) * 64.0f;
            const float ry = 42.0f + static_cast<float>(i) * 21.0f;
            arc.addCentredArc(traceCx, traceCy, rx, ry, 0.0f,
                              -1.8f + spin * 0.35f + static_cast<float>(i) * 0.30f,
                              -0.2f + spin * 0.35f + static_cast<float>(i) * 0.42f,
                              true);
            g.setColour((i % 2 == 0 ? SpaceColours::neonPurple : SpaceColours::neonGreen)
                            .withAlpha(0.020f + sustained * 0.025f));
            g.strokePath(arc, juce::PathStrokeType(1.0f));
        }
    }

    // ── Header: beat numbers ──
    int headerPage = 0;
    for (int ch = 0; ch < NUM_CH; ++ch)
        headerPage = std::max(headerPage, viewPage[idx(ch)]);
    const int stepOffset = headerPage * VISIBLE_STEPS;

    g.setFont(SpaceArt::monoFont(11.0f, true));
    for (int s = 0; s < VISIBLE_STEPS; ++s)
    {
        const int x = stepX(s);
        if (s % 4 == 0)
        {
            // Beat markers — bright downbeat 1/3, mid 2/4
            const float alpha = (s % 8 == 0) ? 0.92f : 0.62f;
            g.setColour(SpaceColours::neonCyan.withAlpha(alpha));
            g.setFont(SpaceArt::monoFont(11.0f, true));
            g.drawText(juce::String((stepOffset + s) / 4 + 1), x, 5, STEP_W, 17,
                       juce::Justification::centred);
            // Crisp beat-group vertical divider
            g.setColour(SpaceColours::gridLine.brighter(0.28f));
            g.drawVerticalLine(x - 2, static_cast<float>(HEADER_H), static_cast<float>(rowY(NUM_CH - 1) + ROW_H));
        }
        else
        {
            g.setColour(SpaceColours::gridLine.brighter(0.30f));
            g.fillEllipse(static_cast<float>(x + STEP_W / 2 - 1), 14.0f, 2.0f, 2.0f);
        }
    }

    // Header labels for control zones. These tiny captions make the dense
    // right-side controls read as intentional modules, not a button spill.
    {
        const int stepEndX = PAD + LABEL_W + STEPS_AREA_W;
        const int paramStartX = stepEndX + PAD;
        const int volX = paramStartX + PARAM_AREA_W + PAD;
        const int panX = volX + KNOB_W + PAD;
        const int msX = panX + PAN_KSZ + PAD;
        const int editX = msX + MS_BTN_W + PAD;
        const int cpX = editX + BTN_W + PAD;
        const int varX = cpX + BTN_W + PAD;
        const int lenX = varX + VAR_BTN_W + PAD;

        auto zoneText = [&](const char* txt, int x, int w, juce::Colour col)
        {
            g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
            g.setColour(col.withAlpha(0.74f));
            g.drawText(txt, x, HEADER_H - 12, w, 9, juce::Justification::centred);
        };

        zoneText("PARAM", paramStartX, PARAM_AREA_W, SpaceColours::textMid);
        zoneText("MIX", volX, KNOB_W + PAD + PAN_KSZ, SpaceColours::neonPurple);
        zoneText("M/S", msX - 1, MS_BTN_W + 2, SpaceColours::neonYellow);
        zoneText("EDIT", editX - 1, BTN_W + 2, SpaceColours::neonCyan);
        zoneText("CLIP", cpX - 1, BTN_W + 2, SpaceColours::neonPurple);
        zoneText("FEEL", varX - 1, VAR_BTN_W + 2, SpaceColours::neonOrange);
        zoneText("LEN", lenX - 1, LEN_BTN_W + PAD + PAGE_BTN_W * 2 + 6, SpaceColours::textMid);

        auto guide = [&](int x, juce::Colour col, float alpha)
        {
            g.setColour(col.withAlpha(alpha));
            g.drawVerticalLine(x, static_cast<float>(HEADER_H - 8),
                               static_cast<float>(rowY(NUM_CH - 1) + ROW_H));
            g.setColour(juce::Colours::black.withAlpha(0.18f));
            g.drawVerticalLine(x + 1, static_cast<float>(HEADER_H - 8),
                               static_cast<float>(rowY(NUM_CH - 1) + ROW_H));
        };

        guide(stepEndX + 1, SpaceColours::sectionLine, 0.28f);
        guide(paramStartX + PARAM_AREA_W + PAD / 2, SpaceColours::gridLine.brighter(0.30f), 0.42f);
        guide(panX + PAN_KSZ + PAD / 2, SpaceColours::sectionLine, 0.20f);
        guide(lenX - PAD / 2, SpaceColours::gridLine.brighter(0.25f), 0.36f);
    }

    // Swing label (positioned cleanly above MIX, zero overlap with LEN)
    {
        const int volX = PAD + LABEL_W + STEPS_AREA_W + PAD + PARAM_AREA_W + PAD;
        g.setColour(SpaceColours::neonCyan.withAlpha(0.75f));
        g.setFont(SpaceArt::monoFont(8.5f, true));
        g.drawText("SW", volX, 3, 22, 14, juce::Justification::centredRight);
    }

    // BPM + Bar counter (single clean readout above PARAM zone — zero overlap with beat markers or PLAY button)
    {
        const float bpm = bpmRef.load(std::memory_order_relaxed);
        const int64_t bar = sequencer.currentBar.load(std::memory_order_relaxed);
        const int barIn64 = static_cast<int>((bar < 0 ? 0 : bar) % 64) + 1;
        const bool isHost = hostPlayingRef.load(std::memory_order_relaxed);
        const int displayX = PAD + LABEL_W + STEPS_AREA_W + PAD;
        const int displayW = PARAM_AREA_W;
        const int displayY = 1;
        const float displayH = 17.0f;

        auto readout = juce::Rectangle<float>(static_cast<float>(displayX), static_cast<float>(displayY),
                                              static_cast<float>(displayW), displayH);
        g.setColour(SpaceColours::groupBg.withAlpha(0.82f));
        g.fillRoundedRectangle(readout, 3.0f);
        g.setColour((playing ? SpaceColours::neonGreen : SpaceColours::neonRed).withAlpha(playing ? 0.45f : 0.25f));
        g.drawRoundedRectangle(readout, 3.0f, 0.9f);

        const float pulse = playing
            ? (0.5f + 0.5f * std::sin(static_cast<float>(nowMs) * 0.008f))
            : 0.0f;
        g.setColour((playing ? SpaceColours::neonGreen : SpaceColours::neonRed)
                        .withAlpha(playing ? (0.55f + pulse * 0.35f) : 0.34f));
        g.fillRoundedRectangle(static_cast<float>(displayX + 6), static_cast<float>(displayY + 5),
                               7.0f, 7.0f, 1.5f);

        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.setColour(SpaceColours::textBright.withAlpha(0.94f));
        g.drawText(juce::String(static_cast<int>(std::round(bpm))) + " BPM",
                   displayX + 18, displayY + 1, 64, 15, juce::Justification::centredLeft);

        g.setColour((playing ? SpaceColours::neonGreen : SpaceColours::textDim).withAlpha(0.92f));
        g.drawText("BAR " + juce::String(barIn64) + "/64",
                   displayX + 86, displayY + 1, 82, 15, juce::Justification::centredLeft);

        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.setColour(playing ? (isHost ? SpaceColours::neonCyan : SpaceColours::neonGreen)
                            : SpaceColours::textDim.withAlpha(0.70f));
        g.drawText(playing ? (isHost ? "DAW" : "INT") : "STOP",
                   displayX + displayW - 44, displayY + 1, 38, 15, juce::Justification::centredRight);
    }
    // ── Header bottom fade (smooth transition into grid) ──
    {
        juce::ColourGradient fade(SpaceColours::bg.withAlpha(0.5f), 0.0f, static_cast<float>(HEADER_H - 6),
                                   juce::Colours::transparentBlack, 0.0f, static_cast<float>(HEADER_H + 4), false);
        g.setGradientFill(fade);
        g.fillRect(0, HEADER_H - 6, getWidth(), 10);
    }

    // ── Channel rows ──
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const int ry  = rowY(ch);
        const bool isBass = ch >= 6;
        const bool muted = sequencer.mute[idx(ch)].load(std::memory_order_relaxed);
        const bool soloed = sequencer.solo[idx(ch)].load(std::memory_order_relaxed);
        const int activeStep = sequencer.currentSteps[idx(ch)].load(std::memory_order_acquire);
        const bool focused = playing && activeStep >= 0 && !muted;
        const juce::Colour base = SpaceColours::channelColor(ch);
        const juce::Colour hotBase = base.withMultipliedSaturation(1.20f)
                                         .withMultipliedBrightness(1.08f + sustained * 0.18f);

        // Per-channel fire glow (decays ~200ms after each trigger)
        float fireGlow = 0.0f;
        if (!muted)
        {
            const int64_t lastFire = visualState.lastFireSample[idx(ch)].load(std::memory_order_relaxed);
            const float   lastVel  = visualState.lastFireVel[idx(ch)].load(std::memory_order_relaxed);
            const int64_t dt = visNow - lastFire;
            if (lastFire > INT64_MIN + 1 && dt >= 0 && dt < 96000)
                fireGlow = std::exp(-static_cast<float>(dt) / 8000.0f) * lastVel;
        }

        // ── Weathered dark-iron rack module: -70% background saturation so ONLY LEDs & steps pop ──
        {
            const auto rowF = juce::Rectangle<float>(
                0.0f, static_cast<float>(ry),
                static_cast<float>(getWidth()), static_cast<float>(ROW_H));

            juce::Colour topCol = (ch % 2 == 0) ? SpaceColours::rowBg.brighter(0.06f)
                                                : SpaceColours::rowBgAlt.brighter(0.05f);
            juce::Colour botCol = (ch % 2 == 0) ? SpaceColours::rowBg.darker(0.14f)
                                                : SpaceColours::rowBgAlt.darker(0.16f);
            if (muted)
            {
                topCol = SpaceColours::rowBgMuted;
                botCol = SpaceColours::rowBgMuted.darker(0.10f);
            }
            else if (fireGlow > 0.01f)
            {
                // Restrained hardware bounce (never washes out the dark iron panel)
                topCol = topCol.interpolatedWith(hotBase, fireGlow * 0.09f);
                botCol = botCol.interpolatedWith(hotBase.darker(0.4f), fireGlow * 0.05f);
            }

            juce::ColourGradient panelGrad(topCol, 0.0f, static_cast<float>(ry),
                                           botCol, 0.0f, static_cast<float>(ry + ROW_H), false);
            g.setGradientFill(panelGrad);
            g.fillRect(rowF);

            // Very subtle left-edge track tint (only 2.5% alpha)
            juce::ColourGradient chanTint(hotBase.withAlpha(muted ? 0.005f : 0.024f + fireGlow * 0.030f),
                                          0.0f, static_cast<float>(ry + ROW_H / 2),
                                          hotBase.withAlpha(0.0f),
                                          140.0f,
                                          static_cast<float>(ry + ROW_H / 2), false);
            g.setGradientFill(chanTint);
            g.fillRect(rowF);

            // Top worn-metal edge highlight + Hover Spotlight (+18% border glow on active hover track)
            const bool isHovered = (hoveredChannel == ch);
            g.setColour(isHovered ? hotBase.withAlpha(0.32f) : juce::Colours::white.withAlpha(0.07f));
            g.drawHorizontalLine(ry, 0.0f, static_cast<float>(getWidth()));
            g.setColour(isHovered ? hotBase.withAlpha(0.22f) : juce::Colours::black.withAlpha(0.55f));
            g.drawHorizontalLine(ry + ROW_H - 1, 0.0f, static_cast<float>(getWidth()));
            if (isHovered)
            {
                g.setColour(hotBase.withAlpha(0.035f));
                g.fillRect(rowF);
            }
        }

        if (muted || soloed)
        {
            const auto statusCol = muted ? SpaceColours::neonRed : SpaceColours::neonYellow;
            g.setColour(statusCol.withAlpha(muted ? 0.38f : 0.50f));
            g.fillRoundedRectangle(8.0f, static_cast<float>(ry + 8),
                                   6.0f, static_cast<float>(ROW_H - 16), 2.0f);
            g.setColour(statusCol.withAlpha(0.24f));
            g.drawRoundedRectangle(7.0f, static_cast<float>(ry + 7),
                                   8.0f, static_cast<float>(ROW_H - 14), 3.0f, 0.8f);
        }

        // Channel color strip (rack "handle") — lights up on fire
        const float stripAlpha = muted ? 0.15f
                                       : std::min(1.0f, 0.55f + fireGlow * 0.55f);
        g.setColour(hotBase.withAlpha(stripAlpha));
        g.fillRect(2, ry + 2, fireGlow > 0.5f ? 3 : 2, ROW_H - 4);
        if (!muted)
        {
            g.setColour(hotBase.withAlpha(0.10f + fireGlow * 0.18f));
            g.fillRect(5, ry + 3, 1, ROW_H - 6);
        }

        // Arcade colour tag behind the channel name — vertically centred with label text.
        {
            const float tagY = static_cast<float>(ry + (ROW_H - 22) / 2);
            auto tag = juce::Rectangle<float>(static_cast<float>(PAD + 2), tagY,
                                              static_cast<float>(LABEL_W - PAD - 6), 22.0f);
            g.setColour(hotBase.withAlpha(muted ? 0.05f : 0.16f + fireGlow * 0.22f));
            g.fillRoundedRectangle(tag, 3.0f);
            g.setColour(hotBase.withAlpha(muted ? 0.18f : 0.58f + fireGlow * 0.35f));
            g.drawRoundedRectangle(tag, 3.0f, 1.0f);
        }

        // ── 4 corner screws (Phillips heads, ~5px) ──
        {
            auto drawScrew = [&g](float cx, float cy)
            {
                // Recessed dark hole
                g.setColour(juce::Colours::black.withAlpha(0.75f));
                g.fillEllipse(cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
                // Metal screw head
                g.setColour(juce::Colour(0xff45454a));
                g.fillEllipse(cx - 1.8f, cy - 1.8f, 3.6f, 3.6f);
                // Phillips slot
                g.setColour(juce::Colours::black.withAlpha(0.60f));
                g.drawLine(cx - 1.3f, cy, cx + 1.3f, cy, 0.6f);
                g.drawLine(cx, cy - 1.3f, cx, cy + 1.3f, 0.6f);
                // Top-left highlight
                g.setColour(juce::Colours::white.withAlpha(0.22f));
                g.fillEllipse(cx - 1.0f, cy - 1.1f, 1.2f, 1.2f);
            };
            const float xL = 5.0f;
            const float xR = static_cast<float>(getWidth() - 6);
            const float yT = static_cast<float>(ry + 4);
            const float yB = static_cast<float>(ry + ROW_H - 5);
            drawScrew(xL, yT);
            drawScrew(xR, yT);
            drawScrew(xL, yB);
            drawScrew(xR, yB);
        }

        // Drum/Bass section divider (above ch 6)
        if (ch == 6)
        {
            g.setColour(SpaceColours::sectionLine.withAlpha(0.30f));
            g.drawHorizontalLine(ry - 1, 0.0f, static_cast<float>(getWidth()));
            g.setColour(SpaceColours::sectionLine.withAlpha(0.10f));
            g.drawHorizontalLine(ry - 2, 0.0f, static_cast<float>(getWidth()));
        }
        else
        {
            // Subtle row separator
            g.setColour(SpaceColours::gridLine);
            g.drawHorizontalLine(ry + ROW_H - 1, 0.0f, static_cast<float>(getWidth()));
        }

        // Channel label — centred inside badge, monospace bold
        g.setColour(muted ? hotBase.withAlpha(0.28f) : hotBase.brighter(fireGlow * 0.35f));
        g.setFont(SpaceArt::monoFont(11.0f, true));
        g.drawText(kChannelNames[ch], PAD + 2, ry, LABEL_W - PAD - 6, ROW_H,
                   juce::Justification::centred);

        // AutoVar indicator (yellow dot when active)
        if (sequencer.autoVar[idx(ch)].load(std::memory_order_relaxed))
        {
            g.setColour(SpaceColours::neonYellow.withAlpha(0.9f));
            g.fillEllipse(static_cast<float>(PAD + 40), static_cast<float>(ry + ROW_H / 2 - 2), 5.0f, 5.0f);
        }

        // Bass note display (show current MIDI note)
        if (isBass)
        {
            static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            const int bassIdx = ch - 6;
            const int note = voiceBank.getBassNote(bassIdx);
            const int octave = (note / 12) - 1;
            const int noteName = note % 12;
            g.setColour(SpaceColours::neonOrange.withAlpha(0.6f));
            g.setFont(8.0f);
            g.drawText(juce::String(noteNames[noteName]) + juce::String(octave),
                       PAD, ry + ROW_H - 12, LABEL_W - PAD, 10,
                       juce::Justification::centredLeft);
        }

        // Page indicator (if multi-page)
        {
            const int len = sequencer.getPatternLength(ch);
            if (len > VISIBLE_STEPS)
            {
                g.setColour(hotBase.withAlpha(0.42f));
                g.setFont(7.5f);
                const juce::String pg = juce::String(viewPage[idx(ch)] + 1)
                                      + "/" + juce::String(len / VISIBLE_STEPS);
                g.drawText(pg, PAD, ry + ROW_H - 12, LABEL_W - PAD, 10,
                           juce::Justification::centredLeft);
            }
        }

        // ── Steps ──
        const int curStep = activeStep;
        const int pageBase = viewPage[idx(ch)] * VISIBLE_STEPS;

        const int chLen = sequencer.getPatternLength(ch);
        for (int vs = 0; vs < VISIBLE_STEPS; ++vs)
        {
            const int absStep = pageBase + vs;
            const bool outOfLen  = (absStep >= chLen);
            const bool on        = !outOfLen && sequencer.getStep(ch, absStep);
            const bool isCurrent = !outOfLen && playing && (absStep == curStep);
            const int  trailDist = (!outOfLen && curStep >= 0)
                ? ((curStep - absStep + chLen) % chLen)
                : 999;
            const bool isTrail   = (playing && trailDist >= 1 && trailDist <= 4);
            const float trailFade = isTrail ? (5.0f - static_cast<float>(trailDist)) / 5.0f : 0.0f;
            const auto r         = stepRect(ch, vs);

            if (outOfLen)
            {
                // Visually darken steps beyond the polyrhythmic loop length
                g.setColour(juce::Colour(0xff06070a));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(juce::Colours::white.withAlpha(0.04f));
                g.drawLine(static_cast<float>(r.getX() + 3), static_cast<float>(r.getBottom() - 3),
                           static_cast<float>(r.getRight() - 3), static_cast<float>(r.getY() + 3), 0.8f);
                continue;
            }

            const float ledPersist = stepLedGlow[idx(ch)][static_cast<size_t>(vs)];

            // Ambient Light Bleed (2.5-4% colored reflection on adjacent dark metal bezel)
            if (on || isCurrent || ledPersist > 0.08f)
            {
                const float bleedA = (isCurrent ? 0.045f : (on ? 0.025f : 0.0f)) + ledPersist * 0.035f;
                g.setColour(hotBase.withAlpha(std::min(0.06f, bleedA)));
                g.fillRoundedRectangle(r.toFloat().expanded(3.0f, 5.0f), 5.0f);
            }

            // Beat grouping: subtle bg shade every 4 steps
            if (!on && !isCurrent)
            {
                const int beatGroup = (vs / 4) % 2;
                if (beatGroup == 1)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.012f));
                    g.fillRect(r.getX() - 1, ry, STEP_W, ROW_H);
                }
                if (ledPersist > 0.04f)
                {
                    // Optical LED falloff persistence (~85 ms) after playhead leaves pad
                    g.setColour(hotBase.withAlpha(ledPersist * 0.24f));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                }
            }

            if (on || isCurrent)
            {
                // Probability / micro-timing visualisation factors
                const uint8_t prob = on ? sequencer.getProbability(ch, absStep) : 100;
                const float   probFactor = on ? (0.18f + 0.82f * (prob / 100.0f)) : 1.0f;
                const int8_t  mt = on ? sequencer.getMicroTiming(ch, absStep) : 0;
                const int     mtShift = (static_cast<int>(mt) * (STEP_W * 4 / 10)) / 50;  // ±~40% step

                // Background glow for current step
                if (isCurrent)
                {
                    g.setColour(SpaceColours::neonMagenta.withAlpha(0.08f));
                    g.fillRoundedRectangle(r.toFloat().expanded(3.0f), 4.0f);
                    g.setColour(SpaceColours::neonMagenta.withAlpha(0.18f));
                    g.fillRoundedRectangle(r.toFloat().expanded(1.0f), 3.0f);
                }
                else
                {
                    g.setColour(hotBase.withAlpha(muted ? 0.04f : 0.12f));
                }
                g.fillRoundedRectangle(r.toFloat(), 2.0f);

                // Velocity fill (shifted by microtiming)
                const float velFrac = on ? (sequencer.getVelocity(ch, absStep) / 127.0f) : 0.f;
                const int fillH = std::max(2, static_cast<int>(r.getHeight() * velFrac));
                const auto filled = r.withTop(r.getBottom() - fillH).translated(mtShift, 0);

                if (isCurrent)
                {
                    // Vertical 22% laser beam scanning down the active step column
                    g.setColour(hotBase.withAlpha(0.14f));
                    g.fillRect(r.getCentreX() - 1, ry + 1, 2, ROW_H - 2);
                    // Crisp hardware LED playhead: hot-spot white centre + sharp track-colour bezel
                    g.setColour(hotBase.withAlpha(0.22f));
                    g.fillRoundedRectangle(r.toFloat().expanded(2.0f), 3.0f);
                    if (on)
                    {
                        g.setColour(hotBase.brighter(0.35f).withMultipliedAlpha(probFactor));
                        g.fillRoundedRectangle(filled.toFloat(), 2.5f);
                        const auto core = filled.toFloat().reduced(5.0f, 5.0f);
                        if (core.getWidth() > 2.0f && core.getHeight() > 2.0f)
                        {
                            g.setColour(juce::Colours::white.withAlpha(0.86f));
                            g.fillRoundedRectangle(core, 1.5f);
                        }
                    }
                    else
                    {
                        g.setColour(hotBase.withAlpha(0.18f));
                        g.fillRoundedRectangle(r.toFloat(), 2.5f);
                        g.setColour(juce::Colours::white.withAlpha(0.60f));
                        g.fillEllipse(static_cast<float>(r.getCentreX() - 2), static_cast<float>(r.getCentreY() - 2), 4.0f, 4.0f);
                    }
                    g.setColour(juce::Colours::white.withAlpha(0.95f));
                    g.drawRoundedRectangle(r.toFloat(), 2.5f, 1.5f);
                }
                else
                {
                    // Hardware backlit translucent rubber pad (radial internal LED glow + bottom velocity bar)
                    const auto rf = r.toFloat();
                    juce::ColourGradient padGlow(hotBase.brighter(0.35f).withMultipliedAlpha(probFactor),
                                                 rf.getCentreX(), rf.getCentreY(),
                                                 hotBase.darker(0.28f).withMultipliedAlpha(probFactor),
                                                 rf.getX(), rf.getBottom(), true);
                    g.setGradientFill(padGlow);
                    g.fillRoundedRectangle(rf, 2.5f);

                    // Velocity accent bar along bottom of pad
                    const float velW = std::max(4.0f, (rf.getWidth() - 4.0f) * velFrac);
                    g.setColour(juce::Colours::white.withAlpha(0.62f * probFactor));
                    g.fillRect(rf.getX() + 2.0f, rf.getBottom() - 3.5f, velW, 2.0f);

                    // Top rubber bezel highlight + crisp border
                    g.setColour(juce::Colours::white.withAlpha(0.22f * probFactor));
                    g.drawHorizontalLine(r.getY() + 1, rf.getX() + 2.0f, rf.getRight() - 2.0f);
                    g.setColour(hotBase.withAlpha(0.75f));
                    g.drawRoundedRectangle(rf, 2.5f, 0.9f);
                }
            }
            else
            {
                // TR-909 4-step checkerboard matte rubber pad (recessed with 1px top grazing light)
                const auto rf = r.toFloat();
                const bool groupA = ((vs / 4) % 2) == 0;
                g.setColour(groupA ? SpaceColours::gridCell : SpaceColours::gridCellAlt);
                g.fillRoundedRectangle(rf, 2.5f);
                g.setColour(juce::Colours::white.withAlpha((vs % 4 == 0) ? 0.11f : 0.05f));
                g.drawHorizontalLine(r.getY() + 1, rf.getX() + 2.0f, rf.getRight() - 2.0f);
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.drawRoundedRectangle(rf, 2.5f, 0.8f);
            }

            // Playhead trail overlay — comet tail behind the playhead
            if (isTrail)
            {
                const auto trailCol = hotBase.interpolatedWith(SpaceColours::neonMagenta, 0.35f);
                g.setColour(trailCol.withAlpha(trailFade * 0.17f));
                g.fillRoundedRectangle(r.toFloat().expanded(trailFade * 5.0f, trailFade * 1.5f), 3.0f);
                g.setColour(trailCol.withAlpha(trailFade * 0.45f));
                g.drawHorizontalLine(r.getCentreY(), static_cast<float>(r.getX() - static_cast<int>(trailFade * 9.0f)),
                                     static_cast<float>(r.getRight()));
                // Extra inner glow on filled-and-trailed cells
                if (on)
                {
                    g.setColour(juce::Colours::white.withAlpha(trailFade * 0.10f));
                    g.fillRoundedRectangle(r.toFloat(), 2.0f);
                }
            }
        }
        // ── Intentional vertical guides: step field, sound controls, channel actions ──
        {
            const int sepX = PAD + LABEL_W + STEPS_AREA_W + 1;
            g.setColour(SpaceColours::sectionLine.withAlpha(0.18f));
            g.drawVerticalLine(sepX, static_cast<float>(ry + 2), static_cast<float>(ry + ROW_H - 2));
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.drawVerticalLine(sepX + 1, static_cast<float>(ry + 2), static_cast<float>(ry + ROW_H - 2));

            const int paramStartX = PAD + LABEL_W + STEPS_AREA_W + PAD;
            const int mixSepX = paramStartX + PARAM_AREA_W + PAD / 2;
            g.setColour(SpaceColours::gridLine.withAlpha(0.48f));
            g.drawVerticalLine(mixSepX, static_cast<float>(ry + 7), static_cast<float>(ry + ROW_H - 8));

            const int volX = paramStartX + PARAM_AREA_W + PAD;
            const int panX = volX + KNOB_W + PAD;
            const int actionSepX = panX + PAN_KSZ + PAD / 2;
            g.setColour(SpaceColours::sectionLine.withAlpha(0.14f));
            g.drawVerticalLine(actionSepX, static_cast<float>(ry + 6), static_cast<float>(ry + ROW_H - 7));

            const int cpX = actionSepX + PAD / 2 + MS_BTN_W + PAD + BTN_W + PAD;
            g.setColour(SpaceColours::gridLine.withAlpha(0.38f));
            g.drawVerticalLine(cpX - PAD / 2, static_cast<float>(ry + 10), static_cast<float>(ry + ROW_H - 11));
        }

        // Button group beds are drawn by intent, not by widget ownership: performance,
        // edit, copy, variation, and pattern navigation read as separate tools.
        {
            const int midY = ry + ROW_H / 2;
            const int paramStartX = PAD + LABEL_W + STEPS_AREA_W + PAD;
            const int volX = paramStartX + PARAM_AREA_W + PAD;
            const int panX = volX + KNOB_W + PAD;
            const int msX = panX + PAN_KSZ + PAD;
            const int btnX = msX + MS_BTN_W + PAD;
            const int cpX = btnX + BTN_W + PAD;
            const int varX = cpX + BTN_W + PAD;
            const int lenX = varX + VAR_BTN_W + PAD;
            const int topY = midY - BTN_H - 3;
            const int groupH = BTN_H * 2 + 6;

            auto drawGroup = [&](int x, int width, juce::Colour, bool)
            {
                auto rr = juce::Rectangle<float>(
                    static_cast<float>(x - 2),
                    static_cast<float>(topY),
                    static_cast<float>(width + 4),
                    static_cast<float>(groupH));
                // Clean matte recessed chassis bay (no neon border clutter on inactive buttons)
                g.setColour(juce::Colour(0xff080a0e).withAlpha(muted ? 0.35f : 0.78f));
                g.fillRoundedRectangle(rr, 3.0f);
                g.setColour(juce::Colours::white.withAlpha(0.04f));
                g.drawRoundedRectangle(rr, 3.0f, 0.6f);
            };

            drawGroup(msX, MS_BTN_W, SpaceColours::neonYellow, true);
            drawGroup(btnX, BTN_W, SpaceColours::neonCyan, false);
            drawGroup(cpX, BTN_W, SpaceColours::neonPurple, false);
            drawGroup(varX, VAR_BTN_W, SpaceColours::neonOrange, false);

            auto navR = juce::Rectangle<float>(
                static_cast<float>(lenX - 2),
                static_cast<float>(midY - BTN_H / 2 - 3),
                static_cast<float>(LEN_BTN_W + PAD + PAGE_BTN_W * 2 + 6),
                static_cast<float>(BTN_H + 6));
            g.setColour(SpaceColours::groupBg.withAlpha(muted ? 0.30f : 0.58f));
            g.fillRoundedRectangle(navR, 3.0f);
            g.setColour(SpaceColours::neonCyan.withAlpha(muted ? 0.08f : 0.18f));
            g.drawRoundedRectangle(navR, 3.0f, 0.7f);
        }

        // ── Param knob labels + MIX VOL/PAN labels + drum synth schematic filler ──
        {
            const int paramStartX = PAD + LABEL_W + STEPS_AREA_W + PAD;
            g.setFont(SpaceArt::monoFont(8.0f, false));
            g.setColour(muted ? SpaceColours::textDim.withAlpha(0.40f)
                              : SpaceColours::textMid.withAlpha(0.88f));

            const int pCount = channelParams[idx(ch)].count;
            for (int p = 0; p < pCount; ++p)
            {
                const int kx = paramStartX + p * (PKNOB_SZ + PKNOB_GAP);
                g.drawText(channelParams[idx(ch)].entries[idx(p)].label,
                           kx, ry + ROW_H - 11, PKNOB_SZ, 10,
                           juce::Justification::centred);
            }

            // Fill unused parameter slots (SNARE/HATS/TOM/PERC1/PERC2) with a hardware silkscreen
            // transient/envelope schematic so there are zero "black holes" on the panel.
            if (pCount < MAX_PKNOBS)
            {
                const int fx = paramStartX + pCount * (PKNOB_SZ + PKNOB_GAP) + 3;
                const int fw = (paramStartX + PARAM_AREA_W) - fx - 3;
                if (fw > 32)
                {
                    const auto box = juce::Rectangle<float>(static_cast<float>(fx), static_cast<float>(ry + 8),
                                                            static_cast<float>(fw), static_cast<float>(ROW_H - 20));
                    SpaceArt::drawCrtBezel(g, box, hotBase.withAlpha(0.45f + fireGlow * 0.55f), true);

                    // Live parameter-driven envelope curve that flashes bright phosphor on each drum hit!
                    const float decayNorm = (pCount >= 2 && channelParams[idx(ch)].entries[1].knob)
                        ? static_cast<float>(channelParams[idx(ch)].entries[1].knob->valueToProportionOfLength(
                              channelParams[idx(ch)].entries[1].knob->getValue()))
                        : 0.4f;
                    juce::Path env;
                    const float x0 = box.getX() + 5.0f;
                    const float x1 = box.getRight() - 5.0f;
                    const float yb = box.getBottom() - 4.0f;
                    const float yt = box.getY() + 11.0f;
                    env.startNewSubPath(x0, yb);
                    env.lineTo(x0 + 3.5f, yt);
                    const float decayK = 1.6f + (1.0f - decayNorm) * 6.5f;
                    for (int k = 1; k <= 18; ++k)
                    {
                        const float t = static_cast<float>(k) / 18.0f;
                        const float px = x0 + 3.5f + t * (x1 - x0 - 3.5f);
                        const float py = yb - (yb - yt) * std::exp(-t * decayK);
                        env.lineTo(px, py);
                    }
                    if (fireGlow > 0.05f)
                    {
                        g.setColour(hotBase.withAlpha(fireGlow * 0.45f));
                        g.strokePath(env, juce::PathStrokeType(4.0f));
                    }
                    g.setColour(hotBase.interpolatedWith(juce::Colours::white, fireGlow * 0.65f));
                    g.strokePath(env, juce::PathStrokeType(1.3f + fireGlow * 0.6f));

                    static const char* kCircuitTags[6] = {
                        "SUB // 909", "SNAP // NOISE", "6-OSC // 808", "SWEEP // TOM", "RING // FM", "NOISE // PRC"
                    };
                    g.setFont(SpaceArt::monoFont(7.0f, true));
                    g.setColour(hotBase.withAlpha(0.65f + fireGlow * 0.35f));
                    g.drawText(kCircuitTags[std::clamp(ch, 0, 5)],
                               static_cast<int>(box.getX()) + 4, static_cast<int>(box.getY()) + 1,
                               static_cast<int>(box.getWidth()) - 8, 9, juce::Justification::centredLeft);
                }
            }

            // Explicit VOL / PAN labels under the MIX column knobs
            const int volX = paramStartX + PARAM_AREA_W + PAD;
            const int panX = volX + KNOB_W + PAD;
            g.setFont(SpaceArt::monoFont(7.5f, true));
            g.setColour(muted ? SpaceColours::textDim.withAlpha(0.40f)
                              : SpaceColours::textMid.withAlpha(0.82f));
            g.drawText("VOL", volX, ry + ROW_H - 11, KNOB_W, 10, juce::Justification::centred);
            g.drawText("PAN", panX, ry + ROW_H - 11, PAN_KSZ, 10, juce::Justification::centred);
        }

        // ── 303 sub-rows: Note + Slide + Accent/Octave (bass channels only) ──
        if (isBass)
        {
            static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            const int bi = ch - 6;
            const int subY0 = ry + ROW_H;          // Note pitch row
            const int subY1 = subY0 + SUB_ROW_H;   // Slide row
            const int subY2 = subY1 + SUB_ROW_H;   // Accent/Octave row
            const int bassPageBase = viewPage[idx(ch)] * VISIBLE_STEPS;

            // Sub-row labels
            g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
            g.setColour(SpaceColours::neonCyan.withAlpha(0.70f));
            g.drawText("NOTE", PAD + 3, subY0, LABEL_W - PAD, SUB_ROW_H, juce::Justification::centredLeft);
            g.setColour(SpaceColours::neonOrange.withAlpha(0.55f));
            g.drawText("SLD", PAD + 3, subY1, LABEL_W - PAD, SUB_ROW_H, juce::Justification::centredLeft);
            g.setColour(SpaceColours::neonMagenta.withAlpha(0.55f));
            g.drawText("ACC", PAD + 3, subY2, LABEL_W - PAD, SUB_ROW_H, juce::Justification::centredLeft);

            // Sub-row backgrounds (darker than main row) with top separators
            g.setColour(SpaceColours::sectionLine.withAlpha(0.20f));
            g.drawHorizontalLine(subY0, 0.0f, static_cast<float>(getWidth()));
            g.setColour(SpaceColours::subRowBg);
            g.fillRect(PAD + LABEL_W, subY0 + 1, STEPS_AREA_W, SUB_ROW_H - 1);
            g.setColour(SpaceColours::gridLine.withAlpha(0.55f));
            g.drawHorizontalLine(subY1, static_cast<float>(PAD + LABEL_W), static_cast<float>(PAD + LABEL_W + STEPS_AREA_W));
            g.setColour(SpaceColours::subRowBg);
            g.fillRect(PAD + LABEL_W, subY1 + 1, STEPS_AREA_W, SUB_ROW_H - 1);
            g.setColour(SpaceColours::gridLine.withAlpha(0.55f));
            g.drawHorizontalLine(subY2, static_cast<float>(PAD + LABEL_W), static_cast<float>(PAD + LABEL_W + STEPS_AREA_W));
            g.setColour(SpaceColours::subRowBg);
            g.fillRect(PAD + LABEL_W, subY2 + 1, STEPS_AREA_W, SUB_ROW_H - 1);

            // Channel color strip extends through sub-rows
            g.setColour(base.withAlpha(muted ? 0.1f : 0.35f));
            g.fillRect(0, subY0, 2, SUB_ROW_H * 3);

            for (int vs = 0; vs < VISIBLE_STEPS; ++vs)
            {
                const int absStep = bassPageBase + vs;
                const int sx = stepX(vs);
                const int cw = STEP_W - 2, ch2 = SUB_ROW_H - 3;
                const bool stepOn = sequencer.getStep(ch, absStep);
                const bool accent = sequencer.getBassAccent(bi, absStep);
                const bool octUp  = sequencer.getBassOctUp(bi, absStep);
                const bool octDn  = sequencer.getBassOctDown(bi, absStep);

                // Note pitch cell
                const int offset = static_cast<int>(sequencer.getBassNoteOffset(bi, absStep));
                const int midiNote = std::clamp(48 + offset + (octUp ? 12 : 0) - (octDn ? 12 : 0), 0, 127);
                const juce::String noteTxt = juce::String(noteNames[midiNote % 12]) + juce::String((midiNote / 12) - 1);
                const bool draggingThis = (dragMode == DragMode::BassNote && dragCh == ch && dragStep == absStep);

                juce::Colour noteBg = ((vs / 4) % 2 == 0) ? SpaceColours::gridCell.darker(0.25f)
                                                          : SpaceColours::gridCellAlt.darker(0.20f);
                if (draggingThis)
                    noteBg = SpaceColours::neonCyan.withAlpha(0.45f);
                else if (stepOn)
                    noteBg = (offset != 0 ? SpaceColours::neonCyan : hotBase).withAlpha(muted ? 0.08f : 0.24f);

                g.setColour(noteBg);
                g.fillRoundedRectangle(static_cast<float>(sx + 1), static_cast<float>(subY0 + 1),
                                       static_cast<float>(cw), static_cast<float>(ch2), 2.0f);
                if (draggingThis || (stepOn && offset != 0))
                {
                    g.setColour(SpaceColours::neonCyan.withAlpha(draggingThis ? 0.90f : 0.48f));
                    g.drawRoundedRectangle(static_cast<float>(sx + 1), static_cast<float>(subY0 + 1),
                                           static_cast<float>(cw), static_cast<float>(ch2), 2.0f, 0.8f);
                }

                g.setFont(SpaceArt::monoFont(8.5f, stepOn));
                g.setColour(draggingThis
                    ? juce::Colours::white
                    : (stepOn ? SpaceColours::textBright.withAlpha(muted ? 0.35f : 0.95f)
                              : SpaceColours::textDim.withAlpha(0.38f)));
                g.drawText(noteTxt, sx + 1, subY0 + 1, cw, ch2, juce::Justification::centred);

                // Slide — glowing tie curve that PHYSICALLY BRIDGES across the border into the next step!
                const bool slide = sequencer.getBassSlide(bi, absStep);
                if (slide)
                {
                    const auto sldCol = SpaceColours::themed(SpaceColours::neonOrange);
                    g.setColour(sldCol.withAlpha(0.22f));
                    g.fillRoundedRectangle(static_cast<float>(sx + 1), static_cast<float>(subY1 + 1),
                                           static_cast<float>(cw), static_cast<float>(ch2), 2.0f);

                    const float xStart = static_cast<float>(sx + cw / 2);
                    const float xNext  = (vs + 1 < VISIBLE_STEPS)
                        ? static_cast<float>(stepX(vs + 1) + 7)
                        : static_cast<float>(sx + cw + 4);
                    const float yMid   = static_cast<float>(subY1 + SUB_ROW_H / 2);

                    juce::Path tie;
                    tie.startNewSubPath(xStart, yMid + 1.5f);
                    tie.quadraticTo((xStart + xNext) * 0.5f, yMid - 6.0f, xNext, yMid + 1.5f);
                    g.setColour(sldCol.withAlpha(0.32f));
                    g.strokePath(tie, juce::PathStrokeType(4.0f));
                    g.setColour(sldCol);
                    g.strokePath(tie, juce::PathStrokeType(1.8f));
                    // Origin dot + arrival arrowhead in next cell
                    g.fillEllipse(xStart - 2.2f, yMid - 0.7f, 4.4f, 4.4f);
                    g.fillEllipse(xNext  - 1.8f, yMid - 0.3f, 3.6f, 3.6f);
                }
                else
                {
                    g.setColour(SpaceColours::gridCellAlt.darker(0.25f));
                    g.fillRoundedRectangle(static_cast<float>(sx + 1), static_cast<float>(subY1 + 1),
                                           static_cast<float>(cw), static_cast<float>(ch2), 2.0f);
                }

                // Accent + Octave indicators
                juce::Colour accCol = SpaceColours::gridCell.darker(0.2f);
                if (accent) accCol = SpaceColours::neonMagenta.withAlpha(0.85f);
                else if (octUp) accCol = SpaceColours::neonCyan.withAlpha(0.85f);
                else if (octDn) accCol = SpaceColours::neonPurple.withAlpha(0.85f);

                g.setColour(accCol);
                g.fillRoundedRectangle(static_cast<float>(sx + 1), static_cast<float>(subY2 + 1),
                                       static_cast<float>(cw), static_cast<float>(ch2), 2.0f);
                if (accent)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.30f));
                    g.fillEllipse(static_cast<float>(sx + cw / 2 - 2), static_cast<float>(subY2 + 5), 4.0f, 4.0f);
                }

                // Octave arrows on accent row
                if (octUp || octDn)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.8f));
                    g.setFont(8.0f);
                    g.drawText(octUp ? juce::CharPointer_UTF8("\xe2\x96\xb2") : juce::CharPointer_UTF8("\xe2\x96\xbc"),
                               sx + 1, subY2 + 1, cw, ch2, juce::Justification::centred);
                }
            }

            // ── Right-side visual panels (occupy previously dead space under BASS1 & BASS2 knobs) ──
            {
                const int panelX = PAD + LABEL_W + STEPS_AREA_W + PAD;
                const int panelW = getWidth() - panelX - PAD;
                const int panelY = subY0 + 3;
                const int panelH = BASS_EXTRA - 6;
                const auto panelR = juce::Rectangle<float>(static_cast<float>(panelX), static_cast<float>(panelY),
                                                           static_cast<float>(panelW), static_cast<float>(panelH));

                const bool fillArmed = (sequencer.fillStartBar.load(std::memory_order_relaxed) >= 0);
                const bool crtSpot   = (hoveredChannel == (6 + bi));
                const auto scopeAccent = SpaceColours::themed(fillArmed ? SpaceColours::neonRed : SpaceColours::neonGreen)
                                             .brighter(crtSpot ? 0.18f : 0.0f);
                const auto wallAccent  = SpaceColours::themed(SpaceColours::neonMagenta)
                                             .brighter(crtSpot ? 0.18f : 0.0f);
                SpaceArt::drawCrtBezel(g, panelR, bi == 0 ? scopeAccent : wallAccent, true);

                // CRT phosphor grid
                g.setColour((bi == 0 ? scopeAccent : SpaceColours::themed(SpaceColours::neonCyan)).withAlpha(0.06f));
                for (int gy = panelY + 6; gy < panelY + panelH; gy += 6)
                    g.drawHorizontalLine(gy, panelR.getX() + 2.0f, panelR.getRight() - 2.0f);
                for (int gx = panelX + 24; gx < panelX + panelW; gx += 24)
                    g.drawVerticalLine(gx, panelR.getY() + 2.0f, panelR.getBottom() - 2.0f);

                if (bi == 0)
                {
                    // BASS1 sub-panel: TEKTRONIX CRT ACID OSCILLOSCOPE with 4-frame phosphor persistence
                    // Turns red/amber on FILL glitch!
                    const float midY = panelR.getCentreY();
                    const float midX = panelR.getCentreX();
                    g.setColour(scopeAccent.withAlpha(0.18f));
                    g.drawHorizontalLine(static_cast<int>(midY), panelR.getX() + 2.0f, panelR.getRight() - 2.0f);
                    g.drawVerticalLine(static_cast<int>(midX), panelR.getY() + 2.0f, panelR.getBottom() - 2.0f);
                    for (int tx = panelX + 12; tx < panelX + panelW - 8; tx += 12)
                        g.drawVerticalLine(tx, midY - 2.0f, midY + 2.0f);

                    constexpr int N = OutputAnalyzer::SCOPE_SIZE;
                    const float ampH = panelR.getHeight() * 0.44f;

                    // Phase-Locked Zero-Crossing Trigger: lock rising zero-crossing so 303 wave stands rock-solid!
                    const int baseHead = analyzerRef.getScopeHead();
                    int trigOff = 0;
                    for (int k = 1; k < N / 2; ++k)
                    {
                        const float s0 = analyzerRef.getScopeSample(baseHead + k - 1);
                        const float s1 = analyzerRef.getScopeSample(baseHead + k);
                        if (s0 <= 0.0f && s1 > 0.002f)
                        {
                            trigOff = k;
                            break;
                        }
                    }

                    static constexpr float kTrailAlphas[SCOPE_HIST] = { 0.12f, 0.24f, 0.42f, 0.95f };
                    float peakAbs = 0.0f;
                    for (int age = 0; age < SCOPE_HIST; ++age)
                    {
                        const int slot = (scopeHistWrite + age) % SCOPE_HIST;
                        const bool isLive = (age == SCOPE_HIST - 1);
                        juce::Path wave;
                        const int plotN = N / 2;
                        for (int i = 0; i < plotN; ++i)
                        {
                            const float rawS = isLive
                                ? analyzerRef.getScopeSample(baseHead + trigOff + i)
                                : scopeHistory[static_cast<size_t>(slot)][static_cast<size_t>(i)];
                            if (isLive) peakAbs = std::max(peakAbs, std::abs(rawS));
                            const float s = std::tanh(rawS * 2.4f);
                            const float wx = panelR.getX() + 4.0f
                                           + (static_cast<float>(i) / static_cast<float>(plotN - 1)) * (panelR.getWidth() - 8.0f);
                            const float wy = midY - s * ampH;
                            if (i == 0) wave.startNewSubPath(wx, wy);
                            else        wave.lineTo(wx, wy);
                        }
                        if (isLive)
                        {
                            const float bloom = (peakAbs > 0.72f) ? 0.42f : 0.22f;
                            g.setColour(scopeAccent.withAlpha(bloom));
                            g.strokePath(wave, juce::PathStrokeType(peakAbs > 0.72f ? 6.0f : 4.2f));
                            g.setColour(scopeAccent.interpolatedWith(juce::Colours::white, peakAbs > 0.72f ? 0.40f : 0.12f));
                            g.strokePath(wave, juce::PathStrokeType(1.6f));
                        }
                        else
                        {
                            g.setColour(scopeAccent.withAlpha(kTrailAlphas[age]));
                            g.strokePath(wave, juce::PathStrokeType(1.1f));
                        }
                    }

                    g.setFont(SpaceArt::monoFont(8.0f, true));
                    g.setColour(scopeAccent.withAlpha(0.86f));
                    g.drawText(fillArmed ? "CRT SCOPE // FILL GLITCH ACTIVE" : "CRT SCOPE // PHOSPHOR PERSISTENCE",
                               panelX + 6, panelY + 2, 240, 11, juce::Justification::centredLeft);
                    g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.72f));
                    g.drawText("303 DIODE LADDER", panelX + panelW - 120, panelY + 2, 114, 11, juce::Justification::centredRight);
                }
                else
                {
                    // BASS2 sub-panel: HERO SPACE INVADERS FREQUENCY WALL (kick-synced + SHOOTING LASERS on hot bands!)
                    const int invFrame = visualState.kickCount.load(std::memory_order_relaxed) & 1;
                    constexpr int NB = OutputAnalyzer::NUM_BANDS;
                    const int wallX = panelX + 8;
                    const int wallW = panelW - 16;
                    const int colW  = wallW / NB;
                    static const char* bandLabels[NB] = { "60", "125", "250", "500", "1K", "2K", "4K", "8K" };

                    for (int b = 0; b < NB; ++b)
                    {
                        const float v = std::min(1.0f, analyzerRef.getBand(b) * 6.5f);
                        const float t = static_cast<float>(b) / static_cast<float>(NB - 1);
                        const auto col = SpaceColours::themed(
                            SpaceColours::neonGreen
                                .interpolatedWith(SpaceColours::neonCyan, std::min(1.0f, t * 1.5f))
                                .interpolatedWith(SpaceColours::neonMagenta, std::max(0.0f, (t - 0.5f) * 2.0f)));

                        const int cx = wallX + b * colW;
                        const float barMaxH = static_cast<float>(panelH - 38);
                        const float barH = barMaxH * v;
                        if (barH > 1.0f)
                        {
                            const auto br = juce::Rectangle<float>(static_cast<float>(cx + 6),
                                                                   static_cast<float>(panelY + panelH - 11) - barH,
                                                                   static_cast<float>(colW - 12), barH);
                            g.setColour(col.withAlpha(0.22f));
                            g.fillRect(br.expanded(1.0f));
                            g.setColour(col.withAlpha(0.90f));
                            g.fillRect(br);
                        }

                        // Razor-sharp integer pixel-art Space Invader (2px pixels, kick-synced animation)
                        const float invY = std::max(static_cast<float>(panelY + 14),
                                                    static_cast<float>(panelY + panelH - 12) - barH - 17.0f);
                        const int cols = (b % 3 == 1) ? 12 : (b % 3 == 0 ? 11 : 9);
                        const float invX = static_cast<float>(cx) + (static_cast<float>(colW) - static_cast<float>(cols * 2)) * 0.5f;
                        SpaceArt::drawInvader(g, b % 3, invFrame ^ (b & 1), invX, invY, 2, col.brighter(0.20f), 0.14f + v * 0.20f);

                        // SHOOTING INVADERS! When a frequency band peaks hot (> 0.72), the invader fires a crisp 3px laser!
                        if (v > 0.72f)
                        {
                            const int lx = cx + colW / 2;
                            const int ly = static_cast<int>(invY) - 6;
                            g.setColour(juce::Colours::white.withAlpha(0.95f));
                            g.fillRect(lx - 1, std::max(panelY + 3, ly), 2, 5);
                            g.setColour(col.withAlpha(0.65f));
                            g.fillRect(lx - 2, std::max(panelY + 3, ly), 4, 5);
                        }

                        g.setFont(SpaceArt::monoFont(7.0f, true));
                        g.setColour(SpaceColours::textDim.withAlpha(0.80f));
                        g.drawText(bandLabels[b], cx, panelY + panelH - 10, colW, 9, juce::Justification::centred);
                    }

                    g.setFont(SpaceArt::monoFont(7.5f, true));
                    g.setColour(wallAccent.withAlpha(0.88f));
                    g.drawText("SPACE INVADERS // 23 // TEKNO WALL", panelX + 6, panelY + 2, 240, 10, juce::Justification::centredLeft);
                }
            }
        }
    }

    // Subtle weathered-metal grain noise overlay across the entire rack
    if (noiseTile.isValid())
    {
        g.setTiledImageFill(noiseTile, 0, 0, 0.28f);
        g.fillRect(getLocalBounds());
    }
}

// ── Resized ────────────────────────────────────────────────────────────────────

void StepGrid::resized()
{
    // Internal Play/Stop button in top-left of header (over label column)
    playButton.setBounds(PAD, 4, LABEL_W - PAD * 2, HEADER_H - 8);

    // Swing knob cleanly positioned above MIX zone (zero overlap with LEN)
    {
        const int volX = PAD + LABEL_W + STEPS_AREA_W + PAD + PARAM_AREA_W + PAD;
        swingKnob->setBounds(volX + 24, 1, SWING_KSZ, SWING_KSZ);
    }

    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const int ry = rowY(ch);
        const int midY = ry + ROW_H / 2;

        // Param knobs — inline after steps
        const int paramStartX = PAD + LABEL_W + STEPS_AREA_W + PAD;
        for (int p = 0; p < channelParams[idx(ch)].count; ++p)
        {
            const int kx = paramStartX + p * (PKNOB_SZ + PKNOB_GAP);
            const int ky = ry + (ROW_H - PKNOB_SZ - 8) / 2;  // -8 for label space below
            channelParams[idx(ch)].entries[idx(p)].knob->setBounds(kx, ky, PKNOB_SZ, PKNOB_SZ);
        }

        // Vol knob — after param area (shifted up slightly for VOL/PAN labels below)
        const int volX = paramStartX + PARAM_AREA_W + PAD;
        volKnobs[idx(ch)]->setBounds(volX, ry + (ROW_H - KNOB_W - 8) / 2, KNOB_W, KNOB_W);

        // Pan knob
        const int panX = volX + KNOB_W + PAD;
        panKnobs[idx(ch)]->setBounds(panX, ry + (ROW_H - PAN_KSZ - 8) / 2, PAN_KSZ, PAN_KSZ);

        // M/S stacked (tiny square buttons)
        const int msX = panX + PAN_KSZ + PAD;
        muteButtons[idx(ch)].setBounds(msX, midY - BTN_H - 1, MS_BTN_W, BTN_H);
        soloButtons[idx(ch)].setBounds(msX, midY + 1,         MS_BTN_W, BTN_H);

        // CLR/RND stacked
        const int btnX = msX + MS_BTN_W + PAD;
        clrButtons[idx(ch)].setBounds(btnX, midY - BTN_H - 1, BTN_W, BTN_H);
        rndButtons[idx(ch)].setBounds(btnX, midY + 1,         BTN_W, BTN_H);

        // CPY/PST stacked
        const int cpX = btnX + BTN_W + PAD;
        copyButtons[idx(ch)].setBounds(cpX, midY - BTN_H - 1, BTN_W, BTN_H);
        pasteButtons[idx(ch)].setBounds(cpX, midY + 1,         BTN_W, BTN_H);

        // H / VAR stacked toggles
        const int varX = cpX + BTN_W + PAD;
        humButtons[idx(ch)].setBounds(varX, midY - BTN_H - 1, VAR_BTN_W, BTN_H);
        varButtons[idx(ch)].setBounds(varX, midY + 1,         VAR_BTN_W, BTN_H);

        // Pattern length button
        const int lenX = varX + VAR_BTN_W + PAD;
        lenButtons[idx(ch)].setBounds(lenX, midY - BTN_H / 2, LEN_BTN_W, BTN_H);

        // Page nav
        const int navX = lenX + LEN_BTN_W + PAD;
        prevBtns[idx(ch)].setBounds(navX,                midY - BTN_H / 2, PAGE_BTN_W, BTN_H);
        nextBtns[idx(ch)].setBounds(navX + PAGE_BTN_W + 2, midY - BTN_H / 2, PAGE_BTN_W, BTN_H);
    }

    // Dedicated RND ACID buttons inside the left label column of BASS1 & BASS2 sub-rows + Scale Lock pill
    for (int bi = 0; bi < 2; ++bi)
    {
        const int subY0 = rowY(6 + bi) + ROW_H;
        acidRndButtons[static_cast<size_t>(bi)].setBounds(PAD + 28, subY0 + 17, 30, 18);
    }
    acidScaleButton.setBounds(PAD + 28, rowY(6) + ROW_H + 37, 30, 15);
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    // Check 303 sub-rows first (bass channels only)
    for (int bch = 6; bch < 8; ++bch)
    {
        const int bi = bch - 6;
        const int ry = rowY(bch);
        const int subY0 = ry + ROW_H;
        const int subY1 = subY0 + SUB_ROW_H;
        const int subY2 = subY1 + SUB_ROW_H;

        if (e.y >= subY0 && e.y < subY2 + SUB_ROW_H)
        {
            const int stepsX = PAD + LABEL_W;
            if (e.x >= stepsX && e.x < stepsX + STEPS_AREA_W)
            {
                const int vs = (e.x - stepsX) / STEP_W;
                const int absStep = viewPage[idx(bch)] * VISIBLE_STEPS + vs;

                if (e.y < subY1) // NOTE row: vertical drag changes pitch (-24..+24)
                {
                    undoRef.push(sequencer);
                    dragMode     = DragMode::BassNote;
                    dragCh       = bch;
                    dragStep     = absStep;
                    dragStartX   = e.x;
                    dragStartY   = e.y;
                    dragStartVal = static_cast<int>(sequencer.getBassNoteOffset(bi, absStep));
                }
                else if (e.y < subY2) // SLD row
                {
                    undoRef.push(sequencer);
                    sequencer.toggleBassSlide(bi, absStep);
                }
                else if (e.mods.isShiftDown()) // Shift+click on ACC row = octave up
                {
                    undoRef.push(sequencer);
                    sequencer.toggleBassOctUp(bi, absStep);
                }
                else if (e.mods.isCommandDown()) // Cmd+click on ACC row = octave down
                {
                    undoRef.push(sequencer);
                    sequencer.toggleBassOctDown(bi, absStep);
                }
                else // ACC row
                {
                    undoRef.push(sequencer);
                    sequencer.toggleBassAccent(bi, absStep);
                }

                repaint();
                return;
            }
        }
    }

    // Channel label area: right-click → Euclidean popup, left-click → audition voice
    if (e.x < PAD + LABEL_W)
    {
        for (int chk = 0; chk < NUM_CH; ++chk)
        {
            const int ry = rowY(chk);
            if (e.y >= ry && e.y < ry + ROW_H)
            {
                if (e.mods.isRightButtonDown())
                {
                    auto popup = std::make_unique<EuclideanPopup>(sequencer, chk,
                        [this] { undoRef.push(sequencer); });
                    const auto target = juce::Rectangle<int>(e.x, e.y, 1, 1);
                    juce::CallOutBox::launchAsynchronously(std::move(popup),
                        localAreaToGlobal(target), nullptr);
                }
                else
                {
                    if (chk >= 6)
                        voiceBank.requestBassNote(chk - 6, voiceBank.getBassNote(chk - 6));
                    else
                        voiceBank.requestTrigger(chk);
                    visualState.noteFire(chk, 1.0f);
                    repaint();
                }
                return;
            }
        }
    }

    auto [ch, absStep] = hitStep(e.x, e.y);
    if (ch < 0) return;

    if (e.mods.isRightButtonDown())
    {
        undoRef.push(sequencer);
        dragCh       = ch;
        dragStep     = absStep;
        dragStartX   = e.x;
        dragStartY   = e.y;

        if (e.mods.isAltDown())
        {
            if (!sequencer.getStep(ch, absStep)) return;
            dragMode     = DragMode::Probability;
            dragStartVal = sequencer.getProbability(ch, absStep);
        }
        else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            if (!sequencer.getStep(ch, absStep)) return;
            dragMode     = DragMode::MicroTiming;
            dragStartVal = sequencer.getMicroTiming(ch, absStep);
        }
        else
        {
            // Right-click & Swipe-to-Erase!
            dragMode = DragMode::Erase;
            sequencer.setStep(ch, absStep, false);
            repaint();
        }
    }
    else if (e.mods.isShiftDown())
    {
        // Shift + Left-drag vertically on a step draws Velocity directly on the pad!
        undoRef.push(sequencer);
        if (!sequencer.getStep(ch, absStep))
            sequencer.setStep(ch, absStep, true);
        dragMode     = DragMode::Velocity;
        dragCh       = ch;
        dragStep     = absStep;
        dragStartX   = e.x;
        dragStartY   = e.y;
        dragStartVal = sequencer.getVelocity(ch, absStep);
        repaint();
    }
    else
    {
        // Left-click & Swipe-to-Paint!
        undoRef.push(sequencer);
        const bool newState = !sequencer.getStep(ch, absStep);
        sequencer.setStep(ch, absStep, newState);
        dragMode = newState ? DragMode::PaintOn : DragMode::PaintOff;
        dragCh   = ch;
        dragStep = absStep;
        repaint();
    }
}

void StepGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode == DragMode::None || dragCh < 0 || dragStep < 0) return;

    switch (dragMode)
    {
        case DragMode::None:
            break;
        case DragMode::PaintOn:
        case DragMode::PaintOff:
        case DragMode::Erase:
        {
            auto [hch, hstep] = hitStep(e.x, e.y);
            if (hch == dragCh && hstep >= 0 && hstep < sequencer.getPatternLength(dragCh))
            {
                const bool targetOn = (dragMode == DragMode::PaintOn);
                if (sequencer.getStep(dragCh, hstep) != targetOn)
                {
                    sequencer.setStep(dragCh, hstep, targetOn);
                    dragStep = hstep;
                    repaint();
                }
            }
            return;
        }
        case DragMode::Velocity:
        {
            const int delta = dragStartY - e.y;  // up = louder
            const int newVal = juce::jlimit(1, 127, dragStartVal + delta * 2);
            sequencer.setVelocity(dragCh, dragStep, static_cast<uint8_t>(newVal));
            break;
        }
        case DragMode::Probability:
        {
            const int delta = dragStartY - e.y;
            const int newVal = juce::jlimit(0, 100, dragStartVal + delta);
            sequencer.setProbability(dragCh, dragStep, static_cast<uint8_t>(newVal));
            break;
        }
        case DragMode::MicroTiming:
        {
            const int delta = e.x - dragStartX;  // right = later
            const int newVal = juce::jlimit(-50, 50, dragStartVal + delta);
            sequencer.setMicroTiming(dragCh, dragStep, static_cast<int8_t>(newVal));
            break;
        }
        case DragMode::BassNote:
        {
            if (dragCh >= 6)
            {
                const int bi = dragCh - 6;
                const int delta = (dragStartY - e.y) / 6;  // up = higher pitch
                const int newVal = juce::jlimit(-24, 24, dragStartVal + delta);
                if (newVal != static_cast<int>(sequencer.getBassNoteOffset(bi, dragStep)))
                {
                    sequencer.setBassNoteOffset(bi, dragStep, static_cast<int8_t>(newVal));
                    const int oct = (sequencer.getBassOctUp(bi, dragStep) ? 12 : 0)
                                  - (sequencer.getBassOctDown(bi, dragStep) ? 12 : 0);
                    voiceBank.requestBassNote(bi, std::clamp(48 + newVal + oct, 0, 127));
                }
            }
            break;
        }
    }
    repaint();
}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    const bool wasBassNote = (dragMode == DragMode::BassNote);
    dragMode = DragMode::None;
    dragCh = dragStep = -1;
    if (wasBassNote) repaint();
}

void StepGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int stepsX = PAD + LABEL_W;
    if (e.x < stepsX || e.x >= stepsX + STEPS_AREA_W) return;

    for (int bch = 6; bch < 8; ++bch)
    {
        const int subY0 = rowY(bch) + ROW_H;
        if (e.y >= subY0 && e.y < subY0 + SUB_ROW_H)
        {
            const int vs = (e.x - stepsX) / STEP_W;
            const int absStep = viewPage[idx(bch)] * VISIBLE_STEPS + vs;
            undoRef.push(sequencer);
            sequencer.setBassNoteOffset(bch - 6, absStep, 0);
            repaint();
            return;
        }
    }
}

void StepGrid::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) > 0.001f)
    {
        // Mouse wheel over any LEN button adjusts exact polyrhythmic step length (1..64)!
        const auto posInGrid = e.getEventRelativeTo(this).getPosition();
        for (int ch = 0; ch < NUM_CH; ++ch)
        {
            if (lenButtons[idx(ch)].getBounds().expanded(2).contains(posInGrid))
            {
                const int dir = (wheel.deltaY > 0.0f) ? 1 : -1;
                const int next = std::clamp(sequencer.getPatternLength(ch) + dir, 1, Sequencer::MAX_STEPS);
                sequencer.setPatternLength(ch, next);
                const int maxPage = std::max(0, (next - 1) / VISIBLE_STEPS);
                viewPage[idx(ch)] = std::min(viewPage[idx(ch)], maxPage);
                updateLenButton(ch);
                updatePageButtons(ch);
                repaint();
                return;
            }
        }

        // Mouse wheel over header BPM readout adjusts internal BPM when host is not playing
        if (e.y < HEADER_H && !hostPlayingRef.load(std::memory_order_relaxed))
        {
            const float cur = bpmRef.load(std::memory_order_relaxed);
            const float step = e.mods.isShiftDown() ? 0.5f : 1.0f;
            const float next = std::clamp(cur + (wheel.deltaY > 0.0f ? step : -step), 40.0f, 240.0f);
            bpmRef.store(next, std::memory_order_relaxed);
            repaint(0, 0, getWidth(), HEADER_H);
            return;
        }

        const int stepsX = PAD + LABEL_W;
        if (e.x >= stepsX && e.x < stepsX + STEPS_AREA_W)
        {
            for (int bch = 6; bch < 8; ++bch)
            {
                const int subY0 = rowY(bch) + ROW_H;
                if (e.y >= subY0 && e.y < subY0 + SUB_ROW_H)
                {
                    const int vs = (e.x - stepsX) / STEP_W;
                    const int absStep = viewPage[idx(bch)] * VISIBLE_STEPS + vs;
                    const int bi = bch - 6;
                    const int cur = static_cast<int>(sequencer.getBassNoteOffset(bi, absStep));
                    const int stepDir = (wheel.deltaY > 0.0f) ? 1 : -1;
                    const int nextVal = juce::jlimit(-24, 24, cur + stepDir);
                    sequencer.setBassNoteOffset(bi, absStep, static_cast<int8_t>(nextVal));
                    const int oct = (sequencer.getBassOctUp(bi, absStep) ? 12 : 0)
                                  - (sequencer.getBassOctDown(bi, absStep) ? 12 : 0);
                    voiceBank.requestBassNote(bi, std::clamp(48 + nextVal + oct, 0, 127));
                    repaint();
                    return;
                }
            }
        }
    }
    juce::Component::mouseWheelMove(e, wheel);
}

void StepGrid::mouseMove(const juce::MouseEvent& e)
{
    // Sub-row tooltips on bass channels
    const int stepsX = PAD + LABEL_W;
    if (e.x >= stepsX && e.x < stepsX + STEPS_AREA_W)
    {
        for (int bch = 6; bch < 8; ++bch)
        {
            const int bi = bch - 6;
            const int subY0 = rowY(bch) + ROW_H;
            const int subY1 = subY0 + SUB_ROW_H;
            const int subY2 = subY1 + SUB_ROW_H;
            if (e.y >= subY0 && e.y < subY2 + SUB_ROW_H)
            {
                const int vs = (e.x - stepsX) / STEP_W;
                const int absStep = viewPage[idx(bch)] * VISIBLE_STEPS + vs;
                if (e.y < subY1)
                {
                    static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    const int off = static_cast<int>(sequencer.getBassNoteOffset(bi, absStep));
                    const int oct = (sequencer.getBassOctUp(bi, absStep) ? 12 : 0)
                                  - (sequencer.getBassOctDown(bi, absStep) ? 12 : 0);
                    const int midiNote = std::clamp(48 + off + oct, 0, 127);
                    setTooltip(juce::String(kChannelNames[bch]) + " Step " + juce::String(absStep + 1)
                               + "  " + noteNames[midiNote % 12] + juce::String((midiNote / 12) - 1)
                               + " (" + (off >= 0 ? "+" : "") + juce::String(off) + " st)"
                               + "   (drag/wheel: pitch, double-click: reset C3)");
                }
                else if (e.y < subY2)
                {
                    setTooltip(juce::String(kChannelNames[bch]) + " Step " + juce::String(absStep + 1)
                               + "  Slide (click to toggle legato glide)");
                }
                else
                {
                    setTooltip(juce::String(kChannelNames[bch]) + " Step " + juce::String(absStep + 1)
                               + "  Accent (click: accent, Shift+click: +1 oct, Cmd+click: -1 oct)");
                }
                return;
            }
        }
    }

    // Channel label tooltip
    if (e.x < PAD + LABEL_W)
    {
        for (int chk = 0; chk < NUM_CH; ++chk)
        {
            const int ry = rowY(chk);
            if (e.y >= ry && e.y < ry + ROW_H)
            {
                setTooltip(juce::String(kChannelNames[chk]) + "  (left-click: audition voice, right-click: Euclidean generator)");
                return;
            }
        }
    }

    auto [ch, absStep] = hitStep(e.x, e.y);
    if (ch >= 0)
    {
        const bool on = sequencer.getStep(ch, absStep);
        juce::String tip = juce::String(kChannelNames[ch])
                         + " Step " + juce::String(absStep + 1);
        if (on)
        {
            tip += "  vel:" + juce::String(sequencer.getVelocity(ch, absStep));
            const int prob = sequencer.getProbability(ch, absStep);
            if (prob < 100) tip += "  prob:" + juce::String(prob) + "%";
            const int mt = sequencer.getMicroTiming(ch, absStep);
            if (mt != 0) tip += "  t:" + (mt > 0 ? juce::String("+") : juce::String())
                              + juce::String(mt) + "%";
        }
        tip += "   (right-drag: vel  shift+right-drag: prob  cmd+right-drag: timing)";
        setTooltip(tip);
    }
    else
    {
        setTooltip({});
    }
}

// ── Timer ─────────────────────────────────────────────────────────────────────

void StepGrid::timerCallback()
{
    syncFromSequencer();
    if (midiLearnRef)
    {
        for (int ch = 0; ch < NUM_CH; ++ch)
        {
            if (volKnobs[idx(ch)]) volKnobs[idx(ch)]->refreshLearnVisual();
            if (panKnobs[idx(ch)]) panKnobs[idx(ch)]->refreshLearnVisual();
            channelParams[idx(ch)].refreshMidiLearnVisuals();
        }
        if (swingKnob) swingKnob->refreshLearnVisual();
    }

    // Hover Spotlight: track which channel row the cursor is over (including child knobs!)
    {
        const auto mp = getMouseXYRelative();
        int newHover = -1;
        if (getLocalBounds().contains(mp))
        {
            for (int ch = 0; ch < NUM_CH; ++ch)
            {
                const int top = rowY(ch);
                const int bot = top + ROW_H + (ch >= 6 ? BASS_EXTRA : 0);
                if (mp.y >= top && mp.y < bot) { newHover = ch; break; }
            }
        }
        if (newHover != hoveredChannel)
        {
            hoveredChannel = newHover;
            repaint();
        }
    }

    // Live Modulation Rings on BASS1/2 Cutoff & SC knobs (visualize live sidechain + kick energy)
    {
        const float kp = visualState.kickPulse.load(std::memory_order_relaxed);
        const float si = visualState.sustainedIntensity.load(std::memory_order_relaxed);
        for (int bch = 6; bch < 8; ++bch)
        {
            if (channelParams[idx(bch)].entries[0].knob)
                channelParams[idx(bch)].entries[0].knob->setModOffset(si * 0.14f);
            if (channelParams[idx(bch)].entries[5].knob)
                channelParams[idx(bch)].entries[5].knob->setModOffset(-kp * 0.22f);
        }
    }

    // Optical LED persistence decay (~85 ms @ 60 FPS)
    const bool playingNow = sequencer.isPlaying.load(std::memory_order_acquire);
    bool anyLedActive = false;
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const int activeSt = playingNow ? sequencer.currentSteps[idx(ch)].load(std::memory_order_acquire) : -1;
        const int pageBase = viewPage[idx(ch)] * VISIBLE_STEPS;
        for (int vs = 0; vs < VISIBLE_STEPS; ++vs)
        {
            float& g = stepLedGlow[idx(ch)][static_cast<size_t>(vs)];
            if (playingNow && (pageBase + vs) == activeSt)
                g = 1.0f;
            else
                g *= 0.80f;
            if (g < 0.01f) g = 0.0f;
            else anyLedActive = true;
        }
    }

    // Capture phase-locked waveform into phosphor persistence history ring (60 Hz)
    {
        const int baseHead = analyzerRef.getScopeHead();
        constexpr int N = OutputAnalyzer::SCOPE_SIZE;
        int trigOff = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            if (analyzerRef.getScopeSample(baseHead + k - 1) <= 0.0f
                && analyzerRef.getScopeSample(baseHead + k) > 0.002f)
            {
                trigOff = k;
                break;
            }
        }
        auto& dst = scopeHistory[static_cast<size_t>(scopeHistWrite)];
        for (int i = 0; i < N / 2; ++i)
            dst[static_cast<size_t>(i)] = analyzerRef.getScopeSample(baseHead + trigOff + i);
        scopeHistWrite = (scopeHistWrite + 1) % SCOPE_HIST;
    }

    const bool playing = playingNow;
    const bool hasGlow = anyLedActive || visualState.totalEnergy.load(std::memory_order_relaxed) > 0.01f;
    bool fullRepaint = false;

    // While playing OR while glow is still decaying, force full repaint to animate
    if (playing || hasGlow)
    {
        repaint();
        // Still update step tracking below so beat indicators stay correct
    }

    // Detect BPM change → repaint header area (fixes display not updating on tempo change)
    const float currentBpm = bpmRef.load(std::memory_order_relaxed);
    if (std::abs(currentBpm - lastDisplayedBpm) > 0.01f)
    {
        lastDisplayedBpm = currentBpm;
        repaint(0, 0, getWidth(), HEADER_H);  // targeted header repaint
    }

    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const int step = sequencer.currentSteps[idx(ch)].load(std::memory_order_acquire);
        if (step != lastDisplayedStep[idx(ch)])
        {
            const int oldStep = lastDisplayedStep[idx(ch)];
            lastDisplayedStep[idx(ch)] = step;

            // Auto-follow: switch visible page to where the playhead is
            if (playing && step >= 0)
            {
                const int playPage = step / VISIBLE_STEPS;
                if (playPage != viewPage[idx(ch)])
                {
                    viewPage[idx(ch)] = playPage;
                    updatePageButtons(ch);
                    fullRepaint = true;
                    continue;
                }
            }

            // Targeted repaint: only the old and new playhead cells
            if (!fullRepaint)
            {
                const int ry = rowY(ch);
                const int pageOff = viewPage[idx(ch)] * VISIBLE_STEPS;

                auto repaintStep = [&](int s) {
                    const int vis = s - pageOff;
                    if (vis >= 0 && vis < VISIBLE_STEPS)
                    {
                        const int sx = PAD + LABEL_W + vis * STEP_W;
                        repaint(sx - 1, ry, STEP_W + 2, ROW_H);
                    }
                };
                repaintStep(oldStep);
                repaintStep(step);
            }
        }
    }

    if (fullRepaint) repaint();
}
