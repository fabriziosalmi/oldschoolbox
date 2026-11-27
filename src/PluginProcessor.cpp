#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/MidiRouter.h"
#include "core/TransportSync.h"
#include "core/ParamIds.h"
#include "voices/DspConstants.h"

static constexpr uint64_t kDefaultPatterns[8] = {
    0x1111, 0x1010, 0x0444, 0x0080, 0x0400, 0x2000, 0x0204, 0x2080
};

OldSchoolBoxProcessor::OldSchoolBoxProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Main",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Kick",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Snare", juce::AudioChannelSet::stereo(), false)
        .withOutput("Hats",  juce::AudioChannelSet::stereo(), false)
        .withOutput("Tom",   juce::AudioChannelSet::stereo(), false)
        .withOutput("Perc1", juce::AudioChannelSet::stereo(), false)
        .withOutput("Perc2", juce::AudioChannelSet::stereo(), false)
        .withOutput("Bass1", juce::AudioChannelSet::stereo(), false)
        .withOutput("Bass2", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "STATE", ParamLayout::create()),
      voiceBank(apvts)
{
    lastSteps.fill(-1);
    pendingMidiNotes.fill(-1);
    for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        sequencer.setPattern(ch, kDefaultPatterns[ch]);

    midiLearn.init(apvts);
    voiceBank.setLfoBank(&beatLfo);
    for (int s = 0; s < Scenes::COUNT; ++s)
    {
        Scenes::capture(scenes[idx(s)], sequencer);
        sceneParams[idx(s)] = apvts.copyState();
    }
}

bool OldSchoolBoxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.outputBuses.isEmpty() || layouts.outputBuses.size() > 1 + OsbBus::COUNT)
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void OldSchoolBoxProcessor::prepareToPlay(double sampleRate, int)
{
    lastSteps.fill(-1);
    prevBar = -1;
    internalPpq = 0.0;
    currentSampleRate = sampleRate;
    crossoverAlpha     = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 300.0 / sampleRate));
    tightCrossoverAlpha = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 150.0 / sampleRate));
    tightHpAlpha       = static_cast<float>(std::exp(-Dsp::TWO_PI * 40.0  / sampleRate));
    tightHpL = tightHpR = 0.0f;
    tightHpPrevL = tightHpPrevR = 0.0f;

    // Master 8 Hz DC Blocker + Linkwitz-Riley LR4 (24 dB/oct) Side HPF @ 120 Hz + Reverb/Delay HPFs
    masterDcAlpha = static_cast<float>(std::exp(-Dsp::TWO_PI * 8.0 / sampleRate));
    masterDcPrevL = masterDcOutL = masterDcPrevR = masterDcOutR = 0.0f;
    {
        // Butterworth 2nd-order HPF @ 120 Hz (Q = 0.70710678); cascaded twice = true LR4 (24 dB/oct)!
        const double w0 = Dsp::TWO_PI * 120.0 / sampleRate;
        const double cosW = std::cos(w0);
        const double alpha = std::sin(w0) / std::sqrt(2.0);
        const double a0 = 1.0 + alpha;
        lr4B0 = static_cast<float>(((1.0 + cosW) * 0.5) / a0);
        lr4B1 = static_cast<float>((-(1.0 + cosW)) / a0);
        lr4B2 = lr4B0;
        lr4A1 = static_cast<float>((-2.0 * cosW) / a0);
        lr4A2 = static_cast<float>((1.0 - alpha) / a0);
        sideLr4A = {};
        sideLr4B = {};
    }
    fxWetHpAlpha = static_cast<float>(std::exp(-Dsp::TWO_PI * 250.0 / sampleRate));
    delHpAlpha   = static_cast<float>(std::exp(-Dsp::TWO_PI * 300.0 / sampleRate));
    delLpAlpha   = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 6000.0 / sampleRate));
    delHp1L = delHp1PrevL = delHp2L = delHp2PrevL = 0.0f;
    delHp1R = delHp1PrevR = delHp2R = delHp2PrevR = 0.0f;
    delLpL  = delLpR      = 0.0f;
    revWetHpL = revWetHpR = revWetPrevL = revWetPrevR = 0.0f;

    // Parameter smoothing (~10 ms) (#28)
    paramSmoothAlpha = 1.0f - static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate)));
    smDrive     = apvts.getRawParameterValue(ParamId::MASTER_DRIVE)->load();
    smDelayMix  = apvts.getRawParameterValue(ParamId::DELAY_MIX)->load();
    smReverbMix = apvts.getRawParameterValue(ParamId::REVERB_MIX)->load();
    smCompAmt   = apvts.getRawParameterValue(ParamId::COMP_AMOUNT)->load();
    smDjFilter  = apvts.getRawParameterValue(ParamId::DJ_FILTER)->load();
    smMasterVol = apvts.getRawParameterValue(ParamId::MASTER_VOL)->load();
    djSvfL = {}; djSvfR = {};
    killHpAlpha   = static_cast<float>(std::exp(-Dsp::TWO_PI * 120.0 / sampleRate));
    killFadeAlpha = 1.0f - static_cast<float>(std::exp(-1.0 / (0.012 * sampleRate))); // 12 ms clickless crossfade
    smKillSub     = 0.0f;
    killHp1L = killHp1PrevL = killHp2L = killHp2PrevL = 0.0f;
    killHp1R = killHp1PrevR = killHp2R = killHp2PrevR = 0.0f;
    for (int ch = 0; ch < OsbBus::COUNT; ++ch)
    {
        smVol[idx(ch)] = apvts.getRawParameterValue(ParamId::vol(ch))->load();
        smPan[idx(ch)] = apvts.getRawParameterValue(ParamId::pan(ch))->load();
    }

    voiceBank.prepare(sampleRate);
    chaosBus.prepare(sampleRate);
    spiralFx.prepare(sampleRate);
    plateReverb.prepare(sampleRate);
    masterDrive.prepare(sampleRate);
    glueComp.prepare(sampleRate);
    masterLimiter.prepare(sampleRate);
    masterLimiter.setThreshold(0.90f);
    masterLimiter.setCeiling(0.966f); // -0.3 dBTP True-Peak ceiling (#30)
    masterLimiter.setHeadroomDb(-1.5f);
    analyzer.prepare(sampleRate);
    for (auto& cs : crossover) { cs.lpL = 0.0f; cs.lpR = 0.0f; }

    delay.size = std::max(2, static_cast<int>(std::ceil(sampleRate * 2.0)) + 2);
    delay.bufL.assign(static_cast<size_t>(delay.size), 0.0f);
    delay.bufR.assign(static_cast<size_t>(delay.size), 0.0f);
    delay.writePos = 0;
    delay.smoothedDelay = 0.0f;
}

void OldSchoolBoxProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    voiceBank.consumeUiTriggers();
    midiLearn.processIncomingMidi(midi);
    MidiRouter::process(midi, voiceBank);

    const float swing = apvts.getRawParameterValue(ParamId::SWING)->load();
    const bool  midiOut = apvts.getRawParameterValue(ParamId::MIDI_OUT)->load() > 0.5f;
    const bool  autoEnabled = apvts.getRawParameterValue(ParamId::AUTO_ENABLE)->load() > 0.5f;
    const float autoMut = apvts.getRawParameterValue(ParamId::AUTO_MUTATION)->load();
    const float autoDr  = apvts.getRawParameterValue(ParamId::AUTO_DRIFT)->load();

    // Capture anchor on rising edge of AUTO_ENABLE
    if (autoEnabled && !autoPrevEnabled)
    {
        Scenes::Snapshot snap;
        Scenes::capture(snap, sequencer);
        autoMode.setAnchor(snap);
    }
    autoPrevEnabled = autoEnabled;

    // Sync BPM from DAW when host reports a tempo change (preserves manual BPM when DAW is idle)
    bool isHostPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            isHostPlaying = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
            {
                const float hBpm = static_cast<float>(*bpm);
                if (hBpm >= 20.0f && (isHostPlaying || std::abs(hBpm - lastHostBpm) > 0.05f))
                {
                    currentBpm.store(hBpm, std::memory_order_relaxed);
                    lastHostBpm = hBpm;
                }
            }
        }
    }
    hostPlaying.store(isHostPlaying, std::memory_order_relaxed);

    const float activeBpm = std::clamp(currentBpm.load(std::memory_order_relaxed), 40.0f, 240.0f);
    const bool  intPlay   = internalPlay.load(std::memory_order_acquire);

    const bool  chaosOn    = apvts.getRawParameterValue(ParamId::CHAOS_ENABLE)->load() > 0.5f;
    const int   chaosBank  = static_cast<int>(apvts.getRawParameterValue(ParamId::CHAOS_BANK)->load());
    const float chaosDens  = apvts.getRawParameterValue(ParamId::CHAOS_DENSITY)->load();
    const float chaosGate  = apvts.getRawParameterValue(ParamId::CHAOS_GATE)->load();
    const float chaosGrit  = apvts.getRawParameterValue(ParamId::CHAOS_GRIT)->load();
    const float chaosPitch = apvts.getRawParameterValue(ParamId::CHAOS_PITCH)->load();
    const float chaosVol   = apvts.getRawParameterValue(ParamId::CHAOS_VOL)->load();
    const bool  monoSubOn  = apvts.getRawParameterValue(ParamId::MONO_SUB)->load() > 0.5f;
    const bool  dubThrowUi = dubThrowHeld.load(std::memory_order_acquire)
                          || (apvts.getRawParameterValue(ParamId::DUB_THROW)->load() > 0.5f);

    bool autoDubThrow = false;
    const int prevKickStep = lastSteps[0];

    juce::MidiBuffer outMidi;
    TransportSync::EventQueue subBlockEvents;
    TransportSync::process(getPlayHead(), sequencer, voiceBank, lastSteps, prevBar, swing,
                            scenes.data(), preFillSnapshot, audioRng,
                            midiOut ? &outMidi : nullptr, pendingMidiNotes,
                            visualState,
                            autoEnabled ? &autoMode : nullptr, autoMut, autoDr,
                            intPlay, internalPpq, activeBpm, currentSampleRate, buffer.getNumSamples(),
                            &subBlockEvents);

    // Update Beat-Synced 32-Bar LFO Bank deterministically from host PPQ or internal clock
    double activePpq = internalPpq;
    {
        if (isHostPlaying)
        {
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                    if (auto ppq = pos->getPpqPosition())
                        activePpq = *ppq;
        }
        beatLfo.update(activePpq);
    }

    // Drive Chaos Sample Bus on every 16th-note step boundary when enabled
    if (chaosOn && sequencer.isPlaying.load(std::memory_order_acquire) && lastSteps[0] != prevKickStep && lastSteps[0] >= 0)
    {
        const int64_t cb = sequencer.currentBar.load(std::memory_order_acquire);
        const int64_t fb = sequencer.fillStartBar.load(std::memory_order_acquire);
        const bool inFill = (fb >= 0 && cb == fb);
        chaosBus.onStep(lastSteps[0] & 15, cb, inFill, chaosBank, chaosDens, chaosGate, chaosPitch, audioRng, autoDubThrow);
    }

    // Advance the visual sample counter (drives all decay envelopes in UI)
    visualState.advance(buffer.getNumSamples());

    if (midiOut)
    {
        // Replace the input MIDI buffer with our generated events for the host
        midi.clear();
        midi.addEvents(outMidi, 0, buffer.getNumSamples(), 0);
    }
    else
    {
        // MIDI out disabled — flush final NoteOff for any pending notes so DAW never gets stuck notes!
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            if (pendingMidiNotes[idx(ch)] >= 0)
            {
                const int midiCh = (ch >= OsbBus::BASS1) ? (MidiMap::BASS1_CH + (ch - OsbBus::BASS1)) : 10;
                midi.addEvent(juce::MidiMessage::noteOff(midiCh, pendingMidiNotes[idx(ch)]), 0);
                pendingMidiNotes[idx(ch)] = -1;
            }
        }
    }

    const int numSamples = buffer.getNumSamples();

    // Master FX params (read once per buffer)
    const float driveAmt     = apvts.getRawParameterValue(ParamId::MASTER_DRIVE)->load();
    const float delayFb      = apvts.getRawParameterValue(ParamId::DELAY_FB)->load();
    const float delayMix     = apvts.getRawParameterValue(ParamId::DELAY_MIX)->load();
    const int   delaySyncDiv = static_cast<int>(apvts.getRawParameterValue(ParamId::DELAY_SYNC)->load());
    const float reverbSize   = apvts.getRawParameterValue(ParamId::REVERB_SIZE)->load();
    const float reverbMix    = apvts.getRawParameterValue(ParamId::REVERB_MIX)->load();
    const float compAmt      = apvts.getRawParameterValue(ParamId::COMP_AMOUNT)->load();
    const float spiralDrive  = apvts.getRawParameterValue(ParamId::SPIRAL_DRIVE)->load();
    const bool  tightMode    = apvts.getRawParameterValue(ParamId::TIGHT)->load() > 0.5f;

    plateReverb.setParams(tightMode ? (reverbSize * 0.72f) : reverbSize, 0.30f);
    glueComp.setAmount(compAmt);

    // Pick crossover alpha based on TIGHT toggle
    const float xoAlpha = tightMode ? tightCrossoverAlpha : crossoverAlpha;

    // Calculate delay time — synced to BPM or free-running
    float delayTimeSec;
    if (delaySyncDiv > 0)
    {
        // BPM-synced divisions: beat = 60/bpm
        const float beat = 60.0f / std::max(20.0f, currentBpm.load(std::memory_order_relaxed));
        // 0=off, 1=1/4, 2=1/8, 3=1/8d, 4=1/16, 5=1/16d, 6=1/32
        static constexpr float divMul[] = { 0.f, 1.0f, 0.5f, 0.75f, 0.25f, 0.375f, 0.125f };
        delayTimeSec = beat * divMul[delaySyncDiv];
    }
    else
    {
        delayTimeSec = apvts.getRawParameterValue(ParamId::DELAY_TIME)->load();
    }
    const int delaySamples = std::max(1, std::min(static_cast<int>(delayTimeSec * currentSampleRate),
                                                     delay.size - 1));

    // ── Main stereo output + optional per-voice aux stereo outputs (buses 1..8) ──
    // Clear all active output buses BEFORE taking write pointers so JUCE marks buffers non-clear.
    buffer.clear();
    auto mainBus = getBusBuffer(buffer, false, 0);
    auto* mainL = mainBus.getWritePointer(0);
    auto* mainR = mainBus.getWritePointer(1);

    // BUG FIX: process in chunks to handle buffers > STACK_MAX (offline bounce)
    analyzer.beginBlock(numSamples);
    chaosBus.consumeManualTrigger(chaosBank);

    constexpr int STACK_MAX = 2048;
    float tmpL[STACK_MAX], tmpR[STACK_MAX];
    float spiralInL[STACK_MAX], spiralInR[STACK_MAX];

    // True 1-sample-accurate sub-block splitting: split audio rendering at every scheduled
    // sequencer event's exact sampleOffset (identical timing at 32 or 2048 samples!)
    int nextEventIdx = 0;
    for (int offset = 0; offset < numSamples; )
    {
    while (nextEventIdx < subBlockEvents.count
           && subBlockEvents.events[static_cast<size_t>(nextEventIdx)].sampleOffset <= offset)
    {
        TransportSync::dispatchEvent(voiceBank, subBlockEvents.events[static_cast<size_t>(nextEventIdx)]);
        ++nextEventIdx;
    }

    int nextSplit = std::min(numSamples, offset + STACK_MAX);
    if (nextEventIdx < subBlockEvents.count)
        nextSplit = std::min(nextSplit, subBlockEvents.events[static_cast<size_t>(nextEventIdx)].sampleOffset);

    const int blockSize = std::max(1, nextSplit - offset);
    std::memset(spiralInL, 0, static_cast<size_t>(blockSize) * sizeof(float));
    std::memset(spiralInR, 0, static_cast<size_t>(blockSize) * sizeof(float));

    for (int voice = 0; voice < OsbBus::COUNT; ++voice)
    {
        const bool active = voiceBank.isActive(voice);
        const bool audible = sequencer.isChannelAudible(voice);

        // Always tick sidechain for bass even when inactive or muted
        if (!active || !audible)
        {
            if (voice >= OsbBus::BASS1)
                voiceBank.renderBus(voice, nullptr, nullptr, blockSize, 0.0f);
            continue;
        }

        const float targetVol = apvts.getRawParameterValue(ParamId::vol(voice))->load();
        const float targetPan = apvts.getRawParameterValue(ParamId::pan(voice))->load();

        // Render voice at unity into temp buffer, then apply click-free smoothed vol + equal-power pan
        std::memset(tmpL, 0, static_cast<size_t>(blockSize) * sizeof(float));
        std::memset(tmpR, 0, static_cast<size_t>(blockSize) * sizeof(float));
        voiceBank.renderBus(voice, tmpL, tmpR, blockSize, 1.0f);

        // ── Smoothed per-voice volume & equal-power panning (#28) ──
        float& vSm = smVol[idx(voice)];
        float& pSm = smPan[idx(voice)];
        for (int i = 0; i < blockSize; ++i)
        {
            vSm += paramSmoothAlpha * (targetVol - vSm);
            pSm += paramSmoothAlpha * (targetPan - pSm);
            const float panR = fastSin(pSm * Dsp::HALF_PI_F);
            const float panL = fastSin((1.0f - pSm) * Dsp::HALF_PI_F);
            const float s = tmpL[i] * vSm;  // mono render
            tmpL[i] = s * panL;
            tmpR[i] = s * panR;
        }
        analyzer.feedVoiceBlock(voice, tmpL, tmpR, blockSize);

        // ── Per-voice mono crossover (TIGHT toggle → tighter @ 150Hz) ──
        {
            auto& cs = crossover[idx(voice)];
            for (int i = 0; i < blockSize; ++i)
            {
                cs.lpL += xoAlpha * (tmpL[i] - cs.lpL);
                cs.lpR += xoAlpha * (tmpR[i] - cs.lpR);

                const float loMono = (cs.lpL + cs.lpR) * 0.5f;
                const float hiL = tmpL[i] - cs.lpL;
                const float hiR = tmpR[i] - cs.lpR;

                tmpL[i] = loMono + hiL;
                tmpR[i] = loMono + hiR;
            }
        }

        // ── Write to individual aux stereo bus if enabled by DAW ──
        const int auxIdx = voice + 1;
        if (auxIdx < getBusCount(false))
        {
            if (auto* bus = getBus(false, auxIdx); bus != nullptr && bus->isEnabled())
            {
                auto auxBuf = getBusBuffer(buffer, false, auxIdx);
                if (auxBuf.getNumChannels() >= 2)
                {
                    auto* auxL = auxBuf.getWritePointer(0);
                    auto* auxR = auxBuf.getWritePointer(1);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        auxL[offset + i] += tmpL[i];
                        auxR[offset + i] += tmpR[i];
                    }
                }
            }
        }

        // ── Sum into main output (+ tap PERC 1 & PERC 2 into Spiral FX send) ──
        const bool toSpiral = (voice == OsbBus::PERC1 || voice == OsbBus::PERC2);
        for (int i = 0; i < blockSize; ++i)
        {
            mainL[offset + i] += tmpL[i];
            mainR[offset + i] += tmpR[i];
            if (toSpiral)
            {
                spiralInL[i] += tmpL[i];
                spiralInR[i] += tmpR[i];
            }
        }
    }

    // ── Master Drive ("Mackie 1202 Redline" 2× oversampled, smoothed #26/#28) ──
    if (driveAmt > 0.001f || smDrive > 0.001f)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            smDrive += paramSmoothAlpha * (driveAmt - smDrive);
            masterDrive.process(mainL[offset + i], mainR[offset + i], smDrive);
        }
    }

    // ── Kick-driven wet ducking envelope for Master Delay, Reverb & Chaos Bus (#15/#27) ──
    float fxDuckBuf[STACK_MAX];
    for (int i = 0; i < blockSize; ++i)
        fxDuckBuf[i] = voiceBank.tickFxDuck();

    // ── Underground Chaos Sample Bus (mixed into master pre-FX, ducked by Kick + tapped to Spiral FX) ──
    if ((chaosOn || chaosBus.isPlaying()) && chaosVol > 0.001f)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            float cL = 0.0f, cR = 0.0f;
            chaosBus.process(cL, cR, chaosGrit, chaosVol, fxDuckBuf[i]);
            analyzer.feedChaosSample(cL, cR);
            mainL[offset + i] += cL;
            mainR[offset + i] += cR;
            spiralInL[i] += cL;
            spiralInR[i] += cR;
        }
    }

    // ── Spiral FX (Jet Flanger + 3/16 Gated Decaying Dub Delay + Distortion for Percs & Samples) ──
    {
        const double chunkPpq = activePpq + (static_cast<double>(offset) * static_cast<double>(activeBpm)) / (60.0 * currentSampleRate);
        spiralFx.process(spiralInL, spiralInR, mainL + offset, mainR + offset, fxDuckBuf,
                         blockSize, activeBpm, chunkPpq, spiralDrive);
    }

    // ── Master Delay (4-pt Hermite Cubic tape delay + authentic Dub Throw send gate + 300 Hz HPF) ──
    const bool  throwActive   = dubThrowUi || autoDubThrow;
    const float baseDelayFb   = tightMode ? (delayFb * 0.75f) : delayFb;
    const bool  delayNeeded   = throwActive || delayMix > 0.001f || smDelayMix > 0.001f
                             || delay.throwSendEnv > 0.001f || delay.tailEnergy > 0.0015f;
    if (delayNeeded)
    {
        auto& dl = delay;
        const float targetDelay = static_cast<float>(delaySamples);
        const float smoothCoeff = 1.0f - std::exp(-1.0f / (0.055f * static_cast<float>(currentSampleRate))); // 55 ms tape glide
        const float throwAlpha  = 1.0f - std::exp(-1.0f / (0.005f * static_cast<float>(currentSampleRate))); // 5 ms send gate
        const float effDelayFb  = (throwActive || dl.tailEnergy > 0.0015f) ? std::max(baseDelayFb, 0.78f) : baseDelayFb;
        const float targetWet   = (throwActive || dl.tailEnergy > 0.0015f) ? std::max(delayMix, 0.58f) : delayMix;

        auto readHermite = [](const std::vector<float>& buf, int wPos, int sz, float del) -> float {
            const int di = static_cast<int>(del);
            const float f = del - static_cast<float>(di);
            const int i0 = (wPos - di + 1 + sz) % sz;
            const int i1 = (wPos - di     + sz) % sz;
            const int i2 = (wPos - di - 1 + sz) % sz;
            const int i3 = (wPos - di - 2 + sz) % sz;
            const float y0 = buf[static_cast<size_t>(i0)];
            const float y1 = buf[static_cast<size_t>(i1)];
            const float y2 = buf[static_cast<size_t>(i2)];
            const float y3 = buf[static_cast<size_t>(i3)];
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + c0;
        };

        float blockTailPeak = 0.0f;
        for (int i = 0; i < blockSize; ++i)
        {
            smDelayMix += paramSmoothAlpha * (targetWet - smDelayMix);
            dl.throwSendEnv += throwAlpha * ((throwActive ? 1.0f : 0.0f) - dl.throwSendEnv);
            dl.smoothedDelay += smoothCoeff * (targetDelay - dl.smoothedDelay);
            const float readDelay = std::max(2.0f, std::min(dl.smoothedDelay,
                                    static_cast<float>(dl.size - 3)));

            const float dL = readHermite(dl.bufL, dl.writePos, dl.size, readDelay);
            const float dR = readHermite(dl.bufR, dl.writePos, dl.size, readDelay);

            // Authentic Dub Console send gate: when DUB THROW is released, input send closes smoothly
            // (if base delayMix == 0) while existing echoes keep decaying freely on the beat!
            const float inSend = (delayMix > 0.001f) ? 1.0f : dl.throwSendEnv;
            const float sendL = mainL[offset + i] * inSend + dL * effDelayFb;
            const float sendR = mainR[offset + i] * inSend + dR * effDelayFb;

            delHp1L = delHpAlpha * (delHp1L + sendL - delHp1PrevL); delHp1PrevL = sendL;
            delHp2L = delHpAlpha * (delHp2L + delHp1L - delHp2PrevL); delHp2PrevL = delHp1L;
            delHp1R = delHpAlpha * (delHp1R + sendR - delHp1PrevR); delHp1PrevR = sendR;
            delHp2R = delHpAlpha * (delHp2R + delHp1R - delHp2PrevR); delHp2PrevR = delHp1R;

            // Gentle 6 kHz analog-style damping inside feedback loop (NaN/Inf protected)
            delLpL += delLpAlpha * (delHp2L - delLpL);
            delLpR += delLpAlpha * (delHp2R - delLpR);
            if (!std::isfinite(delLpL)) delLpL = 0.0f;
            if (!std::isfinite(delLpR)) delLpR = 0.0f;

            dl.bufL[static_cast<size_t>(dl.writePos)] = std::clamp(delLpL, -3.5f, 3.5f);
            dl.bufR[static_cast<size_t>(dl.writePos)] = std::clamp(delLpR, -3.5f, 3.5f);

            // Balanced mono-leaning wet return (mid + 30% side) — rock-solid phase & club-safe
            const float wetMid  = 0.5f * (dL + dR);
            const float wetSide = 0.5f * (dL - dR) * 0.30f;
            const float wetAmt  = smDelayMix * fxDuckBuf[i] * 0.85f;
            mainL[offset + i] += (wetMid + wetSide) * wetAmt;
            mainR[offset + i] += (wetMid - wetSide) * wetAmt;

            blockTailPeak = std::max(blockTailPeak, std::max(std::abs(dL), std::abs(dR)));
            dl.writePos = (dl.writePos + 1) % dl.size;
        }
        dl.tailEnergy = blockTailPeak;
    }

    // ── Master Reverb (Freeverb plate, wet HPF @ 250 Hz + kick ducked #27/#28) ──
    if (reverbMix > 0.001f || smReverbMix > 0.001f)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            smReverbMix += paramSmoothAlpha * (reverbMix - smReverbMix);
            const float wetGain = std::sqrt(std::clamp(smReverbMix, 0.0f, 1.0f)) * fxDuckBuf[i];
            const float dryGain = std::sqrt(1.0f - std::clamp(smReverbMix, 0.0f, 1.0f));
            float wL = 0.0f, wR = 0.0f;
            plateReverb.process(mainL[offset + i], mainR[offset + i], wL, wR);
            revWetHpL = fxWetHpAlpha * (revWetHpL + wL - revWetPrevL); revWetPrevL = wL;
            revWetHpR = fxWetHpAlpha * (revWetHpR + wR - revWetPrevR); revWetPrevR = wR;
            mainL[offset + i] = mainL[offset + i] * dryGain + revWetHpL * wetGain;
            mainR[offset + i] = mainR[offset + i] * dryGain + revWetHpR * wetGain;
        }
    }

    // ── Master 8 Hz DC Blocker (removes any accumulated DC bias before Glue/Phase/Limiter) ──
    for (int i = 0; i < blockSize; ++i)
    {
        const float xL = mainL[offset + i];
        const float xR = mainR[offset + i];
        masterDcOutL = (xL - masterDcPrevL) + masterDcAlpha * masterDcOutL;
        masterDcPrevL = xL;
        masterDcOutR = (xR - masterDcPrevR) + masterDcAlpha * masterDcOutR;
        masterDcPrevR = xR;
        mainL[offset + i] = masterDcOutL;
        mainR[offset + i] = masterDcOutR;
    }

    // ── Master Glue Compressor ──
    if (compAmt > 0.001f || smCompAmt > 0.001f)
    {
        smCompAmt += paramSmoothAlpha * static_cast<float>(blockSize) * (compAmt - smCompAmt);
        glueComp.setAmount(std::clamp(smCompAmt, 0.0f, 1.0f));
        for (int i = 0; i < blockSize; ++i)
            glueComp.process(mainL[offset + i], mainR[offset + i]);
        analyzer.setCompGrDb(glueComp.getGainReductionDb());
    }
    else
    {
        analyzer.setCompGrDb(0.0f);
    }

    // ── Master Linkwitz-Riley LR4 (24 dB/oct) Elliptical M/S Mono-Maker (< 120 Hz Side HPF) ──
    if (monoSubOn)
    {
        auto runBiquad = [&](float x, BiquadState& st) -> float {
            const float y = lr4B0 * x + st.z1;
            st.z1 = lr4B1 * x - lr4A1 * y + st.z2;
            st.z2 = lr4B2 * x - lr4A2 * y;
            return y;
        };
        for (int i = 0; i < blockSize; ++i)
        {
            const float mid  = 0.5f * (mainL[offset + i] + mainR[offset + i]);
            const float side = 0.5f * (mainL[offset + i] - mainR[offset + i]);
            const float sideLr4 = runBiquad(runBiquad(side, sideLr4A), sideLr4B);
            mainL[offset + i] = mid + sideLr4;
            mainR[offset + i] = mid - sideLr4;
        }
    }

    // ── TIGHT low-cut HP (pre-limiter) ──
    if (tightMode)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float xL = mainL[offset + i];
            const float xR = mainR[offset + i];
            tightHpL = tightHpAlpha * (tightHpL + xL - tightHpPrevL);
            tightHpR = tightHpAlpha * (tightHpR + xR - tightHpPrevR);
            tightHpPrevL = xL;
            tightHpPrevR = xR;
            mainL[offset + i] = tightHpL * 0.85f + xL * 0.15f;
            mainR[offset + i] = tightHpR * 0.85f + xR * 0.15f;
        }
    }

    // ── Master Bipolar DJ Isolator ("WASH") + Momentary KILL SUB (< 150 Hz) ──
    const float djTarget = apvts.getRawParameterValue(ParamId::DJ_FILTER)->load();
    const bool  killSub  = killSubHeld.load(std::memory_order_relaxed);
    if (std::abs(djTarget) > 0.012f || std::abs(smDjFilter) > 0.012f)
    {
        const float srF = static_cast<float>(std::max(8000.0, currentSampleRate));
        for (int i = 0; i < blockSize; ++i)
        {
            smDjFilter += paramSmoothAlpha * (djTarget - smDjFilter);
            const float amt = std::abs(smDjFilter);
            if (amt > 0.008f)
            {
                const bool isHp = (smDjFilter > 0.0f);
                // Exponential musical sweep: LP 18kHz → 160Hz | HP 35Hz → 7.5kHz
                const float fc = isHp
                    ? (35.0f * std::pow(215.0f, amt))
                    : (18000.0f * std::pow(160.0f / 18000.0f, amt));
                const float g = std::tan(Dsp::PI_F * std::clamp(fc, 25.0f, srF * 0.44f) / srF);
                const float k = 1.15f - 0.55f * amt; // subtle resonant DJ peak as you sweep
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;

                auto procSvf = [&](float x, DjSvf& st) {
                    const float v3 = x - st.ic2;
                    const float v1 = a1 * st.ic1 + a2 * v3;
                    const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
                    st.ic1 = 2.0f * v1 - st.ic1;
                    st.ic2 = 2.0f * v2 - st.ic2;
                    return isHp ? (x - k * v1 - v2) : v2;
                };
                const float wetL = procSvf(mainL[offset + i], djSvfL);
                const float wetR = procSvf(mainR[offset + i], djSvfR);
                const float mix = std::min(1.0f, amt * 12.0f); // smooth fade-in around center detent
                mainL[offset + i] = mainL[offset + i] * (1.0f - mix) + wetL * mix;
                mainR[offset + i] = mainR[offset + i] * (1.0f - mix) + wetR * mix;
            }
        }
    }
    if (killSub || smKillSub > 0.0005f)
    {
        const float targetKill = killSub ? 1.0f : 0.0f;
        for (int i = 0; i < blockSize; ++i)
        {
            smKillSub += killFadeAlpha * (targetKill - smKillSub);
            const float xL = mainL[offset + i];
            const float xR = mainR[offset + i];
            killHp1L = killHpAlpha * (killHp1L + xL - killHp1PrevL); killHp1PrevL = xL;
            killHp2L = killHpAlpha * (killHp2L + killHp1L - killHp2PrevL); killHp2PrevL = killHp1L;
            killHp1R = killHpAlpha * (killHp1R + xR - killHp1PrevR); killHp1PrevR = xR;
            killHp2R = killHpAlpha * (killHp2R + killHp1R - killHp2PrevR); killHp2PrevR = killHp1R;
            mainL[offset + i] = xL * (1.0f - smKillSub) + killHp2L * smKillSub;
            mainR[offset + i] = xR * (1.0f - smKillSub) + killHp2R * smKillSub;
        }
    }

    // ── Master output fader + peak-following limiter + NaN/Inf sanitization + analyzer tap ──
    const float masterVolTarget = apvts.getRawParameterValue(ParamId::MASTER_VOL)->load();
    bool clippedThisBlock = false;
    for (int i = 0; i < blockSize; ++i)
    {
        smMasterVol += paramSmoothAlpha * (masterVolTarget - smMasterVol);
        float& sL = mainL[offset + i];
        float& sR = mainR[offset + i];
        sL *= smMasterVol;
        sR *= smMasterVol;
        if (!std::isfinite(sL)) sL = 0.0f;
        if (!std::isfinite(sR)) sR = 0.0f;
        if (std::abs(sL) > 0.90f || std::abs(sR) > 0.90f)
            clippedThisBlock = true;
        masterLimiter.process(sL, sR);
        sL = std::isfinite(sL) ? std::clamp(sL, -1.0f, 1.0f) : 0.0f;
        sR = std::isfinite(sR) ? std::clamp(sR, -1.0f, 1.0f) : 0.0f;
        analyzer.process(sL, sR);
    }
    masterClipping.store(clippedThisBlock || masterLimiter.getGainReductionDb() > 0.5f, std::memory_order_relaxed);

    offset += blockSize;
    } // end offset loop

    // Dispatch any remaining events scheduled at the exact buffer end
    while (nextEventIdx < subBlockEvents.count)
    {
        TransportSync::dispatchEvent(voiceBank, subBlockEvents.events[static_cast<size_t>(nextEventIdx)]);
        ++nextEventIdx;
    }

    analyzer.publish();  // commit meter/spectrum snapshots once per buffer
}

