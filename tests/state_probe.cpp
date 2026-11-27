#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/FactoryPresets.h"
#include "core/LoopExporter.h"
#include "core/ParamIds.h"

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
        std::abort(); \
    } \
} while (0)

static void testBassNoteOffsetsAndPersistence()
{
    OldSchoolBoxProcessor procA;

    // Verify clamp boundaries (-24..+24)
    procA.sequencer.setBassNoteOffset(0, 0, -50);
    CHECK(procA.sequencer.getBassNoteOffset(0, 0) == -24);
    procA.sequencer.setBassNoteOffset(0, 1, 50);
    CHECK(procA.sequencer.getBassNoteOffset(0, 1) == 24);

    // Write custom melody on both bass channels across 64 steps
    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
    {
        procA.sequencer.setBassNoteOffset(0, s, static_cast<int8_t>((s % 25) - 12));
        procA.sequencer.setBassNoteOffset(1, s, static_cast<int8_t>(12 - (s % 25)));
    }

    // Serialize state
    juce::MemoryBlock stateBlob;
    procA.getStateInformation(stateBlob);
    CHECK(stateBlob.getSize() > 0);

    // Restore into a fresh processor instance
    OldSchoolBoxProcessor procB;
    procB.setStateInformation(stateBlob.getData(), static_cast<int>(stateBlob.getSize()));

    for (int s = 0; s < Sequencer::MAX_STEPS; ++s)
    {
        const int8_t exp0 = static_cast<int8_t>((s % 25) - 12);
        const int8_t exp1 = static_cast<int8_t>(12 - (s % 25));
        CHECK(procB.sequencer.getBassNoteOffset(0, s) == exp0);
        CHECK(procB.sequencer.getBassNoteOffset(1, s) == exp1);
    }

    std::printf("PASS: bassNoteOffsets clamp + state round-trip (64 steps x 2 bass)\n");
}

