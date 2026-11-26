// Iteration 2 — 303 fidelity A/B vs a real "One Note Sweep" reference.
//
// Compares the resonant character of our BassVoice against a real 303 recording
// (royalty-free MusicRadar pack, kept local under references/, gitignored).
// Metrics are RELATIVE and leakage-tolerant:
//   * resonant-peak prominence (dB) — how far the resonance spikes over the
//     surrounding harmonics; the core "is it acid" measure.
//   * spectral-centroid range over the sweep — how much brightness it travels.
//
// Pass the reference WAV path as argv[1] (default: the 128E dry sweep).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "SpectralProbe.h"
#include "voices/BassVoice.h"

namespace
{
struct Scan { double maxProm = 0.0; double centMin = 1e12; double centMax = 0.0; double promAtSec = 0.0; };

Scan scanSignal(const std::vector<float>& x, double sr, double f0)
{
    Scan r;
    const int fftSize = 8192;
    const int hop = fftSize / 2;
    const double minHz = std::max(150.0, f0 * 1.6);
    for (int off = 0; off + fftSize < static_cast<int>(x.size()); off += hop)
    {
        // require some energy in this frame
        double e = 0.0;
        for (int i = 0; i < fftSize; ++i) e += std::fabs(static_cast<double>(x[static_cast<size_t>(off + i)]));
        if (e / fftSize < 1e-4) continue;
        const double prom = spec::resonanceProminenceDb(x, sr, fftSize, minHz, off);
        const double cent = spec::centroidHz(x, sr, fftSize, off);
        if (prom > r.maxProm) { r.maxProm = prom; r.promAtSec = off / sr; }
        r.centMin = std::min(r.centMin, cent);
        r.centMax = std::max(r.centMax, cent);
    }
    return r;
}

int midiFromHz(double f) { return static_cast<int>(std::lround(69.0 + 12.0 * std::log2(f / 440.0))); }

// Our 303: sustained note with an exponential cutoff sweep at high resonance.
std::vector<float> renderSweep(double sr, int midi, float reso)
{
    BassVoice b; b.prepare(sr); b.setWaveMix(0.0f); b.setSubMix(0.0f);
    const int N = static_cast<int>(sr * 4.0);
    b.setParams({ 150.0f, reso, 0.15f, 1.0f, 0.0f });
    b.trigger303(midi, 1.0f, false, false);
    std::vector<float> x(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
    {
        const float frac = static_cast<float>(i) / N;
        const float cutoff = 150.0f * std::pow(8000.0f / 150.0f, frac);   // exp sweep 150→8000
        b.setParams({ cutoff, reso, 0.15f, 1.0f, 0.0f });
        x[static_cast<size_t>(i)] = b.tick();
    }
    return x;
}

bool check(bool ok, const char* name) { std::printf("%s: %s\n", name, ok ? "ok" : "FAIL"); return ok; }
}

int main(int argc, char** argv)
{
    const std::string refPath = (argc > 1) ? argv[1]
        : "references/303/One Note Sweeps/128bpm/AM_OneNoteSweepDry_128E.wav";

    const auto ref = spec::loadWav(refPath);
    if (!ref.ok) { std::printf("could not load reference: %s\n", refPath.c_str()); return 1; }

    const double rf0 = spec::detectF0(ref.samples, ref.sampleRate, 16384, 30.0, 250.0,
                                      std::min<int>(static_cast<int>(ref.sampleRate * 0.5), static_cast<int>(ref.samples.size()) - 16384));
    const auto refScan = scanSignal(ref.samples, ref.sampleRate, rf0);
    std::printf("REF  %s\n     f0=%.1fHz (~MIDI %d)  resPeakProminence=%.1f dB @%.2fs  centroid %.0f..%.0f Hz\n",
                refPath.c_str(), rf0, midiFromHz(rf0), refScan.maxProm, refScan.promAtSec, refScan.centMin, refScan.centMax);

    const int midi = midiFromHz(rf0);
    const auto mine = renderSweep(44100.0, midi, 0.90f);
    const auto myScan = scanSignal(mine, 44100.0, rf0);
    std::printf("MINE reso=0.90 sweep on MIDI %d\n     resPeakProminence=%.1f dB @%.2fs  centroid %.0f..%.0f Hz\n",
                midi, myScan.maxProm, myScan.promAtSec, myScan.centMin, myScan.centMax);

    // RELIABLE finding (centroid = brightness): the real 303 is brighter and
    // travels a wider brightness range than ours.
    std::printf("GAP  brightness(centroid max): mine %.0f Hz vs ref %.0f Hz  (mine is %s)\n",
                myScan.centMax, refScan.centMax, myScan.centMax < refScan.centMax ? "DARKER" : "brighter");
    std::printf("     [experimental] resonance prominence: mine %.1f vs ref %.1f dB (metric not yet validated — ignore abs)\n",
                myScan.maxProm, refScan.maxProm);

    bool ok = true;
    ok &= check(ref.ok && rf0 > 20.0, "reference loaded + pitched");
    ok &= check(std::isfinite(myScan.centMax) && myScan.centMax > 200.0, "our 303 measured (centroid)");
    ok &= check(refScan.centMax > 1000.0, "reference characterised");
    return ok ? 0 : 1;
}