juce::AudioProcessorEditor* OldSchoolBoxProcessor::createEditor()
{
    return new OldSchoolBoxEditor(*this);
}

void OldSchoolBoxProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto seqNode = juce::ValueTree("SEQ");

    for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
    {
        // Pattern (uint64_t saved as int64)
        seqNode.setProperty("p" + juce::String(ch),
                            static_cast<juce::int64>(sequencer.getPattern(ch)), nullptr);

        // Pattern length
        seqNode.setProperty("l" + juce::String(ch),
                            sequencer.getPatternLength(ch), nullptr);

        // Velocities as a 64-byte base64 blob
        juce::MemoryBlock mb(static_cast<size_t>(Sequencer::MAX_STEPS));
        for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
            static_cast<uint8_t*>(mb.getData())[s] = sequencer.getVelocity(ch, s);
        seqNode.setProperty("vel" + juce::String(ch), mb.toBase64Encoding(), nullptr);

        // Probability (uint8) per step
        juce::MemoryBlock pb(static_cast<size_t>(Sequencer::MAX_STEPS));
        for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
            static_cast<uint8_t*>(pb.getData())[s] = sequencer.getProbability(ch, s);
        seqNode.setProperty("prob" + juce::String(ch), pb.toBase64Encoding(), nullptr);

        // Micro-timing (int8) per step
        juce::MemoryBlock tb(static_cast<size_t>(Sequencer::MAX_STEPS));
        for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
            static_cast<int8_t*>(tb.getData())[s] = sequencer.getMicroTiming(ch, s);
        seqNode.setProperty("tim" + juce::String(ch), tb.toBase64Encoding(), nullptr);

        // Auto-variation flag
        seqNode.setProperty("av" + juce::String(ch),
                            sequencer.autoVar[idx(ch)].load(std::memory_order_acquire) ? 1 : 0, nullptr);

        // Humanize flag
        seqNode.setProperty("hm" + juce::String(ch),
                            sequencer.humanize[idx(ch)].load(std::memory_order_acquire) ? 1 : 0, nullptr);

        // Mute
        seqNode.setProperty("mt" + juce::String(ch),
                            sequencer.mute[idx(ch)].load(std::memory_order_relaxed) ? 1 : 0, nullptr);

        // Solo
        seqNode.setProperty("sl" + juce::String(ch),
                            sequencer.solo[idx(ch)].load(std::memory_order_relaxed) ? 1 : 0, nullptr);
    }

    // 303 per-step attributes
    for (int bi = 0; bi < 2; ++bi)
    {
        seqNode.setProperty("sld" + juce::String(bi), static_cast<juce::int64>(sequencer.getBassSlidePattern(bi)), nullptr);
        seqNode.setProperty("acc" + juce::String(bi), static_cast<juce::int64>(sequencer.getBassAccentPattern(bi)), nullptr);
        seqNode.setProperty("oup" + juce::String(bi), static_cast<juce::int64>(sequencer.getBassOctUpPattern(bi)), nullptr);
        seqNode.setProperty("odn" + juce::String(bi), static_cast<juce::int64>(sequencer.getBassOctDownPattern(bi)), nullptr);

        juce::MemoryBlock nb(static_cast<size_t>(Sequencer::MAX_STEPS));
        for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
            static_cast<int8_t*>(nb.getData())[s] = sequencer.getBassNoteOffset(bi, s);
        seqNode.setProperty("bno" + juce::String(bi), nb.toBase64Encoding(), nullptr);
    }

    // Scenes — structured, versioned ValueTree nodes (ABI-safe across struct changes)
    for (int i = 0; i < Scenes::COUNT; ++i)
    {
        const auto& sc = scenes[idx(i)];
        juce::ValueTree scNode("SCENE");
        scNode.setProperty("version", 1, nullptr);
        scNode.setProperty("index", i, nullptr);

        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            scNode.setProperty("p"  + juce::String(ch), static_cast<juce::int64>(sc.patterns[ch]), nullptr);
            scNode.setProperty("l"  + juce::String(ch), static_cast<int>(sc.patternLengths[ch]), nullptr);
            scNode.setProperty("hm" + juce::String(ch), static_cast<int>(sc.humanize[ch]), nullptr);

            juce::MemoryBlock vb(sc.velocities[ch], static_cast<size_t>(Sequencer::MAX_STEPS));
            scNode.setProperty("vel" + juce::String(ch), vb.toBase64Encoding(), nullptr);

            juce::MemoryBlock pb(sc.probabilities[ch], static_cast<size_t>(Sequencer::MAX_STEPS));
            scNode.setProperty("prob" + juce::String(ch), pb.toBase64Encoding(), nullptr);

            juce::MemoryBlock tb(sc.microTiming[ch], static_cast<size_t>(Sequencer::MAX_STEPS));
            scNode.setProperty("tim" + juce::String(ch), tb.toBase64Encoding(), nullptr);
        }

        for (int bi = 0; bi < 2; ++bi)
        {
            scNode.setProperty("sld" + juce::String(bi), static_cast<juce::int64>(sc.bassSlide[bi]), nullptr);
            scNode.setProperty("acc" + juce::String(bi), static_cast<juce::int64>(sc.bassAccent[bi]), nullptr);
            scNode.setProperty("oup" + juce::String(bi), static_cast<juce::int64>(sc.bassOctUp[bi]), nullptr);
            scNode.setProperty("odn" + juce::String(bi), static_cast<juce::int64>(sc.bassOctDown[bi]), nullptr);

            juce::MemoryBlock nb(sc.bassNoteOffsets[bi], static_cast<size_t>(Sequencer::MAX_STEPS));
            scNode.setProperty("bno" + juce::String(bi), nb.toBase64Encoding(), nullptr);
        }

        seqNode.addChild(scNode, -1, nullptr);
    }
    seqNode.setProperty("activeScene",
                        sequencer.activeScene.load(std::memory_order_acquire), nullptr);

    state.addChild(seqNode, -1, nullptr);
    copyXmlToBinary(*state.createXml(), destData);
}

void OldSchoolBoxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(tree);

    if (auto seq = tree.getChildWithName("SEQ"); seq.isValid())
    {
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            sequencer.setPattern(ch, static_cast<uint64_t>(
                static_cast<juce::int64>(seq.getProperty("p" + juce::String(ch), 0))));

            sequencer.setPatternLength(ch,
                static_cast<int>(seq.getProperty("l" + juce::String(ch), 16)));

            auto velStr = seq.getProperty("vel" + juce::String(ch)).toString();
            if (velStr.isNotEmpty())
            {
                juce::MemoryBlock mb;
                mb.fromBase64Encoding(velStr);
                if (static_cast<int>(mb.getSize()) >= Sequencer::MAX_STEPS)
                    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                        sequencer.setVelocity(ch, s,
                            static_cast<const uint8_t*>(mb.getData())[s]);
            }

            auto probStr = seq.getProperty("prob" + juce::String(ch)).toString();
            if (probStr.isNotEmpty())
            {
                juce::MemoryBlock pb;
                pb.fromBase64Encoding(probStr);
                if (static_cast<int>(pb.getSize()) >= Sequencer::MAX_STEPS)
                    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                        sequencer.setProbability(ch, s,
                            static_cast<const uint8_t*>(pb.getData())[s]);
            }

            auto timStr = seq.getProperty("tim" + juce::String(ch)).toString();
            if (timStr.isNotEmpty())
            {
                juce::MemoryBlock tb;
                tb.fromBase64Encoding(timStr);
                if (static_cast<int>(tb.getSize()) >= Sequencer::MAX_STEPS)
                    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                        sequencer.setMicroTiming(ch, s,
                            static_cast<const int8_t*>(tb.getData())[s]);
            }

            // Auto-variation flag
            sequencer.autoVar[idx(ch)].store(
                static_cast<int>(seq.getProperty("av" + juce::String(ch), 0)) != 0,
                std::memory_order_release);

            // Humanize flag
            sequencer.humanize[idx(ch)].store(
                static_cast<int>(seq.getProperty("hm" + juce::String(ch), 0)) != 0,
                std::memory_order_release);

            // Mute
            sequencer.mute[idx(ch)].store(
                static_cast<int>(seq.getProperty("mt" + juce::String(ch), 0)) != 0,
                std::memory_order_release);

            // Solo
            sequencer.solo[idx(ch)].store(
                static_cast<int>(seq.getProperty("sl" + juce::String(ch), 0)) != 0,
                std::memory_order_release);
        }

        // 303 per-step attributes
        for (int bi = 0; bi < 2; ++bi)
        {
            sequencer.setBassSlidePattern(bi, static_cast<uint64_t>(static_cast<juce::int64>(seq.getProperty("sld" + juce::String(bi), 0))));
            sequencer.setBassAccentPattern(bi, static_cast<uint64_t>(static_cast<juce::int64>(seq.getProperty("acc" + juce::String(bi), 0))));
            sequencer.setBassOctUpPattern(bi, static_cast<uint64_t>(static_cast<juce::int64>(seq.getProperty("oup" + juce::String(bi), 0))));
            sequencer.setBassOctDownPattern(bi, static_cast<uint64_t>(static_cast<juce::int64>(seq.getProperty("odn" + juce::String(bi), 0))));

            auto bnoStr = seq.getProperty("bno" + juce::String(bi)).toString();
            if (bnoStr.isNotEmpty())
            {
                juce::MemoryBlock nb;
                nb.fromBase64Encoding(bnoStr);
                if (static_cast<int>(nb.getSize()) >= Sequencer::MAX_STEPS)
                    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                        sequencer.setBassNoteOffset(bi, s,
                            static_cast<const int8_t*>(nb.getData())[s]);
            }
        }

        // Scenes — prefer structured versioned SCENE children; fall back to legacy raw blobs
        bool anySceneLoaded = false;
        for (int c = 0; c < seq.getNumChildren(); ++c)
        {
            const auto scNode = seq.getChild(c);
            if (!scNode.hasType("SCENE")) continue;
            const int si = static_cast<int>(scNode.getProperty("index", -1));
            if (si < 0 || si >= Scenes::COUNT) continue;

            Scenes::Snapshot sc{};
            for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
            {
                sc.patterns[ch]       = static_cast<uint64_t>(static_cast<juce::int64>(scNode.getProperty("p" + juce::String(ch), 0)));
                sc.patternLengths[ch] = static_cast<uint8_t>(std::clamp(static_cast<int>(scNode.getProperty("l" + juce::String(ch), 16)), 16, 64));
                sc.humanize[ch]       = static_cast<int>(scNode.getProperty("hm" + juce::String(ch), 0)) != 0 ? 1 : 0;

                for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
                {
                    sc.velocities[ch][s]    = 100;
                    sc.probabilities[ch][s] = 100;
                    sc.microTiming[ch][s]   = 0;
                }

                auto loadBytes = [&](const juce::String& prop, void* dst) {
                    const auto str = scNode.getProperty(prop).toString();
                    if (str.isNotEmpty())
                    {
                        juce::MemoryBlock mb;
                        mb.fromBase64Encoding(str);
                        if (static_cast<int>(mb.getSize()) >= Sequencer::MAX_STEPS)
                            std::memcpy(dst, mb.getData(), static_cast<size_t>(Sequencer::MAX_STEPS));
                    }
                };
                loadBytes("vel"  + juce::String(ch), sc.velocities[ch]);
                loadBytes("prob" + juce::String(ch), sc.probabilities[ch]);
                loadBytes("tim"  + juce::String(ch), sc.microTiming[ch]);
            }
            for (int bi = 0; bi < 2; ++bi)
            {
                sc.bassSlide[bi]   = static_cast<uint64_t>(static_cast<juce::int64>(scNode.getProperty("sld" + juce::String(bi), 0)));
                sc.bassAccent[bi]  = static_cast<uint64_t>(static_cast<juce::int64>(scNode.getProperty("acc" + juce::String(bi), 0)));
                sc.bassOctUp[bi]   = static_cast<uint64_t>(static_cast<juce::int64>(scNode.getProperty("oup" + juce::String(bi), 0)));
                sc.bassOctDown[bi] = static_cast<uint64_t>(static_cast<juce::int64>(scNode.getProperty("odn" + juce::String(bi), 0)));
                const auto bnoStr = scNode.getProperty("bno" + juce::String(bi)).toString();
                if (bnoStr.isNotEmpty())
                {
                    juce::MemoryBlock nb;
                    nb.fromBase64Encoding(bnoStr);
                    if (static_cast<int>(nb.getSize()) >= Sequencer::MAX_STEPS)
                        std::memcpy(sc.bassNoteOffsets[bi], nb.getData(), static_cast<size_t>(Sequencer::MAX_STEPS));
                }
            }
            scenes[idx(si)] = sc;
            anySceneLoaded = true;
        }
        if (!anySceneLoaded)
        {
            for (int i = 0; i < Scenes::COUNT; ++i)
            {
                auto sceneStr = seq.getProperty("scene" + juce::String(i)).toString();
                if (sceneStr.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    mb.fromBase64Encoding(sceneStr);
                    if (mb.getSize() == sizeof(Scenes::Snapshot))
                    {
                        std::memcpy(&scenes[idx(i)], mb.getData(), sizeof(Scenes::Snapshot));
                        anySceneLoaded = true;
                    }
                }
            }
        }
        if (!anySceneLoaded)
        {
            // Legacy state without scenes — snapshot the loaded sequencer into all 4
            for (auto& sc : scenes) Scenes::capture(sc, sequencer);
        }
        sequencer.activeScene.store(
            std::clamp(static_cast<int>(seq.getProperty("activeScene", 0)), 0, Scenes::COUNT - 1),
            std::memory_order_release);
        sequencer.queuedScene.store(-1, std::memory_order_release);
        sequencer.fillStartBar.store(-1, std::memory_order_release);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OldSchoolBoxProcessor();
}
