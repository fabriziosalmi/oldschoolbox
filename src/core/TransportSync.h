#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include "VoiceBank.h"
#include "PatternPresets.h"
#include "Scenes.h"
#include "VisualState.h"
#include "AutoMode.h"
#include "../Sequencer.h"

// Reads AudioPlayHead, advances each channel's sequencer step independently.
//
// Step-firing model: per-channel lookahead. Each block, we compute the scheduled
// PPQ for every step that hasn't yet fired and trigger any whose time has come.
// This supports per-step micro-timing offsets (the step's scheduled time is
// shifted away from its nominal grid position).
//
// Additional handling at bar boundaries: scene switching, FILL trigger, and
// auto-variation. RNG is used for probability gating and humanise.
struct TransportSync
{
    // Sample-accurate scheduled voice event for zero-allocation sub-block splitting
    struct ScheduledEvent
    {
        enum class Type : uint8_t { DrumTrigger, BassTrigger, BassNoteOff };
        int   sampleOffset = 0;
        int   ch           = 0;
        Type  type         = Type::DrumTrigger;
        float vel          = 1.0f;
        int   note         = 48;
        bool  slide        = false;
        bool  accent       = false;
    };

    struct EventQueue
    {
        static constexpr int MAX_EVENTS = 64;
        std::array<ScheduledEvent, MAX_EVENTS> events {};
        int count = 0;

        void push(const ScheduledEvent& ev)
        {
            if (count < MAX_EVENTS)
                events[static_cast<std::size_t>(count++)] = ev;
        }

        void sortByOffset()
        {
            if (count <= 1) return;
            std::stable_sort(events.begin(), events.begin() + count,
                [](const ScheduledEvent& a, const ScheduledEvent& b) {
                    return a.sampleOffset < b.sampleOffset;
                });
        }
    };

    static void dispatchEvent(VoiceBank& voices, const ScheduledEvent& ev)
    {
        switch (ev.type)
        {
            case ScheduledEvent::Type::DrumTrigger:
                voices.trigger(ev.ch, ev.vel);
                break;
            case ScheduledEvent::Type::BassTrigger:
                voices.triggerBass303(ev.ch - OsbBus::BASS1, ev.note, ev.vel, ev.slide, ev.accent);
                break;
            case ScheduledEvent::Type::BassNoteOff:
                voices.noteOffBass(ev.ch - OsbBus::BASS1);
                break;
        }
    }