static void testAuditionAndInternalClock()
{
    OldSchoolBoxProcessor proc;
    proc.prepareToPlay(44100.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;

    // 1. Audition trigger while transport is stopped must produce audio
    proc.voiceBank.requestTrigger(OsbBus::KICK);
    buf.clear();
    proc.processBlock(buf, midi);
    float peakAudition = buf.getMagnitude(0, 256);
    CHECK(peakAudition > 0.01f);

    // 2. Internal clock without DAW AudioPlayHead must advance steps and produce audio
    proc.currentBpm.store(120.0f, std::memory_order_relaxed);
    proc.internalPlay.store(true, std::memory_order_release);

    float maxPeak = 0.0f;
    int maxStepSeen = -1;
    // Run ~0.6s (about 5 sixteenth-notes at 120 BPM)
    for (int b = 0; b < 100; ++b)
    {
        buf.clear();
        midi.clear();
        proc.processBlock(buf, midi);
        maxPeak = std::max(maxPeak, buf.getMagnitude(0, 256));
        maxStepSeen = std::max(maxStepSeen, proc.sequencer.currentSteps[0].load(std::memory_order_acquire));
    }
    CHECK(proc.sequencer.isPlaying.load(std::memory_order_acquire) == true);
    CHECK(maxStepSeen >= 3);
    CHECK(maxPeak > 0.05f);

    // 3. Stopping internal clock must reset sequencer playhead
    proc.internalPlay.store(false, std::memory_order_release);
    buf.clear();
    proc.processBlock(buf, midi);
    CHECK(proc.sequencer.isPlaying.load(std::memory_order_acquire) == false);
    CHECK(proc.sequencer.currentSteps[0].load(std::memory_order_acquire) == -1);

    std::printf("PASS: audition (peak=%.3f) + internal clock (maxStep=%d, peak=%.3f)\n",
                peakAudition, maxStepSeen, maxPeak);
}

static void testSequencerUndoRedo()
{
    OldSchoolBoxProcessor proc;
    const uint64_t origKick = proc.sequencer.getPattern(OsbBus::KICK);
    const int8_t   origNote = proc.sequencer.getBassNoteOffset(1, 2);

    CHECK(!proc.sequencerUndo.canUndo());
    CHECK(!proc.sequencerUndo.canRedo());

    // Edit 1: clear kick + change bass2 step 2 note
    proc.sequencerUndo.push(proc.sequencer);
    proc.sequencer.clearChannel(OsbBus::KICK);
    proc.sequencer.setBassNoteOffset(1, 2, 11);
    CHECK(proc.sequencer.getPattern(OsbBus::KICK) == 0);
    CHECK(proc.sequencer.getBassNoteOffset(1, 2) == 11);
    CHECK(proc.sequencerUndo.canUndo());

    // Undo restores both
    CHECK(proc.sequencerUndo.undo(proc.sequencer));
    CHECK(proc.sequencer.getPattern(OsbBus::KICK) == origKick);
    CHECK(proc.sequencer.getBassNoteOffset(1, 2) == origNote);
    CHECK(proc.sequencerUndo.canRedo());

    // Redo reapplies both
    CHECK(proc.sequencerUndo.redo(proc.sequencer));
    CHECK(proc.sequencer.getPattern(OsbBus::KICK) == 0);
    CHECK(proc.sequencer.getBassNoteOffset(1, 2) == 11);

    // New edit clears redo stack
    proc.sequencerUndo.push(proc.sequencer);
    proc.sequencer.setPattern(OsbBus::KICK, 0xAAAA);
    CHECK(!proc.sequencerUndo.canRedo());

    std::printf("PASS: sequencer undo/redo (pattern + bass note)\n");
}

static void testResizableEditor()
{
    OldSchoolBoxProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    CHECK(ed != nullptr);
    CHECK(ed->isResizable());
    const int initW = ed->getWidth();
    const int initH = ed->getHeight();
    CHECK(initW > 0 && initH > 0);
    ed->setSize(860, 560);
    CHECK(ed->getWidth() == 860 && ed->getHeight() == 560);
    std::printf("PASS: resizable editor (init=%dx%d, resized=860x560)\n", initW, initH);
}

static void testUiStateSyncOnReload()
{
    OldSchoolBoxProcessor procA;
    procA.sequencer.mute[0].store(true, std::memory_order_release);
    procA.sequencer.solo[2].store(true, std::memory_order_release);
    procA.sequencer.autoVar[1].store(true, std::memory_order_release);
    procA.sequencer.humanize[3].store(true, std::memory_order_release);
    procA.sequencer.setPatternLength(4, 64);

    juce::MemoryBlock blob;
    procA.getStateInformation(blob);

    OldSchoolBoxProcessor procB;
    procB.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

    // Verify processor state restored
    CHECK(procB.sequencer.mute[0].load(std::memory_order_acquire) == true);
    CHECK(procB.sequencer.solo[2].load(std::memory_order_acquire) == true);
    CHECK(procB.sequencer.autoVar[1].load(std::memory_order_acquire) == true);
    CHECK(procB.sequencer.humanize[3].load(std::memory_order_acquire) == true);
    CHECK(procB.sequencer.getPatternLength(4) == 64);

    // Construct editor and verify toggle buttons match restored state immediately
    std::unique_ptr<juce::AudioProcessorEditor> ed(procB.createEditor());
    CHECK(ed != nullptr);
    int toggledM = 0, toggledS = 0, toggledV = 0, toggledH = 0, found4B = 0;
    std::function<void(juce::Component*)> walk = [&](juce::Component* c) {
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
        {
            if (b->getButtonText() == "M"   && b->getToggleState()) ++toggledM;
            if (b->getButtonText() == "S"   && b->getToggleState()) ++toggledS;
            if (b->getButtonText() == "VAR" && b->getToggleState()) ++toggledV;
            if (b->getButtonText() == "H"   && b->getToggleState()) ++toggledH;
            if (b->getButtonText() == "4B") ++found4B;
        }
        for (int i = 0; i < c->getNumChildComponents(); ++i)
            walk(c->getChildComponent(i));
    };
    walk(ed.get());
    CHECK(toggledM == 1);
    CHECK(toggledS == 1);
    CHECK(toggledV == 1);
    CHECK(toggledH == 1);
    CHECK(found4B == 1);

    std::printf("PASS: UI toggle sync on reload (M=%d S=%d VAR=%d H=%d 4B=%d)\n",
                toggledM, toggledS, toggledV, toggledH, found4B);
}

static void testFactoryPresetsAndVersionedScenes()
{
    OldSchoolBoxProcessor procA;
    const auto& presets = FactoryPresets::all();
    CHECK(presets.size() == 5);

    // Apply Acid House preset (index 1) and verify both pattern AND synth params change
    FactoryPresets::applyParams(presets[1], procA.apvts);
    Scenes::apply(presets[1].snapshot, procA.sequencer);
    const float acidReso = procA.apvts.getRawParameterValue(ParamId::BASS2_RESO)->load();
    const float acidSwing = procA.apvts.getRawParameterValue(ParamId::SWING)->load();
    CHECK(std::abs(acidReso - 0.86f) < 0.02f);
    CHECK(std::abs(acidSwing - 0.20f) < 0.02f);

    // Apply UK Garage preset (index 2) and verify swing + drive reset cleanly
    FactoryPresets::applyParams(presets[2], procA.apvts);
    const float ukgSwing = procA.apvts.getRawParameterValue(ParamId::SWING)->load();
    const float ukgDrive = procA.apvts.getRawParameterValue(ParamId::MASTER_DRIVE)->load();
    CHECK(std::abs(ukgSwing - 0.55f) < 0.02f);
    CHECK(ukgDrive < 0.01f); // reset from Acid House's 0.22

    // Save distinct scene data in slot 2 and verify versioned ValueTree round-trip
    procA.sequencer.setPattern(OsbBus::KICK, 0xCAFEBABE12345678ULL);
    procA.sequencer.setBassNoteOffset(1, 7, -9);
    Scenes::capture(procA.scenes[2], procA.sequencer);

    juce::MemoryBlock blob;
    procA.getStateInformation(blob);

    // Verify serialized XML uses structured SCENE nodes (no raw scene0 memcpy blob)
    auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(xml != nullptr);
    auto* seqXml = xml->getChildByName("SEQ");
    CHECK(seqXml != nullptr);
    CHECK(!seqXml->hasAttribute("scene0"));
    CHECK(seqXml->getChildByName("SCENE") != nullptr);

    OldSchoolBoxProcessor procB;
    procB.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(procB.scenes[2].patterns[OsbBus::KICK] == 0xCAFEBABE12345678ULL);
    CHECK(procB.scenes[2].bassNoteOffsets[1][7] == -9);

    std::printf("PASS: factory presets (sound+pattern) + versioned XML scenes\n");
}

static void testMultiOutAndSampleAccurateMidi()
{
    // 1. Multi-out buses: Main + 8 aux stereo buses
    OldSchoolBoxProcessor proc;
    CHECK(proc.getBusCount(false) == 1 + OsbBus::COUNT);
    CHECK(proc.enableAllBuses());
    proc.prepareToPlay(44100.0, 256);

    const int totalCh = proc.getTotalNumOutputChannels();
    CHECK(totalCh == (1 + OsbBus::COUNT) * 2);

    juce::AudioBuffer<float> buf(totalCh, 256);
    juce::MidiBuffer midi;

    proc.voiceBank.requestTrigger(OsbBus::KICK);
    proc.processBlock(buf, midi);

    const float mainPeak  = proc.getBusBuffer(buf, false, 0).getMagnitude(0, 256);
    const float kickPeak  = proc.getBusBuffer(buf, false, 1 + OsbBus::KICK).getMagnitude(0, 256);
    const float snarePeak = proc.getBusBuffer(buf, false, 1 + OsbBus::SNARE).getMagnitude(0, 256);
    CHECK(mainPeak > 0.05f);
    CHECK(kickPeak > 0.05f);
    CHECK(snarePeak < 1.0e-6f);

    // 2. Sample-accurate MIDI Out: 120 BPM @ 44100 Hz => 16th note = 5512.5 samples;
    //    4/4 kick (every 4 steps) = 22050.0 samples. With blockSize=2048, kick #2 must
    //    land at sample 22050 % 2048 = 1570 (non-zero offset!), NOT 0.
    OldSchoolBoxProcessor procMidi;
    procMidi.prepareToPlay(44100.0, 2048);
    if (auto* p = procMidi.apvts.getParameter(ParamId::MIDI_OUT))
        p->setValueNotifyingHost(1.0f);
    for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        procMidi.sequencer.clearChannel(ch);
    procMidi.sequencer.setPattern(OsbBus::KICK, 0x1111); // steps 0, 4, 8, 12
    procMidi.currentBpm.store(120.0f, std::memory_order_relaxed);
    procMidi.internalPlay.store(true, std::memory_order_release);

    juce::AudioBuffer<float> mbuf(2, 2048);
    std::vector<int64_t> kickNoteOnAbsSamples;
    bool sawNonZeroOffset = false;

    for (int b = 0; b < 30; ++b)
    {
        mbuf.clear();
        midi.clear();
        procMidi.processBlock(mbuf, midi);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getNoteNumber() == MidiMap::KICK)
            {
                if (meta.samplePosition > 0) sawNonZeroOffset = true;
                kickNoteOnAbsSamples.push_back(static_cast<int64_t>(b) * 2048 + meta.samplePosition);
            }
        }
    }

    CHECK(kickNoteOnAbsSamples.size() >= 3);
    CHECK(sawNonZeroOffset);
    const int64_t delta01 = kickNoteOnAbsSamples[1] - kickNoteOnAbsSamples[0];
    const int64_t delta12 = kickNoteOnAbsSamples[2] - kickNoteOnAbsSamples[1];
    CHECK(std::abs(delta01 - 22050) <= 2);
    CHECK(std::abs(delta12 - 22050) <= 2);

    std::printf("PASS: multi-out (main=%.3f kick=%.3f snare=%.3f) + sample-accurate MIDI (deltas=%lld,%lld)\n",
                mainPeak, kickPeak, snarePeak, (long long)delta01, (long long)delta12);
}

