#pragma once
#include <JuceHeader.h>
#include "../Sequencer.h"
#include "VoiceBank.h"
#include "Scenes.h"
#include "TransportSync.h"

// Zero-dialog 4-bar WAV & MIDI exporter for direct DAW Drag & Drop ([↗ WAV] / [↗ MIDI]).
namespace LoopExporter
{
    // Export 4 bars (64 16th-note steps) of the current Sequencer as a Standard MIDI File.
    // Drums (ch 0..5) → MIDI ch 10 (GM notes), BASS1 → ch 1, BASS2 → ch 2 with 303 slide/accent/oct.
    inline juce::File exportMidi4Bars(const Sequencer& seq, float bpm)
    {
        const double safeBpm = std::clamp(static_cast<double>(bpm), 40.0, 260.0);
        constexpr int TPQ = 960;
        constexpr double ticksPerStep = TPQ / 4.0; // 16th note = 240 ticks
        constexpr int totalSteps = 64;             // 4 bars

        juce::MidiMessageSequence track;
        track.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::llround(60000000.0 / safeBpm))), 0.0);

        static constexpr int drumNotes[6] = { 36, 38, 42, 45, 39, 49 };
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            if (!seq.isChannelAudible(ch)) continue;
            const int len = std::max(1, seq.getPatternLength(ch));
            const int midiCh = (ch < 6) ? 10 : (ch == 6 ? 1 : 2);

            for (int g = 0; g < totalSteps; ++g)
            {
                const int st = g % len;
                if (!seq.getStep(ch, st)) continue;

                const double startTick = static_cast<double>(g) * ticksPerStep;
                int note = (ch < 6) ? drumNotes[ch] : 36;
                int vel  = std::clamp(static_cast<int>(seq.getVelocity(ch, st)), 1, 127);
                double durTicks = ticksPerStep * 0.5;

                if (ch >= 6)
                {
                    const int bi = ch - 6;
                    note = 36 + static_cast<int>(seq.getBassNoteOffset(bi, st));
                    if (seq.getBassOctUp(bi, st))   note += 12;
                    if (seq.getBassOctDown(bi, st)) note -= 12;
                    note = std::clamp(note, 12, 108);
                    if (seq.getBassAccent(bi, st)) vel = 127;
                    if (seq.getBassSlide(bi, st))  durTicks = ticksPerStep * 1.15; // legato overlap for 303 slide
                }

                track.addEvent(juce::MidiMessage::noteOn(midiCh, note, static_cast<juce::uint8>(vel)), startTick);
                track.addEvent(juce::MidiMessage::noteOff(midiCh, note), startTick + durTicks);
            }
        }
        track.sort();
        track.updateMatchedPairs();

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote(TPQ);
        mf.addTrack(track);

        const auto outFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("OldSchoolBox_4bar_" + juce::String(static_cast<int>(std::round(safeBpm))) + "bpm.mid");
        outFile.deleteFile();
        if (auto stream = outFile.createOutputStream())
        {
            mf.writeTo(*stream);
            stream->flush();
        }
        return outFile;
    }

    // Export 4 bars of offline-rendered stereo 24-bit WAV using current Sequencer & APVTS parameters.
    inline juce::File exportWav4Bars(const Sequencer& srcSeq,
                                     juce::AudioProcessorValueTreeState& apvts,
                                     float bpm,
                                     double sampleRate = 44100.0)
    {
        const double safeBpm = std::clamp(static_cast<double>(bpm), 40.0, 260.0);
        const double sr = (sampleRate >= 22050.0) ? sampleRate : 44100.0;
        const int totalSamples = static_cast<int>(std::ceil((16.0 * 60.0 / safeBpm) * sr)); // 4 bars = 16 beats
        constexpr int blockSize = 512;

        // Clone sequencer state safely on message thread (non-intrusive to live audio)
        Sequencer seq;
        Scenes::Snapshot snap;
        Scenes::capture(snap, srcSeq);
        Scenes::apply(snap, seq);
        for (int ch = 0; ch < Sequencer::NUM_CHANNELS; ++ch)
        {
            seq.mute[static_cast<size_t>(ch)].store(srcSeq.mute[static_cast<size_t>(ch)].load(std::memory_order_relaxed), std::memory_order_relaxed);
            seq.solo[static_cast<size_t>(ch)].store(srcSeq.solo[static_cast<size_t>(ch)].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        VoiceBank voices(apvts);
        voices.prepare(sr);

        std::array<int, Sequencer::NUM_CHANNELS> lastSteps;
        lastSteps.fill(-1);
        std::array<int, Sequencer::NUM_CHANNELS> pendingMidi;
        pendingMidi.fill(-1);
        int64_t prevBar = -1;
        uint32_t rng = 0xC0FFEE42u;
        double ppq = 0.0;
        VisualState vis;
        std::array<Scenes::Snapshot, Scenes::COUNT> dummyScenes;
        for (auto& sc : dummyScenes) sc = snap;

        const float swing = apvts.getRawParameterValue(ParamId::SWING)->load();
        std::array<float, OsbBus::COUNT> vol {}, pan {};
        for (int b = 0; b < OsbBus::COUNT; ++b)
        {
            vol[static_cast<size_t>(b)] = apvts.getRawParameterValue(ParamId::vol(b))->load();
            pan[static_cast<size_t>(b)] = apvts.getRawParameterValue(ParamId::pan(b))->load();
        }

        juce::AudioBuffer<float> outBuf(2, totalSamples);
        outBuf.clear();

        // 2-Pass Seamless Loop Wrap-Around: pass 0 warms up voice/decay tails so bar 1
        // starts with natural wrapped tails, and pass 1 is written to the 24-bit WAV!
        juce::AudioBuffer<float> scratch(2, blockSize);
        float tmpL[blockSize], tmpR[blockSize];
        for (int pass = 0; pass < 2; ++pass)
        {
            int pos = 0;
            while (pos < totalSamples)
            {
                const int n = std::min(blockSize, totalSamples - pos);
                TransportSync::EventQueue evQ;
                TransportSync::process(nullptr, seq, voices, lastSteps, prevBar, swing,
                                       dummyScenes.data(), snap, rng, nullptr, pendingMidi,
                                       vis, nullptr, 0.0f, 0.0f,
                                       true, ppq, static_cast<float>(safeBpm), sr, n, &evQ);

                scratch.clear();
                float* outL = scratch.getWritePointer(0);
                float* outR = scratch.getWritePointer(1);

                int nextEv = 0;
                for (int off = 0; off < n; )
                {
                    while (nextEv < evQ.count && evQ.events[static_cast<size_t>(nextEv)].sampleOffset <= off)
                    {
                        TransportSync::dispatchEvent(voices, evQ.events[static_cast<size_t>(nextEv)]);
                        ++nextEv;
                    }
                    int split = n;
                    if (nextEv < evQ.count)
                        split = std::min(split, evQ.events[static_cast<size_t>(nextEv)].sampleOffset);
                    const int subN = std::max(1, split - off);

                    for (int b = 0; b < OsbBus::COUNT; ++b)
                    {
                        if (!seq.isChannelAudible(b)) continue;
                        std::memset(tmpL, 0, static_cast<size_t>(subN) * sizeof(float));
                        std::memset(tmpR, 0, static_cast<size_t>(subN) * sizeof(float));
                        voices.renderBus(b, tmpL, tmpR, subN, vol[static_cast<size_t>(b)]);
                        // Equal-Power -3 dB Pan Law (p in 0..1, 0.5 = centre)
                        const float p  = std::clamp(pan[static_cast<size_t>(b)], 0.0f, 1.0f);
                        const float gL = std::sin((1.0f - p) * Dsp::HALF_PI_F);
                        const float gR = std::sin(p * Dsp::HALF_PI_F);
                        for (int i = 0; i < subN; ++i)
                        {
                            outL[off + i] += tmpL[i] * gL;
                            outR[off + i] += tmpL[i] * gR;
                        }
                    }
                    off += subN;
                }
                while (nextEv < evQ.count)
                {
                    TransportSync::dispatchEvent(voices, evQ.events[static_cast<size_t>(nextEv)]);
                    ++nextEv;
                }

                if (pass == 1)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        outL[i] = std::tanh(outL[i]);
                        outR[i] = std::tanh(outR[i]);
                    }
                    outBuf.copyFrom(0, pos, scratch, 0, 0, n);
                    outBuf.copyFrom(1, pos, scratch, 1, 0, n);
                }
                pos += n;
            }
        }

        const auto outFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("OldSchoolBox_4bar_" + juce::String(static_cast<int>(std::round(safeBpm))) + "bpm.wav");
        outFile.deleteFile();
        juce::WavAudioFormat wav;
        if (auto stream = std::unique_ptr<juce::OutputStream>(outFile.createOutputStream()))
        {
            const auto opts = juce::AudioFormatWriterOptions{}
                                  .withSampleRate(sr)
                                  .withNumChannels(2)
                                  .withBitsPerSample(24);
            if (auto writer = wav.createWriterFor(stream, opts))
                writer->writeFromAudioSampleBuffer(outBuf, 0, totalSamples);
        }
        return outFile;
    }
} // namespace LoopExporter