    static void process(juce::AudioPlayHead* playHead,
                        Sequencer&           seq,
                        VoiceBank&           voices,
                        std::array<int, Sequencer::NUM_CHANNELS>& lastSteps,
                        int64_t&             prevBar,
                        float                swing,
                        Scenes::Snapshot*    scenesArr,
                        const Scenes::Snapshot& preFillSnapshot,
                        uint32_t&            audioRng,
                        juce::MidiBuffer*    outMidi,
                        std::array<int, Sequencer::NUM_CHANNELS>& pendingMidiNotes,
                        VisualState&         visualState,
                        AutoMode*            autoModePtr,   // nullable
                        float                autoMutation,
                        float                autoDrift,
                        bool                 internalPlay = false,
                        double&              internalPpq = dummyInternalPpq(),
                        float                internalBpm = 120.0f,
                        double               sampleRate = 44100.0,
                        int                  numSamples = 0,
                        EventQueue*          deferredEvents = nullptr)
    {
        bool hostPlaying = false;
        double ppq = 0.0;
        bool hasPpq = false;

        if (playHead)
        {
            if (const auto pos = playHead->getPosition())
            {
                hostPlaying = pos->getIsPlaying();
                if (hostPlaying)
                {
                    if (const auto ppqOpt = pos->getPpqPosition())
                    {
                        ppq = *ppqOpt < 0.0 ? 0.0 : *ppqOpt;
                        hasPpq = true;
                        internalPpq = ppq;
                    }
                }
            }
        }

        const bool usingInternal = !hostPlaying && internalPlay;
        const bool playing = (hostPlaying && hasPpq) || usingInternal;
        seq.isPlaying.store(playing, std::memory_order_release);

        if (!playing)
        {
            for (auto& s : seq.currentSteps)
                s.store(-1, std::memory_order_release);
            lastSteps.fill(-1);
            prevBar = -1;
            internalPpq = 0.0;
            // Flush any pending MIDI note-offs
            if (outMidi)
                for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
                    if (pendingMidiNotes[idx(ch)] >= 0)
                    {
                        outMidi->addEvent(juce::MidiMessage::noteOff(midiChForChannel(ch),
                                          pendingMidiNotes[idx(ch)]), 0);
                        pendingMidiNotes[idx(ch)] = -1;
                    }
            return;
        }

        const double ppqPerSample = (sampleRate > 0.0)
            ? (static_cast<double>(std::max(20.0f, internalBpm)) / 60.0) / sampleRate
            : 0.0;
        const double blockPpqLen = ppqPerSample * static_cast<double>(std::max(0, numSamples));

        if (usingInternal)
        {
            ppq = internalPpq;
            internalPpq += blockPpqLen;
        }

        const double ppqStart = ppq;
        const double ppqEnd   = ppqStart + (blockPpqLen > 0.0 ? blockPpqLen : 1.0e-9);

        const double swingFactor = 0.5 + static_cast<double>(swing) * 0.25;
        constexpr double ppqPerPair = 0.5;
        constexpr double ppqPerStep = 0.25;

        const int64_t nowPairIdx = static_cast<int64_t>(std::floor(ppq / ppqPerPair));
        const int64_t nowGlobalStep = nowPairIdx * 2;  // approx — we use this only for safety bounds

        const int64_t currentBar  = nowGlobalStep / 16;
        seq.currentBar.store(currentBar, std::memory_order_release);

        // Quantized DJ performance boundary check (0=OFF immediate, 1=1/4 beat, 2=1B bar)
        const int qntMode = seq.quantizeMode.load(std::memory_order_relaxed);
        const bool isBarBoundary  = (currentBar != prevBar);
        const bool isBeatBoundary = (qntMode == 1) && ((nowGlobalStep % 4) == 0)
                                    && (lastSteps[0] < nowGlobalStep);
        const bool qntTrigger = (qntMode == 0)
                                || (qntMode == 1 && (isBarBoundary || isBeatBoundary))
                                || (qntMode == 2 && isBarBoundary);

        if (qntTrigger)
        {
            // Quantized MUTE toggles (zero mid-kick cuts!)
            const uint8_t mMask = seq.queuedMuteMask.exchange(0, std::memory_order_acq_rel);
            if (mMask != 0)
            {
                for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
                {
                    if ((mMask & (1u << ch)) != 0)
                    {
                        const bool cur = seq.mute[idx(ch)].load(std::memory_order_relaxed);
                        seq.mute[idx(ch)].store(!cur, std::memory_order_release);
                    }
                }
            }

            // Quantized Scene switch OR Snap-Back / Panic Reload (REVERT)
            const bool doRevert = seq.revertQueued.exchange(false, std::memory_order_acq_rel);
            int qs = seq.queuedScene.load(std::memory_order_acquire);
            if (doRevert && qs < 0)
                qs = seq.activeScene.load(std::memory_order_acquire);

            if (qs >= 0 && qs < Sequencer::NUM_SCENES)
            {
                Scenes::apply(scenesArr[qs], seq);
                seq.activeScene.store(qs, std::memory_order_release);
                seq.queuedScene.store(-1, std::memory_order_release);
                seq.pendingParamSceneApply.store(qs, std::memory_order_release);
            }
        }

        // ── Bar boundary work ──
        if (isBarBoundary)
        {
            prevBar = currentBar;
            visualState.onBar();

            const int64_t fb = seq.fillStartBar.load(std::memory_order_acquire);
            const bool inFillBar   = (fb >= 0 && currentBar == fb);
            const bool postFillBar = (fb >= 0 && currentBar == fb + 1);
            const bool fillStale   = (fb >= 0 && currentBar > fb + 1);

            if (inFillBar)
            {
                for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
                    seq.setPattern(ch, PatternPresets::PATTERNS[3][ch]);
            }
            else if (postFillBar)
            {
                for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
                    if (!seq.autoVar[idx(ch)].load(std::memory_order_relaxed))
                        seq.setPattern(ch, preFillSnapshot.patterns[ch]);
                seq.fillStartBar.store(-1, std::memory_order_release);
                applyAutoVariations(seq, currentBar);
            }
            else
            {
                if (fillStale)
                    seq.fillStartBar.store(-1, std::memory_order_release);
                applyAutoVariations(seq, currentBar);
            }

            // AUTO mode mutation — runs after auto-variation, mutates the
            // current state stochastically around the captured anchor.
            // Skip on fill bar so the fill plays cleanly.
            if (autoModePtr && !inFillBar)
            {
                const float energy = visualState.sustainedIntensity.load(std::memory_order_relaxed);
                autoModePtr->evolve(seq, autoMutation, autoDrift, energy, audioRng, currentBar);
            }
        }

        // ── Per-channel step lookahead & fire ──
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            const int len = seq.getPatternLength(ch);

            // Safety: large jump backwards → reset
            if (lastSteps[idx(ch)] > nowGlobalStep + 8 || lastSteps[idx(ch)] < nowGlobalStep - 64)
                lastSteps[idx(ch)] = static_cast<int>(nowGlobalStep) - 1;

            // Fire any due steps
            int firesThisBlock = 0;
            while (firesThisBlock < 8)  // safety: at most 8 steps per block per channel
            {
                const int64_t G = static_cast<int64_t>(lastSteps[idx(ch)]) + 1;
                const int64_t pIdx = G >> 1;
                const bool    isOdd = (G & 1) != 0;
                const double nominalPpq = static_cast<double>(pIdx) * ppqPerPair
                                        + (isOdd ? swingFactor * ppqPerPair : 0.0);

                const int localStep = static_cast<int>(((G % len) + len) % len);
                const int8_t mt = seq.getMicroTiming(ch, localStep);
                // mt is -50..+50 % of step width → ±0.125 PPQ
                const double scheduledPpq = nominalPpq + (static_cast<double>(mt) / 100.0) * ppqPerStep;

                // Stop if step not yet due within this audio block, OR if we'd be firing too far ahead
                if (scheduledPpq >= ppqEnd) break;
                if (G > nowGlobalStep + 4) break;

                // Compute exact sample offset within the current buffer for sample-accurate MIDI out
                const int sampleOffset = (ppqPerSample > 0.0 && numSamples > 1)
                    ? std::clamp(static_cast<int>(std::llround((scheduledPpq - ppqStart) / ppqPerSample)),
                                 0, numSamples - 1)
                    : 0;

                // Fire step G
                lastSteps[idx(ch)] = static_cast<int>(G);
                seq.currentSteps[idx(ch)].store(localStep, std::memory_order_release);
                ++firesThisBlock;

                const bool on = seq.getStep(ch, localStep);
                if (on)
                {
                    // ── Probability gate ──
                    const uint8_t prob = seq.getProbability(ch, localStep);
                    bool fire = (prob >= 100);
                    if (!fire)
                    {
                        audioRng = nextRng(audioRng);
                        fire = (audioRng % 100u) < prob;
                    }

                    if (fire)
                    {
                        float vel = seq.getVelocity(ch, localStep) / 127.0f;

                        // ── Humanise: ±15% velocity ──
                        if (seq.humanize[idx(ch)].load(std::memory_order_relaxed))
                        {
                            audioRng = nextRng(audioRng);
                            const int delta = static_cast<int>(audioRng % 31u) - 15;
                            vel *= 1.0f + static_cast<float>(delta) * 0.01f;
                            vel = std::clamp(vel, 0.05f, 1.0f);
                        }

                        int midiNote = -1;
                        if (ch >= OsbBus::BASS1)
                        {
                            const int bi = ch - OsbBus::BASS1;
                            const bool slide = seq.getBassSlide(bi, localStep);
                            const bool accent = seq.getBassAccent(bi, localStep);
                            int note = 48 + seq.getBassNoteOffset(bi, localStep);
                            if (seq.getBassOctUp(bi, localStep))    note += 12;
                            if (seq.getBassOctDown(bi, localStep))  note -= 12;
                            if (accent)
                                vel = std::min(1.0f, vel + 0.35f);
                            if (deferredEvents != nullptr)
                                deferredEvents->push({ sampleOffset, ch, ScheduledEvent::Type::BassTrigger, vel, note, slide, accent });
                            else
                                voices.triggerBass303(bi, note, vel, slide, accent);
                            midiNote = note;
                        }
                        else
                        {
                            if (deferredEvents != nullptr)
                                deferredEvents->push({ sampleOffset, ch, ScheduledEvent::Type::DrumTrigger, vel, 0, false, false });
                            else
                                voices.trigger(ch, vel);
                            midiNote = midiNoteForDrum(ch);
                        }

                        // Visual feedback
                        visualState.noteFire(ch, vel);

                        // ── MIDI out (sample-accurate offset within block) ──
                        if (outMidi && midiNote >= 0)
                        {
                            const int midiCh = midiChForChannel(ch);
                            // Close previous note on this channel if still pending
                            if (pendingMidiNotes[idx(ch)] >= 0)
                                outMidi->addEvent(juce::MidiMessage::noteOff(midiCh,
                                                  pendingMidiNotes[idx(ch)]), sampleOffset);
                            const uint8_t vMidi = static_cast<uint8_t>(
                                std::clamp(static_cast<int>(vel * 127.0f), 1, 127));
                            outMidi->addEvent(juce::MidiMessage::noteOn(midiCh, midiNote, vMidi), sampleOffset);
                            pendingMidiNotes[idx(ch)] = midiNote;
                        }
                    }
                }
                else if (ch >= OsbBus::BASS1)
                {
                    const int bi = ch - OsbBus::BASS1;
                    const int prevLocal = (localStep == 0) ? (len - 1) : (localStep - 1);
                    if (!seq.getBassSlide(bi, prevLocal))
                    {
                        if (deferredEvents != nullptr)
                            deferredEvents->push({ sampleOffset, ch, ScheduledEvent::Type::BassNoteOff, 0.0f, 0, false, false });
                        else if (voices.isActive(ch))
                            voices.noteOffBass(bi);

                        // Anti-Stuck-Notes: emit sample-accurate MIDI NoteOff on non-slid bass off-steps
                        if (outMidi && pendingMidiNotes[idx(ch)] >= 0)
                        {
                            outMidi->addEvent(juce::MidiMessage::noteOff(midiChForChannel(ch),
                                              pendingMidiNotes[idx(ch)]), sampleOffset);
                            pendingMidiNotes[idx(ch)] = -1;
                        }
                    }
                }
            }
        }
        if (deferredEvents != nullptr)
            deferredEvents->sortByOffset();
    }

