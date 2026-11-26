#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Euclidean.h"
#include "../voices/DspConstants.h"

// Underground Sample Bus / Chaos Generator ("TEKNO DNA")
//
// 100% RT-safe:
// - 8 built-in procedurally synthesized Free Tekno one-shots ready at second 0
//   (WHISTLE_98, SIREN_23, RADIO_VOX, ACID_GLITCH, IND_CLANG, LASER_8BIT, TRAIN_HORN, METAL_SCRP)
// - Optional custom WAV/AIFF loader (max 8s) parsed on UI thread and swapped lock-free
// - Mid-Range Auto-Carver (24 dB/oct HPF @ 400 Hz + megaphone resonant bandpass)
// - Akai S950 sample-rate reducer + console grit + DC blocker
// - Bar-aware trigger engine (turnarounds on bar 4/8/16 + rising-pitch stutter on FILL)
// - Euclidean 1/16 auto-gate + pitch/reverse randomization + auto-panning + Dub-Throw flag
class ChaosSampleBus
{
public:
    static constexpr int NUM_FACTORY_BANKS = 8;
    static constexpr int CUSTOM_BANK_IDX   = 8;
    static constexpr int PREVIEW_POINTS    = 64;

    struct SampleData
    {
        std::vector<float> samples; // mono normalized @ engine sample rate
        juce::String       name;
    };

    static const char* bankName(int idx)
    {
        static const char* kNames[NUM_FACTORY_BANKS + 1] = {
            "WHISTLE_98", "SIREN_23", "RADIO_VOX", "ACID_GLITCH",
            "IND_CLANG",  "LASER_8B", "TRAIN_HRN", "METAL_SCRP", "CUSTOM_WAV"
        };
        return kNames[std::clamp(idx, 0, NUM_FACTORY_BANKS)];
    }

    void prepare(double sr)
    {
        sampleRate = std::max(8000.0, sr);
        hpAlpha  = static_cast<float>(std::exp(-Dsp::TWO_PI * 400.0 / sampleRate));
        dcAlpha  = static_cast<float>(std::exp(-Dsp::TWO_PI * 20.0  / sampleRate));
        gateAlpha = 1.0f - static_cast<float>(std::exp(-1.0 / (0.004 * sampleRate)));
        hp1State = hp1Prev = hp2State = hp2Prev = 0.0f;
        bpLp = bpBp = 0.0f;
        dcPrev = dcOut = 0.0f;
        srHold = 0.0f;
        srCounter = 0;
        active = false;
        playPos = 0.0;
        playRate = 1.0;
        gateEnv = 1.0f;
        gateTarget = 1.0f;
        panPos = 0.5f;
        eucMask = 0xFFFFULL;

        buildFactoryBank();
        updatePreviewFromActive();
    }

    // UI thread: load a short WAV/AIFF file (clamped to 8s, resampled to current sampleRate)
    bool loadCustomFile(const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return false;

        const int64_t maxSrcSamples = static_cast<int64_t>(reader->sampleRate * 8.0);
        const int srcLen = static_cast<int>(std::min<int64_t>(reader->lengthInSamples, maxSrcSamples));
        juce::AudioBuffer<float> tmp(static_cast<int>(reader->numChannels), srcLen);
        reader->read(&tmp, 0, srcLen, 0, true, true);

        const double ratio = sampleRate / reader->sampleRate;
        const int dstLen = std::max(64, std::min(static_cast<int>(sampleRate * 8.0),
                                                 static_cast<int>(std::round(srcLen * ratio))));
        auto data = std::make_shared<SampleData>();
        data->name = file.getFileNameWithoutExtension().substring(0, 12).toUpperCase();
        data->samples.resize(static_cast<size_t>(dstLen));

        float peak = 0.0f;
        for (int i = 0; i < dstLen; ++i)
        {
            const double srcIdx = static_cast<double>(i) / ratio;
            const int i0 = std::clamp(static_cast<int>(srcIdx), 0, srcLen - 1);
            const int i1 = std::min(i0 + 1, srcLen - 1);
            const float frac = static_cast<float>(srcIdx - i0);
            float mono = 0.0f;
            for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
            {
                const float* p = tmp.getReadPointer(ch);
                mono += p[i0] * (1.0f - frac) + p[i1] * frac;
            }
            mono /= static_cast<float>(std::max(1, tmp.getNumChannels()));
            data->samples[static_cast<size_t>(i)] = mono;
            peak = std::max(peak, std::abs(mono));
        }
        if (peak > 1.0e-4f)
        {
            const float norm = 0.90f / peak;
            for (auto& s : data->samples) s *= norm;
        }

        // Lock-free handoff via double-buffered atomic slot
        const int nextSlot = 1 - customReadSlot.load(std::memory_order_acquire);
        customSlots[static_cast<size_t>(nextSlot)] = std::move(data);
        customReadSlot.store(nextSlot, std::memory_order_release);
        hasCustom.store(true, std::memory_order_release);
        updatePreviewFromBuffer(customSlots[static_cast<size_t>(nextSlot)]->samples);
        return true;
    }

