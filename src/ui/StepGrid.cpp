#include "StepGrid.h"
#include "../core/ParamIds.h"
#include "SpaceLookAndFeel.h"
#include "SpaceArt.h"
#include "EuclideanPopup.h"
#include <cmath>

namespace
{
static const char* kChannelTitles[8] = {
    "KICK 909", "SNARE", "HATS", "TOM",
    "PERC 1", "PERC 2", "BASS 1 303", "BASS 2 303"
};
static const char* kChannelShort[8] = {
    "KICK", "SNARE", "HATS", "TOM", "PERC1", "PERC2", "BASS1", "BASS2"
};
static const char* kChannelBadge[8] = {
    "[909 kick]", "[909 snare]", "[909 hat]", "[909 tom]",
    "[fm perc]", "[noise perc]", "[303 acid]", "[303 acid]"
};

static juce::String noteNameFromOffset(int8_t off)
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int midi = 48 + static_cast<int>(off);
    const int pc   = ((midi % 12) + 12) % 12;
    const int oct  = (midi / 12) - 1;
    return juce::String(names[pc]) + juce::String(oct);
}

// Clean mixi-inspired spaced vertical pill LED meter with calibrated 500ms Peak-Hold (#16)
static void drawChannelPillMeter(juce::Graphics& g, juce::Rectangle<float> area, float value, int numSegs, juce::Colour baseCol, bool isSampler = false, float peakHold = 0.0f)
{
    value = std::clamp(value, 0.0f, 1.0f);
    const int lit = static_cast<int>(std::round(value * static_cast<float>(numSegs)));
    const int pkIdx = (peakHold > 0.06f) ? std::clamp(static_cast<int>(std::round(peakHold * static_cast<float>(numSegs))) - 1, 0, numSegs - 1) : -1;
    const float gap = 2.0f;
    const float segH = (area.getHeight() - gap * static_cast<float>(numSegs - 1)) / static_cast<float>(numSegs);

    g.setColour(juce::Colour(0xff090b0a));
    g.fillRoundedRectangle(area.expanded(1.5f), 2.5f);

    for (int i = 0; i < numSegs; ++i)
    {
        const float frac = static_cast<float>(i) / static_cast<float>(std::max(1, numSegs - 1));
        juce::Colour c = isSampler
            ? (frac > 0.78f ? SpaceColours::neonCyan : SpaceColours::themed(SpaceColours::neonMagenta))
            : (frac > 0.82f ? SpaceColours::neonRed : (frac > 0.60f ? SpaceColours::neonYellow : baseCol));

        const float sy = area.getBottom() - static_cast<float>(i + 1) * segH - static_cast<float>(i) * gap;
        const auto seg = juce::Rectangle<float>(area.getX(), sy, area.getWidth(), std::max(2.0f, segH));
        if (i < lit)
        {
            g.setColour(c.withAlpha(0.24f));
            g.fillRoundedRectangle(seg.expanded(0.8f), 1.5f);
            g.setColour(c);
            g.fillRoundedRectangle(seg, 1.2f);
        }
        else if (i == pkIdx)
        {
            // Suspended 500ms Peak-Hold micro-bar
            g.setColour(juce::Colours::white.withAlpha(0.88f));
            g.fillRoundedRectangle(seg.withHeight(std::max(1.8f, segH * 0.55f)), 1.0f);
        }
        else
        {
            g.setColour(c.withAlpha(0.09f));
            g.fillRoundedRectangle(seg, 1.2f);
        }
    }
}
} // namespace

