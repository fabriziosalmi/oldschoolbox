#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include "../voices/DspConstants.h"

// Spiral Tribe / Freetekno performance FX for PERC 1 + PERC 2 + CHAOS Samples:
// 1) 12 dB/oct HPF @ 300 Hz on send (zero low-end mud)
// 2) Beat-synced metallic Jet Flanger (0.6 - 6.2 ms, resonant feedback)
// 3) Adjustable Distortion / Grit stage (0..100% range)
// 4) Beat-synced 3/16 Gated Decaying Dub Delay (each repeat chopped at 1/16 & fading down cleanly)
// Trigger: Click = 2-beat Throw (captures hits, then lets gated spiral tail decay naturally)
//          Shift+Click = Latch ON/OFF continuous
class SpiralFx
{
public:
    void prepare(double sr)
    {
        sampleRate = std::max(8000.0, sr);

        // Flanger buffer (~25 ms max)
        flangeSize = std::max(64, static_cast<int>(std::ceil(sampleRate * 0.025)) + 4);
        flangeL.assign(static_cast<size_t>(flangeSize), 0.0f);
        flangeR.assign(static_cast<size_t>(flangeSize), 0.0f);
        flangeWrite = 0;

        // Dub delay buffer (2.0 sec max)
        delaySize = std::max(128, static_cast<int>(std::ceil(sampleRate * 2.0)) + 4);
        delayL.assign(static_cast<size_t>(delaySize), 0.0f);
        delayR.assign(static_cast<size_t>(delaySize), 0.0f);
        delayWrite = 0;
        smoothedDelay = static_cast<float>(sampleRate * 0.15);

        // Filters & smoothers
        hpAlpha     = static_cast<float>(std::exp(-Dsp::TWO_PI * 300.0 / sampleRate));
        lpAlpha     = 1.0f - static_cast<float>(std::exp(-Dsp::TWO_PI * 5800.0 / sampleRate));
        sendAlpha   = 1.0f - static_cast<float>(std::exp(-1.0 / (0.005 * sampleRate)));
        gateAlpha   = 1.0f - static_cast<float>(std::exp(-1.0 / (0.0025 * sampleRate)));
        delayAlpha  = 1.0f - static_cast<float>(std::exp(-1.0 / (0.008 * sampleRate)));

        resetStates();
    }

    void resetStates()
    {
        hp1L = hp1PrevL = hp2L = hp2PrevL = 0.0f;
        hp1R = hp1PrevR = hp2R = hp2PrevR = 0.0f;
        fbHpL = fbHpPrevL = fbHpR = fbHpPrevR = 0.0f;
        fbLpL = fbLpR = 0.0f;
        sendEnv = 0.0f;
        gateEnv = 1.0f;
        tailEnergy = 0.0f;
        throwSamplesLeft = 0;
    }

    // UI thread: 1-click momentary Throw (captures 2 beats, then spirals down in volume)
    void triggerThrow()
    {
        if (latched.load(std::memory_order_acquire))
        {
            latched.store(false, std::memory_order_release);
            return;
        }
        throwRequested.store(true, std::memory_order_release);
    }

    // UI thread: Shift+Click toggles continuous Latch
    void toggleLatch()
    {
        const bool next = !latched.load(std::memory_order_acquire);
        latched.store(next, std::memory_order_release);
    }

    bool isLatched() const { return latched.load(std::memory_order_relaxed); }
    bool isActive()  const { return activeVisual.load(std::memory_order_relaxed); }