    bool hasCustomSample() const { return hasCustom.load(std::memory_order_acquire); }

    // Called from audio thread on each 16th-note step boundary
    void onStep(int stepInBar,
                int64_t currentBar,
                bool inFill,
                int bankIdx,
                float density,
                float gateAmt,
                float pitchChaos,
                uint32_t& rng,
                bool& triggerDubThrowOut)
    {
        triggerDubThrowOut = false;

        // Regenerate Euclidean auto-gate mask every 4 bars
        if (stepInBar == 0 && (currentBar % 4 == 0))
        {
            rng = nextRng(rng);
            const int hits = 5 + static_cast<int>(rng % 9u); // 5..13 hits in 16 steps
            const int rot  = static_cast<int>((rng >> 8) % 16u);
            eucMask = Euclidean::generate(hits, 16, rot);
        }

        // Euclidean rhythmic gate target (when gateAmt > 0.05)
        const bool gateOpen = (gateAmt < 0.05f) || (((eucMask >> (stepInBar & 15)) & 1ULL) != 0);
        gateTarget = gateOpen ? 1.0f : (1.0f - gateAmt * 0.92f);
        lastGateOpen.store(gateOpen, std::memory_order_relaxed);
        lastStepIdx.store(stepInBar & 15, std::memory_order_relaxed);

        // Bar-aware trigger probability (#13)
        const int barIn4  = static_cast<int>(currentBar >= 0 ? (currentBar % 4)  : 0);
        const int barIn16 = static_cast<int>(currentBar >= 0 ? (currentBar % 16) : 0);
        float prob = density * 0.22f;
        if (barIn4 == 3)  prob *= 1.65f; // turnaround boost
        if (barIn16 >= 14) prob *= 1.50f;
        if ((stepInBar & 3) == 2) prob *= 1.35f; // favor offbeats

        // FILL stutter mode (#16): ratchet rising-pitch slice every 16th
        if (inFill)
        {
            triggerSlice(bankIdx, 1.0 + static_cast<double>(stepInBar & 7) * 0.08, false, 0.5f, 0.0);
            if (stepInBar == 12) triggerDubThrowOut = true;
            return;
        }

        rng = nextRng(rng);
        const float r = static_cast<float>(rng & 0xFFFFu) / 65535.0f;
        if (r < std::min(0.85f, prob))
        {
            // Pitch & reverse randomization (#17)
            static constexpr double kSemis[6] = { 0.0, 0.0, -12.0, 7.0, 12.0, -5.0 };
            rng = nextRng(rng);
            const double semis = (pitchChaos > 0.05f) ? kSemis[rng % 6u] * static_cast<double>(pitchChaos) : 0.0;
            const double rate = std::pow(2.0, semis / 12.0);
            rng = nextRng(rng);
            const bool rev = (pitchChaos > 0.25f) && ((rng % 100u) < 20u);
            rng = nextRng(rng);
            const float pan = 0.15f + (static_cast<float>(rng & 0xFFu) / 255.0f) * 0.70f;

            // Occasional Dub-Throw on bar turnaround (#18)
            if (barIn4 == 3 && (stepInBar == 8 || stepInBar == 12))
                triggerDubThrowOut = true;

            triggerSlice(bankIdx, rate, rev, pan, 0.0);
        }
    }