static void testSprint1And3Dsp()
{
    // 1. Anti-click 1 ms SidechainDucker attack
    SidechainDucker sc;
    sc.prepare(44100.0, 0.15f);
    sc.trigger(0.80f);
    const float g0 = sc.tick();
    CHECK(g0 > 0.90f && g0 < 1.0f); // smooth attack, does NOT jump instantly to 0.20
    float minGain = g0;
    for (int i = 0; i < 120; ++i) minGain = std::min(minGain, sc.tick());
    CHECK(minGain < 0.30f);         // reaches deep duck within ~2ms

    // 2. True-Peak Limiter -0.3 dBTP ceiling (0.966)
    MasterLimiter lim;
    lim.prepare(44100.0);
    lim.setThreshold(0.90f);
    lim.setCeiling(0.966f);
    float maxLim = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        float L = 3.0f * std::sin(0.05f * static_cast<float>(i));
        float R = -3.0f * std::sin(0.05f * static_cast<float>(i));
        lim.process(L, R);
        maxLim = std::max(maxLim, std::max(std::abs(L), std::abs(R)));
    }
    CHECK(maxLim <= 0.967f);

    // 3. AutoMode 64-bar micro-arrangement: bar 15 drops kick steps 12..15
    OldSchoolBoxProcessor proc;
    proc.sequencer.setPattern(0, 0xFFFFULL);
    Scenes::Snapshot snap;
    Scenes::capture(snap, proc.sequencer);
    proc.autoMode.setAnchor(snap);
    uint32_t rng = 0x12345678u;
    proc.autoMode.evolve(proc.sequencer, 0.0f, 1.0f, 0.0f, rng, 15);
    CHECK((proc.sequencer.getPattern(0) & 0xF000ULL) == 0ULL);

    std::printf("PASS: Sprint 1+3 DSP (sc_g0=%.3f sc_min=%.3f lim_max=%.4f auto_bar15_drop=ok)\n",
                g0, minGain, maxLim);
}

