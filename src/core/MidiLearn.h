#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Lock-free 1-click MIDI CC Learn manager.
// Audio thread reads incoming CC messages, binds if learning, and writes normalized
// values into an atomic queue. Message thread (30 Hz timer) flushes updates to APVTS
// so both DSP and UI knobs stay 100% synchronized with zero audio-thread allocations.
struct MidiLearnManager
{
    static constexpr int MAX_CC = 128;

    void init(juce::AudioProcessorValueTreeState& apvts)
    {
        paramIds.clear();
        params.clear();
        for (auto* p : apvts.processor.getParameters())
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                paramIds.push_back(rp->getParameterID());
                params.push_back(rp);
            }
        }
        const size_t n = params.size();
        pendingVal = std::make_unique<std::atomic<float>[]>(n);
        pendingDirty = std::make_unique<std::atomic<bool>[]>(n);
        for (size_t i = 0; i < n; ++i)
        {
            pendingVal[i].store(0.0f, std::memory_order_relaxed);
            pendingDirty[i].store(false, std::memory_order_relaxed);
        }
        for (auto& m : ccToParam)
            m.store(-1, std::memory_order_relaxed);
        learningParamIdx.store(-1, std::memory_order_relaxed);
    }

    int findParamIndex(const juce::String& paramId) const
    {
        for (size_t i = 0; i < paramIds.size(); ++i)
            if (paramIds[i] == paramId)
                return static_cast<int>(i);
        return -1;
    }

    // UI thread: arm/disarm 1-click MIDI learn for a parameter
    void toggleLearn(const juce::String& paramId)
    {
        const int idx = findParamIndex(paramId);
        if (idx < 0) return;
        if (learningParamIdx.load(std::memory_order_relaxed) == idx)
            learningParamIdx.store(-1, std::memory_order_release);
        else
            learningParamIdx.store(idx, std::memory_order_release);
    }

    void clearMapping(const juce::String& paramId)
    {
        const int idx = findParamIndex(paramId);
        if (idx < 0) return;
        for (auto& m : ccToParam)
            if (m.load(std::memory_order_relaxed) == idx)
                m.store(-1, std::memory_order_release);
        if (learningParamIdx.load(std::memory_order_relaxed) == idx)
            learningParamIdx.store(-1, std::memory_order_release);
    }

    bool isLearning(const juce::String& paramId) const
    {
        const int cur = learningParamIdx.load(std::memory_order_relaxed);
        return cur >= 0 && cur == findParamIndex(paramId);
    }

    int getMappedCc(const juce::String& paramId) const
    {
        const int idx = findParamIndex(paramId);
        if (idx < 0) return -1;
        for (int cc = 0; cc < MAX_CC; ++cc)
            if (ccToParam[static_cast<size_t>(cc)].load(std::memory_order_relaxed) == idx)
                return cc;
        return -1;
    }

    // Audio thread: scan incoming MIDI buffer for CC messages
    void processIncomingMidi(const juce::MidiBuffer& midi)
    {
        const int numParams = static_cast<int>(params.size());
        if (numParams <= 0 || !pendingVal || !pendingDirty) return;

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (!msg.isController()) continue;

            const int cc = msg.getControllerNumber();
            if (cc < 0 || cc >= MAX_CC) continue;
            const float norm = static_cast<float>(msg.getControllerValue()) / 127.0f;

            const int learnIdx = learningParamIdx.load(std::memory_order_acquire);
            if (learnIdx >= 0 && learnIdx < numParams)
            {
                // Unbind any previous CC mapped to this parameter
                for (auto& m : ccToParam)
                    if (m.load(std::memory_order_relaxed) == learnIdx)
                        m.store(-1, std::memory_order_relaxed);
                ccToParam[static_cast<size_t>(cc)].store(learnIdx, std::memory_order_release);
                learningParamIdx.store(-1, std::memory_order_release);
            }

            const int target = ccToParam[static_cast<size_t>(cc)].load(std::memory_order_acquire);
            if (target >= 0 && target < numParams)
            {
                pendingVal[static_cast<size_t>(target)].store(norm, std::memory_order_relaxed);
                pendingDirty[static_cast<size_t>(target)].store(true, std::memory_order_release);
            }
        }
    }

    // Message thread (timer): flush CC updates to APVTS
    void flushToParameters()
    {
        const size_t n = params.size();
        if (!pendingVal || !pendingDirty) return;
        for (size_t i = 0; i < n; ++i)
        {
            if (pendingDirty[i].exchange(false, std::memory_order_acq_rel))
            {
                const float norm = pendingVal[i].load(std::memory_order_relaxed);
                params[i]->setValueNotifyingHost(norm);
            }
        }
    }

    // State persistence helpers
    juce::String serializeMappings() const
    {
        juce::String out;
        for (int cc = 0; cc < MAX_CC; ++cc)
        {
            const int pIdx = ccToParam[static_cast<size_t>(cc)].load(std::memory_order_relaxed);
            if (pIdx >= 0 && pIdx < static_cast<int>(paramIds.size()))
            {
                if (out.isNotEmpty()) out << ",";
                out << cc << ":" << paramIds[static_cast<size_t>(pIdx)];
            }
        }
        return out;
    }

    void deserializeMappings(const juce::String& data)
    {
        for (auto& m : ccToParam) m.store(-1, std::memory_order_relaxed);
        juce::StringArray pairs;
        pairs.addTokens(data, ",", "");
        for (const auto& pair : pairs)
        {
            const int sep = pair.indexOfChar(':');
            if (sep <= 0) continue;
            const int cc = pair.substring(0, sep).getIntValue();
            const juce::String pid = pair.substring(sep + 1);
            const int pIdx = findParamIndex(pid);
            if (cc >= 0 && cc < MAX_CC && pIdx >= 0)
                ccToParam[static_cast<size_t>(cc)].store(pIdx, std::memory_order_release);
        }
    }

private:
    std::vector<juce::String> paramIds;
    std::vector<juce::RangedAudioParameter*> params;
    std::array<std::atomic<int>, MAX_CC> ccToParam {};
    std::atomic<int> learningParamIdx { -1 };
    std::unique_ptr<std::atomic<float>[]> pendingVal;
    std::unique_ptr<std::atomic<bool>[]>  pendingDirty;
};