StepGrid::StepGrid(Sequencer& seq, VoiceBank& voices, juce::AudioProcessorValueTreeState& apvts,
                   std::atomic<float>& bpm, std::atomic<bool>& internalPlay,
                   std::atomic<bool>& hostPlaying, VisualState& visState,
                   OutputAnalyzer& analyzer, SequencerUndo& undo,
                   ChaosSampleBus& chaosBus,
                   BeatLfoBank& lfoBank,
                   MidiLearnManager* midiLearn)
    : sequencer(seq), voiceBank(voices), apvtsRef(apvts), bpmRef(bpm),
      internalPlayRef(internalPlay), hostPlayingRef(hostPlaying),
      visualState(visState), analyzerRef(analyzer), undoRef(undo),
      chaosBusRef(chaosBus), lfoBankRef(lfoBank), midiLearnRef(midiLearn)
{
    noiseTile = SpaceArt::makeNoiseTile(128);

    // ── Per-channel strip widgets (8 mirror-symmetric strips: Meter + VOL Fader + M/S + 4x 54px Knobs) ──
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto chCol = SpaceColours::channelColor(ch);

        selectButtons[idx(ch)].setButtonText("SELECT");
        selectButtons[idx(ch)].setTooltip("Select " + juce::String(kChannelTitles[ch]) + " in Command Core (Right-click header: Euclidean)");
        selectButtons[idx(ch)].setClickingTogglesState(false);
        selectButtons[idx(ch)].setColour(juce::TextButton::buttonOnColourId, chCol);
        selectButtons[idx(ch)].onClick = [this, ch] { setSelectedChannel(ch); };
        addAndMakeVisible(selectButtons[idx(ch)]);

        muteButtons[idx(ch)].setButtonText("M");
        muteButtons[idx(ch)].setTooltip("Mute " + juce::String(kChannelTitles[ch]) + " (Shift+Click: instant)");
        muteButtons[idx(ch)].setClickingTogglesState(false);
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

        soloButtons[idx(ch)].setButtonText("S");
        soloButtons[idx(ch)].setTooltip("Solo " + juce::String(kChannelTitles[ch]));
        soloButtons[idx(ch)].setClickingTogglesState(true);
        soloButtons[idx(ch)].onClick = [this, ch] {
            sequencer.solo[idx(ch)].store(soloButtons[idx(ch)].getToggleState(), std::memory_order_release);
        };
        addAndMakeVisible(soloButtons[idx(ch)]);

        varButtons[idx(ch)].setButtonText("VAR");
        varButtons[idx(ch)].setTooltip("Auto-Variation — cycles fills & variations over 64-bar phrase");
        varButtons[idx(ch)].setClickingTogglesState(true);
        varButtons[idx(ch)].onClick = [this, ch] {
            sequencer.autoVar[idx(ch)].store(varButtons[idx(ch)].getToggleState(), std::memory_order_release);
            updateCoreButtons();
        };
        addAndMakeVisible(varButtons[idx(ch)]);

        humButtons[idx(ch)].setButtonText("H");
        humButtons[idx(ch)].setTooltip("Humanize — randomises velocity +/-15% on each trigger");
        humButtons[idx(ch)].setClickingTogglesState(true);
        humButtons[idx(ch)].onClick = [this, ch] {
            sequencer.humanize[idx(ch)].store(humButtons[idx(ch)].getToggleState(), std::memory_order_release);
        };
        addAndMakeVisible(humButtons[idx(ch)]);

        lenButtons[idx(ch)].onClick = [this, ch] {
            undoRef.push(sequencer);
            const auto mods = juce::ModifierKeys::currentModifiers;
            const int cur = sequencer.getPatternLength(ch);
            int next = cur;
            if (mods.isShiftDown())      next = std::max(1, cur - 1);
            else if (mods.isAltDown())   next = std::min(Sequencer::MAX_STEPS, cur + 1);
            else                         next = (cur >= 64) ? 16 : ((cur / 16) + 1) * 16;
            sequencer.setPatternLength(ch, next);
            const int maxPage = std::max(0, (next - 1) / VISIBLE_STEPS);
            viewPage[idx(ch)] = std::min(viewPage[idx(ch)], maxPage);
            updateLenButton(ch);
            updateCoreButtons();
            if (onSelectedChannelChanged) onSelectedChannelChanged(selectedChannel);
            repaint();
        };
        addAndMakeVisible(lenButtons[idx(ch)]);
        updateLenButton(ch);

        // Quick 1-click LFO toggle in taller 30px channel header
        lfoButtons[idx(ch)].setButtonText("LFO");
        lfoButtons[idx(ch)].setComponentID("chLfo");
        lfoButtons[idx(ch)].setTooltip("Toggle Beat-Synced LFO on " + juce::String(kChannelTitles[ch]));
        lfoButtons[idx(ch)].setClickingTogglesState(true);
        lfoButtons[idx(ch)].onClick = [this, ch] {
            lfoBankRef.channels[idx(ch)].enabled.store(lfoButtons[idx(ch)].getToggleState(), std::memory_order_relaxed);
            repaint();
        };
        addAndMakeVisible(lfoButtons[idx(ch)]);

        // Independent Channel FLIP button (Face A = Live Knobs | Face B = Setup + Local LFO)
        flipButtons[idx(ch)].setButtonText("FLIP");
        flipButtons[idx(ch)].setTooltip("Flip " + juce::String(kChannelTitles[ch]) + " between Live (A) and Setup/LFO (B)");
        flipButtons[idx(ch)].setClickingTogglesState(true);
        flipButtons[idx(ch)].onClick = [this, ch] {
            isFlipped[idx(ch)] = flipButtons[idx(ch)].getToggleState();
            resized();
            repaint();
        };
        addAndMakeVisible(flipButtons[idx(ch)]);

        // Vertical Hardware Volume Fader (mixi-style recessed slot + 3D metallic cap!)
        const juce::String volId = "vol_" + juce::String(ch);
        auto fader = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        fader->setTooltip(juce::String(kChannelTitles[ch]) + " Volume Fader (double-click: reset)");
        fader->setColour(juce::Slider::trackColourId, chCol);
        fader->setPopupDisplayEnabled(false, false, nullptr);
        if (auto* p = apvts.getParameter(volId))
            fader->setDoubleClickReturnValue(true, static_cast<double>(p->convertFrom0to1(p->getDefaultValue())));
        fader->onValueChange = [ch, volId, fPtr = fader.get()] {
            if (fPtr->isMouseOverOrDragging())
                MusicalUnits::publishHud(juce::String(kChannelShort[ch]) + " VOL", volId, fPtr->getValue());
        };
        volAttachments[idx(ch)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, volId, *fader);
        addAndMakeVisible(*fader);
        volFaders[idx(ch)] = std::move(fader);

        // Pan knob (bipolar 12-o'clock arc + magnetic center detent!)
        const juce::String panId = ParamId::pan(ch);
        auto pKnob = std::make_unique<DjKnob>(panId, &apvts, midiLearnRef, juce::String(kChannelShort[ch]) + " PAN");
        pKnob->setBipolar(true);
        pKnob->setTooltip(juce::String(kChannelTitles[ch]) + " Pan (double-click: centre)");
        pKnob->setColour(juce::Slider::rotarySliderFillColourId, chCol.withMultipliedSaturation(0.75f));
        panAttachments[idx(ch)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, panId, *pKnob);
        addAndMakeVisible(*pKnob);
        panKnobs[idx(ch)] = std::move(pKnob);
    }

    // Synced mirror HATS PAN knob for the 4x2 MIX console (stays 1:1 with Strip 3 via APVTS)
    {
        const juce::String hatsPanId = ParamId::pan(2);
        mixHatsPanKnob = std::make_unique<DjKnob>(hatsPanId, &apvts, midiLearnRef, "HATS PAN");
        mixHatsPanKnob->setBipolar(true);
        mixHatsPanKnob->setTooltip("HATS Pan (synced 1:1 with Strip 3 | double-click: centre)");
        mixHatsPanKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::channelColor(2).withMultipliedSaturation(0.75f));
        mixHatsPanAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, hatsPanId, *mixHatsPanKnob);
        addAndMakeVisible(*mixHatsPanKnob);
    }

    // Per-channel synth parameters (4 primary on strip; secondary in MIX screen)
    channelParams[0].add(apvts, ParamId::KICK_TUNE,     "TUNE",  *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_DECAY,    "DECAY", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_PUNCH,    "PUNCH", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_DRIVE,    "DRIVE", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_PITCHENV, "P-ENV", *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");
    channelParams[0].add(apvts, ParamId::KICK_SUB,      "SUB",   *this, SpaceColours::channelColor(0), midiLearnRef, "KICK");

    channelParams[1].add(apvts, ParamId::SNARE_TUNE,  "TUNE",  *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");
    channelParams[1].add(apvts, ParamId::SNARE_DECAY, "DECAY", *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");
    channelParams[1].add(apvts, ParamId::SNARE_SNAP,  "SNAP",  *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");
    channelParams[1].add(apvts, ParamId::SNARE_NOISE, "NOISE", *this, SpaceColours::channelColor(1), midiLearnRef, "SNARE");

    channelParams[2].add(apvts, ParamId::HATS_DECAY, "DECAY", *this, SpaceColours::channelColor(2), midiLearnRef, "HATS");
    channelParams[2].add(apvts, ParamId::HATS_TONE,  "TONE",  *this, SpaceColours::channelColor(2), midiLearnRef, "HATS");
    channelParams[2].add(apvts, ParamId::HATS_BODY,  "BODY",  *this, SpaceColours::channelColor(2), midiLearnRef, "HATS");

    channelParams[3].add(apvts, ParamId::TOM_TUNE,     "TUNE",  *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");
    channelParams[3].add(apvts, ParamId::TOM_DECAY,    "DECAY", *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");
    channelParams[3].add(apvts, ParamId::TOM_PITCHENV, "P-ENV", *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");
    channelParams[3].add(apvts, ParamId::TOM_ATTACK,   "ATTK",  *this, SpaceColours::channelColor(3), midiLearnRef, "TOM");

    channelParams[4].add(apvts, ParamId::PERC1_TUNE,   "TUNE",  *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");
    channelParams[4].add(apvts, ParamId::PERC1_DECAY,  "DECAY", *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");
    channelParams[4].add(apvts, ParamId::PERC1_DETUNE, "DETN",  *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");
    channelParams[4].add(apvts, ParamId::PERC1_DRIVE,  "DRIVE", *this, SpaceColours::channelColor(4), midiLearnRef, "PERC1");

    channelParams[5].add(apvts, ParamId::PERC2_TUNE,  "TUNE",  *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");
    channelParams[5].add(apvts, ParamId::PERC2_DECAY, "DECAY", *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");
    channelParams[5].add(apvts, ParamId::PERC2_NOISE, "NOISE", *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");
    channelParams[5].add(apvts, ParamId::PERC2_PITCH, "PITCH", *this, SpaceColours::channelColor(5), midiLearnRef, "PERC2");

    channelParams[6].add(apvts, ParamId::BASS1_CUTOFF, "CUTOFF", *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_RESO,   "RES",    *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_ENVMOD, "ENV",    *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_DECAY,  "DECAY",  *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::BASS1_ACCENT, "ACCENT", *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");
    channelParams[6].add(apvts, ParamId::SC_BASS1,     "SC",     *this, SpaceColours::channelColor(6), midiLearnRef, "BASS1");

    channelParams[7].add(apvts, ParamId::BASS2_CUTOFF, "CUTOFF", *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_RESO,   "RES",    *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_ENVMOD, "ENV",    *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_DECAY,  "DECAY",  *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::BASS2_ACCENT, "ACCENT", *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");
    channelParams[7].add(apvts, ParamId::SC_BASS2,     "SC",     *this, SpaceColours::channelColor(7), midiLearnRef, "BASS2");

    // Assign per-section physical knob hardware styles (909 Drums | Elektron Percs | Silver TB-303)
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const juce::String kStyle = (ch >= 6) ? "303" : (ch >= 4 ? "elektron" : "909");
        if (panKnobs[idx(ch)]) panKnobs[idx(ch)]->getProperties().set("knobStyle", kStyle);
        for (int k = 0; k < channelParams[idx(ch)].count; ++k)
            if (auto* kn = channelParams[idx(ch)].entries[static_cast<size_t>(k)].knob.get())
                kn->getProperties().set("knobStyle", kStyle);
    }
    if (mixHatsPanKnob) mixHatsPanKnob->getProperties().set("knobStyle", "909");

    // ── Transport PLAY/STOP & SWING ──
    playButton.setButtonText("PLAY");
    playButton.setTooltip("Internal Clock Play/Stop (Spacebar)");
    playButton.setClickingTogglesState(true);
    playButton.setToggleState(internalPlayRef.load(std::memory_order_acquire), juce::dontSendNotification);
    playButton.onClick = [this] {
        const bool on = playButton.getToggleState();
        internalPlayRef.store(on, std::memory_order_release);
    };
    addAndMakeVisible(playButton);

    swingKnob = std::make_unique<DjKnob>(ParamId::SWING, &apvts, midiLearnRef, "SWING");
    swingKnob->setTooltip("Swing (0 = straight, 1 = max shuffle)");
    swingKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonGreen);
    swingKnob->setPopupDisplayEnabled(false, false, nullptr);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ParamId::SWING, *swingKnob);
    addAndMakeVisible(*swingKnob);

    // ── 5-Page Unified Screen Tabs: SEQ | MATRIX | MIX | CHAOS | SCOPE ──
    tabSeqBtn.setButtonText("SEQ");
    tabSeqBtn.setTooltip("16-Step OLED Sequencer");
    tabSeqBtn.onClick = [this] { setCoreScreen(CoreScreen::Seq); };
    addAndMakeVisible(tabSeqBtn);

    tabMatrixBtn.setButtonText("MATRIX");
    tabMatrixBtn.setTooltip("8x16 All-Tracks Matrix Overview");
    tabMatrixBtn.onClick = [this] { setCoreScreen(CoreScreen::Matrix); };
    addAndMakeVisible(tabMatrixBtn);

    tabMixBtn.setButtonText("MIX");
    tabMixBtn.setTooltip("Secondary Voice Params & Stereo Pan Console");
    tabMixBtn.onClick = [this] { setCoreScreen(CoreScreen::Mix); };
    addAndMakeVisible(tabMixBtn);

    tabLfoBtn.setButtonText("LFO");
    tabLfoBtn.setTooltip("Beat-Synced 32-Bar LFO Modulation Matrix (8 channels, max 50% range)");
    tabLfoBtn.onClick = [this] { setCoreScreen(CoreScreen::Lfo); };
    addAndMakeVisible(tabLfoBtn);

    tabChaosBtn.setButtonText("CHAOS");
    tabChaosBtn.setComponentID("tabChaos");
    tabChaosBtn.setTooltip("Centered Chaos Sample Bus (Waveform, Sampler Meter, 5 Knobs, WAV Drop)");
    tabChaosBtn.onClick = [this] { setCoreScreen(CoreScreen::Chaos); };
    addAndMakeVisible(tabChaosBtn);

    tabScopeBtn.setButtonText("SCOPE");
    tabScopeBtn.setTooltip("Full-Screen CRT Oscilloscope & 8-Band Space Invaders Spectrum");
    tabScopeBtn.onClick = [this] { setCoreScreen(CoreScreen::Scope); };
    addAndMakeVisible(tabScopeBtn);

    // ── Core Page, Copy/Paste, Acid, and Quick VAR/RND/CLR ──
    corePrevPageBtn.setButtonText("<");
    corePrevPageBtn.setTooltip("Previous 16-step page");
    corePrevPageBtn.onClick = [this] {
        if (viewPage[idx(selectedChannel)] > 0)
        {
            --viewPage[idx(selectedChannel)];
            updateCoreButtons();
            repaint();
        }
    };
    addAndMakeVisible(corePrevPageBtn);

    coreNextPageBtn.setButtonText(">");
    coreNextPageBtn.setTooltip("Next 16-step page");
    coreNextPageBtn.onClick = [this] {
        const int maxPage = std::max(0, (sequencer.getPatternLength(selectedChannel) - 1) / VISIBLE_STEPS);
        if (viewPage[idx(selectedChannel)] < maxPage)
        {
            ++viewPage[idx(selectedChannel)];
            updateCoreButtons();
            repaint();
        }
    };
    addAndMakeVisible(coreNextPageBtn);

    coreCopyBtn.setButtonText("C");
    coreCopyBtn.setTooltip("Copy selected channel pattern (+303 notes)");
    coreCopyBtn.onClick = [this] {
        const int ch = selectedChannel;
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
    addAndMakeVisible(coreCopyBtn);

    corePasteBtn.setButtonText("P");
    corePasteBtn.setTooltip("Paste copied pattern into selected channel");
    corePasteBtn.onClick = [this] {
        const int ch = selectedChannel;
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
    addAndMakeVisible(corePasteBtn);

    acidRndBtn.setButtonText("ACID");
    acidRndBtn.setTooltip("Randomize 303 Notes, Slides, Accents & Octaves locked to Scale");
    acidRndBtn.onClick = [this] {
        if (selectedChannel >= 6)
        {
            undoRef.push(sequencer);
            sequencer.randomizeBassAcid(selectedChannel - 6);
            repaint();
        }
    };
    addAndMakeVisible(acidRndBtn);

    acidScaleBtn.setButtonText(Sequencer::acidScaleName(sequencer.acidScale.load(std::memory_order_relaxed)));
    acidScaleBtn.setTooltip("Acid Scale Lock: PHR23 / DMIN / HYP2 / DIM");
    acidScaleBtn.onClick = [this] {
        const int next = (sequencer.acidScale.load(std::memory_order_relaxed) + 1) % static_cast<int>(Sequencer::AcidScale::Count);
        sequencer.acidScale.store(next, std::memory_order_release);
        acidScaleBtn.setButtonText(Sequencer::acidScaleName(next));
        repaint();
    };
    addAndMakeVisible(acidScaleBtn);

    hubVarBtn.setButtonText("VAR");
    hubVarBtn.setTooltip("Toggle Auto-Variation on selected channel");
    hubVarBtn.setClickingTogglesState(false);
    hubVarBtn.onClick = [this] {
        const bool cur = sequencer.autoVar[idx(selectedChannel)].load(std::memory_order_acquire);
        sequencer.autoVar[idx(selectedChannel)].store(!cur, std::memory_order_release);
        syncFromSequencer();
        updateCoreButtons();
    };
    addAndMakeVisible(hubVarBtn);

    hubRndBtn.setButtonText("RND");
    hubRndBtn.setTooltip("Smart-randomize selected channel pattern");
    hubRndBtn.onClick = [this] {
        undoRef.push(sequencer);
        sequencer.smartRandomize(selectedChannel);
        repaint();
    };
    addAndMakeVisible(hubRndBtn);

    hubClrBtn.setButtonText("CLR");
    hubClrBtn.setTooltip("Clear selected channel pattern");
    hubClrBtn.onClick = [this] {
        undoRef.push(sequencer);
        sequencer.clearChannel(selectedChannel);
        repaint();
    };
    addAndMakeVisible(hubClrBtn);

    setSelectedChannel(0);
    setCoreScreen(CoreScreen::Seq);
    syncFromSequencer();
    startTimerHz(60);
}

StepGrid::~StepGrid() { stopTimer(); }

std::array<StepGrid::StripKnobSlot, 4> StepGrid::getStripKnobSlots(int ch) const
{
    std::array<StripKnobSlot, 4> slots {};
    if (ch == 2) // HATS: DECAY, TONE, BODY, PAN (VOL is on the vertical fader!)
    {
        for (int i = 0; i < 3; ++i)
            slots[static_cast<size_t>(i)] = { channelParams[2].entries[static_cast<size_t>(i)].knob.get(),
                                              channelParams[2].entries[static_cast<size_t>(i)].label };
        slots[3] = { panKnobs[2].get(), "PAN" };
    }
    else // All other 7 channels: 4 primary synth knobs (VOL is on the vertical fader!)
    {
        for (int i = 0; i < 4; ++i)
            slots[static_cast<size_t>(i)] = { channelParams[idx(ch)].entries[static_cast<size_t>(i)].knob.get(),
                                              channelParams[idx(ch)].entries[static_cast<size_t>(i)].label };
    }
    return slots;
}

void StepGrid::setSelectedChannel(int ch)
{
    selectedChannel = std::clamp(ch, 0, NUM_CH - 1);
    for (int i = 0; i < NUM_CH; ++i)
        selectButtons[idx(i)].setToggleState(i == selectedChannel, juce::dontSendNotification);
    updateCoreButtons();
    if (onSelectedChannelChanged)
        onSelectedChannelChanged(selectedChannel);
    repaint();
}

void StepGrid::setCoreScreen(CoreScreen s)
{
    coreScreen = s;
    tabSeqBtn.setToggleState(s == CoreScreen::Seq, juce::dontSendNotification);
    tabMatrixBtn.setToggleState(s == CoreScreen::Matrix, juce::dontSendNotification);
    tabMixBtn.setToggleState(s == CoreScreen::Mix, juce::dontSendNotification);
    tabLfoBtn.setToggleState(s == CoreScreen::Lfo, juce::dontSendNotification);
    tabChaosBtn.setToggleState(s == CoreScreen::Chaos, juce::dontSendNotification);
    tabScopeBtn.setToggleState(s == CoreScreen::Scope, juce::dontSendNotification);
    resized();
    if (onCoreScreenChanged)
        onCoreScreenChanged(coreScreen);
    repaint();
}

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
        syncOne(panKnobs[idx(ch)].get());
        channelParams[idx(ch)].syncGhosts(apvts, savedState);
    }
    syncOne(swingKnob.get());
}

void StepGrid::refreshThemeColors()
{
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto col = SpaceColours::channelColor(ch);
        selectButtons[idx(ch)].setColour(juce::TextButton::buttonOnColourId, col);
        if (volFaders[idx(ch)]) volFaders[idx(ch)]->setColour(juce::Slider::trackColourId, col);
        if (panKnobs[idx(ch)])  panKnobs[idx(ch)]->setColour(juce::Slider::rotarySliderFillColourId, col.withMultipliedSaturation(0.75f));
        for (int k = 0; k < channelParams[idx(ch)].count; ++k)
            if (auto* kn = channelParams[idx(ch)].entries[static_cast<size_t>(k)].knob.get())
                kn->setColour(juce::Slider::rotarySliderFillColourId, col);
    }
    if (mixHatsPanKnob)
        mixHatsPanKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::channelColor(2).withMultipliedSaturation(0.75f));
    if (swingKnob)
        swingKnob->setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::theme().masterAccent);
    rebuildStaticBackdrop();
    repaint();
}

void StepGrid::triggerMuteToggle(int ch)
{
    if (ch < 0 || ch >= NUM_CH) return;
    const bool playing = sequencer.isPlaying.load(std::memory_order_acquire);
    const int  qnt     = sequencer.quantizeMode.load(std::memory_order_relaxed);
    if (playing && qnt > 0)
        sequencer.queuedMuteMask.fetch_xor(static_cast<uint8_t>(1u << ch), std::memory_order_acq_rel);
    else
        sequencer.mute[idx(ch)].store(!sequencer.mute[idx(ch)].load(std::memory_order_acquire), std::memory_order_release);
    syncFromSequencer();
}

void StepGrid::updateLenButton(int ch)
{
    const int len = sequencer.getPatternLength(ch);
    const juce::String lbl = (len % 16 == 0) ? (juce::String(len / 16) + "B")
                                             : (juce::String(len) + "S");
    lenButtons[idx(ch)].setButtonText(lbl);
    lenButtons[idx(ch)].setTooltip("Length: " + juce::String(len) + " steps (Click: 1B-4B | Shift/Alt or Wheel: +/-1)");
}

void StepGrid::updateCoreButtons()
{
    const int len = sequencer.getPatternLength(selectedChannel);
    const int maxPage = std::max(0, (len - 1) / VISIBLE_STEPS);
    viewPage[idx(selectedChannel)] = std::min(viewPage[idx(selectedChannel)], maxPage);
    corePrevPageBtn.setEnabled(viewPage[idx(selectedChannel)] > 0);
    coreNextPageBtn.setEnabled(viewPage[idx(selectedChannel)] < maxPage);

    const bool isBass = (selectedChannel >= 6) && (coreScreen == CoreScreen::Seq);
    acidRndBtn.setVisible(isBass);
    acidScaleBtn.setVisible(isBass);
    hubVarBtn.setToggleState(sequencer.autoVar[idx(selectedChannel)].load(std::memory_order_acquire),
                             juce::dontSendNotification);
}

void StepGrid::syncFromSequencer()
{
    bool changed = false;
    const bool intPlay = internalPlayRef.load(std::memory_order_acquire);
    if (playButton.getToggleState() != intPlay)
    {
        playButton.setToggleState(intPlay, juce::dontSendNotification);
        changed = true;
    }

    const juce::String scLbl = Sequencer::acidScaleName(sequencer.acidScale.load(std::memory_order_relaxed));
    if (acidScaleBtn.getButtonText() != scLbl)
    {
        acidScaleBtn.setButtonText(scLbl);
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
            muteButtons[idx(ch)].repaint();
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

        const bool lfoOn = lfoBankRef.channels[idx(ch)].enabled.load(std::memory_order_relaxed);
        if (lfoButtons[idx(ch)].getToggleState() != lfoOn)
        {
            lfoButtons[idx(ch)].setToggleState(lfoOn, juce::dontSendNotification);
            changed = true;
        }

        const int len = sequencer.getPatternLength(ch);
        const juce::String expectedLen = (len % 16 == 0) ? (juce::String(len / 16) + "B")
                                                         : (juce::String(len) + "S");
        if (lenButtons[idx(ch)].getButtonText() != expectedLen)
        {
            updateLenButton(ch);
            if (ch == selectedChannel) updateCoreButtons();
            changed = true;
        }
    }

    const bool selVar = sequencer.autoVar[idx(selectedChannel)].load(std::memory_order_acquire);
    if (hubVarBtn.getToggleState() != selVar)
        hubVarBtn.setToggleState(selVar, juce::dontSendNotification);

    if (changed) repaint();
}

void StepGrid::rebuildStaticBackdrop()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    lastBackdropTheme = SpaceColours::activeTheme();
    staticBackdrop = juce::Image(juce::Image::RGB, w, h, true);
    juce::Graphics g(staticBackdrop);
    const auto& th = SpaceColours::theme();
    g.fillAll(th.bg);

    juce::ColourGradient grad(th.bg.brighter(0.05f), 0.0f, 0.0f,
                              th.bg.darker(0.14f), 0.0f, static_cast<float>(h), false);
    g.setGradientFill(grad);
    g.fillAll();

    if (noiseTile.isValid())
    {
        g.setTiledImageFill(noiseTile, 0, 0, 0.16f);
        g.fillAll();
    }
}

// ── Layout Geometry ──────────────────────────────────────────────────────────

juce::Rectangle<int> StepGrid::stripRect(int ch) const
{
    const bool rightCol = (ch >= 4);
    const int row = ch % 4;
    const int x = rightCol ? (CORE_X + CORE_W + 4) : 4;
    return { x, row * STRIP_H + 2, STRIP_W, STRIP_H - 4 };
}

juce::Rectangle<int> StepGrid::statusCrtRect() const
{
    return { CORE_X + 12, 10, CORE_W - 24, 96 };
}

juce::Rectangle<int> StepGrid::bpmBoxRect() const
{
    const auto crt = statusCrtRect();
    return { crt.getRight() - 114, crt.getY() + 9, 104, 62 };
}

juce::Rectangle<float> StepGrid::stripFlipLfoRect(int ch) const
{
    const auto sr = stripRect(ch);
    const bool rightCol = (ch >= 4);
    const int knobAreaX = rightCol ? (sr.getX() + 8) : (sr.getX() + 86);
    const int knobAreaW = sr.getWidth() - 94;
    if (ch == 0 || ch == 6 || ch == 7) // 3 setup knobs (left 3/4) + compact LFO card (right 1/4)
        return { static_cast<float>(knobAreaX + knobAreaW - 116), static_cast<float>(sr.getY() + 36), 114.0f, 80.0f };
    if (ch == 2) // HATS: full-width spacious LFO card
        return { static_cast<float>(knobAreaX + 8), static_cast<float>(sr.getY() + 38), static_cast<float>(knobAreaW - 16), 76.0f };
    // SNARE / TOM / PERC1 / PERC2: 1 PAN knob (left) + spacious LFO card (right)
    return { static_cast<float>(knobAreaX + 74), static_cast<float>(sr.getY() + 38), static_cast<float>(knobAreaW - 82), 76.0f };
}

void StepGrid::resized()
{
    rebuildStaticBackdrop();

    // Hide all secondary knobs before placing active ones
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        panKnobs[idx(ch)]->setVisible(false);
        for (int k = 0; k < channelParams[idx(ch)].count; ++k)
            channelParams[idx(ch)].entries[static_cast<size_t>(k)].knob->setVisible(false);
    }
    if (mixHatsPanKnob) mixHatsPanKnob->setVisible(false);

    // ── 8 Mirror-Symmetric Channel Strips: 30px Header + FLIP (Face A Live / Face B Setup+LFO) ──
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto sr = stripRect(ch);
        const bool rightCol = (ch >= 4);
        const int hdrY = sr.getY() + 6;

        selectButtons[idx(ch)].setBounds(sr.getRight() - 52,  hdrY, 46, 18);
        lenButtons[idx(ch)].setBounds   (sr.getRight() - 80,  hdrY, 25, 18);
        humButtons[idx(ch)].setBounds   (sr.getRight() - 99,  hdrY, 17, 18);
        varButtons[idx(ch)].setBounds   (sr.getRight() - 128, hdrY, 27, 18);
        lfoButtons[idx(ch)].setBounds   (sr.getRight() - 158, hdrY, 28, 18);

        // Mirror-symmetric Meter + Vertical VOL Fader + M/S + FLIP button
        const int bodyY = sr.getY() + 34;
        const int bodyH = sr.getHeight() - 40;
        int knobAreaX = 0;
        const int knobAreaW = sr.getWidth() - 92;

        if (!rightCol)
        {
            volFaders[idx(ch)]->setBounds(sr.getX() + 24, bodyY, 26, bodyH - 12);
            muteButtons[idx(ch)].setBounds(sr.getX() + 57, bodyY + 14, 20, 20);
            soloButtons[idx(ch)].setBounds(sr.getX() + 57, bodyY + 42, 20, 20);
            flipButtons[idx(ch)].setBounds(sr.getRight() - 38, sr.getBottom() - 20, 32, 15);
            knobAreaX = sr.getX() + 86;
        }
        else
        {
            knobAreaX = sr.getX() + 6;
            muteButtons[idx(ch)].setBounds(sr.getRight() - 77, bodyY + 14, 20, 20);
            soloButtons[idx(ch)].setBounds(sr.getRight() - 77, bodyY + 42, 20, 20);
            flipButtons[idx(ch)].setBounds(sr.getX() + 6, sr.getBottom() - 20, 32, 15);
            volFaders[idx(ch)]->setBounds(sr.getRight() - 50, bodyY, 26, bodyH - 12);
        }

        if (!isFlipped[idx(ch)])
        {
            // FACE A (LIVE): 4 uniform 52px knobs centered horizontally below 30px header
            const auto slots = getStripKnobSlots(ch);
            const float pitch = static_cast<float>(knobAreaW) / 4.0f;
            const int knobY = sr.getY() + 45;
            for (int col = 0; col < 4; ++col)
            {
                if (auto* k = slots[static_cast<size_t>(col)].knob)
                {
                    const int kx = knobAreaX + static_cast<int>(std::round((static_cast<float>(col) + 0.5f) * pitch)) - KNOB_SZ / 2;
                    k->setBounds(kx, knobY, KNOB_SZ, KNOB_SZ);
                    k->setVisible(true);
                }
            }
        }
        else
        {
            // FACE B (SETUP + LOCAL LFO): secondary voice knobs + PAN on left, LFO card on right
            const int ky = sr.getY() + 46;
            const int sz = 48;
            if (ch == 0) // KICK: PTCHE, SUB, PAN
            {
                DjKnob* ks[3] = { channelParams[0].entries[4].knob.get(), channelParams[0].entries[5].knob.get(), panKnobs[0].get() };
                for (int i = 0; i < 3; ++i)
                    if (ks[i]) { ks[i]->setBounds(knobAreaX + 8 + i * 68, ky, sz, sz); ks[i]->setVisible(true); }
            }
            else if (ch == 6 || ch == 7) // BASS 1/2: ACCENT, SC, PAN
            {
                DjKnob* ks[3] = { channelParams[idx(ch)].entries[4].knob.get(), channelParams[idx(ch)].entries[5].knob.get(), panKnobs[idx(ch)].get() };
                for (int i = 0; i < 3; ++i)
                    if (ks[i]) { ks[i]->setBounds(knobAreaX + 8 + i * 68, ky, sz, sz); ks[i]->setVisible(true); }
            }
            else if (ch != 2) // SNARE, TOM, PERC1, PERC2: PAN
            {
                if (panKnobs[idx(ch)]) { panKnobs[idx(ch)]->setBounds(knobAreaX + 12, ky, sz, sz); panKnobs[idx(ch)]->setVisible(true); }
            }
        }
    }

    // ── Center Core Transport: SWING knob (with breathing room!) + PLAY button ──
    swingKnob->setBounds(CORE_X + 51, 136, 26, 26);
    playButton.setBounds(CORE_X + 88, 134, 46, 28);

    // ── 6-Page Screen Selector Tabs (SEQ | MATRIX | MIX | LFO | CHAOS | SCOPE) ──
    const int barY = 179;
    tabSeqBtn.setBounds   (CORE_X + 10,  barY, 34, 20);
    tabMatrixBtn.setBounds(CORE_X + 46,  barY, 46, 20);
    tabMixBtn.setBounds   (CORE_X + 94,  barY, 32, 20);
    tabLfoBtn.setBounds   (CORE_X + 128, barY, 32, 20);
    tabChaosBtn.setBounds (CORE_X + 162, barY, 44, 20);
    tabScopeBtn.setBounds (CORE_X + 208, barY, 42, 20);

    coreCopyBtn.setBounds    (CORE_X + 298, barY, 22, 20);
    corePasteBtn.setBounds   (CORE_X + 322, barY, 22, 20);
    corePrevPageBtn.setBounds(CORE_X + 350, barY, 20, 20);
    coreNextPageBtn.setBounds(CORE_X + 388, barY, 20, 20);

    // ── Screen-Specific Interactive Controls inside mainScreenRect ──
    const auto scr = mainScreenRect();
    const bool showSeqFooter = (coreScreen == CoreScreen::Seq);
    hubVarBtn.setVisible(showSeqFooter);
    hubRndBtn.setVisible(showSeqFooter);
    hubClrBtn.setVisible(showSeqFooter);
    acidRndBtn.setVisible(showSeqFooter && selectedChannel >= 6);
    acidScaleBtn.setVisible(showSeqFooter && selectedChannel >= 6);

    if (showSeqFooter)
    {
        const int fy = scr.getBottom() - 28;
        // Balanced distribution: VAR on left | ACID + SCALE centered (on 303s) | RND + CLR on right
        hubVarBtn.setBounds   (scr.getX() + 14,       fy, 46, 20);
        acidRndBtn.setBounds  (scr.getCentreX() - 48, fy, 44, 20);
        acidScaleBtn.setBounds(scr.getCentreX() + 2,  fy, 48, 20);
        hubRndBtn.setBounds   (scr.getRight() - 106,  fy, 44, 20);
        hubClrBtn.setBounds   (scr.getRight() - 56,   fy, 44, 20);
    }

    // ── MIX Screen Knobs (Secondary voice params + symmetric 4x2 PAN 1..8 Console) ──
    if (coreScreen == CoreScreen::Mix)
    {
        const int mx = scr.getX() + 14;
        const int mw = scr.getWidth() - 28;
        DjKnob* secKnobs[6] = {
            channelParams[0].entries[4].knob.get(), // KICK P-ENV
            channelParams[0].entries[5].knob.get(), // KICK SUB
            channelParams[6].entries[4].knob.get(), // BASS1 ACCENT
            channelParams[6].entries[5].knob.get(), // BASS1 SC
            channelParams[7].entries[4].knob.get(), // BASS2 ACCENT
            channelParams[7].entries[5].knob.get()  // BASS2 SC
        };
        const float p1 = static_cast<float>(mw) / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            if (secKnobs[i])
            {
                const int kx = mx + static_cast<int>(std::round((static_cast<float>(i) + 0.5f) * p1)) - 22;
                secKnobs[i]->setBounds(kx, scr.getY() + 56, 44, 44);
                secKnobs[i]->setVisible(true);
            }
        }

        // Complete symmetric 4x2 Pan Console (all 8 channels, HATS synced with Strip 3!)
        const float rowPitch = static_cast<float>(mw) / 4.0f;
        for (int ch = 0; ch < NUM_CH; ++ch)
        {
            const int row = ch / 4;
            const int col = ch % 4;
            const int kx = mx + static_cast<int>(std::round((static_cast<float>(col) + 0.5f) * rowPitch)) - 23;
            const int ky = scr.getY() + (row == 0 ? 156 : 250);
            DjKnob* pk = (ch == 2) ? mixHatsPanKnob.get() : panKnobs[idx(ch)].get();
            if (pk)
            {
                pk->setBounds(kx, ky, 46, 46);
                pk->setVisible(true);
            }
        }
    }
}

// ── Timer (60 FPS) ───────────────────────────────────────────────────────────

void StepGrid::timerCallback()
{
    syncFromSequencer();

    {
        const int head = analyzerRef.getScopeHead();
        auto& dst = scopeHistory[static_cast<size_t>(scopeHistWrite)];
        for (int i = 0; i < OutputAnalyzer::SCOPE_SIZE; ++i)
            dst[static_cast<size_t>(i)] = analyzerRef.getScopeSample(head + i);
        scopeHistWrite = (scopeHistWrite + 1) % SCOPE_HIST;
    }

    const bool playing = sequencer.isPlaying.load(std::memory_order_acquire);
    const int64_t nowSample = visualState.sampleCounter.load(std::memory_order_acquire);
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const int curStep = sequencer.currentSteps[idx(ch)].load(std::memory_order_acquire);
        const int pageOff = viewPage[idx(ch)] * VISIBLE_STEPS;
        const int64_t firedAt = visualState.lastFireSample[idx(ch)].load(std::memory_order_acquire);
        const float firedVel  = visualState.lastFireVel[idx(ch)].load(std::memory_order_acquire);
        float chFire = 0.0f;
        if (firedAt != INT64_MIN && nowSample >= firedAt)
        {
            const float ageSec = static_cast<float>(nowSample - firedAt) / 44100.0f;
            if (ageSec < 0.28f)
                chFire = firedVel * (1.0f - ageSec / 0.28f);
        }
        channelTriggerGlow[idx(ch)] = std::max(chFire, channelTriggerGlow[idx(ch)] * 0.85f);

        // Calibrated 500ms Peak-Hold (30 ticks @ 60Hz) for per-channel real audio meters (#16)
        const float vPk = std::clamp(analyzerRef.getVoicePeak(ch) * 1.15f, 0.0f, 1.0f);
        if (vPk >= voicePeakHold[idx(ch)]) { voicePeakHold[idx(ch)] = vPk; voicePeakTicks[idx(ch)] = 30; }
        else if (voicePeakTicks[idx(ch)] > 0) { --voicePeakTicks[idx(ch)]; }
        else { voicePeakHold[idx(ch)] = std::max(vPk, voicePeakHold[idx(ch)] - 0.022f); }

        for (int s = 0; s < VISIBLE_STEPS; ++s)
        {
            const int absStep = pageOff + s;
            const bool hit = playing && (curStep == absStep) && sequencer.getStep(ch, absStep);
            float& glow = stepLedGlow[idx(ch)][static_cast<size_t>(s)];
            glow = hit ? 1.0f : (glow * 0.82f);
        }
        channelParams[idx(ch)].refreshMidiLearnVisuals();
        if (panKnobs[idx(ch)]) panKnobs[idx(ch)]->refreshLearnVisual();

        // Drive Live Modulation Rings (#11) on BeatLfo target knobs!
        // Target mapping matches VoiceBank.cpp (ch -> slot0 param idx, slot1 param idx)
        static constexpr int kTargetEntry[8][2] = {
            { 0, 1 }, { 0, 2 }, { 0, 1 }, { 0, 1 },
            { 0, 3 }, { 0, 3 }, { 0, 1 }, { 0, 1 }
        };
        for (int k = 0; k < channelParams[idx(ch)].count; ++k)
        {
            float mOff = 0.0f;
            if (k == kTargetEntry[ch][0]) mOff = lfoBankRef.getOffsetFor(ch, 0);
            else if (k == kTargetEntry[ch][1]) mOff = lfoBankRef.getOffsetFor(ch, 1);
            if (auto* kn = channelParams[idx(ch)].entries[static_cast<size_t>(k)].knob.get())
                kn->setModOffset(mOff);
        }
    }
    if (swingKnob) swingKnob->refreshLearnVisual();

    repaint();
}

// ── Painting ─────────────────────────────────────────────────────────────────

void StepGrid::paint(juce::Graphics& g)
{
    if (lastBackdropTheme != SpaceColours::activeTheme()
        || staticBackdrop.getWidth() != getWidth()
        || staticBackdrop.getHeight() != getHeight())
        rebuildStaticBackdrop();

    if (staticBackdrop.isValid())
        g.drawImageAt(staticBackdrop, 0, 0);
    else
        g.fillAll(SpaceColours::bg);

    paintChannelStrips(g);
    paintCoreFrameAndStatus(g);
    paintMainScreen(g);
}

void StepGrid::paintChannelStrips(juce::Graphics& g)
{
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto sr = stripRect(ch).toFloat();
        const bool rightCol = (ch >= 4);
        const auto chCol = SpaceColours::channelColor(ch);
        const bool selected = (ch == selectedChannel);
        const bool muted = sequencer.mute[idx(ch)].load(std::memory_order_acquire);
        // Real post-fader per-channel audio peak meter (reacts to VOL fader & synth decay!)
        const float voiceAudio = analyzerRef.getVoicePeak(ch);
        const float liveLevel = muted ? 0.0f : std::clamp(voiceAudio * 1.15f, 0.0f, 1.0f);

        // Per-Section Hardware Faceplate (909 Drum | Deeper Carbon Elektron Perc | Brushed Silver-Graphite TB-303!)
        const auto& th = SpaceColours::theme();
        g.setColour(muted ? th.rowBgMuted : SpaceColours::stripPanelColor(ch));
        g.fillRoundedRectangle(sr, 4.0f);
        if (th.isGlossy && !muted)
        {
            // Gleuomorphic Piano-Black glass lacquer reflection across top half of each strip!
            juce::ColourGradient pianoSheen(juce::Colours::white.withAlpha(0.085f), sr.getX(), sr.getY(),
                                            juce::Colours::transparentWhite, sr.getX(), sr.getY() + sr.getHeight() * 0.45f, false);
            g.setGradientFill(pianoSheen);
            g.fillRoundedRectangle(sr, 4.0f);
        }
        else if (th.isLight && !muted)
        {
            // Subtle luxury ceramic / marble satin top highlight
            juce::ColourGradient porcelainSheen(juce::Colours::white.withAlpha(0.55f), sr.getX(), sr.getY(),
                                                juce::Colours::transparentWhite, sr.getX(), sr.getBottom(), false);
            g.setGradientFill(porcelainSheen);
            g.fillRoundedRectangle(sr, 4.0f);
        }
        else if (ch >= 6 && !muted)
        {
            // Authentic brushed silver-graphite 303 vertical sheen
            juce::ColourGradient sheen303(juce::Colours::white.withAlpha(0.055f), sr.getX(), sr.getY(),
                                          juce::Colours::transparentWhite, sr.getX(), sr.getBottom(), false);
            g.setGradientFill(sheen303);
            g.fillRoundedRectangle(sr, 4.0f);
        }
        if (selected)
        {
            g.setColour(chCol.withAlpha(0.038f));
            g.fillRoundedRectangle(sr, 4.0f);
        }

        if (noiseTile.isValid())
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(sr.toNearestInt());
            g.setTiledImageFill(noiseTile, ch * 31, ch * 17, 0.24f);
            g.fillRect(sr);
        }

        // Top 1px metallic highlight bevel
        g.setColour(juce::Colours::white.withAlpha(0.065f));
        g.drawHorizontalLine(static_cast<int>(sr.getY()) + 1, sr.getX() + 4.0f, sr.getRight() - 4.0f);

        // Subtle refined taller 30px top strip header
        const auto hdr = sr.withHeight(30.0f);
        g.setColour(selected ? th.stripHeader.brighter(0.06f) : th.stripHeader);
        g.fillRoundedRectangle(hdr, 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawHorizontalLine(static_cast<int>(hdr.getBottom()), hdr.getX() + 1.0f, hdr.getRight() - 1.0f);
        if (ch < 4)
        {
            // Subtle Roland 909 silkscreen accent line under header
            g.setColour(chCol.withAlpha(muted ? 0.12f : 0.32f));
            g.drawHorizontalLine(static_cast<int>(hdr.getBottom()) - 1, hdr.getX() + 8.0f, hdr.getRight() - 8.0f);
        }

        // Minimal numbered channel indicator + geometric engine glyph (+ optional Trigger Halo!)
        const int vizM = SpaceColours::vizMode();
        const float trigGlow = muted ? 0.0f : channelTriggerGlow[idx(ch)];
        const auto badge = juce::Rectangle<float>(sr.getX() + 7.0f, sr.getY() + 7.0f, 16.0f, 16.0f);
        if (vizM >= SpaceColours::VIZ_SUBTLE && trigGlow > 0.04f)
        {
            const float mul = (vizM == SpaceColours::VIZ_RAVE) ? 0.45f : 0.24f;
            g.setColour(chCol.withAlpha(trigGlow * mul));
            g.fillRoundedRectangle(badge.expanded(2.5f), 4.5f);
            // Crisp beat-synced micro-pulse along top bevel of strip
            g.setColour(chCol.withAlpha(trigGlow * (vizM == SpaceColours::VIZ_RAVE ? 0.55f : 0.28f)));
            g.drawHorizontalLine(static_cast<int>(sr.getY()) + 1, sr.getX() + 6.0f, sr.getRight() - 6.0f);
        }
        g.setColour(muted ? chCol.withAlpha(0.22f) : chCol.withAlpha(selected ? 0.92f : 0.72f));
        g.fillRoundedRectangle(badge, 3.0f);
        g.setFont(SpaceArt::uiFont(10.0f, true));
        g.setColour(juce::Colour(0xff0d0e12));
        g.drawText(juce::String(ch + 1), badge.toNearestInt(), juce::Justification::centred);

        // Channel title (clean geometric sans-serif!)
        g.setFont(SpaceArt::uiFont(12.0f, true));
        g.setColour(muted ? SpaceColours::txtDim() : (selected ? SpaceColours::txtBright() : SpaceColours::txtMid().brighter(th.isLight ? -0.15f : 0.25f)));
        g.drawText(kChannelTitles[ch], static_cast<int>(badge.getRight() + 8), static_cast<int>(sr.getY() + 5),
                   100, 20, juce::Justification::centredLeft);

        // Constant-stroke 1.25px geometric engine glyph (909 sine/snap/hat/tom | FM/Noise | 303 SAW/SQR)
        SpaceArt::drawChannelGlyph(g, ch, badge.getRight() + 92.0f, sr.getY() + 10.0f, 16.0f, 10.0f,
                                   muted ? SpaceColours::txtDim() : chCol);

        // Live trigger LED dot neatly aligned before [LFO]
        {
            const float lx = sr.getRight() - 170.0f;
            const float ly = sr.getY() + 12.0f;
            if (liveLevel > 0.08f)
            {
                g.setColour(chCol.withAlpha(liveLevel * 0.45f));
                g.fillEllipse(lx - 3.0f, ly - 3.0f, 12.0f, 12.0f);
            }
            g.setColour(liveLevel > 0.08f ? chCol : (th.isLight ? juce::Colour(0xffc7c5bc) : juce::Colour(0xff252933)));
            g.fillEllipse(lx, ly, 6.0f, 6.0f);
        }

        // Optional [909 kick] / [303 acid] tag (neatly spaced after the geometric engine glyph)
        if (kChannelBadge[ch][0] != '\0')
        {
            g.setFont(SpaceArt::uiFont(8.2f, false));
            g.setColour(SpaceColours::txtDim());
            g.drawText(kChannelBadge[ch], static_cast<int>(badge.getRight() + 114), static_cast<int>(sr.getY() + 8),
                       66, 14, juce::Justification::centredLeft);
        }

        // Mirror-Symmetric Per-Channel Vertical LED Meter with 500ms Peak-Hold
        {
            const float mx = rightCol ? (sr.getRight() - 17.0f) : (sr.getX() + 9.0f);
            const auto meterR = juce::Rectangle<float>(mx, sr.getY() + 38.0f, 8.0f, sr.getHeight() - 54.0f);
            drawChannelPillMeter(g, meterR, liveLevel, 10, chCol, false, muted ? 0.0f : voicePeakHold[idx(ch)]);

            const auto fb = volFaders[idx(ch)]->getBounds();
            g.setFont(SpaceArt::silkFont(7.2f, true));
            g.setColour(SpaceColours::txtDim());
            g.drawText("VOL", fb.getX() - 4, static_cast<int>(sr.getBottom()) - 17, fb.getWidth() + 8, 12, juce::Justification::centred);
        }

        // Face A (Live) vs Face B (Setup + Local LFO Card)
        if (!isFlipped[idx(ch)])
        {
            const auto slots = getStripKnobSlots(ch);
            // RAVE mode: 303 Acid Shimmer around CUTOFF knob when RES > 68% and channel triggers!
            if (vizM == SpaceColours::VIZ_RAVE && (ch == 6 || ch == 7) && trigGlow > 0.08f && slots[0].knob)
            {
                const float resVal = apvtsRef.getRawParameterValue(ch == 6 ? ParamId::BASS1_RESO : ParamId::BASS2_RESO)->load();
                if (resVal > 0.68f)
                {
                    const auto kb = slots[0].knob->getBounds().toFloat();
                    const float intensity = std::clamp((resVal - 0.68f) / 0.32f, 0.0f, 1.0f) * trigGlow;
                    g.setColour(chCol.withAlpha(0.35f * intensity));
                    g.drawEllipse(kb.reduced(1.0f), 2.2f);
                }
            }
            for (const auto& sl : slots)
            {
                if (sl.knob)
                {
                    const auto kb = sl.knob->getBounds();
                    const auto lblR = juce::Rectangle<int>(kb.getX() - 10, kb.getBottom() + 4, kb.getWidth() + 20, 13);
                    if (sl.knob->isShowingValue())
                        SpaceArt::drawValueWithUnit(g, sl.knob->getCompactValueText(), lblR, 8.5f, SpaceColours::txtBright());
                    else
                    {
                        g.setFont(SpaceArt::silkFont(8.2f, true));
                        g.setColour(muted ? SpaceColours::txtDim() : SpaceColours::txtMid());
                        g.drawText(sl.label, lblR, juce::Justification::centred);
                    }
                }
            }
        }
        else
        {
            // Draw labels for visible Face B setup knobs
            auto drawSetupLbl = [&](DjKnob* k, const char* lbl) {
                if (!k || !k->isVisible()) return;
                const auto kb = k->getBounds();
                const auto lblR = juce::Rectangle<int>(kb.getX() - 8, kb.getBottom() + 4, kb.getWidth() + 16, 13);
                if (k->isShowingValue())
                    SpaceArt::drawValueWithUnit(g, k->getCompactValueText(), lblR, 8.2f, juce::Colours::white);
                else
                {
                    g.setFont(SpaceArt::silkFont(8.0f, true));
                    g.setColour(SpaceColours::textMid);
                    g.drawText(lbl, lblR, juce::Justification::centred);
                }
            };
            if (ch == 0) { drawSetupLbl(channelParams[0].entries[4].knob.get(), "PTCHE"); drawSetupLbl(channelParams[0].entries[5].knob.get(), "SUB"); drawSetupLbl(panKnobs[0].get(), "PAN"); }
            else if (ch == 6 || ch == 7) { drawSetupLbl(channelParams[idx(ch)].entries[4].knob.get(), "ACCENT"); drawSetupLbl(channelParams[idx(ch)].entries[5].knob.get(), "SC"); drawSetupLbl(panKnobs[idx(ch)].get(), "PAN"); }
            else if (ch != 2) { drawSetupLbl(panKnobs[idx(ch)].get(), "PAN"); }

            // Local Channel LFO Card (interactive: click TARGET / RATE / WAVE / DEPTH directly on strip!)
            const auto lr = stripFlipLfoRect(ch);
            const auto& lc = lfoBankRef.channels[idx(ch)];
            const bool lOn = lc.enabled.load(std::memory_order_relaxed);
            const auto lCol = SpaceColours::themed(SpaceColours::neonPurple);
            g.setColour(juce::Colour(0xff0f1116));
            g.fillRoundedRectangle(lr, 4.0f);
            g.setColour(lOn ? lCol.withAlpha(0.55f) : SpaceColours::sectionLine.withAlpha(0.55f));
            g.drawRoundedRectangle(lr, 4.0f, 0.9f);

            g.setFont(SpaceArt::silkFont(7.2f, true));
            g.setColour(lOn ? lCol : SpaceColours::textDim);
            g.drawText(juce::String("LFO // ") + BeatLfoBank::targetName(ch, lc.targetIdx.load(std::memory_order_relaxed)),
                       static_cast<int>(lr.getX() + 6), static_cast<int>(lr.getY() + 4), static_cast<int>(lr.getWidth() - 12), 13, juce::Justification::centredLeft);

            // Rate + Wave row
            const auto midR = juce::Rectangle<float>(lr.getX() + 6.0f, lr.getY() + 22.0f, lr.getWidth() - 12.0f, 20.0f);
            const float halfW = (midR.getWidth() - 4.0f) * 0.5f;
            g.setColour(juce::Colour(0xff171a22));
            g.fillRoundedRectangle(midR.getX(), midR.getY(), halfW, midR.getHeight(), 3.0f);
            g.fillRoundedRectangle(midR.getX() + halfW + 4.0f, midR.getY(), halfW, midR.getHeight(), 3.0f);
            g.setFont(SpaceArt::monoFont(8.5f, true));
            g.setColour(lOn ? SpaceColours::neonCyan : SpaceColours::textMid);
            g.drawText(BeatLfoBank::rateName(lc.rateIdx.load(std::memory_order_relaxed)),
                       static_cast<int>(midR.getX()), static_cast<int>(midR.getY()), static_cast<int>(halfW), static_cast<int>(midR.getHeight()), juce::Justification::centred);
            g.setColour(lOn ? SpaceColours::neonYellow : SpaceColours::textMid);
            g.drawText(BeatLfoBank::waveName(lc.waveIdx.load(std::memory_order_relaxed)),
                       static_cast<int>(midR.getX() + halfW + 4.0f), static_cast<int>(midR.getY()), static_cast<int>(halfW), static_cast<int>(midR.getHeight()), juce::Justification::centred);

            // Depth bar (0..50%)
            const auto depR = juce::Rectangle<float>(lr.getX() + 6.0f, lr.getBottom() - 24.0f, lr.getWidth() - 12.0f, 18.0f);
            const float dep = lc.depth.load(std::memory_order_relaxed);
            g.setColour(juce::Colour(0xff14171f));
            g.fillRoundedRectangle(depR, 3.0f);
            g.setColour((lOn ? lCol : SpaceColours::textDim).withAlpha(lOn ? 0.50f : 0.22f));
            g.fillRoundedRectangle(depR.getX() + 2.0f, depR.getY() + 2.0f, (dep / 0.50f) * (depR.getWidth() - 4.0f), depR.getHeight() - 4.0f, 2.0f);
            g.setFont(SpaceArt::monoFont(7.8f, true));
            g.setColour(SpaceColours::textBright);
            g.drawText("DEPTH " + juce::String(static_cast<int>(std::round(dep * 100.0f))) + "%", depR.toNearestInt(), juce::Justification::centred);
        }

        // Panel subtle top bevel + clean negative-space separation (no heavy boxy borders!)
        g.setColour(juce::Colours::white.withAlpha(0.055f));
        g.drawHorizontalLine(static_cast<int>(sr.getY()) + 1, sr.getX() + 4.0f, sr.getRight() - 4.0f);
        if (selected)
        {
            // Backlit studio highlight: soft outer micro-glow + refined 68% border
            g.setColour(chCol.withAlpha(0.11f));
            g.drawRoundedRectangle(sr.expanded(0.8f), 4.8f, 2.4f);
            g.setColour(chCol.withAlpha(0.68f));
            g.drawRoundedRectangle(sr, 4.0f, 1.1f);
        }
        else
        {
            g.setColour(SpaceColours::sectionLine.withAlpha(0.60f));
            g.drawRoundedRectangle(sr, 4.0f, 0.7f);
        }
    }
}

void StepGrid::paintCoreFrameAndStatus(juce::Graphics& g)
{
    const auto& th = SpaceColours::theme();
    const auto coreRect = juce::Rectangle<float>(static_cast<float>(CORE_X), 2.0f, static_cast<float>(CORE_W), static_cast<float>(getHeight() - 4));
    g.setColour(th.panelBg);
    g.fillRoundedRectangle(coreRect, 4.0f);
    if (noiseTile.isValid())
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(coreRect.toNearestInt());
        g.setTiledImageFill(noiseTile, 19, 43, 0.14f);
        g.fillRect(coreRect);
    }
    g.setColour(juce::Colours::white.withAlpha(0.055f));
    g.drawHorizontalLine(3, coreRect.getX() + 4.0f, coreRect.getRight() - 4.0f);
    g.setColour(th.sectionLine.withAlpha(0.65f));
    g.drawRoundedRectangle(coreRect, 4.0f, 0.8f);

    // ── Compact Top CRT Status Monitor (tap to cycle screens!) ──
    const auto crt = statusCrtRect().toFloat();
    const auto greenCol  = SpaceColours::themed(SpaceColours::neonGreen);
    const auto amberCol  = SpaceColours::themed(SpaceColours::neonOrange);
    const auto yellowCol = SpaceColours::themed(SpaceColours::neonYellow);
    const int  vizM      = SpaceColours::vizMode();
    const float kickPulse = visualState.kickPulse.load(std::memory_order_relaxed);
    const int   animFrame = visualState.kickCount.load(std::memory_order_relaxed) & 1;

    // RAVE mode: subtle 2px Chassis Edge LED Bars flanking the center core (driven by 60+125 Hz sub energy!)
    if (vizM == SpaceColours::VIZ_RAVE)
    {
        const float subE = std::clamp((analyzerRef.getBand(0) + analyzerRef.getBand(1)) * 1.45f, 0.0f, 1.0f);
        const bool isFill = sequencer.fillStartBar.load(std::memory_order_relaxed) >= 0;
        const bool isDrop = ((sequencer.currentBar.load(std::memory_order_relaxed) % 16) == 15);
        const auto edgeCol = isFill ? SpaceColours::themed(SpaceColours::neonCyan)
                           : isDrop ? SpaceColours::themed(SpaceColours::neonMagenta)
                                    : greenCol;
        if (subE > 0.03f)
        {
            const float barH = (coreRect.getHeight() - 20.0f) * (0.20f + 0.80f * subE);
            const float barY = coreRect.getCentreY() - barH * 0.5f;
            g.setColour(edgeCol.withAlpha(0.18f + subE * 0.55f));
            g.fillRoundedRectangle(coreRect.getX() + 1.5f, barY, 2.0f, barH, 1.0f);
            g.fillRoundedRectangle(coreRect.getRight() - 3.5f, barY, 2.0f, barH, 1.0f);
        }
    }

    SpaceArt::drawCrtBezel(g, crt, greenCol, true, 7.0f, th.crtCenter, th.crtScanAlpha);

    // SUBTLE / RAVE: Kick Sub-Breath on top CRT rim
    if (vizM >= SpaceColours::VIZ_SUBTLE && kickPulse > 0.03f)
    {
        const float breathA = kickPulse * (vizM == SpaceColours::VIZ_RAVE ? 0.34f : 0.16f);
        g.setColour(greenCol.withAlpha(breathA));
        g.drawRoundedRectangle(crt.reduced(1.0f), 6.0f, 1.4f);
    }

    // RAVE mode: gentle 1/4-beat CRT phosphor scan wave across top HUD
    if (vizM == SpaceColours::VIZ_RAVE && sequencer.isPlaying.load(std::memory_order_relaxed))
    {
        const int st = sequencer.currentSteps[0].load(std::memory_order_relaxed);
        const float phase = static_cast<float>(st % 4) / 4.0f;
        const float wy = crt.getY() + 6.0f + phase * (crt.getHeight() - 28.0f);
        g.setColour(greenCol.withAlpha(0.08f + kickPulse * 0.08f));
        g.fillRect(crt.getX() + 6.0f, wy, crt.getWidth() - 12.0f, 5.0f);
    }

    const float ix = crt.getX() + 10.0f;
    const float iy = crt.getY() + 7.0f;

    // Left: Living Beat-Synced Space Invaders Arcade Fleet (Bass / Mids / Highs + UFO Mothership + Lasers!)
    {
        const bool isPlay = sequencer.isPlaying.load(std::memory_order_relaxed);
        const int  step0  = std::max(0, sequencer.currentSteps[0].load(std::memory_order_relaxed));
        const float bassE = std::clamp((analyzerRef.getBand(0) + analyzerRef.getBand(1)) * 1.45f, 0.0f, 1.0f);
        const float midE  = std::clamp((analyzerRef.getBand(2) + analyzerRef.getBand(3) + analyzerRef.getBand(4)) * 1.15f, 0.0f, 1.0f);
        const float highE = std::clamp((analyzerRef.getBand(5) + analyzerRef.getBand(6) + analyzerRef.getBand(7)) * 1.35f, 0.0f, 1.0f);
        const float snareGlow = channelTriggerGlow[1];
        const float hatGlow   = channelTriggerGlow[2];
        const float acidGlow  = std::max(channelTriggerGlow[6], channelTriggerGlow[7]);

        // Classic arcade 8-step horizontal march (-3..+3 px) synced to 1/8th notes when playing
        static constexpr float kMarch[8] = { -3.0f, -1.5f, 0.0f, 1.5f, 3.0f, 1.5f, 0.0f, -1.5f };
        const float marchX = (isPlay && vizM > 0) ? kMarch[(step0 / 2) & 7] : 0.0f;
        const float raveBoost = (vizM == SpaceColours::VIZ_RAVE) ? 1.35f : (vizM == SpaceColours::VIZ_SUBTLE ? 0.90f : 0.35f);

        // 1. Mystery UFO Mothership cruising the top lane synced to 16-step bar + flashing on Highs/Hats!
        if (vizM > 0 || isPlay)
        {
            const float ufoPhase = static_cast<float>(step0 % 16) / 15.0f;
            const float ufoX = ix + 4.0f + ufoPhase * 80.0f;
            const float ufoY = iy - 1.5f - highE * 2.0f * raveBoost;
            const auto ufoCol = SpaceColours::themed(SpaceColours::neonMagenta).brighter(highE * 0.45f + hatGlow * 0.35f);
            g.setColour(ufoCol.withAlpha(0.70f + highE * 0.30f));
            g.fillRoundedRectangle(ufoX, ufoY + 2.0f, 16.0f, 3.2f, 1.5f);
            g.fillRoundedRectangle(ufoX + 4.0f, ufoY, 8.0f, 2.5f, 1.2f);
            g.setColour(juce::Colours::white.withAlpha(0.55f + hatGlow * 0.45f));
            g.fillRect(ufoX + 3.0f, ufoY + 3.0f, 2.0f, 1.2f);
            g.fillRect(ufoX + 7.0f, ufoY + 3.0f, 2.0f, 1.2f);
            g.fillRect(ufoX + 11.0f, ufoY + 3.0f, 2.0f, 1.2f);
        }

        // 2. Row 0 (4× Mid/High Crab Invaders: 250Hz..4kHz) & Row 1 (4× Heavy Sub/Bass Invaders: 60Hz..303)
        for (int r = 0; r < 2; ++r)
        {
            const float rowMarch = (r == 0) ? marchX : -marchX;
            for (int c = 0; c < 4; ++c)
            {
                const float bandVal = (r == 0)
                    ? std::clamp(analyzerRef.getBand(3 + c) * 2.2f, 0.0f, 1.0f)   // 500, 1K, 2K, 4K
                    : std::clamp(analyzerRef.getBand(c < 2 ? 0 : (c - 1)) * 2.0f, 0.0f, 1.0f); // 60, 60, 125, 250

                const float jump = (r == 0)
                    ? (-(bandVal * 4.2f + midE * 1.8f + snareGlow * 2.5f) * raveBoost)
                    : (-(bandVal * 3.8f + kickPulse * 3.4f + bassE * 1.6f) * raveBoost);

                const int pose = (r == 0)
                    ? ((animFrame ^ (step0 & 1) ^ c) & 1)
                    : ((animFrame ^ (c & 1)) & 1);

                const auto baseCol = (r == 0)
                    ? (c >= 2 ? SpaceColours::themed(SpaceColours::neonCyan) : greenCol)
                    : (acidGlow > 0.25f ? SpaceColours::themed(SpaceColours::neonMagenta) : yellowCol);
                const auto col = baseCol.brighter(bandVal * 0.35f + (r == 1 ? kickPulse * 0.25f : highE * 0.25f));
                const float glowA = 0.12f + bandVal * 0.26f + (r == 1 ? kickPulse * 0.22f : snareGlow * 0.18f);

                const float invX = ix + static_cast<float>(c) * 26.0f + rowMarch;
                const float invY = iy + 11.0f + static_cast<float>(r) * 22.0f + jump;
                SpaceArt::drawInvader(g, (r == 0 ? 0 : 1), pose, invX, invY, 2, col, glowA);

                // Arcade laser bolts on Snare / 303 Acid / High-Energy transients!
                if (vizM > 0 && ((r == 0 && snareGlow > 0.35f && ((c + step0) & 1) == 0)
                              || (r == 1 && acidGlow > 0.35f && ((c + step0) & 1) == 1)))
                {
                    const float boltX = invX + 10.0f;
                    const float boltY = (r == 0) ? (invY + 15.0f) : (invY - 6.0f);
                    g.setColour((r == 0 ? SpaceColours::themed(SpaceColours::neonCyan) : SpaceColours::themed(SpaceColours::neonMagenta)).withAlpha(0.85f));
                    g.fillRect(boltX, boltY, 1.8f, 5.0f);
                }
            }
        }

        // 3. Sub-Bass floor shockwave line under the fleet on Kick pulse
        if (vizM > 0 && (kickPulse > 0.05f || bassE > 0.12f))
        {
            const float floorE = std::max(kickPulse, bassE);
            const float fw = 104.0f * (0.35f + 0.65f * floorE);
            const float fx = ix + (104.0f - fw) * 0.5f;
            g.setColour(greenCol.withAlpha(0.20f + floorE * 0.55f));
            g.fillRoundedRectangle(fx, iy + 59.0f, fw, 2.0f, 1.0f);
        }
    }

    // Center: Live Mini Oscilloscope + 8-Band Spectrum OR Direct Live DSP Morphing Curve (#18)!
    // Clean borderless layout with subtle 15% vertical dividers (eliminates concentric box fatigue #24!)
    {
        const auto scopeR = juce::Rectangle<float>(ix + 114.0f, iy + 2.0f, 146.0f, 62.0f);
        g.setColour(greenCol.withAlpha(0.15f));
        g.drawVerticalLine(static_cast<int>(scopeR.getX() - 5.0f), iy + 3.0f, iy + 63.0f);
        g.drawVerticalLine(static_cast<int>(scopeR.getRight() + 5.0f), iy + 3.0f, iy + 63.0f);

        const auto& hudState = MusicalUnits::hud();
        const bool hudLive = hudState.activeParamId.isNotEmpty()
                          && (juce::Time::getMillisecondCounterHiRes() - hudState.lastUpdateMs < 1800.0);
        const auto& pid = hudState.activeParamId;
        const bool show303Curve   = hudLive && (pid.startsWith("bass1_") || pid.startsWith("bass2_"));
        const bool showKickCurve  = hudLive && pid.startsWith("kick_");
        const bool showDriveCurve = hudLive && (pid == ParamId::MASTER_DRIVE);
        const bool showWashCurve  = hudLive && (pid == ParamId::DJ_FILTER);

        if (show303Curve)
        {
            const bool isB2 = pid.startsWith("bass2_");
            const float cut = apvtsRef.getRawParameterValue(isB2 ? ParamId::BASS2_CUTOFF : ParamId::BASS1_CUTOFF)->load();
            const float res = apvtsRef.getRawParameterValue(isB2 ? ParamId::BASS2_RESO   : ParamId::BASS1_RESO)->load();
            const float env = apvtsRef.getRawParameterValue(isB2 ? ParamId::BASS2_ENVMOD : ParamId::BASS1_ENVMOD)->load();
            const auto curveCol = SpaceColours::channelColor(isB2 ? 7 : 6);
            juce::Path p;
            const float cutX = 0.14f + std::clamp(cut, 0.0f, 1.0f) * 0.72f;
            for (int i = 0; i <= 48; ++i)
            {
                const float x = static_cast<float>(i) / 48.0f;
                const float dist = (x - cutX) * 6.5f;
                const float lp = 1.0f / (1.0f + std::exp(dist * 2.2f));
                const float peak = res * 0.58f * std::exp(-dist * dist * 1.4f);
                const float yVal = std::clamp(lp * 0.68f + peak, 0.02f, 0.98f);
                const float px = scopeR.getX() + 3.0f + x * (scopeR.getWidth() - 6.0f);
                const float py = scopeR.getBottom() - 4.0f - yVal * (scopeR.getHeight() - 10.0f);
                if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
            }
            g.setColour(curveCol.withAlpha(0.28f));
            g.strokePath(p, juce::PathStrokeType(4.0f));
            g.setColour(curveCol);
            g.strokePath(p, juce::PathStrokeType(1.6f));
            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(curveCol.withAlpha(0.80f));
            g.drawText("303 DIODE // ENV " + juce::String(static_cast<int>(env * 100.0f)) + "%",
                       static_cast<int>(scopeR.getX() + 4), static_cast<int>(scopeR.getY() + 2), 138, 10, juce::Justification::centredLeft);
        }
        else if (showKickCurve)
        {
            const float dec   = apvtsRef.getRawParameterValue(ParamId::KICK_DECAY)->load();
            const float punch = apvtsRef.getRawParameterValue(ParamId::KICK_PUNCH)->load();
            const float drv   = apvtsRef.getRawParameterValue(ParamId::KICK_DRIVE)->load();
            juce::Path p;
            const float midY = scopeR.getCentreY();
            for (int i = 0; i <= 64; ++i)
            {
                const float t = static_cast<float>(i) / 64.0f;
                const float env = std::exp(-t * (5.5f - dec * 3.8f));
                const float freq = 4.0f + punch * 14.0f * std::exp(-t * 10.0f);
                const float s = std::tanh(std::sin(t * freq * 3.14159f) * (1.0f + drv * 3.2f)) * env;
                const float px = scopeR.getX() + 3.0f + t * (scopeR.getWidth() - 6.0f);
                const float py = midY - s * (scopeR.getHeight() * 0.42f);
                if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
            }
            g.setColour(greenCol.withAlpha(0.26f));
            g.strokePath(p, juce::PathStrokeType(4.0f));
            g.setColour(greenCol);
            g.strokePath(p, juce::PathStrokeType(1.6f));
            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(greenCol.withAlpha(0.80f));
            g.drawText("909 KICK ENVELOPE", static_cast<int>(scopeR.getX() + 4), static_cast<int>(scopeR.getY() + 2), 138, 10, juce::Justification::centredLeft);
        }
        else if (showDriveCurve)
        {
            const float drv = apvtsRef.getRawParameterValue(ParamId::MASTER_DRIVE)->load();
            juce::Path p;
            for (int i = 0; i <= 48; ++i)
            {
                const float x = (static_cast<float>(i) / 48.0f) * 2.0f - 1.0f;
                const float y = std::tanh(x * (1.0f + drv * 5.0f));
                const float px = scopeR.getX() + 6.0f + (x * 0.5f + 0.5f) * (scopeR.getWidth() - 12.0f);
                const float py = scopeR.getCentreY() - y * (scopeR.getHeight() * 0.42f);
                if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
            }
            g.setColour(amberCol.withAlpha(0.28f));
            g.strokePath(p, juce::PathStrokeType(4.0f));
            g.setColour(amberCol);
            g.strokePath(p, juce::PathStrokeType(1.6f));
            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(amberCol.withAlpha(0.85f));
            g.drawText("MACKIE 1202 CURVE", static_cast<int>(scopeR.getX() + 4), static_cast<int>(scopeR.getY() + 2), 138, 10, juce::Justification::centredLeft);
        }
        else if (showWashCurve)
        {
            const float dj = apvtsRef.getRawParameterValue(ParamId::DJ_FILTER)->load();
            const auto cCol = SpaceColours::themed(SpaceColours::neonCyan);
            juce::Path p;
            for (int i = 0; i <= 48; ++i)
            {
                const float x = static_cast<float>(i) / 48.0f;
                float yVal = 0.65f;
                if (dj < -0.02f) { const float c = 1.0f + dj * 0.85f; yVal = 0.65f / (1.0f + std::exp((x - c) * 14.0f)); }
                else if (dj > 0.02f) { const float c = dj * 0.85f; yVal = 0.65f / (1.0f + std::exp((c - x) * 14.0f)); }
                const float px = scopeR.getX() + 3.0f + x * (scopeR.getWidth() - 6.0f);
                const float py = scopeR.getBottom() - 6.0f - yVal * (scopeR.getHeight() - 12.0f);
                if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
            }
            g.setColour(cCol.withAlpha(0.28f));
            g.strokePath(p, juce::PathStrokeType(4.0f));
            g.setColour(cCol);
            g.strokePath(p, juce::PathStrokeType(1.6f));
            g.setFont(SpaceArt::monoFont(6.5f, true));
            g.setColour(cCol.withAlpha(0.85f));
            g.drawText("DJ ISOLATOR WASH", static_cast<int>(scopeR.getX() + 4), static_cast<int>(scopeR.getY() + 2), 138, 10, juce::Justification::centredLeft);
        }
        else
        {
            g.drawHorizontalLine(static_cast<int>(scopeR.getCentreY()), scopeR.getX(), scopeR.getRight());
            const int liveHead = analyzerRef.getScopeHead();
            const float midY = scopeR.getCentreY();
            const float amp  = scopeR.getHeight() * 0.40f;
            juce::Path p;
            for (int i = 0; i < OutputAnalyzer::SCOPE_SIZE; ++i)
            {
                const float s = analyzerRef.getScopeSample(liveHead + i);
                const float px = scopeR.getX() + 2.0f + (static_cast<float>(i) / static_cast<float>(OutputAnalyzer::SCOPE_SIZE - 1)) * (scopeR.getWidth() - 4.0f);
                const float py = midY - std::clamp(s, -1.0f, 1.0f) * amp;
                if (i == 0) p.startNewSubPath(px, py);
                else        p.lineTo(px, py);
            }
            g.setColour(greenCol.withAlpha(0.92f));
            g.strokePath(p, juce::PathStrokeType(1.3f));

            const float colW = (scopeR.getWidth() - 8.0f) / static_cast<float>(OutputAnalyzer::NUM_BANDS);
            for (int b = 0; b < OutputAnalyzer::NUM_BANDS; ++b)
            {
                const float v = std::clamp(analyzerRef.getBand(b) * 1.8f, 0.04f, 1.0f);
                const float bh = v * 26.0f;
                const float bx = scopeR.getX() + 4.0f + static_cast<float>(b) * colW;
                g.setColour((b < 3 ? yellowCol : amberCol).withAlpha(0.45f));
                g.fillRect(bx + 1.0f, scopeR.getBottom() - 2.0f - bh, colW - 3.0f, bh);
            }
        }
    }

    // Right: BPM Digital Readout + BAR counter (borderless — clean vertical divider already separates it!)
    {
        const auto bpmBox = juce::Rectangle<float>(crt.getRight() - 114.0f, iy + 2.0f, 104.0f, 62.0f);
        g.setFont(SpaceArt::monoFont(9.0f, true));
        g.setColour(greenCol.withAlpha(0.85f));
        g.drawText("BPM", static_cast<int>(bpmBox.getX() + 6), static_cast<int>(bpmBox.getY() + 4), 30, 12, juce::Justification::centredLeft);

        const int bpmInt = static_cast<int>(std::round(bpmRef.load(std::memory_order_relaxed)));
        g.setFont(SpaceArt::monoFont(24.0f, true));
        g.setColour(yellowCol);
        g.drawText(juce::String(bpmInt), static_cast<int>(bpmBox.getX() + 6), static_cast<int>(bpmBox.getY() + 14),
                   static_cast<int>(bpmBox.getWidth() - 12), 26, juce::Justification::centredRight);

        const int barNum = static_cast<int>((std::max<int64_t>(0, sequencer.currentBar.load(std::memory_order_relaxed)) % 64) + 1);
        const char* clkSrc = hostPlayingRef.load(std::memory_order_relaxed) ? "HOST" : "INT";
        g.setFont(SpaceArt::monoFont(7.5f, true));
        g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.90f));
        g.drawText("BAR " + juce::String(barNum) + "/64  " + clkSrc,
                   static_cast<int>(bpmBox.getX() + 6), static_cast<int>(bpmBox.getBottom() - 14),
                   static_cast<int>(bpmBox.getWidth() - 12), 11, juce::Justification::centred);
    }

    // Bottom status bar inside CRT: Live parameter HUD or real hardware telemetry (zero slop!)
    {
        const auto& hudState = MusicalUnits::hud();
        const bool hudLive = hudState.text.isNotEmpty()
                          && (juce::Time::getMillisecondCounterHiRes() - hudState.lastUpdateMs < 1800.0);
        g.setColour(greenCol.withAlpha(0.28f));
        g.drawHorizontalLine(static_cast<int>(crt.getBottom() - 20.0f), crt.getX() + 8.0f, crt.getRight() - 8.0f);

        juce::String idleTelemetry;
        if (!hudLive)
        {
            static const char* kSceneName[4] = { "A", "B", "C", "D" };
            static const char* kScrName[6]   = { "SEQ", "MATRIX", "MIX", "LFO", "CHAOS", "SCOPE" };
            const int scIdx  = std::clamp(sequencer.activeScene.load(std::memory_order_relaxed), 0, 3);
            const int scrIdx = std::clamp(static_cast<int>(coreScreen), 0, 5);
            const int patLen = sequencer.getPatternLength(selectedChannel);
            idleTelemetry = "CH " + juce::String(selectedChannel + 1) + " " + kChannelTitles[selectedChannel]
                          + "  |  " + juce::String(patLen) + "S  |  SCENE " + kSceneName[scIdx]
                          + "  |  " + kScrName[scrIdx];
        }
        g.setFont(SpaceArt::monoFont(hudLive ? 9.5f : 8.8f, true));
        g.setColour(hudLive ? juce::Colours::white : greenCol.withAlpha(0.86f));
        g.drawText(hudLive ? hudState.text : idleTelemetry,
                   static_cast<int>(crt.getX() + 8), static_cast<int>(crt.getBottom() - 18),
                   static_cast<int>(crt.getWidth() - 16), 14, juce::Justification::centred);
    }

    // Transport strip labels
    {
        g.setFont(SpaceArt::monoFont(8.0f, true));
        g.setColour(SpaceColours::txtMid());
        g.drawText("QNT",   CORE_X + 6,   118, 38, 12, juce::Justification::centred);
        g.drawText("SWING", CORE_X + 44,  118, 38, 12, juce::Justification::centred);
        g.drawText("TRANS", CORE_X + 86,  118, 102, 12, juce::Justification::centred);
        g.drawText("SCENE", CORE_X + 196, 118, 118, 12, juce::Justification::centred);
    }

    // Page indicator between < and >
    {
        const int curPage = viewPage[idx(selectedChannel)] + 1;
        const int totPages = std::max(1, (sequencer.getPatternLength(selectedChannel) + VISIBLE_STEPS - 1) / VISIBLE_STEPS);
        g.setFont(SpaceArt::monoFont(8.0f, true));
        g.setColour(SpaceColours::txtMid());
        g.drawText(juce::String(curPage) + "/" + juce::String(totPages), CORE_X + 370, 179, 18, 20, juce::Justification::centred);
    }
}