static void testChaosSampleBus()
{
    ChaosSampleBus cb;
    cb.prepare(44100.0);

    uint32_t rng = 0xCAFE1234u;
    bool dubThrow = false;
    // Trigger via FILL stutter (guaranteed trigger) on bank 0 (WHISTLE_98)
    cb.onStep(12, 3, true, 0, 1.0f, 0.0f, 0.5f, rng, dubThrow);
    CHECK(dubThrow == true);

    float peak = 0.0f;
    for (int i = 0; i < 1024; ++i)
    {
        float L = 0.0f, R = 0.0f;
        cb.process(L, R, 0.5f, 0.8f, 1.0f);
        peak = std::max(peak, std::max(std::abs(L), std::abs(R)));
    }
    CHECK(peak > 0.02f);
    CHECK(cb.getPlayheadNorm() >= 0.0f);

    std::printf("PASS: ChaosSampleBus (Tekno DNA bank peak=%.3f dubThrow=%d)\n", peak, (int)dubThrow);
}

static void testDjLivePerformance()
{
    OldSchoolBoxProcessor proc;
    proc.prepareToPlay(44100.0, 256);

    // 1. True Polyrhythm: odd step lengths 13 and 11
    proc.sequencer.setPatternLength(4, 13);
    proc.sequencer.setPatternLength(7, 11);
    CHECK(proc.sequencer.getPatternLength(4) == 13);
    CHECK(proc.sequencer.getPatternLength(7) == 11);

    // 2. Acid Scale Lock: HYP2 only emits {0, 1, 12}
    proc.sequencer.acidScale.store(static_cast<int>(Sequencer::AcidScale::Hypno2Note), std::memory_order_release);
    proc.sequencer.randomizeBassAcid(1);
    for (int s = 0; s < 11; ++s)
    {
        const int n = static_cast<int>(proc.sequencer.getBassNoteOffset(1, s));
        CHECK(n == 0 || n == 1 || n == 12);
    }

    // 3. 1-Click MIDI Learn: arm bass2_cutoff, send CC 74 = 100, flush, verify value moved
    proc.midiLearn.toggleLearn(ParamId::BASS2_CUTOFF);
    CHECK(proc.midiLearn.isLearning(ParamId::BASS2_CUTOFF));
    juce::MidiBuffer ccBuf;
    ccBuf.addEvent(juce::MidiMessage::controllerEvent(1, 74, 100), 0);
    proc.midiLearn.processIncomingMidi(ccBuf);
    CHECK(!proc.midiLearn.isLearning(ParamId::BASS2_CUTOFF));
    CHECK(proc.midiLearn.getMappedCc(ParamId::BASS2_CUTOFF) == 74);
    proc.midiLearn.flushToParameters();
    CHECK(std::abs(proc.apvts.getParameter(ParamId::BASS2_CUTOFF)->getValue() - (100.0f / 127.0f)) < 0.02f);

    // 4. Quantized Mute + Snap-Back REVERT + DJ Filter / KILL SUB audio safety
    proc.sequencer.quantizeMode.store(1, std::memory_order_release); // 1/4 beat
    proc.sequencer.queuedMuteMask.store(1u << 0, std::memory_order_release);
    proc.internalPlay.store(true, std::memory_order_release);
    proc.currentBpm.store(155.0f, std::memory_order_relaxed);
    proc.apvts.getParameter(ParamId::DJ_FILTER)->setValueNotifyingHost(0.75f); // HP wash
    proc.killSubHeld.store(true, std::memory_order_release);
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b)
    {
        buf.clear(); midi.clear();
        proc.processBlock(buf, midi);
    }
    CHECK(proc.sequencer.mute[0].load(std::memory_order_acquire) == true); // quantized mute snapped!
    CHECK(std::isfinite(buf.getMagnitude(0, 256)));

    // 5. Direct 4-bar WAV & MIDI export for DAW Drag & Drop
    proc.sequencer.mute[0].store(false, std::memory_order_release);
    const auto midFile = LoopExporter::exportMidi4Bars(proc.sequencer, 155.0f);
    const auto wavFile = LoopExporter::exportWav4Bars(proc.sequencer, proc.apvts, 155.0f, 44100.0);
    CHECK(midFile.existsAsFile() && midFile.getSize() > 64);
    CHECK(wavFile.existsAsFile() && wavFile.getSize() > 10000);
    // 6. Real Musical Units formatting (Hz + note, kHz, ms, +dB [MACKIE SAT])
    CHECK(MusicalUnits::format(ParamId::KICK_TUNE, 55.0).contains("55.0 Hz (A1)"));
    CHECK(MusicalUnits::format(ParamId::MASTER_DRIVE, 0.5).contains("[MACKIE SAT]"));
    CHECK(MusicalUnits::format(ParamId::DJ_FILTER, 0.0) == "BYPASS");

    std::printf("PASS: DJ Live + Craft UX (poly=13/11, HYP2, CC74, QNT, WAV=%lldB, MID=%lldB, Units=%s)\n",
                static_cast<long long>(wavFile.getSize()), static_cast<long long>(midFile.getSize()),
                MusicalUnits::format(ParamId::KICK_TUNE, 55.0).toRawUTF8());
    midFile.deleteFile();
    wavFile.deleteFile();
}