    // Audio thread: render one stereo sample (pre-ducking; caller passes kick duck gain)
    void process(float& outL, float& outR, float grit, float vol, float kickDuckGain)
    {
        outL = outR = 0.0f;
        if (!active || activeBuf == nullptr || activeBuf->empty())
        {
            playheadNorm.store(-1.0f, std::memory_order_relaxed);
            return;
        }

        const int len = static_cast<int>(activeBuf->size());
        const int idx0 = static_cast<int>(playPos);
        if (idx0 < 0 || idx0 >= len)
        {
            active = false;
            playheadNorm.store(-1.0f, std::memory_order_relaxed);
            return;
        }

        const int idx1 = std::min(idx0 + 1, len - 1);
        const float frac = static_cast<float>(playPos - idx0);
        float s = (*activeBuf)[static_cast<size_t>(idx0)] * (1.0f - frac)
                + (*activeBuf)[static_cast<size_t>(idx1)] * frac;

        playPos += reversePlay ? -playRate : playRate;
        playheadNorm.store(std::clamp(static_cast<float>(playPos) / static_cast<float>(len), 0.0f, 1.0f),
                           std::memory_order_relaxed);

        // 1. Akai S950 Sample-Rate Reducer + 12-bit grit (#19)
        const int srDiv = 1 + static_cast<int>(grit * 7.0f);
        if (++srCounter >= srDiv)
        {
            srCounter = 0;
            srHold = s;
        }
        s = s * (1.0f - grit * 0.65f) + srHold * (grit * 0.65f);

        // 2. Mid-Range Auto-Carver: 24 dB/oct HPF @ 400 Hz (#12)
        hp1State = hpAlpha * (hp1State + s - hp1Prev); hp1Prev = s;
        hp2State = hpAlpha * (hp2State + hp1State - hp2Prev); hp2Prev = hp1State;
        float mid = hp2State;

        // 3. Resonant Megaphone / Radio Bandpass (~1.6 kHz..2.8 kHz)
        const float bpFreq = 1600.0f + grit * 1400.0f;
        const float g = std::min(0.85f, 2.0f * std::sin(Dsp::PI_F * bpFreq / static_cast<float>(sampleRate)));
        const float damp = 0.45f - grit * 0.18f;
        const float hp = mid - bpLp - damp * bpBp;
        bpBp += g * hp;
        bpLp += g * bpBp;
        s = mid * (1.0f - grit * 0.55f) + bpBp * (0.45f + grit * 0.65f);

        // 4. Console overdrive + DC blocker
        const float drv = 1.0f + grit * 4.5f;
        s = fastTanh(s * drv) / (1.0f + grit * 0.55f);
        dcOut = (s - dcPrev) + dcAlpha * dcOut;
        dcPrev = s;
        s = dcOut;

        // 5. Euclidean gate + reverse Kick ducking (~70% depth, #14/#15)
        gateEnv += gateAlpha * (gateTarget - gateEnv);
        const float duck = 0.30f + 0.70f * kickDuckGain;
        s *= gateEnv * duck * vol * 0.72f;

        // 6. Auto-panning (#20)
        const float panR = fastSin(panPos * Dsp::HALF_PI_F);
        const float panL = fastSin((1.0f - panPos) * Dsp::HALF_PI_F);
        outL = s * panL;
        outR = s * panR;
    }

    // UI thread getters for the magenta Chaos CRT display
    float getPreviewSample(int i) const
    {
        return previewWave[static_cast<size_t>(std::clamp(i, 0, PREVIEW_POINTS - 1))].load(std::memory_order_relaxed);
    }
    float getPlayheadNorm() const { return playheadNorm.load(std::memory_order_relaxed); }
    bool  isGateOpen() const      { return lastGateOpen.load(std::memory_order_relaxed); }
    int   getLastStep() const     { return lastStepIdx.load(std::memory_order_relaxed); }

    void syncPreviewBank(int bankIdx)
    {
        if (bankIdx == CUSTOM_BANK_IDX && hasCustom.load(std::memory_order_acquire))
        {
            const auto& ptr = customSlots[static_cast<size_t>(customReadSlot.load(std::memory_order_acquire))];
            if (ptr) updatePreviewFromBuffer(ptr->samples);
        }
        else
        {
            const int b = std::clamp(bankIdx, 0, NUM_FACTORY_BANKS - 1);
            updatePreviewFromBuffer(factoryBanks[static_cast<size_t>(b)]);
        }
    }

private:
    static uint32_t nextRng(uint32_t r)
    {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return r;
    }