void StepGrid::paintMainScreen(juce::Graphics& g)
{
    const auto oled = mainScreenRect().toFloat();
    const auto accent = (coreScreen == CoreScreen::Matrix) ? SpaceColours::themed(SpaceColours::neonCyan)
                      : (coreScreen == CoreScreen::Mix)    ? SpaceColours::themed(SpaceColours::neonYellow)
                      : (coreScreen == CoreScreen::Lfo)    ? SpaceColours::themed(SpaceColours::neonPurple)
                      : (coreScreen == CoreScreen::Chaos)  ? SpaceColours::themed(SpaceColours::neonMagenta)
                      : (coreScreen == CoreScreen::Scope)  ? SpaceColours::themed(SpaceColours::neonGreen)
                                                           : SpaceColours::channelColor(selectedChannel);
    const auto& th = SpaceColours::theme();
    SpaceArt::drawCrtBezel(g, oled, accent, true, 6.0f, th.crtCenter, th.crtScanAlpha);

    const int vizM = SpaceColours::vizMode();
    if (vizM >= SpaceColours::VIZ_SUBTLE)
    {
        const float kp = visualState.kickPulse.load(std::memory_order_relaxed);
        if (kp > 0.03f)
        {
            g.setColour(accent.withAlpha(kp * (vizM == SpaceColours::VIZ_RAVE ? 0.28f : 0.12f)));
            g.drawRoundedRectangle(oled.reduced(1.0f), 5.0f, 1.3f);
        }
    }

    switch (coreScreen)
    {
        case CoreScreen::Seq:    paintSeqScreen(g, oled);    break;
        case CoreScreen::Matrix: paintMatrixScreen(g, oled); break;
        case CoreScreen::Mix:    paintMixScreen(g, oled);    break;
        case CoreScreen::Lfo:    paintLfoScreen(g, oled);    break;
        case CoreScreen::Chaos:  paintChaosScreen(g, oled);  break;
        case CoreScreen::Scope:  paintScopeScreen(g, oled);  break;
    }

    // Subtle OLED curved-glass corner vignette for extra realism
    juce::ColourGradient vig(juce::Colours::transparentBlack, oled.getCentreX(), oled.getCentreY(),
                             juce::Colours::black.withAlpha(0.28f), oled.getX(), oled.getY(), true);
    g.setGradientFill(vig);
    g.fillRoundedRectangle(oled.reduced(1.5f), 5.0f);
}