private:
    static double& dummyInternalPpq() { static double d = 0.0; return d; }
    static std::size_t idx(int i) { return static_cast<std::size_t>(i); }

    static uint32_t nextRng(uint32_t r)
    {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return r;
    }

    // MIDI mapping: drum channels use channel 10 (GM standard drums) with
    // their canonical notes; bass channels use channels 1 and 2 with their pitch.
    static int midiChForChannel(int ch)
    {
        return (ch >= OsbBus::BASS1) ? (MidiMap::BASS1_CH + (ch - OsbBus::BASS1)) : 10;
    }

    static int midiNoteForDrum(int ch)
    {
        switch (ch)
        {
            case OsbBus::KICK:  return MidiMap::KICK;
            case OsbBus::SNARE: return MidiMap::SNARE;
            case OsbBus::HAT:   return MidiMap::HAT;
            case OsbBus::TOM:   return MidiMap::TOM;
            case OsbBus::PERC1: return MidiMap::PERC1;
            case OsbBus::PERC2: return MidiMap::PERC2;
            default:            return -1;
        }
    }

    static void applyAutoVariations(Sequencer& seq, int64_t bar)
    {
        const int barIn64 = static_cast<int>(bar % 64);
        const int barIn16 = static_cast<int>(bar % 16);
        const int barIn4  = static_cast<int>(bar % 4);
        const int variation = PatternPresets::variationForBar(barIn64);

        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            if (!seq.autoVar[idx(ch)].load(std::memory_order_acquire))
                continue;

            switch (ch)
            {
                case OsbBus::KICK:
                    if (barIn16 == 15)
                        seq.setPattern(ch, PatternPresets::PATTERNS[3][ch]);
                    else if (barIn4 == 3 && barIn16 >= 12)
                        seq.setPattern(ch, PatternPresets::PATTERNS[1][ch]);
                    else
                        seq.setPattern(ch, PatternPresets::PATTERNS[variation][ch]);
                    break;
                case OsbBus::SNARE:
                    if (barIn16 == 15)
                        seq.setPattern(ch, PatternPresets::PATTERNS[3][ch]);
                    else if (barIn4 == 3)
                        seq.setPattern(ch, PatternPresets::PATTERNS[1][ch]);
                    else
                        seq.setPattern(ch, PatternPresets::PATTERNS[variation][ch]);
                    break;
                case OsbBus::HAT:
                    seq.setPattern(ch, barIn4 < 2 ? PatternPresets::PATTERNS[variation][ch]
                                                  : PatternPresets::PATTERNS[1][ch]);
                    break;
                case OsbBus::TOM:
                case OsbBus::PERC1:
                case OsbBus::PERC2:
                    seq.setPattern(ch, barIn16 >= 14 ? PatternPresets::PATTERNS[3][ch]
                                                    : PatternPresets::PATTERNS[variation][ch]);
                    break;
                case OsbBus::BASS1:
                    seq.setPattern(ch, barIn16 == 15 ? PatternPresets::PATTERNS[3][ch]
                                                    : PatternPresets::PATTERNS[variation][ch]);
                    break;
                case OsbBus::BASS2:
                    seq.setPattern(ch, barIn16 >= 14 ? PatternPresets::PATTERNS[3][ch]
                                                    : PatternPresets::PATTERNS[variation][ch]);
                    break;
                default:
                    seq.setPattern(ch, PatternPresets::PATTERNS[variation][ch]);
                    break;
            }
        }
    }
};