    void triggerSlice(int bankIdx, double rate, bool rev, float pan, double startNorm)
    {
        if (bankIdx == CUSTOM_BANK_IDX && hasCustom.load(std::memory_order_acquire))
        {
            const auto& ptr = customSlots[static_cast<size_t>(customReadSlot.load(std::memory_order_acquire))];
            activeBuf = ptr ? &ptr->samples : &factoryBanks[0];
        }
        else
        {
            activeBuf = &factoryBanks[static_cast<size_t>(std::clamp(bankIdx, 0, NUM_FACTORY_BANKS - 1))];
        }
        if (!activeBuf || activeBuf->empty()) return;

        playRate = std::clamp(rate, 0.25, 3.0);
        reversePlay = rev;
        panPos = pan;
        const int len = static_cast<int>(activeBuf->size());
        playPos = rev ? static_cast<double>(len - 2) : (startNorm * static_cast<double>(len));
        active = true;
    }

    void updatePreviewFromActive()
    {
        updatePreviewFromBuffer(factoryBanks[0]);
    }

    void updatePreviewFromBuffer(const std::vector<float>& buf)
    {
        if (buf.empty()) return;
        const size_t n = buf.size();
        for (int i = 0; i < PREVIEW_POINTS; ++i)
        {
            const size_t start = (static_cast<size_t>(i) * n) / PREVIEW_POINTS;
            const size_t end   = std::max(start + 1, ((static_cast<size_t>(i + 1)) * n) / PREVIEW_POINTS);
            float peak = 0.0f;
            for (size_t k = start; k < end && k < n; ++k)
                if (std::abs(buf[k]) > std::abs(peak)) peak = buf[k];
            previewWave[static_cast<size_t>(i)].store(std::clamp(peak, -1.0f, 1.0f), std::memory_order_relaxed);
        }
    }

    // Procedurally synthesize the 8 iconic Free Tekno "Tekno DNA" one-shots (#22)
    void buildFactoryBank()
    {
        const double sr = sampleRate;
        uint32_t rng = 0x2398F00Du;
        auto noise = [&]() {
            rng = nextRng(rng);
            return static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f;
        };

        auto makeBank = [&](int idx, double sec, auto&& fn) {
            const int n = std::max(64, static_cast<int>(sr * sec));
            auto& v = factoryBanks[static_cast<size_t>(idx)];
            v.resize(static_cast<size_t>(n));
            double ph1 = 0.0, ph2 = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double t = static_cast<double>(i) / sr;
                const double norm = static_cast<double>(i) / static_cast<double>(n);
                v[static_cast<size_t>(i)] = fn(t, norm, ph1, ph2);
            }
        };

        // 0: WHISTLE_98 (Rave pea-whistle trilled @ 2.8 kHz)
        makeBank(0, 0.35, [&](double t, double norm, double& p1, double& p2) {
            const double env = std::sin(std::min(1.0, norm * 8.0) * (Dsp::PI * 0.5)) * std::exp(-norm * 3.2);
            const double f = 2750.0 + 180.0 * std::sin(Dsp::TWO_PI * 28.0 * t);
            p1 += Dsp::TWO_PI * f / sr;
            p2 += Dsp::TWO_PI * (f * 1.007) / sr;
            return static_cast<float>((std::sin(p1) * 0.7 + std::sin(p2) * 0.3 + noise() * 0.12) * env);
        });

        // 1: SIREN_23 (Classic Dub/Tekno LFO siren)
        makeBank(1, 0.65, [&](double t, double norm, double& p1, double&) {
            const double env = std::exp(-norm * 2.2);
            const double f = 680.0 + 420.0 * std::sin(Dsp::TWO_PI * 6.5 * t);
            p1 += Dsp::TWO_PI * f / sr;
            return static_cast<float>(fastTanh(static_cast<float>(std::sin(p1) * 2.2)) * env * 0.85);
        });

        // 2: RADIO_VOX (Formant-modulated pirate VHF vocal burst)
        makeBank(2, 0.45, [&](double t, double norm, double& p1, double& p2) {
            const double env = (norm < 0.05 ? norm / 0.05 : std::exp(-(norm - 0.05) * 3.5));
            const double f0 = 165.0 + 35.0 * std::sin(Dsp::TWO_PI * 5.0 * t);
            p1 += f0 / sr; if (p1 >= 1.0) p1 -= 1.0;
            const double formant = 900.0 + 700.0 * std::sin(Dsp::TWO_PI * 9.0 * t);
            p2 += Dsp::TWO_PI * formant / sr;
            const double saw = 2.0 * p1 - 1.0;
            return static_cast<float>(fastTanh(static_cast<float>((saw * std::sin(p2) * 1.8 + noise() * 0.25) * 2.0)) * env * 0.85);
        });

