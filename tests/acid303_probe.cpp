#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "voices/BassVoice.h"

namespace
{
constexpr double sampleRate = 44100.0;

struct Metrics
{
    float peak = 0.0f;
    double rms = 0.0;
    double first80msRms = 0.0;
    int zeroCrossings = 0;
    double hfRatio = 0.0;   // resonant emphasis: HF energy / total energy
    bool finite = true;
};

Metrics analyse(const std::vector<float>& audio)
{
    Metrics m;
    double sum = 0.0;
    double early = 0.0;
    double hfSum = 0.0;
    const int earlyCount = std::min<int>(static_cast<int>(audio.size()), static_cast<int>(sampleRate * 0.08));
    float prev = 0.0f;

    // One-pole high-pass @ 800Hz — measures how much energy sits ABOVE the bass
    // fundamental. A working resonant filter pushes a peak up there as the
    // cutoff opens; a dead filter leaves the signal dominated by the fundamental.
    const float aHP = static_cast<float>(std::exp(-2.0 * M_PI * 800.0 / sampleRate));
    float hpPrev = 0.0f, hp = 0.0f;

    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
    {
        const float s = audio[static_cast<size_t>(i)];
        m.finite = m.finite && std::isfinite(s);
        m.peak = std::max(m.peak, std::abs(s));
        sum += static_cast<double>(s) * static_cast<double>(s);
        if (i < earlyCount)
            early += static_cast<double>(s) * static_cast<double>(s);
        if ((s >= 0.0f && prev < 0.0f) || (s < 0.0f && prev >= 0.0f))
            ++m.zeroCrossings;
        prev = s;

        hp = aHP * (hp + s - hpPrev);
        hpPrev = s;
        hfSum += static_cast<double>(hp) * static_cast<double>(hp);
    }

    m.rms = std::sqrt(sum / std::max<size_t>(1, audio.size()));
    m.first80msRms = std::sqrt(early / std::max(1, earlyCount));
    m.hfRatio = std::sqrt(hfSum / std::max<size_t>(1, audio.size())) / std::max(1e-9, m.rms);
    return m;
}

std::vector<float> renderNote(const BassVoice::Params& params, bool accent, float waveMix = 0.0f)
{
    BassVoice voice;
    voice.prepare(sampleRate);
    voice.setWaveMix(waveMix);
    voice.setSubMix(0.0f);
    voice.setParams(params);
    voice.trigger303(48, 1.0f, false, accent);

    std::vector<float> audio(static_cast<size_t>(sampleRate * 0.45));
    for (auto& s : audio)
        s = voice.tick();
    return audio;
}

bool check(bool ok, const char* name)
{
    std::printf("%s: %s\n", name, ok ? "ok" : "FAIL");
    return ok;
}
}

int main()
{
    BassVoice::Params lowRes { 420.0f, 0.12f, 0.45f, 0.22f, 0.0f };
    BassVoice::Params highRes { 420.0f, 0.88f, 0.45f, 0.22f, 0.0f };
    BassVoice::Params accentParams { 420.0f, 0.62f, 0.55f, 0.20f, 0.85f };

    const auto normalAudio = renderNote(accentParams, false);
    const auto accentAudio = renderNote(accentParams, true);
    const auto lowResAudio = renderNote(lowRes, false);
    const auto highResAudio = renderNote(highRes, false);

    const auto normal = analyse(normalAudio);
    const auto accent = analyse(accentAudio);
    const auto low = analyse(lowResAudio);
    const auto high = analyse(highResAudio);

    BassVoice slide;
    slide.prepare(sampleRate);
    slide.setParams({ 360.0f, 0.70f, 0.55f, 0.28f, 0.75f });
    slide.trigger303(48, 1.0f, false, true);
    std::vector<float> slideAudio(static_cast<size_t>(sampleRate * 0.35));
    for (int i = 0; i < static_cast<int>(slideAudio.size()); ++i)
    {
        if (i == static_cast<int>(sampleRate * 0.10))
            slide.trigger303(60, 1.0f, true, false);
        slideAudio[static_cast<size_t>(i)] = slide.tick();
    }
    const auto slideMetrics = analyse(slideAudio);

    std::printf("normal early_rms=%.6f peak=%.6f zc=%d\n", normal.first80msRms, normal.peak, normal.zeroCrossings);
    std::printf("accent early_rms=%.6f peak=%.6f zc=%d\n", accent.first80msRms, accent.peak, accent.zeroCrossings);
    std::printf("low_res  rms=%.6f peak=%.6f zc=%d hf=%.4f\n", low.rms, low.peak, low.zeroCrossings, low.hfRatio);
    std::printf("high_res rms=%.6f peak=%.6f zc=%d hf=%.4f\n", high.rms, high.peak, high.zeroCrossings, high.hfRatio);
    std::printf("slide    rms=%.6f peak=%.6f zc=%d\n", slideMetrics.rms, slideMetrics.peak, slideMetrics.zeroCrossings);
    std::printf("reso emphasis ratio hf_high/hf_low = %.3f\n", high.hfRatio / std::max(1e-9, low.hfRatio));

    bool ok = true;
    ok &= check(normal.finite && accent.finite && low.finite && high.finite && slideMetrics.finite, "finite output");
    ok &= check(accent.first80msRms > normal.first80msRms * 1.18, "accent lift");
    // A real resonant filter must visibly reshape the spectrum when the reso
    // knob is opened — not just nudge the level by a few percent. Cranking reso
    // 0.12 → 0.88 should push substantially more energy into the resonant band.
    ok &= check(high.hfRatio > low.hfRatio * 1.6, "resonance reshapes spectrum");
    ok &= check(high.zeroCrossings > low.zeroCrossings * 1.25, "resonance adds HF motion");
    ok &= check(slideMetrics.peak > 0.04f && slideMetrics.peak < 0.98f, "slide bounded");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
