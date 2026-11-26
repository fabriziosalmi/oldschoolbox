// Anti-aliasing / spectral-cleanliness probe (Iteration 1).
//
// Method: render each nonlinear voice at the base rate and again at 8x (an
// alias-free reference, FIR-decimated back to the base rate), then compare
// magnitude spectra — EXCESS energy in the base-rate render is candidate
// aliasing.
//
// HONEST CAVEAT: the 303 carries intentional analog imperfections (thermal
// drift) and an internal anti-alias decimator whose passband differs slightly
// from the reference's, so the absolute 303 number is NOT a trustworthy alias
// gate (it is dominated by those benign differences, not aliasing). It is
// printed for information only. The kick has no resonance/drift confound, so
// its reference comparison IS reliable and is gated. Musicality of the 303 is
// gated separately by acid303_probe.
#include <cmath>
#include <cstdio>
#include <vector>

#include "SpectralProbe.h"
#include "voices/BassVoice.h"
#include "voices/KickVoice.h"

namespace
{
constexpr double baseSr = 44100.0;
constexpr int    OSREF  = 8;
double midiHz(int n) { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

std::vector<float> renderBass(double sr, int midi, BassVoice::Params p)
{
    BassVoice b; b.prepare(sr); b.setWaveMix(0.0f); b.setSubMix(0.0f);
    b.setParams(p); b.trigger303(midi, 1.0f, false, false);
    std::vector<float> x(static_cast<size_t>(sr * 0.5));
    for (auto& s : x) s = b.tick();
    return x;
}

std::vector<float> renderKick(double sr, KickVoice::Params p)
{
    KickVoice k; k.prepare(sr); k.setParams(p); k.trigger(1.0f);
    std::vector<float> x(static_cast<size_t>(sr * 0.4));
    for (auto& s : x) s = k.tick();
    return x;
}

bool check(bool ok, const char* name) { std::printf("%s: %s\n", name, ok ? "ok" : "FAIL"); return ok; }
}

int main()
{
    const int midi = 36;

    // ── 303 (informational) ──
    const BassVoice::Params bp { 9000.0f, 0.50f, 0.20f, 0.60f, 0.0f };
    const auto bTest  = renderBass(baseSr, midi, bp);
    const auto bRefHi = renderBass(baseSr * OSREF, midi, bp);
    const auto bRef   = spec::decimateFir(bRefHi, OSREF, baseSr * OSREF, 20000.0);
    const auto ba = spec::aliasVsReference(bTest, bRef, 16384, 2048);
    float bPeak = 0.0f; bool bFin = true;
    for (float v : bTest) { bFin = bFin && std::isfinite(v); bPeak = std::max(bPeak, std::fabs(v)); }
    std::printf("303  [info] excessVsRef peak=%.1f dB rms=%.1f dB | render peak=%.3f finite=%d\n",
                ba.peakExcessDb, ba.excessRmsDb, bPeak, (int)bFin);

    // ── Kick (gated — reliable) ──
    const KickVoice::Params kp { 55.0f, 0.45f, 0.6f, 0.8f, 1.0f, 0.5f };
    const auto kTest  = renderKick(baseSr, kp);
    const auto kRefHi = renderKick(baseSr * OSREF, kp);
    const auto kRef   = spec::decimateFir(kRefHi, OSREF, baseSr * OSREF, 20000.0);
    const auto ka = spec::aliasVsReference(kTest, kRef, 16384, 1024);
    std::printf("kick excessVsRef peak=%.1f dB rms=%.1f dB\n", ka.peakExcessDb, ka.excessRmsDb);

    bool ok = true;
    ok &= check(std::isfinite(ba.peakExcessDb) && std::isfinite(ka.peakExcessDb), "spectra finite");
    ok &= check(bFin && bPeak < 1.0f, "303 render finite + bounded");
    ok &= check(ka.peakExcessDb < -45.0, "kick alias floor < -45 dB");
    return ok ? 0 : 1;
}