void StepGrid::paintSeqScreen(juce::Graphics& g, juce::Rectangle<float> oled)
{
    const int ch = selectedChannel;
    const auto chCol = SpaceColours::channelColor(ch);
    const int pageOff = viewPage[idx(ch)] * VISIBLE_STEPS;
    const int patLen = sequencer.getPatternLength(ch);
    const int curStep = sequencer.currentSteps[idx(ch)].load(std::memory_order_acquire);
    const bool playing = sequencer.isPlaying.load(std::memory_order_acquire);
    const bool isBass = (ch >= 6);

    {
        const auto hdrR = juce::Rectangle<float>(oled.getX() + 6.0f, oled.getY() + 6.0f, oled.getWidth() - 12.0f, 22.0f);
        g.setColour(chCol.withAlpha(0.10f));
        g.fillRoundedRectangle(hdrR, 3.0f);
        g.setFont(SpaceArt::monoFont(11.5f, true));
        g.setColour(chCol);
        g.drawText("<   " + juce::String(ch + 1) + "  " + juce::String(kChannelTitles[ch]) + "   >",
                   hdrR.toNearestInt(), juce::Justification::centred);
    }

    const float labelW = 42.0f;
    const float gridX  = oled.getX() + labelW;
    const float gridW  = oled.getWidth() - labelW - 10.0f;
    const float colW   = gridW / 16.0f;

    g.setFont(SpaceArt::monoFont(8.0f, true));
    for (int s = 0; s < 16; ++s)
    {
        const int absStep = pageOff + s;
        const bool isPlay = playing && (curStep == absStep);
        const bool beatHead = (s % 4 == 0);
        g.setColour(isPlay ? juce::Colours::white : (beatHead ? chCol.withAlpha(0.90f) : SpaceColours::textDim));
        g.drawText(juce::String(s + 1), static_cast<int>(gridX + static_cast<float>(s) * colW),
                   static_cast<int>(oled.getY() + 31), static_cast<int>(colW), 12, juce::Justification::centred);
    }

    g.setColour(juce::Colours::white.withAlpha(0.14f));
    for (int b = 1; b < 4; ++b)
    {
        const float bx = gridX + static_cast<float>(b * 4) * colW;
        g.drawVerticalLine(static_cast<int>(bx), oled.getY() + 32.0f, oled.getBottom() - 36.0f);
    }

    const float velY = oled.getY() + 46.0f;
    const float velH = 114.0f;
    g.setFont(SpaceArt::monoFont(8.0f, true));
    g.setColour(chCol.withAlpha(0.90f));
    g.drawText("GATE", static_cast<int>(oled.getX() + 6), static_cast<int>(velY + 8), 34, 12, juce::Justification::centredLeft);
    g.drawText("VEL",  static_cast<int>(oled.getX() + 6), static_cast<int>(velY + 24), 34, 12, juce::Justification::centredLeft);

    for (int s = 0; s < 16; ++s)
    {
        const int absStep = pageOff + s;
        const bool inLen = (absStep < patLen);
        const bool on = inLen && sequencer.getStep(ch, absStep);
        const bool isPlay = playing && (curStep == absStep);
        const float vel = static_cast<float>(sequencer.getVelocity(ch, absStep)) / 127.0f;
        const auto colR = juce::Rectangle<float>(gridX + static_cast<float>(s) * colW + 2.0f, velY, colW - 4.0f, velH);

        if (isPlay)
        {
            g.setColour(juce::Colours::white.withAlpha(0.11f));
            g.fillRect(colR.expanded(1.0f, 2.0f));
        }

        const int segs = 10;
        const float segH = (velH - static_cast<float>(segs - 1) * 2.0f) / static_cast<float>(segs);
        const int lit = on ? std::max(1, static_cast<int>(std::round(vel * static_cast<float>(segs)))) : 0;
        for (int i = 0; i < segs; ++i)
        {
            const float sy = colR.getBottom() - static_cast<float>(i + 1) * segH - static_cast<float>(i) * 2.0f;
            const auto segR = juce::Rectangle<float>(colR.getX(), sy, colR.getWidth(), segH);
            if (i < lit)
            {
                g.setColour(chCol.withAlpha(isPlay ? 1.0f : 0.88f));
                g.fillRoundedRectangle(segR, 1.0f);
            }
            else
            {
                g.setColour(inLen ? juce::Colour(0xff192019) : juce::Colour(0xff0b0e0b));
                g.fillRoundedRectangle(segR, 1.0f);
            }
        }
    }

    const float midY = velY + velH + 10.0f;
    if (isBass)
    {
        g.setFont(SpaceArt::monoFont(7.5f, true));
        g.setColour(SpaceColours::themed(SpaceColours::neonCyan));
        g.drawText("NOTE", static_cast<int>(oled.getX() + 6), static_cast<int>(midY + 2), 34, 14, juce::Justification::centredLeft);
        g.setColour(SpaceColours::themed(SpaceColours::neonOrange));
        g.drawText("SLD",  static_cast<int>(oled.getX() + 6), static_cast<int>(midY + 22), 34, 14, juce::Justification::centredLeft);
        g.setColour(SpaceColours::themed(SpaceColours::neonMagenta));
        g.drawText("ACC",  static_cast<int>(oled.getX() + 6), static_cast<int>(midY + 42), 34, 14, juce::Justification::centredLeft);

        for (int s = 0; s < 16; ++s)
        {
            const int absStep = pageOff + s;
            if (absStep >= patLen) continue;
            const float cx = gridX + static_cast<float>(s) * colW;
            const bool on = sequencer.getStep(ch, absStep);
            const bool sld = sequencer.getBassSlide(ch - 6, absStep);
            const bool acc = sequencer.getBassAccent(ch - 6, absStep);

            g.setFont(SpaceArt::monoFont(7.0f, true));
            g.setColour(on ? SpaceColours::textBright : SpaceColours::textDim);
            g.drawText(noteNameFromOffset(sequencer.getBassNoteOffset(ch - 6, absStep)),
                       static_cast<int>(cx), static_cast<int>(midY + 2), static_cast<int>(colW), 14, juce::Justification::centred);

            g.setColour(sld ? SpaceColours::themed(SpaceColours::neonOrange) : juce::Colour(0xff1e221e));
            g.fillRoundedRectangle(cx + 2.5f, midY + 22.0f, colW - 5.0f, 13.0f, 2.0f);

            g.setColour(acc ? SpaceColours::themed(SpaceColours::neonMagenta) : juce::Colour(0xff1e221e));
            g.fillRoundedRectangle(cx + 2.5f, midY + 42.0f, colW - 5.0f, 13.0f, 2.0f);
        }
    }
    else
    {
        g.setFont(SpaceArt::monoFont(7.5f, true));
        g.setColour(SpaceColours::themed(SpaceColours::neonCyan));
        g.drawText("NUDGE", static_cast<int>(oled.getX() + 6), static_cast<int>(midY + 18), 36, 14, juce::Justification::centredLeft);

        const float centerY = midY + 26.0f;
        // Structured 0ms center axis + subtle +/- range rails
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawHorizontalLine(static_cast<int>(centerY - 16.0f), gridX, gridX + gridW);
        g.drawHorizontalLine(static_cast<int>(centerY + 16.0f), gridX, gridX + gridW);
        g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.32f));
        g.drawHorizontalLine(static_cast<int>(centerY), gridX, gridX + gridW);

        for (int s = 0; s < 16; ++s)
        {
            const int absStep = pageOff + s;
            if (absStep >= patLen) continue;
            const float cx = gridX + (static_cast<float>(s) + 0.5f) * colW;
            const int8_t mt = sequencer.getMicroTiming(ch, absStep);
            const float dy = -static_cast<float>(mt) / 50.0f * 16.0f;
            const bool on = sequencer.getStep(ch, absStep);
            const bool nudged = (mt != 0);

            // Micro center tick at 0 ms reference
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.drawVerticalLine(static_cast<int>(cx), centerY - 2.5f, centerY + 2.5f);

            if (nudged)
            {
                g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(on ? 0.90f : 0.45f));
                g.drawLine(cx, centerY, cx, centerY + dy, 1.6f);
            }
            g.setColour(on ? (nudged ? SpaceColours::themed(SpaceColours::neonCyan).brighter(0.25f) : SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.85f))
                           : SpaceColours::textDim.withAlpha(0.40f));
            g.fillEllipse(cx - 2.8f, centerY + dy - 2.8f, 5.6f, 5.6f);
        }
    }

    const float probY = midY + 66.0f;
    const float probH = oled.getBottom() - 36.0f - probY;
    g.setFont(SpaceArt::monoFont(8.0f, true));
    const auto probCol = SpaceColours::themed(SpaceColours::neonOrange);
    g.setColour(probCol.withAlpha(0.75f));
    g.drawText("PROB%", static_cast<int>(oled.getX() + 6), static_cast<int>(probY + probH * 0.35f), 36, 12, juce::Justification::centredLeft);

    for (int s = 0; s < 16; ++s)
    {
        const int absStep = pageOff + s;
        const bool inLen = (absStep < patLen);
        const int probInt = sequencer.getProbability(ch, absStep);
        const float prob = static_cast<float>(probInt) / 100.0f;
        const bool isException = inLen && (probInt < 100);
        const auto colR = juce::Rectangle<float>(gridX + static_cast<float>(s) * colW + 2.0f, probY, colW - 4.0f, probH);

        const int segs = 7;
        const float segH = (probH - static_cast<float>(segs - 1) * 1.8f) / static_cast<float>(segs);
        const int lit = inLen ? static_cast<int>(std::round(prob * static_cast<float>(segs))) : 0;
        for (int i = 0; i < segs; ++i)
        {
            const float sy = colR.getBottom() - static_cast<float>(i + 1) * segH - static_cast<float>(i) * 1.8f;
            const auto segR = juce::Rectangle<float>(colR.getX(), sy, colR.getWidth(), segH);
            if (i < lit)
                // Calm muted amber when 100% (default), bright vivid amber when <100% (real exception!)
                g.setColour(isException ? probCol.withAlpha(0.92f) : probCol.withAlpha(0.20f));
            else
                g.setColour(juce::Colour(0xff14161a));
            g.fillRoundedRectangle(segR, 1.0f);
        }
    }

    g.setColour(chCol.withAlpha(0.24f));
    g.drawHorizontalLine(static_cast<int>(oled.getBottom() - 33.0f), oled.getX() + 8.0f, oled.getRight() - 8.0f);
}