        // 3: ACID_GLITCH (Stuttered FM digital glitch burst)
        makeBank(3, 0.28, [&](double t, double norm, double& p1, double& p2) {
            const double gate = (static_cast<int>(t * 42.0) & 1) ? 1.0 : 0.15;
            const double mod = std::sin(Dsp::TWO_PI * (340.0 + 900.0 * norm) * t);
            p1 += Dsp::TWO_PI * (520.0 + 800.0 * mod) / sr;
            p2 = gate * std::exp(-norm * 3.0);
            return static_cast<float>(std::sin(p1) * p2 * 0.9);
        });

        // 4: IND_CLANG (Inharmonic metallic pipe strike)
        makeBank(4, 0.50, [&](double, double norm, double& p1, double& p2) {
            const double env = std::exp(-norm * 5.5);
            p1 += Dsp::TWO_PI * 487.0 / sr;
            p2 += Dsp::TWO_PI * 1319.0 / sr;
            return static_cast<float>(fastTanh(static_cast<float>((std::sin(p1) * std::sin(p2) * 2.5 + noise() * 0.2))) * env);
        });

        // 5: LASER_8B (Arcade Space Invaders downward zap)
        makeBank(5, 0.24, [&](double, double norm, double& p1, double&) {
            const double f = 2400.0 * std::exp(-norm * 3.2) + 180.0;
            p1 += f / sr; if (p1 >= 1.0) p1 -= 1.0;
            const double sq = (p1 < 0.35) ? 1.0 : -1.0;
            return static_cast<float>(sq * std::exp(-norm * 2.0) * 0.80);
        });

        // 6: TRAIN_HRN (Dissonant industrial chord horn)
        makeBank(6, 0.55, [&](double, double norm, double& p1, double& p2) {
            const double env = std::min(1.0, norm * 12.0) * std::exp(-norm * 2.4);
            p1 += Dsp::TWO_PI * 311.0 / sr;
            p2 += Dsp::TWO_PI * 392.0 / sr;
            return static_cast<float>(fastTanh(static_cast<float>((std::sin(p1) + std::sin(p2) + std::sin(p1 * 1.498)) * 1.4)) * env * 0.85);
        });

        // 7: METAL_SCRP (Filtered metallic noise sweep)
        makeBank(7, 0.38, [&](double t, double norm, double& p1, double&) {
            const double env = std::exp(-norm * 4.0);
            p1 += Dsp::TWO_PI * (1800.0 - 900.0 * norm) / sr;
            return static_cast<float>((noise() * 0.6 + std::sin(p1) * 0.5) * env * (0.7 + 0.3 * std::sin(Dsp::TWO_PI * 18.0 * t)));
        });
    }

    double sampleRate = 44100.0;
    std::array<std::vector<float>, NUM_FACTORY_BANKS> factoryBanks;
    std::array<std::shared_ptr<SampleData>, 2> customSlots;
    std::atomic<int>  customReadSlot { 0 };
    std::atomic<bool> hasCustom { false };

    const std::vector<float>* activeBuf = nullptr;
    bool   active = false;
    bool   reversePlay = false;
    double playPos = 0.0;
    double playRate = 1.0;
    float  panPos = 0.5f;

    uint64_t eucMask = 0xFFFFULL;
    float gateEnv = 1.0f, gateTarget = 1.0f, gateAlpha = 0.01f;

    float hpAlpha = 0.94f, hp1State = 0.0f, hp1Prev = 0.0f, hp2State = 0.0f, hp2Prev = 0.0f;
    float bpLp = 0.0f, bpBp = 0.0f;
    float dcAlpha = 0.997f, dcPrev = 0.0f, dcOut = 0.0f;
    float srHold = 0.0f;
    int   srCounter = 0;

    std::array<std::atomic<float>, PREVIEW_POINTS> previewWave {};
    std::atomic<float> playheadNorm { -1.0f };
    std::atomic<bool>  lastGateOpen { true };
    std::atomic<int>   lastStepIdx  { 0 };
};