    // Audio thread: processes PERC1+PERC2+CHAOS input and ADDS spiral wet return into outL/outR
    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 const float* fxDuckBuf,
                 int numSamples, float bpm, double startPpq, float driveAmt)
    {
        const float safeBpm = std::clamp(bpm, 40.0f, 240.0f);
        const double beatSec = 60.0 / static_cast<double>(safeBpm);

        if (throwRequested.exchange(false, std::memory_order_acq_rel))
            throwSamplesLeft = static_cast<int>(std::round(beatSec * 2.0 * sampleRate)); // 2-beat capture window

        const bool isLatch = latched.load(std::memory_order_acquire);
        const bool wantSend = isLatch || (throwSamplesLeft > 0);

        if (!wantSend && tailEnergy < 1.0e-5f)
        {
            activeVisual.store(false, std::memory_order_relaxed);
            return;
        }

        // Beat-synced 3/16 (dotted-16th) dub delay time
        const float targetDelay = std::clamp(static_cast<float>(beatSec * 0.375 * sampleRate),
                                             16.0f, static_cast<float>(delaySize - 3));
        const double ppqPerSample = static_cast<double>(safeBpm) / (60.0 * sampleRate);
        const float drv = std::clamp(driveAmt, 0.0f, 1.0f);
        const float driveGain = 1.0f + drv * 7.5f;         // up to ~8.5x saturation
        const float driveComp = 1.0f / (1.0f + drv * 0.58f); // balanced loudness across 0..100%

        float blockPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const bool sending = isLatch || (throwSamplesLeft > 0);
            if (throwSamplesLeft > 0) --throwSamplesLeft;

            const float targetSend = sending ? 1.0f : 0.0f;
            sendEnv += sendAlpha * (targetSend - sendEnv);

            // 1. 12 dB/oct HPF @ 300 Hz on Percs+Samples send
            const float rawL = inL[i] * sendEnv;
            const float rawR = inR[i] * sendEnv;
            hp1L = hpAlpha * (hp1L + rawL - hp1PrevL); hp1PrevL = rawL;
            hp2L = hpAlpha * (hp2L + hp1L - hp2PrevL); hp2PrevL = hp1L;
            hp1R = hpAlpha * (hp1R + rawR - hp1PrevR); hp1PrevR = rawR;
            hp2R = hpAlpha * (hp2R + hp1R - hp2PrevR); hp2PrevR = hp1R;

            // 2. Beat-synced Jet Flanger (2-bar sweep, 0.6 - 6.2 ms, stereo phase offset)
            const double curPpq = startPpq + static_cast<double>(i) * ppqPerSample;
            const float flPhaseL = static_cast<float>(std::fmod(curPpq * 0.125, 1.0)); // 1 cycle per 2 bars (8 beats)
            const float flPhaseR = static_cast<float>(std::fmod(curPpq * 0.125 + 0.18, 1.0));
            const float lfoL = 0.5f - 0.5f * std::cos(flPhaseL * Dsp::TWO_PI_F);
            const float lfoR = 0.5f - 0.5f * std::cos(flPhaseR * Dsp::TWO_PI_F);

            const float flDelL = (0.0006f + lfoL * 0.0056f) * static_cast<float>(sampleRate);
            const float flDelR = (0.0006f + lfoR * 0.0056f) * static_cast<float>(sampleRate);

            auto readInterp = [](const std::vector<float>& buf, int wPos, int sz, float del) -> float {
                const int di = static_cast<int>(del);
                const float fr = del - static_cast<float>(di);
                const int r0 = (wPos - di + sz) % sz;
                const int r1 = (r0 - 1 + sz) % sz;
                return buf[static_cast<size_t>(r0)] * (1.0f - fr) + buf[static_cast<size_t>(r1)] * fr;
            };

            const float flTapL = readInterp(flangeL, flangeWrite, flangeSize, flDelL);
            const float flTapR = readInterp(flangeR, flangeWrite, flangeSize, flDelR);

            // Resonant metallic jet flange feedback (soft-limited + NaN/Inf safe)
            const float flWrL = std::tanh(hp2L + flTapL * 0.64f);
            const float flWrR = std::tanh(hp2R + flTapR * 0.64f);
            flangeL[static_cast<size_t>(flangeWrite)] = std::isfinite(flWrL) ? flWrL : 0.0f;
            flangeR[static_cast<size_t>(flangeWrite)] = std::isfinite(flWrR) ? flWrR : 0.0f;
            flangeWrite = (flangeWrite + 1) % flangeSize;

            float fL = (hp2L + flTapL * 0.85f) * 0.65f;
            float fR = (hp2R + flTapR * 0.85f) * 0.65f;

            // 3. Distortion / Grit range (0..100%, asymmetric analog clip + subtle fold)
            if (drv > 0.002f)
            {
                const float bL = fL * driveGain + drv * 0.08f;
                const float bR = fR * driveGain + drv * 0.08f;
                fL = (std::tanh(bL) - std::tanh(drv * 0.08f)) * driveComp;
                fR = (std::tanh(bR) - std::tanh(drv * 0.08f)) * driveComp;
            }

            // 4. Beat-synced 3/16 Decaying Dub Delay with 300 Hz HPF + 5.8 kHz LP damping in feedback
            smoothedDelay += delayAlpha * (targetDelay - smoothedDelay);
            const float dL = readInterp(delayL, delayWrite, delaySize, smoothedDelay);
            const float dR = readInterp(delayR, delayWrite, delaySize, smoothedDelay);

            const float fbAmt = isLatch ? 0.80f : 0.76f; // clean progressive volume decay
            const float fbInL = fL + dL * fbAmt;
            const float fbInR = fR + dR * fbAmt;

            fbHpL = hpAlpha * (fbHpL + fbInL - fbHpPrevL); fbHpPrevL = fbInL;
            fbHpR = hpAlpha * (fbHpR + fbInR - fbHpPrevR); fbHpPrevR = fbInR;
            fbLpL += lpAlpha * (fbHpL - fbLpL);
            fbLpR += lpAlpha * (fbHpR - fbLpR);

            const float dlWrL = std::tanh(fbLpL * 1.05f);
            const float dlWrR = std::tanh(fbLpR * 1.05f);
            delayL[static_cast<size_t>(delayWrite)] = std::isfinite(dlWrL) ? dlWrL : 0.0f;
            delayR[static_cast<size_t>(delayWrite)] = std::isfinite(dlWrR) ? dlWrR : 0.0f;
            delayWrite = (delayWrite + 1) % delaySize;

            // 5. Beat-synced 1/16 Rhythmic Gate on the decaying spiral wet return (anti-click smoothed)
            const float gatePhase = static_cast<float>(std::fmod(curPpq * 4.0, 1.0)); // 16th-note pulse
            const float targetGate = (gatePhase < 0.56f) ? 1.0f : 0.08f; // deep rhythmic chop (leaves tiny air)
            gateEnv += gateAlpha * (targetGate - gateEnv);

            const float wetL = (fL * 0.45f + dL * 0.92f) * gateEnv;
            const float wetR = (fR * 0.45f + dR * 0.92f) * gateEnv;

            // Balanced mono-leaning return (Mid + 35% Side) + subtle kick ducking
            const float mid  = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * 0.35f;
            const float duck = (fxDuckBuf != nullptr) ? (0.45f + 0.55f * fxDuckBuf[i]) : 1.0f;

            const float addL = (mid + side) * duck * 0.85f;
            const float addR = (mid - side) * duck * 0.85f;
            outL[i] += addL;
            outR[i] += addR;

            blockPeak = std::max(blockPeak, std::max(std::abs(dL), std::abs(dR)));
            blockPeak = std::max(blockPeak, std::max(std::abs(addL), std::abs(addR)));
        }

        tailEnergy = blockPeak;
        activeVisual.store(wantSend || (tailEnergy > 0.008f), std::memory_order_relaxed);
    }

private:
    double sampleRate = 44100.0;

    std::vector<float> flangeL, flangeR;
    int flangeSize = 64, flangeWrite = 0;

    std::vector<float> delayL, delayR;
    int delaySize = 128, delayWrite = 0;
    float smoothedDelay = 6000.0f;

    float hpAlpha = 0.95f, lpAlpha = 0.45f;
    float sendAlpha = 0.004f, gateAlpha = 0.01f, delayAlpha = 0.003f;

    float hp1L = 0.0f, hp1PrevL = 0.0f, hp2L = 0.0f, hp2PrevL = 0.0f;
    float hp1R = 0.0f, hp1PrevR = 0.0f, hp2R = 0.0f, hp2PrevR = 0.0f;
    float fbHpL = 0.0f, fbHpPrevL = 0.0f, fbHpR = 0.0f, fbHpPrevR = 0.0f;
    float fbLpL = 0.0f, fbLpR = 0.0f;

    float sendEnv = 0.0f;
    float gateEnv = 1.0f;
    float tailEnergy = 0.0f;
    int   throwSamplesLeft = 0;

    std::atomic<bool> throwRequested { false };
    std::atomic<bool> latched        { false };
    std::atomic<bool> activeVisual   { false };
};