void StepGrid::paintMatrixScreen(juce::Graphics& g, juce::Rectangle<float> oled)
{
    g.setFont(SpaceArt::monoFont(10.0f, true));
    g.setColour(SpaceColours::themed(SpaceColours::neonCyan));
    g.drawText("8-TRACK MATRIX OVERVIEW  //  SWIPE TO PAINT",
               static_cast<int>(oled.getX() + 8), static_cast<int>(oled.getY() + 6),
               static_cast<int>(oled.getWidth() - 16), 16, juce::Justification::centred);

    // 4x4 rhythmic chunking with beat headers (1, 5, 9, 13) & 4px beat-group gaps!
    const float lblW = 50.0f;
    const float gx = oled.getX() + lblW;
    const float gy = oled.getY() + 38.0f;
    const float gw = oled.getWidth() - lblW - 10.0f;
    const float gh = oled.getHeight() - 46.0f;
    const float groupGap = 4.0f;
    const float colW = (gw - 3.0f * groupGap) / 16.0f;
    const float rowH = gh / 8.0f;
    const bool playing = sequencer.isPlaying.load(std::memory_order_acquire);

    auto stepX = [&](int s) -> float {
        return gx + static_cast<float>(s) * colW + static_cast<float>(s / 4) * groupGap;
    };

    // Beat-head step numbers (1, 5, 9, 13) + subtle 12% 4x4 vertical dividers
    g.setFont(SpaceArt::monoFont(7.8f, true));
    for (int s = 0; s < 16; ++s)
    {
        const bool isBeat = (s % 4 == 0);
        if (isBeat)
        {
            g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(0.85f));
            g.drawText(juce::String(s + 1), static_cast<int>(stepX(s)), static_cast<int>(oled.getY() + 24),
                       static_cast<int>(colW), 12, juce::Justification::centred);
        }
        else
        {
            g.setColour(SpaceColours::textDim.withAlpha(0.55f));
            g.drawText(juce::String(s + 1), static_cast<int>(stepX(s)), static_cast<int>(oled.getY() + 24),
                       static_cast<int>(colW), 12, juce::Justification::centred);
        }
    }
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    for (int b = 1; b < 4; ++b)
    {
        const float divX = stepX(b * 4) - groupGap * 0.5f;
        g.drawVerticalLine(static_cast<int>(divX), gy - 2.0f, gy + gh);
    }

    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto chCol = SpaceColours::channelColor(ch);
        const float ry = gy + static_cast<float>(ch) * rowH;
        const bool sel = (ch == selectedChannel);
        const bool muted = sequencer.mute[idx(ch)].load(std::memory_order_acquire);
        const int pageOff = viewPage[idx(ch)] * VISIBLE_STEPS;
        const int patLen = sequencer.getPatternLength(ch);
        const int curStep = sequencer.currentSteps[idx(ch)].load(std::memory_order_acquire);

        if (sel)
        {
            g.setColour(chCol.withAlpha(0.10f));
            g.fillRect(oled.getX() + 4.0f, ry, oled.getWidth() - 8.0f, rowH - 1.0f);
        }

        g.setFont(SpaceArt::monoFont(8.5f, true));
        g.setColour(muted ? SpaceColours::textDim : chCol);
        g.drawText(kChannelShort[ch], static_cast<int>(oled.getX() + 8), static_cast<int>(ry),
                   static_cast<int>(lblW - 10), static_cast<int>(rowH), juce::Justification::centredLeft);

        for (int s = 0; s < 16; ++s)
        {
            const int absStep = pageOff + s;
            const bool inLen = (absStep < patLen);
            const bool on = inLen && sequencer.getStep(ch, absStep);
            const bool isPlay = playing && (curStep == absStep);
            const auto cell = juce::Rectangle<float>(stepX(s) + 1.0f,
                                                     ry + 3.0f, colW - 2.0f, rowH - 6.0f);
            if (!inLen)
            {
                g.setColour(juce::Colour(0xff090b09));
                g.fillRoundedRectangle(cell, 2.5f);
            }
            else if (on)
            {
                g.setColour(muted ? chCol.withAlpha(0.30f) : chCol.withAlpha(isPlay ? 1.0f : 0.85f));
                g.fillRoundedRectangle(cell, 2.5f);
                if (isPlay)
                {
                    g.setColour(juce::Colours::white);
                    g.drawRoundedRectangle(cell, 2.5f, 1.3f);
                }
            }
            else
            {
                const bool grp = ((s / 4) & 1) == 0;
                g.setColour(isPlay ? chCol.withAlpha(0.35f) : (grp ? juce::Colour(0xff1d231d) : juce::Colour(0xff131713)));
                g.fillRoundedRectangle(cell, 2.5f);
            }
        }
    }
}

