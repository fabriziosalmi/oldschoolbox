#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/GlueCompressor.h"
#include "core/MasterDrive.h"
#include "core/MasterLimiter.h"

namespace
{
constexpr double sampleRate = 44100.0;

struct Metrics
{
    float peak = 0.0f;
    double rms = 0.0;
    double mean = 0.0;
    double edge = 0.0;
    double crest = 0.0;
    bool finite = true;
};

Metrics analyse(const std::vector<float>& audio)
{
    Metrics m;
    double sum = 0.0;
    double diff = 0.0;
    float prev = 0.0f;

    for (float s : audio)
    {
        m.finite = m.finite && std::isfinite(s);
        m.peak = std::max(m.peak, std::abs(s));
        m.mean += s;
        sum += static_cast<double>(s) * static_cast<double>(s);
        const double d = static_cast<double>(s) - static_cast<double>(prev);
        diff += d * d;
        prev = s;
    }

    m.mean /= std::max<size_t>(1, audio.size());
    m.rms = std::sqrt(sum / std::max<size_t>(1, audio.size()));
    m.edge = std::sqrt(diff / std::max<size_t>(1, audio.size()));
    m.crest = static_cast<double>(m.peak) / std::max(1.0e-6, m.rms);
    return m;
}

std::vector<float> render(float driveAmount, float compAmount, float inputGain)
{
    MasterDrive drive;
    GlueCompressor comp;
    MasterLimiter limiter;
    drive.prepare(sampleRate);
    comp.prepare(sampleRate);
    comp.setAmount(compAmount);
    limiter.prepare(sampleRate);
    limiter.setThreshold(0.90f);
    limiter.setCeiling(0.975f);
    limiter.setHeadroomDb(-1.5f);

    std::vector<float> out(static_cast<size_t>(sampleRate * 0.6));
    double ph1 = 0.0;
    double ph2 = 0.0;
    for (int i = 0; i < static_cast<int>(out.size()); ++i)
    {
        ph1 += 2.0 * M_PI * 55.0 / sampleRate;
        ph2 += 2.0 * M_PI * 730.0 / sampleRate;
        if (ph1 >= 2.0 * M_PI) ph1 -= 2.0 * M_PI;
        if (ph2 >= 2.0 * M_PI) ph2 -= 2.0 * M_PI;

        float L = inputGain * static_cast<float>(std::sin(ph1) * 0.72 + std::sin(ph2) * 0.14);
        float R = inputGain * static_cast<float>(std::sin(ph1 + 0.2) * 0.72 + std::sin(ph2 + 0.5) * 0.14);
        drive.process(L, R, driveAmount);
        comp.process(L, R);
        limiter.process(L, R);
        out[static_cast<size_t>(i)] = (L + R) * 0.5f;
    }
    return out;
}

std::vector<float> renderPulsed(float compAmount)
{
    GlueCompressor comp;
    comp.prepare(sampleRate);
    comp.setAmount(compAmount);

    std::vector<float> out(static_cast<size_t>(sampleRate * 0.9));
    double tone = 0.0;
    double kick = 0.0;
    for (int i = 0; i < static_cast<int>(out.size()); ++i)
    {
        tone += 2.0 * M_PI * 92.0 / sampleRate;
        kick += 2.0 * M_PI * 54.0 / sampleRate;
        if (tone >= 2.0 * M_PI) tone -= 2.0 * M_PI;
        if (kick >= 2.0 * M_PI) kick -= 2.0 * M_PI;

        const int step = i % static_cast<int>(sampleRate * 0.125);
        const float hitEnv = std::exp(-static_cast<float>(step) / static_cast<float>(sampleRate * 0.018));
        const float bed = 0.25f * static_cast<float>(std::sin(tone));
        const float hit = hitEnv * static_cast<float>(std::sin(kick)) * 1.35f;
        float L = bed + hit;
        float R = bed * 0.96f + hit * 0.94f;
        comp.process(L, R);
        out[static_cast<size_t>(i)] = (L + R) * 0.5f;
    }
    return out;
}

bool check(bool ok, const char* name)
{
    std::printf("%s: %s\n", name, ok ? "ok" : "FAIL");
    return ok;
}
}

int main()
{
    const auto clean = analyse(render(0.0f, 0.0f, 0.95f));
    const auto driven = analyse(render(0.85f, 0.0f, 0.95f));
    const auto hot = analyse(render(1.0f, 0.0f, 3.20f));
    const auto uncompressedPulse = analyse(renderPulsed(0.0f));
    const auto compressedPulse = analyse(renderPulsed(0.72f));

    std::printf("clean peak=%.6f rms=%.6f crest=%.6f edge=%.6f mean=%.8f\n", clean.peak, clean.rms, clean.crest, clean.edge, clean.mean);
    std::printf("driven peak=%.6f rms=%.6f crest=%.6f edge=%.6f mean=%.8f\n", driven.peak, driven.rms, driven.crest, driven.edge, driven.mean);
    std::printf("hot peak=%.6f rms=%.6f crest=%.6f edge=%.6f mean=%.8f\n", hot.peak, hot.rms, hot.crest, hot.edge, hot.mean);
    std::printf("pulse raw peak=%.6f rms=%.6f crest=%.6f comp peak=%.6f rms=%.6f crest=%.6f\n",
                uncompressedPulse.peak, uncompressedPulse.rms, uncompressedPulse.crest,
                compressedPulse.peak, compressedPulse.rms, compressedPulse.crest);

    bool ok = true;
    ok &= check(clean.finite && driven.finite && hot.finite && uncompressedPulse.finite && compressedPulse.finite, "finite output");
    ok &= check(std::abs(driven.mean) < 0.010 && std::abs(hot.mean) < 0.015, "dc controlled");
    ok &= check(driven.edge > clean.edge * 1.18, "drive adds edge");
    ok &= check(hot.peak < 1.0f, "limiter bounded");
    ok &= check(hot.rms > clean.rms * 0.75, "hot signal not crushed silent");
    ok &= check(compressedPulse.peak < uncompressedPulse.peak * 0.95, "compressor controls peaks");
    ok &= check(compressedPulse.rms > uncompressedPulse.rms * 0.82, "compressor keeps body");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