static void testMultiSampleRateOddBlocksCorruptXmlAndCpuBudget()
{
    // 1. Sample-rate invariance (44.1 / 48 / 96 / 192 kHz) + variable odd block sizes (1, 7, 13, 257)
    static constexpr double kRates[4] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    static constexpr int    kBlocks[4] = { 1, 7, 13, 257 };
    for (double sr : kRates)
    {
        OldSchoolBoxProcessor proc;
        proc.prepareToPlay(sr, 512);
        proc.currentBpm.store(160.0f, std::memory_order_relaxed);
        proc.internalPlay.store(true, std::memory_order_release);
        if (auto* p = proc.apvts.getParameter(ParamId::REVERB_MIX)) p->setValueNotifyingHost(0.35f);
        if (auto* p = proc.apvts.getParameter(ParamId::DELAY_MIX))  p->setValueNotifyingHost(0.25f);
        juce::MidiBuffer midi;
        for (int b = 0; b < 32; ++b)
        {
            const int n = kBlocks[b & 3];
            juce::AudioBuffer<float> buf(2, n);
            buf.clear();
            midi.clear();
            proc.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float s = buf.getSample(ch, i);
                    CHECK(std::isfinite(s) && std::abs(s) <= 1.001f);
                }
        }
    }

    // 2. Adversarial corrupted XML state (out-of-range activeScene must clamp to [0, 3])
    {
        OldSchoolBoxProcessor proc;
        juce::MemoryBlock blob;
        proc.getStateInformation(blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
        CHECK(xml != nullptr);
        if (auto* seq = xml->getChildByName("SEQ"))
            seq->setAttribute("activeScene", 99);
        juce::MemoryBlock corruptHigh;
        juce::AudioProcessor::copyXmlToBinary(*xml, corruptHigh);
        proc.setStateInformation(corruptHigh.getData(), static_cast<int>(corruptHigh.getSize()));
        const int hi = proc.sequencer.activeScene.load(std::memory_order_acquire);
        CHECK(hi >= 0 && hi < Scenes::COUNT);

        if (auto* seq = xml->getChildByName("SEQ"))
            seq->setAttribute("activeScene", -5);
        juce::MemoryBlock corruptLow;
        juce::AudioProcessor::copyXmlToBinary(*xml, corruptLow);
        proc.setStateInformation(corruptLow.getData(), static_cast<int>(corruptLow.getSize()));
        const int lo = proc.sequencer.activeScene.load(std::memory_order_acquire);
        CHECK(lo >= 0 && lo < Scenes::COUNT);
    }

    // 3. Real-time CPU headroom check: 1.0s of 8+1 audio at 44.1 kHz must render well under real-time
    {
        OldSchoolBoxProcessor proc;
        proc.prepareToPlay(44100.0, 256);
        proc.currentBpm.store(170.0f, std::memory_order_relaxed);
        proc.internalPlay.store(true, std::memory_order_release);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 173; ++b) // ~1.004 seconds of audio
        {
            buf.clear();
            midi.clear();
            proc.processBlock(buf, midi);
        }
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        CHECK(elapsedMs < 500);
        std::printf("PASS: Multi-SR (44.1/48/96/192kHz), odd blocks (1/7/13/257), corrupt XML clamp, RT CPU=%lldms/1000ms\n",
                    static_cast<long long>(elapsedMs));
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && std::string(argv[1]) == "--render")
    {
        OldSchoolBoxProcessor proc;
        proc.prepareToPlay(44100.0, 256);
        const auto& presets = FactoryPresets::all();
        Scenes::apply(presets[1].snapshot, proc.sequencer); // Acid House
        FactoryPresets::applyParams(presets[1], proc.apvts);
        if (auto* p = proc.apvts.getParameter(ParamId::CHAOS_ENABLE))
            p->setValueNotifyingHost(1.0f);
        proc.sequencer.setPatternLength(4, 13); // showcase true 13-step polyrhythm on PERC1!
        proc.sequencer.isPlaying.store(true, std::memory_order_release);
        proc.sequencer.currentBar.store(22, std::memory_order_release);
        proc.currentBpm.store(155.0f, std::memory_order_relaxed);
        proc.internalPlay.store(true, std::memory_order_release);
        // Run real audio blocks so CRT scope and 8-band Invader spectrum show live 303 + kick audio
        juce::AudioBuffer<float> rbuf(2, 256);
        juce::MidiBuffer rmidi;
        for (int b = 0; b < 24; ++b)
        {
            rbuf.clear();
            rmidi.clear();
            proc.processBlock(rbuf, rmidi);
        }
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
            proc.sequencer.currentSteps[static_cast<size_t>(ch)].store(4, std::memory_order_release);
        proc.visualState.sustainedIntensity.store(0.85f, std::memory_order_relaxed);
        proc.visualState.kickPulse.store(0.70f, std::memory_order_relaxed);
        proc.visualState.totalEnergy.store(0.80f, std::memory_order_relaxed);
        proc.visualState.noteFire(0, 1.0f);
        proc.visualState.noteFire(7, 0.95f);

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* osbEd = dynamic_cast<OldSchoolBoxEditor*>(ed.get()))
            osbEd->skipBootAnimation();
        ed->setSize(OldSchoolBoxView::baseWidth(), OldSchoolBoxView::baseHeight());
        auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File outFile(argv[2]);
        outFile.deleteFile();
        juce::FileOutputStream fos(outFile);
        juce::PNGImageFormat png;
        png.writeImageToStream(img, fos);
        std::printf("RENDERED: %s (%dx%d)\n", argv[2], img.getWidth(), img.getHeight());
        return 0;
    }

    testBassNoteOffsetsAndPersistence();
    testAuditionAndInternalClock();
    testSequencerUndoRedo();
    testResizableEditor();
    testUiStateSyncOnReload();
    testFactoryPresetsAndVersionedScenes();
    testMultiOutAndSampleAccurateMidi();
    testSprint1And3Dsp();
    testChaosSampleBus();
    testDjLivePerformance();
    testMultiSampleRateOddBlocksCorruptXmlAndCpuBudget();
    return 0;
}