void StepGrid::paintMixScreen(juce::Graphics& g, juce::Rectangle<float> oled)
{
    const auto yCol = SpaceColours::themed(SpaceColours::neonYellow);
    g.setFont(SpaceArt::monoFont(10.0f, true));
    g.setColour(yCol);
    g.drawText("VOICE EXTENDED  &  STEREO PAN CONSOLE",
               static_cast<int>(oled.getX() + 8), static_cast<int>(oled.getY() + 8),
               static_cast<int>(oled.getWidth() - 16), 16, juce::Justification::centred);

    g.setFont(SpaceArt::monoFont(8.5f, true));
    g.setColour(SpaceColours::textMid);
    g.drawText("KICK & BASS 303 SECONDARY", static_cast<int>(oled.getX() + 14), static_cast<int>(oled.getY() + 34), 220, 14, juce::Justification::centredLeft);

    static const char* secLabels[6] = { "K-PENV", "K-SUB", "B1-ACC", "B1-SC", "B2-ACC", "B2-SC" };
    DjKnob* secKnobs[6] = {
        channelParams[0].entries[4].knob.get(),
        channelParams[0].entries[5].knob.get(),
        channelParams[6].entries[4].knob.get(),
        channelParams[6].entries[5].knob.get(),
        channelParams[7].entries[4].knob.get(),
        channelParams[7].entries[5].knob.get()
    };
    g.setFont(SpaceArt::monoFont(8.0f, true));
    for (int i = 0; i < 6; ++i)
    {
        if (secKnobs[i])
        {
            const auto kb = secKnobs[i]->getBounds();
            g.setColour(SpaceColours::textBright.withAlpha(0.88f));
            g.drawText(secLabels[i], kb.getX() - 8, kb.getBottom() + 3, kb.getWidth() + 16, 12, juce::Justification::centred);
        }
    }

    g.setColour(yCol.withAlpha(0.22f));
    g.drawHorizontalLine(static_cast<int>(oled.getY() + 126.0f), oled.getX() + 12.0f, oled.getRight() - 12.0f);

    g.setFont(SpaceArt::monoFont(8.5f, true));
    g.setColour(SpaceColours::textMid);
    g.drawText("STEREO PAN CONSOLE", static_cast<int>(oled.getX() + 14), static_cast<int>(oled.getY() + 134), 240, 14, juce::Justification::centredLeft);

    // Complete 4x2 Pan grid (all 8 channels) with flanking L / R micro-labels
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        DjKnob* pk = (ch == 2) ? mixHatsPanKnob.get() : panKnobs[idx(ch)].get();
        if (!pk) continue;
        const auto kb = pk->getBounds();

        // Micro L / R markers on either side of each bipolar Pan knob
        g.setFont(SpaceArt::silkFont(7.0f, true));
        g.setColour(SpaceColours::textDim);
        g.drawText("L", kb.getX() - 12, kb.getBottom() - 15, 11, 12, juce::Justification::centredRight);
        g.drawText("R", kb.getRight() + 1, kb.getBottom() - 15, 11, 12, juce::Justification::centredLeft);

        // Channel Pan label (or In-Place live value)
        const auto lblR = juce::Rectangle<int>(kb.getX() - 14, kb.getBottom() + 4, kb.getWidth() + 28, 13);
        if (pk->isShowingValue())
            SpaceArt::drawValueWithUnit(g, pk->getCompactValueText(), lblR, 8.2f, juce::Colours::white);
        else
        {
            g.setFont(SpaceArt::silkFont(8.0f, true));
            g.setColour(SpaceColours::channelColor(ch));
            g.drawText(juce::String(kChannelShort[ch]) + " PAN", lblR, juce::Justification::centred);
        }
    }
}

