#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "voices/KickVoice.h"

namespace
{
constexpr double sampleRate = 44100.0;

struct Metrics
{
    float peak = 0.0f;
    double rms = 0.0;
    double mean = 0.0;
    double earlyRms = 0.0;
    double bodyRms = 0.0;
    double tailRms = 0.0;
    double diffRms = 0.0;
    int zeroCrossings = 0;
    bool finite = true;
};

Metrics analyse(const std::vector<float>& audio)
{
    Metrics m;
    double sum = 0.0;
    double early = 0.0;
    double body = 0.0;
    double tail = 0.0;
    double diff = 0.0;
    const int earlyEnd = std::min<int>(static_cast<int>(audio.size()), static_cast<int>(sampleRate * 0.030));
    const int bodyStart = std::min<int>(static_cast<int>(audio.size()), static_cast<int>(sampleRate * 0.045));
    const int bodyEnd = std::min<int>(static_cast<int>(audio.size()), static_cast<int>(sampleRate * 0.160));
    const int tailStart = std::min<int>(static_cast<int>(audio.size()), static_cast<int>(sampleRate * 0.220));
    float prev = 0.0f;

    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
    {
        const float s = audio[static_cast<size_t>(i)];
        m.finite = m.finite && std::isfinite(s);
        m.peak = std::max(m.peak, std::abs(s));
        m.mean += s;
        sum += static_cast<double>(s) * static_cast<double>(s);
        if (i < earlyEnd)
            early += static_cast<double>(s) * static_cast<double>(s);
        if (i >= bodyStart && i < bodyEnd)
            body += static_cast<double>(s) * static_cast<double>(s);
        if (i >= tailStart)
            tail += static_cast<double>(s) * static_cast<double>(s);
        if ((s >= 0.0f && prev < 0.0f) || (s < 0.0f && prev >= 0.0f))
            ++m.zeroCrossings;
        const double d = static_cast<double>(s) - static_cast<double>(prev);
        diff += d * d;
        prev = s;
    }

    m.mean /= std::max<size_t>(1, audio.size());
    m.rms = std::sqrt(sum / std::max<size_t>(1, audio.size()));
    m.earlyRms = std::sqrt(early / std::max(1, earlyEnd));
    m.bodyRms = std::sqrt(body / std::max(1, bodyEnd - bodyStart));
    m.tailRms = std::sqrt(tail / std::max<int>(1, static_cast<int>(audio.size()) - tailStart));
    m.diffRms = std::sqrt(diff / std::max<size_t>(1, audio.size()));
    return m;
}

std::vector<float> render(const KickVoice::Params& params)
{
    KickVoice voice;
    voice.prepare(sampleRate);
    voice.setParams(params);
    voice.trigger();

    std::vector<float> audio(static_cast<size_t>(sampleRate * 1.1));
    for (auto& s : audio)
        s = voice.tick();
    return audio;
}

std::vector<float> renderRetrigger(const KickVoice::Params& params)
{
    KickVoice voice;
    voice.prepare(sampleRate);
    voice.setParams(params);

    std::vector<float> audio(static_cast<size_t>(sampleRate * 0.75));
    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
    {
        if (i == 0 || i == static_cast<int>(sampleRate * 0.125) || i == static_cast<int>(sampleRate * 0.250))
            voice.trigger();
        audio[static_cast<size_t>(i)] = voice.tick();
    }
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
    const auto baseAudio = render({ 58.0f, 0.50f, 0.45f, 0.75f, 0.35f, 0.55f });
    const auto lowPunchAudio = render({ 58.0f, 0.50f, 0.12f, 0.75f, 0.35f, 0.55f });
    const auto highPunchAudio = render({ 58.0f, 0.50f, 0.92f, 0.75f, 0.35f, 0.55f });
    const auto lowSubAudio = render({ 58.0f, 0.50f, 0.45f, 0.75f, 0.35f, 0.10f });
    const auto highSubAudio = render({ 58.0f, 0.50f, 0.45f, 0.75f, 0.35f, 0.95f });
    const auto lowDriveAudio = render({ 58.0f, 0.50f, 0.55f, 0.75f, 0.05f, 0.55f });
    const auto highDriveAudio = render({ 58.0f, 0.50f, 0.55f, 0.75f, 0.92f, 0.55f });
    const auto longAudio = render({ 52.0f, 0.82f, 0.50f, 0.70f, 0.45f, 0.75f });
    const auto lowTuneAudio = render({ 42.0f, 0.55f, 0.48f, 0.65f, 0.40f, 0.70f });
    const auto highTuneAudio = render({ 96.0f, 0.55f, 0.48f, 0.65f, 0.40f, 0.70f });
    const auto retriggerAudio = renderRetrigger({ 56.0f, 0.52f, 0.60f, 0.80f, 0.55f, 0.65f });

    const auto base = analyse(baseAudio);
    const auto lowPunch = analyse(lowPunchAudio);
    const auto highPunch = analyse(highPunchAudio);
    const auto lowSub = analyse(lowSubAudio);
    const auto highSub = analyse(highSubAudio);
    const auto lowDrive = analyse(lowDriveAudio);
    const auto highDrive = analyse(highDriveAudio);
    const auto longKick = analyse(longAudio);
    const auto lowTune = analyse(lowTuneAudio);
    const auto highTune = analyse(highTuneAudio);
    const auto retrigger = analyse(retriggerAudio);

    std::printf("base peak=%.6f rms=%.6f early=%.6f body=%.6f tail=%.6f zc=%d mean=%.8f\n",
                base.peak, base.rms, base.earlyRms, base.bodyRms, base.tailRms, base.zeroCrossings, base.mean);
    std::printf("low_punch early=%.6f high_punch peak=%.6f early=%.6f body=%.6f\n",
                lowPunch.earlyRms, highPunch.peak, highPunch.earlyRms, highPunch.bodyRms);
    std::printf("low_sub tail=%.6f high_sub tail=%.6f\n", lowSub.tailRms, highSub.tailRms);
    std::printf("low_drive rms=%.6f edge=%.6f zc=%d high_drive rms=%.6f edge=%.6f zc=%d peak=%.6f\n",
                lowDrive.rms, lowDrive.diffRms, lowDrive.zeroCrossings,
                highDrive.rms, highDrive.diffRms, highDrive.zeroCrossings, highDrive.peak);
    std::printf("long tail=%.6f peak=%.6f\n", longKick.tailRms, longKick.peak);
    std::printf("low_tune zc=%d tail=%.6f high_tune zc=%d tail=%.6f retrigger peak=%.6f mean=%.8f\n",
                lowTune.zeroCrossings, lowTune.tailRms, highTune.zeroCrossings, highTune.tailRms,
                retrigger.peak, retrigger.mean);

    bool ok = true;
    ok &= check(base.finite && lowPunch.finite && highPunch.finite && lowSub.finite && highSub.finite && lowDrive.finite && highDrive.finite && longKick.finite && lowTune.finite && highTune.finite && retrigger.finite, "finite output");
    ok &= check(base.peak > 0.12f && base.peak < 0.985f, "base bounded and alive");
    ok &= check(std::abs(base.mean) < 0.010, "dc controlled");
    ok &= check(highPunch.earlyRms > lowPunch.earlyRms * 1.28, "punch lifts transient");
    ok &= check(highSub.tailRms > lowSub.tailRms * 1.35, "sub lifts tail");
    ok &= check(highDrive.diffRms > lowDrive.diffRms * 1.08 || highDrive.zeroCrossings > lowDrive.zeroCrossings * 1.06, "drive adds color");
    ok &= check(highDrive.peak < 0.985f, "drive bounded");
    ok &= check(longKick.tailRms > base.tailRms * 1.25, "decay extends tail");
    ok &= check(highTune.zeroCrossings > lowTune.zeroCrossings * 1.25, "tune changes pitch");
    ok &= check(retrigger.peak < 0.985f && std::abs(retrigger.mean) < 0.012, "retrigger stable");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
