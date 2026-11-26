// Hi-hat probe: verifies (1) the square oscillators are band-limited
// (polyBLEP) so they no longer alias, and (2) velocity meaningfully brightens
// the timbre. The old hard `phase < PI` squares dumped huge alias energy into
// the low band when tone was high (top partial sat at ~52kHz).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "voices/DspConstants.h"
#include "voices/HatsVoice.h"

namespace
{
constexpr double sampleRate = 44100.0;

double rms(const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x) s += static_cast<double>(v) * v;
    return std::sqrt(s / std::max<size_t>(1, x.size()));
}

// Energy in the low band (below fc) via a 2nd-order lowpass. A band-limited
// square has almost no energy below its fundamental; an aliased one does,
// because partials above Nyquist fold down into this band.
double lowBandRms(const std::vector<float>& x, double fc)
{
    const float a = 1.0f - static_cast<float>(std::exp(-2.0 * M_PI * fc / sampleRate));
    float s1 = 0.0f, s2 = 0.0f;
    double sum = 0.0;
    for (float v : x) { s1 += a * (v - s1); s2 += a * (s1 - s2); sum += static_cast<double>(s2) * s2; }
    return std::sqrt(sum / std::max<size_t>(1, x.size()));
}

double hfRms(const std::vector<float>& x, double fc)
{
    const float a = static_cast<float>(std::exp(-2.0 * M_PI * fc / sampleRate));
    float hp = 0.0f, prev = 0.0f;
    double s = 0.0;
    for (float v : x) { hp = a * (hp + v - prev); prev = v; s += static_cast<double>(hp) * hp; }
    return std::sqrt(s / std::max<size_t>(1, x.size()));
}

std::vector<float> renderHat(float velocity)
{
    HatsVoice h;
    h.prepare(sampleRate);
    h.setParams({ 0.40f, 1.0f, 0.30f });   // open, bright
    h.trigger(velocity);
    std::vector<float> b(static_cast<size_t>(sampleRate * 0.5));
    for (auto& s : b) s = h.tick();
    return b;
}

bool check(bool ok, const char* name) { std::printf("%s: %s\n", name, ok ? "ok" : "FAIL"); return ok; }
}

int main()
{
    const int N = 8000;
    const double f0 = 15000.0;   // above sr/4 — a hard square aliases badly here

    std::vector<float> hard(N), blep(N);
    {
        double ph = 0.0;
        for (int i = 0; i < N; ++i) { ph += f0 / sampleRate; if (ph >= 1.0) ph -= 1.0; hard[i] = (ph < 0.5) ? 1.0f : -1.0f; }
    }
    {
        float ph = 0.0f;
        const float dt = static_cast<float>(f0 / sampleRate);
        for (int i = 0; i < N; ++i)
        {
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            float sq = (ph < 0.5f) ? 1.0f : -1.0f;
            sq += polyBlep(ph, dt);
            float t2 = ph + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
            sq -= polyBlep(t2, dt);
            blep[i] = sq;
        }
    }

    const double hardLo = lowBandRms(hard, 5000.0);
    const double blepLo = lowBandRms(blep, 5000.0);

    const auto hi = renderHat(1.0f);
    const auto lo = renderHat(0.3f);
    const double hiHf = hfRms(hi, 8000.0);
    const double loHf = hfRms(lo, 8000.0);

    std::printf("15kHz square low-band(<5k) RMS: hard=%.4f bandlimited=%.4f reduction=%.1fx\n",
                hardLo, blepLo, hardLo / std::max(1e-9, blepLo));
    std::printf("hat vel=1.0 rms=%.4f hf=%.4f | vel=0.3 rms=%.4f hf=%.4f\n",
                rms(hi), hiHf, rms(lo), loHf);

    bool finite = true, bounded = true;
    for (float v : hi) { finite = finite && std::isfinite(v); bounded = bounded && std::fabs(v) < 1.5f; }

    bool ok = true;
    ok &= check(finite && bounded, "hat finite + bounded");
    ok &= check(blepLo * 3.0 < hardLo, "band-limiting cuts alias energy");
    ok &= check(hiHf > loHf * 1.15, "velocity brightens timbre");

    return ok ? 0 : 1;
}