void StepGrid::paintLfoScreen(juce::Graphics& g, juce::Rectangle<float> oled)
{
    const auto lfoCol = SpaceColours::themed(SpaceColours::neonPurple);
    g.setFont(SpaceArt::silkFont(9.5f, true));
    g.setColour(lfoCol);
    g.drawText("BEAT-SYNCED 32-BAR LFO MATRIX  (MAX 50% RANGE)",
               static_cast<int>(oled.getX() + 8), static_cast<int>(oled.getY() + 8),
               static_cast<int>(oled.getWidth() - 16), 16, juce::Justification::centred);

    // Column headers (high contrast studio legibility)
    const float topY = oled.getY() + 30.0f;
    g.setFont(SpaceArt::silkFont(7.4f, true));
    g.setColour(SpaceColours::textMid.brighter(0.15f));
    g.drawText("CH",     static_cast<int>(oled.getX() + 10),  static_cast<int>(topY), 48, 12, juce::Justification::centredLeft);
    g.drawText("ON",     static_cast<int>(oled.getX() + 62),  static_cast<int>(topY), 30, 12, juce::Justification::centred);
    g.drawText("TARGET", static_cast<int>(oled.getX() + 98),  static_cast<int>(topY), 58, 12, juce::Justification::centred);
    g.drawText("SYNC",   static_cast<int>(oled.getX() + 162), static_cast<int>(topY), 46, 12, juce::Justification::centred);
    g.drawText("WAVE",   static_cast<int>(oled.getX() + 214), static_cast<int>(topY), 40, 12, juce::Justification::centred);
    g.drawText("DEPTH",  static_cast<int>(oled.getX() + 260), static_cast<int>(topY), 76, 12, juce::Justification::centred);
    g.drawText("PHASE",  static_cast<int>(oled.getX() + 340), static_cast<int>(topY), 48, 12, juce::Justification::centred);

    const float rowStartY = topY + 16.0f;
    const float rowH = (oled.getBottom() - 26.0f - rowStartY) / 8.0f;
    const auto offTextCol = juce::Colour(0xff929ab0);

    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto& lc = lfoBankRef.channels[idx(ch)];
        const bool on    = lc.enabled.load(std::memory_order_relaxed);
        const int  tIdx  = lc.targetIdx.load(std::memory_order_relaxed);
        const int  rIdx  = lc.rateIdx.load(std::memory_order_relaxed);
        const int  wIdx  = lc.waveIdx.load(std::memory_order_relaxed);
        const float dep  = lc.depth.load(std::memory_order_relaxed); // 0..0.50
        const float ph   = lc.currentPhase.load(std::memory_order_relaxed);
        const float ry   = rowStartY + static_cast<float>(ch) * rowH;
        const auto chCol = SpaceColours::channelColor(ch);

        if (on)
        {
            g.setColour(lfoCol.withAlpha(0.11f));
            g.fillRoundedRectangle(oled.getX() + 6.0f, ry + 1.0f, oled.getWidth() - 12.0f, rowH - 2.0f, 3.0f);
        }
        else if ((ch & 1) == 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.fillRect(oled.getX() + 6.0f, ry, oled.getWidth() - 12.0f, rowH - 1.0f);
        }

        // Channel label
        g.setFont(SpaceArt::uiFont(9.5f, true));
        g.setColour(on ? chCol.brighter(0.15f) : offTextCol);
        g.drawText(kChannelShort[ch], static_cast<int>(oled.getX() + 10), static_cast<int>(ry), 48, static_cast<int>(rowH), juce::Justification::centredLeft);

        auto drawCell = [&](float cx, float cw, const juce::String& txt, bool lit, juce::Colour col)
        {
            const auto cr = juce::Rectangle<float>(cx, ry + 4.0f, cw, rowH - 8.0f);
            g.setColour(lit ? col.withAlpha(0.30f) : juce::Colour(0xff191c24));
            g.fillRoundedRectangle(cr, 3.0f);
            if (lit)
            {
                g.setColour(col.withAlpha(0.75f));
                g.drawRoundedRectangle(cr, 3.0f, 0.9f);
            }
            g.setFont(SpaceArt::monoFont(8.4f, true));
            g.setColour(lit ? juce::Colours::white : offTextCol);
            g.drawText(txt, cr.toNearestInt(), juce::Justification::centred);
        };

        drawCell(oled.getX() + 62.0f,  30.0f, on ? "ON" : "OFF", on, lfoCol);
        drawCell(oled.getX() + 98.0f,  58.0f, BeatLfoBank::targetName(ch, tIdx), on, chCol);
        drawCell(oled.getX() + 162.0f, 46.0f, BeatLfoBank::rateName(rIdx), on, SpaceColours::neonCyan);
        drawCell(oled.getX() + 214.0f, 40.0f, BeatLfoBank::waveName(wIdx), on, SpaceColours::neonYellow);

        // Depth bar (0..50%, clickable/draggable!)
        const auto depR = juce::Rectangle<float>(oled.getX() + 260.0f, ry + 5.0f, 74.0f, rowH - 10.0f);
        g.setColour(juce::Colour(0xff14171f));
        g.fillRoundedRectangle(depR, 3.0f);
        const float fillW = (dep / 0.50f) * (depR.getWidth() - 4.0f);
        if (fillW > 1.0f)
        {
            g.setColour((on ? lfoCol.brighter(0.15f) : offTextCol).withAlpha(on ? 0.68f : 0.34f));
            g.fillRoundedRectangle(depR.getX() + 2.0f, depR.getY() + 2.0f, fillW, depR.getHeight() - 4.0f, 2.0f);
        }
        g.setFont(SpaceArt::monoFont(8.0f, true));
        g.setColour(on ? juce::Colours::white : offTextCol.brighter(0.15f));
        g.drawText(juce::String(static_cast<int>(std::round(dep * 100.0f))) + "%", depR.toNearestInt(), juce::Justification::centred);

        // Live LFO phase mini-wave + moving dot (high visibility even when bypassed!)
        const auto phR = juce::Rectangle<float>(oled.getX() + 340.0f, ry + 5.0f, 46.0f, rowH - 10.0f);
        juce::Path wp;
        for (int i = 0; i <= 16; ++i)
        {
            const float t = static_cast<float>(i) / 16.0f;
            const float v = BeatLfoBank::evalWave(wIdx, t);
            const float px = phR.getX() + t * phR.getWidth();
            const float py = phR.getCentreY() - v * (phR.getHeight() - 4.0f);
            if (i == 0) wp.startNewSubPath(px, py); else wp.lineTo(px, py);
        }
        if (on)
        {
            g.setColour(lfoCol.withAlpha(0.32f));
            g.strokePath(wp, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(lfoCol.brighter(0.35f));
            g.strokePath(wp, juce::PathStrokeType(1.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float dx = phR.getX() + ph * phR.getWidth();
            const float dy = phR.getCentreY() - BeatLfoBank::evalWave(wIdx, ph) * (phR.getHeight() - 4.0f);
            g.setColour(juce::Colours::white);
            g.fillEllipse(dx - 2.4f, dy - 2.4f, 4.8f, 4.8f);
        }
        else
        {
            g.setColour(offTextCol.withAlpha(0.55f));
            g.strokePath(wp, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    g.setFont(SpaceArt::silkFont(7.2f, false));
    g.setColour(SpaceColours::textMid);
    g.drawText("CLICK CELLS TO TOGGLE / CYCLE   |   CLICK DEPTH BAR TO SET 0-50%",
               static_cast<int>(oled.getX() + 8), static_cast<int>(oled.getBottom() - 20),
               static_cast<int>(oled.getWidth() - 16), 14, juce::Justification::centred);
}

void StepGrid::paintChaosScreen(juce::Graphics& g, juce::Rectangle<float> oled)
{
    const auto magCol = SpaceColours::themed(SpaceColours::neonMagenta);
    const bool chaosOn = apvtsRef.getRawParameterValue(ParamId::CHAOS_ENABLE)->load() > 0.5f;

    g.setFont(SpaceArt::monoFont(10.5f, true));
    g.setColour(magCol);
    g.drawText("CHAOS // UNDERGROUND SAMPLE BUS",
               static_cast<int>(oled.getX() + 8), static_cast<int>(oled.getY() + 8),
               static_cast<int>(oled.getWidth() - 16), 16, juce::Justification::centred);

    // Expanded mixi-style vertical-bar waveform display (+24% taller!) + dedicated Sampler Meter!
    const auto waveR = juce::Rectangle<float>(oled.getX() + 14.0f, oled.getY() + 60.0f, oled.getWidth() - 50.0f, 146.0f);
    g.setColour(juce::Colour(0xff08090c));
    g.fillRoundedRectangle(waveR, 4.0f);
    g.setColour(magCol.withAlpha(0.32f));
    g.drawRoundedRectangle(waveR, 4.0f, 0.9f);
    g.setColour(magCol.withAlpha(0.14f));
    g.drawHorizontalLine(static_cast<int>(waveR.getCentreY()), waveR.getX() + 2.0f, waveR.getRight() - 2.0f);

    // High-density vertical bar waveform (inspired by mixi!)
    constexpr int NP = ChaosSampleBus::PREVIEW_POINTS;
    const float midY = waveR.getCentreY();
    const float ampH = waveR.getHeight() * 0.44f;
    const float barStep = (waveR.getWidth() - 10.0f) / static_cast<float>(NP);
    for (int i = 0; i < NP; ++i)
    {
        const float s = std::abs(chaosBusRef.getPreviewSample(i));
        const float bh = std::max(2.0f, s * ampH * 2.0f);
        const float bx = waveR.getX() + 5.0f + static_cast<float>(i) * barStep;
        const auto barCol = (i % 4 == 0) ? SpaceColours::themed(SpaceColours::neonCyan) : magCol;
        g.setColour(barCol.withAlpha(chaosOn ? 0.88f : 0.35f));
        g.fillRoundedRectangle(bx, midY - bh * 0.5f, std::max(1.8f, barStep - 1.0f), bh, 1.0f);
    }

    // START & END Slice Markers (luminous vertical lines + flags on waveform!)
    {
        const float slNorm = chaosBusRef.sliceStartNorm.load(std::memory_order_relaxed);
        const float slX = waveR.getX() + 5.0f + slNorm * (waveR.getWidth() - 10.0f);
        const auto cCol = SpaceColours::themed(SpaceColours::neonCyan);
        g.setColour(cCol.withAlpha(0.24f));
        g.fillRect(slX - 1.5f, waveR.getY() + 2.0f, 3.0f, waveR.getHeight() - 4.0f);
        g.setColour(cCol.withAlpha(0.92f));
        g.drawVerticalLine(static_cast<int>(slX), waveR.getY() + 2.0f, waveR.getBottom() - 2.0f);
        g.fillRoundedRectangle(slX, waveR.getY() + 2.0f, 16.0f, 9.0f, 1.5f);
        g.setFont(SpaceArt::monoFont(6.2f, true));
        g.setColour(juce::Colour(0xff08090c));
        g.drawText("ST", static_cast<int>(slX), static_cast<int>(waveR.getY() + 2), 16, 9, juce::Justification::centred);

        const float endX = waveR.getRight() - 6.0f;
        g.setColour(magCol.withAlpha(0.75f));
        g.drawVerticalLine(static_cast<int>(endX), waveR.getY() + 2.0f, waveR.getBottom() - 2.0f);
        g.fillRoundedRectangle(endX - 18.0f, waveR.getY() + 2.0f, 18.0f, 9.0f, 1.5f);
        g.setColour(juce::Colour(0xff08090c));
        g.drawText("END", static_cast<int>(endX - 18.0f), static_cast<int>(waveR.getY() + 2), 18, 9, juce::Justification::centred);
    }

    // Playhead slice line
    const float ph = chaosBusRef.getPlayheadNorm();
    if (chaosOn && ph >= 0.0f)
    {
        const float sx = waveR.getX() + 5.0f + ph * (waveR.getWidth() - 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.fillRect(sx - 1.0f, waveR.getY() + 2.0f, 2.0f, waveR.getHeight() - 4.0f);
    }

    // Dedicated small Sampler Meter (real post-process Chaos audio peak!)
    const auto smpMeterR = juce::Rectangle<float>(waveR.getRight() + 10.0f, waveR.getY() + 4.0f, 12.0f, waveR.getHeight() - 8.0f);
    const float smpActivity = std::clamp(analyzerRef.getChaosPeak() * 1.35f, 0.0f, 1.0f);
    drawChannelPillMeter(g, smpMeterR, smpActivity, 12, magCol, true);
    g.setFont(SpaceArt::monoFont(6.5f, true));
    g.setColour(magCol.withAlpha(0.85f));
    g.drawText("SMP", static_cast<int>(smpMeterR.getX() - 4), static_cast<int>(waveR.getBottom() + 2), 20, 10, juce::Justification::centred);

    // Hint below waveform (the 5 Chaos knobs & buttons are overlaid cleanly by OldSchoolBoxView!)
    g.setFont(SpaceArt::monoFont(8.0f, false));
    g.setColour(SpaceColours::textDim);
    g.drawText("DRAG & DROP WAV / AIFF (MAX 8S) ANYWHERE ON WINDOW",
               static_cast<int>(oled.getX() + 12), static_cast<int>(oled.getBottom() - 22),
               static_cast<int>(oled.getWidth() - 24), 14, juce::Justification::centred);
}

void StepGrid::paintScopeScreen(juce::Graphics& g, juce::Rectangle<float> oled)
{
    const auto greenCol = SpaceColours::themed(SpaceColours::neonGreen);
    const auto amberCol = SpaceColours::themed(SpaceColours::neonOrange);
    const auto yellowCol = SpaceColours::themed(SpaceColours::neonYellow);
    const int animFrame = visualState.kickCount.load(std::memory_order_relaxed) & 1;

    // Clean studio-instrument header: OSCILLOSCOPE on left | live peak dBFS on right (no marketing slop)
    const float pk = std::max(analyzerRef.getPeakL(), analyzerRef.getPeakR());
    const juce::String dbStr = (pk > 0.0005f)
        ? ("OUT " + juce::String(20.0f * std::log10(pk), 1) + " dBFS")
        : "OUT -inf dBFS";
    g.setFont(SpaceArt::monoFont(8.5f, true));
    g.setColour(greenCol.withAlpha(0.75f));
    g.drawText("OSCILLOSCOPE", static_cast<int>(oled.getX() + 12), static_cast<int>(oled.getY() + 8), 140, 14, juce::Justification::centredLeft);
    g.setColour(pk > 0.92f ? SpaceColours::neonRed : SpaceColours::textMid);
    g.drawText(dbStr, static_cast<int>(oled.getRight() - 152), static_cast<int>(oled.getY() + 8), 140, 14, juce::Justification::centredRight);

    const auto scopeR = juce::Rectangle<float>(oled.getX() + 12.0f, oled.getY() + 26.0f, oled.getWidth() - 24.0f, 130.0f);
    // Whisper-subtle Tektronix graticule (7% grid so the phosphor trace pops with maximum contrast #27!)
    g.setColour(greenCol.withAlpha(0.07f));
    for (int gx = 1; gx < 10; ++gx)
        g.drawVerticalLine(static_cast<int>(scopeR.getX() + scopeR.getWidth() * (static_cast<float>(gx) / 10.0f)), scopeR.getY(), scopeR.getBottom());
    for (int gy = 1; gy < 6; ++gy)
        g.drawHorizontalLine(static_cast<int>(scopeR.getY() + scopeR.getHeight() * (static_cast<float>(gy) / 6.0f)), scopeR.getX(), scopeR.getRight());
    g.setColour(greenCol.withAlpha(0.12f));
    g.drawRect(scopeR, 0.7f);
    g.drawHorizontalLine(static_cast<int>(scopeR.getCentreY()), scopeR.getX(), scopeR.getRight());

    const int liveHead = analyzerRef.getScopeHead();
    const float midY = scopeR.getCentreY();
    const float amp  = scopeR.getHeight() * 0.44f;
    for (int age = SCOPE_HIST - 1; age >= 0; --age)
    {
        const int hi = (scopeHistWrite - 1 - age + SCOPE_HIST) % SCOPE_HIST;
        const auto& frame = scopeHistory[static_cast<size_t>(hi)];
        juce::Path p;
        for (int i = 0; i < OutputAnalyzer::SCOPE_SIZE; ++i)
        {
            const float sample = (age == 0) ? analyzerRef.getScopeSample(liveHead + i) : frame[static_cast<size_t>(i)];
            const float px = scopeR.getX() + 3.0f + (static_cast<float>(i) / static_cast<float>(OutputAnalyzer::SCOPE_SIZE - 1)) * (scopeR.getWidth() - 6.0f);
            const float py = midY - std::clamp(sample, -1.0f, 1.0f) * amp;
            if (i == 0) p.startNewSubPath(px, py);
            else        p.lineTo(px, py);
        }
        const float a = (age == 0) ? 0.96f : (0.16f * static_cast<float>(SCOPE_HIST - age));
        g.setColour((age == 0 ? greenCol : amberCol).withAlpha(a));
        g.strokePath(p, juce::PathStrokeType(age == 0 ? 1.8f : 1.0f));
    }

    // Phase Correlation Bar (-1 .. 0 .. +1 with explicit scale ticks & red/amber warning below 0!)
    const auto phR = juce::Rectangle<float>(oled.getX() + 12.0f, scopeR.getBottom() + 8.0f, oled.getWidth() - 24.0f, 22.0f);
    g.setColour(juce::Colour(0xff080b09));
    g.fillRoundedRectangle(phR, 3.0f);
    g.setColour(greenCol.withAlpha(0.18f));
    g.drawRoundedRectangle(phR, 3.0f, 0.7f);
    {
        const float corr = analyzerRef.getPhaseCorr(); // -1..+1
        const float trackX = phR.getX() + 68.0f;
        const float trackW = phR.getWidth() - 124.0f;
        const float midX = trackX + trackW * 0.5f;
        const float dotX = trackX + (corr * 0.5f + 0.5f) * trackW;
        g.setFont(SpaceArt::monoFont(7.4f, true));
        g.setColour(SpaceColours::textMid);
        g.drawText("CORR", static_cast<int>(phR.getX() + 6), static_cast<int>(phR.getY() + 4), 42, 14, juce::Justification::centredLeft);

        // Scale labels & ticks: -1, 0 (pronounced center tick), +1
        g.setFont(SpaceArt::monoFont(6.5f, true));
        g.setColour(SpaceColours::neonRed.withAlpha(0.75f));
        g.drawText("-1", static_cast<int>(trackX - 18), static_cast<int>(phR.getY() + 4), 15, 14, juce::Justification::centredRight);
        g.setColour(greenCol.withAlpha(0.80f));
        g.drawText("+1", static_cast<int>(trackX + trackW + 3), static_cast<int>(phR.getY() + 4), 16, 14, juce::Justification::centredLeft);

        // Track rail + pronounced center 0 tick
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawHorizontalLine(static_cast<int>(phR.getCentreY()), trackX, trackX + trackW);
        g.setColour(juce::Colours::white.withAlpha(0.42f));
        g.drawVerticalLine(static_cast<int>(midX), phR.getY() + 3.0f, phR.getBottom() - 3.0f);

        const auto pCol = (corr < 0.0f) ? SpaceColours::neonRed : (corr > 0.65f ? greenCol : amberCol);
        g.setColour(pCol.withAlpha(0.55f));
        g.fillRect(std::min(midX, dotX), phR.getCentreY() - 2.5f, std::abs(dotX - midX), 5.0f);
        g.setColour(pCol);
        g.fillEllipse(dotX - 4.0f, phR.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setFont(SpaceArt::monoFont(7.4f, true));
        g.setColour(pCol);
        g.drawText(juce::String(corr >= 0.0f ? "+" : "") + juce::String(corr, 2),
                   static_cast<int>(phR.getRight() - 38), static_cast<int>(phR.getY() + 4), 34, 14, juce::Justification::centredRight);
    }

    const auto specR = juce::Rectangle<float>(oled.getX() + 12.0f, phR.getBottom() + 8.0f, oled.getWidth() - 24.0f, oled.getBottom() - phR.getBottom() - 20.0f);
    g.setColour(greenCol.withAlpha(0.16f));
    g.drawRect(specR, 0.8f);

    static const char* bandLbls[8] = { "60", "125", "250", "500", "1K", "2K", "4K", "8K" };
    const float colW = (specR.getWidth() - 12.0f) / 8.0f;
    for (int b = 0; b < OutputAnalyzer::NUM_BANDS; ++b)
    {
        const float v = std::clamp(analyzerRef.getBand(b) * 2.2f, 0.05f, 1.0f);
        const float maxH = specR.getHeight() - 34.0f;
        const float bh = v * maxH;
        const float bx = specR.getX() + 6.0f + static_cast<float>(b) * colW;
        const auto col = (b < 3) ? yellowCol : (b < 6 ? greenCol : SpaceColours::themed(SpaceColours::neonCyan));

        SpaceArt::drawLedBar(g, { bx + 4.0f, specR.getBottom() - 16.0f - maxH, colW - 8.0f, maxH }, v, 14, true, false);
        SpaceArt::drawInvader(g, b % 3, animFrame ^ (b & 1),
                              bx + colW * 0.5f - 10.0f, specR.getBottom() - 34.0f - bh, 2, col, 0.16f);

        g.setFont(SpaceArt::monoFont(7.5f, true));
        g.setColour(SpaceColours::textMid);
        g.drawText(bandLbls[b], static_cast<int>(bx), static_cast<int>(specR.getBottom() - 14), static_cast<int>(colW), 12, juce::Justification::centred);
    }
}

// ── Hit Testing & Mouse Interaction ──────────────────────────────────────────

std::pair<int,int> StepGrid::hitMatrixCell(int x, int y) const
{
    const auto oled = mainScreenRect().toFloat();
    const float lblW = 50.0f;
    const float gx = oled.getX() + lblW;
    const float gy = oled.getY() + 38.0f;
    const float gw = oled.getWidth() - lblW - 10.0f;
    const float gh = oled.getHeight() - 46.0f;
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    if (fx < gx || fx >= gx + gw || fy < gy || fy >= gy + gh)
        return { -1, -1 };
    const int ch = std::clamp(static_cast<int>((fy - gy) / (gh / 8.0f)), 0, NUM_CH - 1);
    const float groupGap = 4.0f;
    const float colW = (gw - 3.0f * groupGap) / 16.0f;
    const float groupW = 4.0f * colW + groupGap;
    const float relX = fx - gx;
    const int grp = std::clamp(static_cast<int>(relX / groupW), 0, 3);
    const int inGrp = std::clamp(static_cast<int>((relX - static_cast<float>(grp) * groupW) / colW), 0, 3);
    return { ch, grp * 4 + inGrp };
}

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    // 1a. Drag BPM box inside top CRT to adjust Standalone BPM (40..240)
    if (bpmBoxRect().contains(e.x, e.y))
    {
        dragMode = DragMode::BpmDrag;
        dragStartY = e.y;
        dragStartBpm = bpmRef.load(std::memory_order_relaxed);
        return;
    }

    // 1b. Tap rest of top CRT status bar to cycle Core Screen (SEQ -> MATRIX -> MIX -> LFO -> CHAOS -> SCOPE)
    if (statusCrtRect().contains(e.x, e.y))
    {
        const int next = (static_cast<int>(coreScreen) + 1) % 6;
        setCoreScreen(static_cast<CoreScreen>(next));
        return;
    }

    // 2. Click on any channel strip header (select/audition/Euclidean) OR Face-B Local LFO card!
    for (int ch = 0; ch < NUM_CH; ++ch)
    {
        const auto sr = stripRect(ch);
        const auto hdr = sr.withHeight(30);
        if (hdr.contains(e.x, e.y))
        {
            if (e.mods.isRightButtonDown())
            {
                auto popup = std::make_unique<EuclideanPopup>(sequencer, ch,
                    [this] { undoRef.push(sequencer); repaint(); });
                juce::CallOutBox::launchAsynchronously(std::move(popup),
                    localAreaToGlobal(hdr.withWidth(140)), nullptr);
                return;
            }
            setSelectedChannel(ch);
            voiceBank.requestTrigger(ch);
            return;
        }
        if (isFlipped[idx(ch)])
        {
            const auto lr = stripFlipLfoRect(ch);
            const float fx = static_cast<float>(e.x), fy = static_cast<float>(e.y);
            if (lr.contains(fx, fy))
            {
                auto& lc = lfoBankRef.channels[idx(ch)];
                const float relY = fy - lr.getY();
                const float relX = fx - lr.getX();
                if (relY < 20.0f)
                    lc.targetIdx.store((lc.targetIdx.load(std::memory_order_relaxed) + 1) & 1, std::memory_order_relaxed);
                else if (relY < 46.0f)
                {
                    if (relX < lr.getWidth() * 0.5f)
                        lc.rateIdx.store((lc.rateIdx.load(std::memory_order_relaxed) + 1) % BeatLfoBank::NUM_RATES, std::memory_order_relaxed);
                    else
                        lc.waveIdx.store((lc.waveIdx.load(std::memory_order_relaxed) + 1) % BeatLfoBank::NUM_WAVES, std::memory_order_relaxed);
                }
                else
                {
                    const float norm = std::clamp((relX - 6.0f) / std::max(1.0f, lr.getWidth() - 12.0f), 0.0f, 1.0f);
                    lc.depth.store(norm * 0.50f, std::memory_order_relaxed);
                    lc.enabled.store(true, std::memory_order_relaxed);
                }
                repaint();
                return;
            }
        }
    }

    // 3. Main Screen interaction
    const auto oled = mainScreenRect().toFloat();
    if (!mainScreenRect().contains(e.x, e.y))
        return;

    const float ex = static_cast<float>(e.x);
    const float ey = static_cast<float>(e.y);
    const int ch = selectedChannel;
    const int pageOff = viewPage[idx(ch)] * VISIBLE_STEPS;
    const int patLen = sequencer.getPatternLength(ch);

    if (coreScreen == CoreScreen::Seq)
    {
        if (ey >= oled.getY() + 4.0f && ey <= oled.getY() + 30.0f)
        {
            if (ex < oled.getCentreX()) setSelectedChannel((ch + NUM_CH - 1) % NUM_CH);
            else                        setSelectedChannel((ch + 1) % NUM_CH);
            return;
        }

        const float labelW = 42.0f;
        const float gridX  = oled.getX() + labelW;
        const float gridW  = oled.getWidth() - labelW - 10.0f;
        const float colW   = gridW / 16.0f;
        if (ex < gridX || ex >= gridX + gridW) return;

        const int visStep = std::clamp(static_cast<int>((ex - gridX) / colW), 0, 15);
        const int absStep = pageOff + visStep;
        if (absStep >= patLen) return;

        const bool isBass = (ch >= 6);
        const float velY  = oled.getY() + 46.0f;
        const float velH  = 114.0f;
        const float midY  = velY + velH + 10.0f;
        const float probY = midY + 66.0f;

        undoRef.push(sequencer);
        if (ey >= velY && ey <= velY + velH)
        {
            const bool newState = !sequencer.getStep(ch, absStep);
            sequencer.setStep(ch, absStep, newState);
            dragMode = DragMode::Velocity;
            dragCh = ch; dragStep = absStep; dragStartY = e.y;
            dragStartVal = sequencer.getVelocity(ch, absStep);
            repaint();
        }
        else if (isBass && ey >= midY && ey < midY + 20.0f)
        {
            dragMode = DragMode::BassNote;
            dragCh = ch; dragStep = absStep; dragStartY = e.y;
            dragStartVal = sequencer.getBassNoteOffset(ch - 6, absStep);
        }
        else if (isBass && ey >= midY + 20.0f && ey < midY + 39.0f)
        {
            sequencer.toggleBassSlide(ch - 6, absStep);
            repaint();
        }
        else if (isBass && ey >= midY + 39.0f && ey < midY + 58.0f)
        {
            sequencer.toggleBassAccent(ch - 6, absStep);
            repaint();
        }
        else if (!isBass && ey >= midY && ey < probY)
        {
            dragMode = DragMode::MicroTiming;
            dragCh = ch; dragStep = absStep; dragStartY = e.y;
            dragStartVal = sequencer.getMicroTiming(ch, absStep);
        }
        else if (ey >= probY && ey <= oled.getBottom() - 36.0f)
        {
            const float norm = std::clamp((oled.getBottom() - 36.0f - ey) / std::max(1.0f, oled.getBottom() - 36.0f - probY), 0.0f, 1.0f);
            sequencer.setProbability(ch, absStep, static_cast<uint8_t>(std::round(norm * 100.0f)));
            dragMode = DragMode::Probability;
            dragCh = ch; dragStep = absStep; dragStartY = e.y;
            repaint();
        }
    }
    else if (coreScreen == CoreScreen::Matrix)
    {
        const float gy = oled.getY() + 28.0f;
        const float gh = oled.getHeight() - 38.0f;
        if (ex >= oled.getX() && ex < oled.getX() + 50.0f && ey >= gy && ey < gy + gh)
        {
            const int rowCh = std::clamp(static_cast<int>((ey - gy) / (gh / 8.0f)), 0, NUM_CH - 1);
            setSelectedChannel(rowCh);
            voiceBank.requestTrigger(rowCh);
            return;
        }
        const auto [mCh, mStep] = hitMatrixCell(e.x, e.y);
        if (mCh >= 0 && mStep >= 0)
        {
            const int absStep = viewPage[idx(mCh)] * VISIBLE_STEPS + mStep;
            if (absStep < sequencer.getPatternLength(mCh))
            {
                undoRef.push(sequencer);
                const bool newState = !sequencer.getStep(mCh, absStep);
                sequencer.setStep(mCh, absStep, newState);
                dragMode = newState ? DragMode::PaintOn : DragMode::PaintOff;
                dragCh = mCh; dragStep = absStep;
                setSelectedChannel(mCh);
                repaint();
            }
        }
    }
    else if (coreScreen == CoreScreen::Lfo)
    {
        const float rowStartY = oled.getY() + 46.0f;
        const float rowH = (oled.getBottom() - 26.0f - rowStartY) / 8.0f;
        if (ey >= rowStartY && ey < rowStartY + rowH * 8.0f)
        {
            const int rowCh = std::clamp(static_cast<int>((ey - rowStartY) / rowH), 0, NUM_CH - 1);
            auto& lc = lfoBankRef.channels[idx(rowCh)];
            const float rx = ex - oled.getX();
            if (rx >= 60.0f && rx < 94.0f)
                lc.enabled.store(!lc.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            else if (rx >= 96.0f && rx < 158.0f)
                lc.targetIdx.store((lc.targetIdx.load(std::memory_order_relaxed) + 1) & 1, std::memory_order_relaxed);
            else if (rx >= 160.0f && rx < 210.0f)
                lc.rateIdx.store((lc.rateIdx.load(std::memory_order_relaxed) + 1) % BeatLfoBank::NUM_RATES, std::memory_order_relaxed);
            else if (rx >= 212.0f && rx < 256.0f)
                lc.waveIdx.store((lc.waveIdx.load(std::memory_order_relaxed) + 1) % BeatLfoBank::NUM_WAVES, std::memory_order_relaxed);
            else if (rx >= 260.0f && rx <= 336.0f)
            {
                const float norm = std::clamp((rx - 260.0f) / 74.0f, 0.0f, 1.0f);
                lc.depth.store(norm * 0.50f, std::memory_order_relaxed);
                lc.enabled.store(true, std::memory_order_relaxed);
            }
            setSelectedChannel(rowCh);
            repaint();
        }
    }
    else if (coreScreen == CoreScreen::Chaos)
    {
        // Click-to-Slice anywhere on the expanded 146px OLED waveform!
        const auto waveR = juce::Rectangle<float>(oled.getX() + 14.0f, oled.getY() + 60.0f, oled.getWidth() - 50.0f, 146.0f);
        if (waveR.contains(ex, ey))
        {
            const float normX = std::clamp((ex - (waveR.getX() + 5.0f)) / std::max(1.0f, waveR.getWidth() - 10.0f), 0.0f, 0.95f);
            chaosBusRef.triggerFromSlice(normX);
            repaint();
        }
    }
    else if (coreScreen == CoreScreen::Scope)
    {
        setCoreScreen(CoreScreen::Seq);
    }
}

void StepGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode == DragMode::BpmDrag)
    {
        const float delta = static_cast<float>(dragStartY - e.y) * (e.mods.isShiftDown() ? 0.25f : 0.5f);
        const float newBpm = std::clamp(std::round(dragStartBpm + delta), 40.0f, 240.0f);
        bpmRef.store(newBpm, std::memory_order_relaxed);
        repaint();
        return;
    }

    if (dragMode == DragMode::None || dragCh < 0) return;

    // Horizontal swipe-to-paint across 16 steps in SEQ mode!
    if (coreScreen == CoreScreen::Seq && (dragMode == DragMode::Velocity || dragMode == DragMode::Probability || dragMode == DragMode::MicroTiming))
    {
        const auto oled = mainScreenRect().toFloat();
        const float gridX = oled.getX() + 42.0f;
        const float gridW = oled.getWidth() - 52.0f;
        const float colW  = gridW / 16.0f;
        const float ex = static_cast<float>(e.x);
        const float ey = static_cast<float>(e.y);
        if (ex >= gridX && ex < gridX + gridW)
        {
            const int visStep = std::clamp(static_cast<int>((ex - gridX) / colW), 0, 15);
            const int absStep = viewPage[idx(dragCh)] * VISIBLE_STEPS + visStep;
            if (absStep < sequencer.getPatternLength(dragCh))
            {
                if (dragMode == DragMode::Velocity)
                {
                    const float velY = oled.getY() + 46.0f;
                    const float velH = 114.0f;
                    const float norm = std::clamp((velY + velH - ey) / velH, 0.05f, 1.0f);
                    sequencer.setStep(dragCh, absStep, true);
                    sequencer.setVelocity(dragCh, absStep, static_cast<uint8_t>(std::round(norm * 127.0f)));
                    repaint();
                    return;
                }
                if (dragMode == DragMode::Probability)
                {
                    const float probY = oled.getY() + 236.0f;
                    const float norm = std::clamp((oled.getBottom() - 36.0f - ey) / std::max(1.0f, oled.getBottom() - 36.0f - probY), 0.0f, 1.0f);
                    sequencer.setProbability(dragCh, absStep, static_cast<uint8_t>(std::round(norm * 100.0f)));
                    repaint();
                    return;
                }
                if (dragMode == DragMode::MicroTiming)
                {
                    const float centerY = oled.getY() + 196.0f;
                    const float norm = std::clamp((centerY - ey) / 20.0f, -1.0f, 1.0f);
                    sequencer.setMicroTiming(dragCh, absStep, static_cast<int8_t>(std::round(norm * 50.0f)));
                    repaint();
                    return;
                }
            }
        }
    }

    if (dragMode == DragMode::Velocity && dragStep >= 0)
    {
        const int delta = (dragStartY - e.y) * 2;
        const int v = std::clamp(dragStartVal + delta, 1, 127);
        sequencer.setStep(dragCh, dragStep, true);
        sequencer.setVelocity(dragCh, dragStep, static_cast<uint8_t>(v));
        repaint();
        return;
    }
    if (dragMode == DragMode::BassNote && dragCh >= 6 && dragStep >= 0)
    {
        const int delta = (dragStartY - e.y) / 6;
        sequencer.setBassNoteOffset(dragCh - 6, dragStep, static_cast<int8_t>(std::clamp(dragStartVal + delta, -24, 24)));
        repaint();
        return;
    }
    if (dragMode == DragMode::MicroTiming && dragStep >= 0)
    {
        const int delta = (dragStartY - e.y) * 2;
        sequencer.setMicroTiming(dragCh, dragStep, static_cast<int8_t>(std::clamp(dragStartVal + delta, -50, 50)));
        repaint();
        return;
    }
    if (dragMode == DragMode::Probability && dragStep >= 0)
    {
        const auto oled = mainScreenRect().toFloat();
        const float probY = oled.getY() + 236.0f;
        const float norm = std::clamp((oled.getBottom() - 36.0f - static_cast<float>(e.y)) / std::max(1.0f, oled.getBottom() - 36.0f - probY), 0.0f, 1.0f);
        sequencer.setProbability(dragCh, dragStep, static_cast<uint8_t>(std::round(norm * 100.0f)));
        repaint();
        return;
    }
    if ((dragMode == DragMode::PaintOn || dragMode == DragMode::PaintOff) && coreScreen == CoreScreen::Matrix)
    {
        const bool targetState = (dragMode == DragMode::PaintOn);
        const auto [mCh, mStep] = hitMatrixCell(e.x, e.y);
        if (mCh == dragCh && mStep >= 0)
        {
            const int absStep = viewPage[idx(mCh)] * VISIBLE_STEPS + mStep;
            if (absStep < sequencer.getPatternLength(mCh))
            {
                sequencer.setStep(mCh, absStep, targetState);
                repaint();
            }
        }
    }
}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::None;
    dragCh = -1;
    dragStep = -1;
}

void StepGrid::mouseMove(const juce::MouseEvent&) {}
void StepGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (bpmBoxRect().contains(e.x, e.y))
    {
        bpmRef.store(155.0f, std::memory_order_relaxed);
        repaint();
    }
}

void StepGrid::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) < 1.0e-5f) return;
    const int dir = (wheel.deltaY > 0.0f) ? 1 : -1;

    if (bpmBoxRect().contains(e.x, e.y))
    {
        const float step = e.mods.isShiftDown() ? 5.0f : 1.0f;
        const float cur = bpmRef.load(std::memory_order_relaxed);
        bpmRef.store(std::clamp(std::round(cur + static_cast<float>(dir) * step), 40.0f, 240.0f), std::memory_order_relaxed);
        repaint();
        return;
    }

    if (coreScreen == CoreScreen::Seq && selectedChannel >= 6 && mainScreenRect().contains(e.x, e.y))
    {
        const auto oled = mainScreenRect().toFloat();
        const float gridX = oled.getX() + 42.0f;
        const float gridW = oled.getWidth() - 52.0f;
        const float ex = static_cast<float>(e.x);
        if (ex >= gridX && ex < gridX + gridW)
        {
            const int visStep = std::clamp(static_cast<int>((ex - gridX) / (gridW / 16.0f)), 0, 15);
            const int absStep = viewPage[idx(selectedChannel)] * VISIBLE_STEPS + visStep;
            if (absStep < sequencer.getPatternLength(selectedChannel))
            {
                const int stepSemis = e.mods.isShiftDown() ? 12 : 1;
                const int cur = sequencer.getBassNoteOffset(selectedChannel - 6, absStep);
                sequencer.setBassNoteOffset(selectedChannel - 6, absStep,
                    static_cast<int8_t>(std::clamp(cur + dir * stepSemis, -24, 24)));
                repaint();
            }
        }
    }
}
