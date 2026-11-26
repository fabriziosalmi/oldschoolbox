// Shared spectral measurement harness for rigorous DSP probes.
// Self-contained radix-2 FFT + windowing + alias/THD/centroid metrics.
// Used across iterations to measure spectra against numeric acceptance criteria
// (alias floor in dBFS, THD %, spectral centroid Hz, A/B match vs reference).
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace spec
{
inline bool isPow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

// Iterative Cooley-Tukey radix-2 FFT (in place). size must be a power of 2.
inline void fft(std::vector<std::complex<double>>& a, bool inverse)
{
    const int n = static_cast<int>(a.size());
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * M_PI / len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= n;
}

// Windowed (Hann) magnitude spectrum from a chunk starting at `offset`.
// Returns linear magnitude per bin (size fftSize/2+1 useful bins).
inline std::vector<double> magSpectrum(const std::vector<float>& x, int fftSize, int offset = 0)
{
    std::vector<std::complex<double>> buf(static_cast<size_t>(fftSize), {0.0, 0.0});
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = offset + i;
        const double s = (idx < static_cast<int>(x.size())) ? static_cast<double>(x[static_cast<size_t>(idx)]) : 0.0;
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (fftSize - 1));  // Hann
        buf[static_cast<size_t>(i)] = { s * w, 0.0 };
    }
    fft(buf, false);
    std::vector<double> mag(static_cast<size_t>(fftSize / 2 + 1));
    for (size_t i = 0; i < mag.size(); ++i)
        mag[i] = std::abs(buf[i]);
    return mag;
}

inline double toDb(double lin, double ref) { return 20.0 * std::log10(std::max(1e-12, lin) / std::max(1e-12, ref)); }

struct AliasResult
{
    double peakAliasDb = -200.0;   // loudest inharmonic spur, dB below the fundamental
    double aliasRmsDb  = -200.0;   // total inharmonic energy, dB below total harmonic energy
    double fundamentalBinHz = 0.0;
};

// Measures aliasing of a monophonic, pitched signal at fundamental f0.
// Legitimate energy sits at integer multiples of f0; everything else (above
// f0/2, away from any harmonic) is treated as alias/inharmonic spur.
inline AliasResult aliasFloor(const std::vector<float>& x, double sr, double f0, int fftSize, int offset = -1)
{
    AliasResult r;
    if (!isPow2(fftSize)) return r;
    if (offset < 0) offset = std::min<int>(static_cast<int>(sr * 0.03), std::max(0, static_cast<int>(x.size()) - fftSize)); // skip attack
    const auto mag = magSpectrum(x, fftSize, offset);
    const double binHz = sr / fftSize;
    const double tolHz = std::max(binHz * 2.5, f0 * 0.04);  // harmonic capture width

    double fund = 1e-12;
    for (size_t i = 0; i < mag.size(); ++i)
    {
        const double f = i * binHz;
        if (std::abs(f - f0) <= tolHz) fund = std::max(fund, mag[i]);
    }

    double harmPow = 0.0, aliasPow = 0.0, peakAlias = 1e-12;
    for (size_t i = 0; i < mag.size(); ++i)
    {
        const double f = i * binHz;
        if (f < f0 * 0.5) continue;             // ignore sub/DC region
        if (f > sr * 0.5 - binHz) continue;
        const double n = std::round(f / f0);
        const bool harmonic = (n >= 1.0) && (std::abs(f - n * f0) <= tolHz);
        const double p = mag[i] * mag[i];
        if (harmonic) harmPow += p;
        else { aliasPow += p; peakAlias = std::max(peakAlias, mag[i]); }
    }
    r.peakAliasDb = toDb(peakAlias, fund);
    r.aliasRmsDb  = 10.0 * std::log10(std::max(1e-12, aliasPow) / std::max(1e-12, harmPow));
    r.fundamentalBinHz = f0;
    return r;
}

// FIR-decimate a high-rate signal by `factor` (linear-phase windowed sinc).
// Used to build a near-alias-free reference at the base rate from a high-rate
// render of the SAME algorithm.
inline std::vector<float> decimateFir(const std::vector<float>& x, int factor, double srHigh, double fcHz)
{
    const int taps = 32 * factor | 1;
    std::vector<double> h(static_cast<size_t>(taps));
    const double wc = 2.0 * M_PI * fcHz / srHigh;
    const double m = (taps - 1) / 2.0;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        const double t = i - m;
        const double sinc = (std::abs(t) < 1e-9) ? (wc / M_PI) : (std::sin(wc * t) / (M_PI * t));
        const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (taps - 1)) + 0.08 * std::cos(4.0 * M_PI * i / (taps - 1));
        h[static_cast<size_t>(i)] = sinc * w;
        sum += h[static_cast<size_t>(i)];
    }
    for (auto& v : h) v /= sum;

    const int outN = static_cast<int>(x.size()) / factor;
    std::vector<float> out(static_cast<size_t>(outN), 0.0f);
    for (int n = 0; n < outN; ++n)
    {
        const int center = n * factor;
        double acc = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const int idx = center + k - (taps - 1) / 2;
            if (idx >= 0 && idx < static_cast<int>(x.size()))
                acc += h[static_cast<size_t>(k)] * x[static_cast<size_t>(idx)];
        }
        out[static_cast<size_t>(n)] = static_cast<float>(acc);
    }
    return out;
}

struct AliasVsRef
{
    double peakExcessDb = -200.0;   // loudest bin where test exceeds the alias-free ref
    double excessRmsDb  = -200.0;   // total excess energy, dB below ref total
};

// Leakage-immune alias metric: compares the test render against an alias-free
// reference (same algorithm, higher rate, decimated). Both share identical
// window leakage from identical harmonics, so any EXCESS energy in `test` is
// genuine aliasing. Magnitude spectra → phase-insensitive.
inline AliasVsRef aliasVsReference(const std::vector<float>& test, const std::vector<float>& ref,
                                   int fftSize, int offset = 0)
{
    AliasVsRef r;
    const auto mt = magSpectrum(test, fftSize, offset);
    const auto mr = magSpectrum(ref, fftSize, offset);
    double refTot = 0.0, exc = 0.0, peakExc = 1e-12, refPeak = 1e-12;
    const size_t n = std::min(mt.size(), mr.size());
    for (size_t i = 1; i < n; ++i) { refTot += mr[i] * mr[i]; refPeak = std::max(refPeak, mr[i]); }
    for (size_t i = 1; i < n; ++i)
    {
        const double e = std::max(0.0, mt[i] - mr[i]);
        exc += e * e;
        peakExc = std::max(peakExc, e);
    }
    r.peakExcessDb = toDb(peakExc, refPeak);
    r.excessRmsDb  = 10.0 * std::log10(std::max(1e-12, exc) / std::max(1e-12, refTot));
    return r;
}

// ── WAV loading (RIFF PCM 16/24-bit, mono/stereo → mono float) ──────────────
struct Wav { std::vector<float> samples; double sampleRate = 0.0; bool ok = false; };

inline uint32_t rdU32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
inline uint16_t rdU16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

inline Wav loadWav(const std::string& path)
{
    Wav w;
    std::ifstream f(path, std::ios::binary);
    if (!f) return w;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0)
        return w;

    uint16_t channels = 1, bits = 16;
    uint32_t rate = 44100;
    size_t dataOff = 0, dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= b.size())
    {
        const char* id = reinterpret_cast<const char*>(b.data() + pos);
        const uint32_t sz = rdU32(b.data() + pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0 && body + 16 <= b.size())
        {
            channels = rdU16(b.data() + body + 2);
            rate     = rdU32(b.data() + body + 4);
            bits     = rdU16(b.data() + body + 14);
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            dataOff = body;
            dataLen = std::min<size_t>(sz, b.size() - body);
        }
        pos = body + sz + (sz & 1);   // chunks are word-aligned
    }
    if (dataOff == 0 || channels == 0) return w;

    const int bytesPer = bits / 8;
    const size_t frames = dataLen / (static_cast<size_t>(bytesPer) * channels);
    w.samples.reserve(frames);
    const unsigned char* d = b.data() + dataOff;
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const unsigned char* s = d + (i * channels + c) * bytesPer;
            double v = 0.0;
            if (bits == 16) { int16_t x = static_cast<int16_t>(rdU16(s)); v = x / 32768.0; }
            else if (bits == 24) { int32_t x = (s[0]) | (s[1] << 8) | (s[2] << 16); if (x & 0x800000) x |= ~0xFFFFFF; v = x / 8388608.0; }
            else if (bits == 32) { int32_t x = static_cast<int32_t>(rdU32(s)); v = x / 2147483648.0; }
            acc += v;
        }
        w.samples.push_back(static_cast<float>(acc / channels));
    }
    w.sampleRate = rate;
    w.ok = true;
    return w;
}

// Detect the fundamental (Hz) — strongest spectral peak in [loHz, hiHz].
inline double detectF0(const std::vector<float>& x, double sr, int fftSize, double loHz, double hiHz, int offset = 0)
{
    const auto mag = magSpectrum(x, fftSize, offset);
    const double binHz = sr / fftSize;
    double best = 0.0; int bestBin = 0;
    for (size_t i = 1; i < mag.size(); ++i)
    {
        const double fHz = i * binHz;
        if (fHz < loHz || fHz > hiHz) continue;
        if (mag[i] > best) { best = mag[i]; bestBin = static_cast<int>(i); }
    }
    return bestBin * binHz;
}

// Resonant-peak prominence (dB): largest local bump of the spectrum ABOVE its
// own ~1-octave-smoothed envelope.
// EXPERIMENTAL / NOT YET VALIDATED: in practice this still conflates harmonic
// structure with true resonance for rich/aliased inputs, so treat its absolute
// value with suspicion. Kept for relative/exploratory use only — do not gate on
// it. Reliable spectral measures here are centroidHz and detectF0; definitive
// resonance-character judgement is currently best done by ear against the
// reference WAVs.
inline double resonanceProminenceDb(const std::vector<float>& x, double sr, int fftSize, double minHz, int offset = 0)
{
    const auto mag = magSpectrum(x, fftSize, offset);
    const int n = static_cast<int>(mag.size());
    const double binHz = sr / fftSize;
    std::vector<double> logm(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) logm[static_cast<size_t>(i)] = 20.0 * std::log10(std::max(1e-9, mag[static_cast<size_t>(i)]));

    const int lo = std::max(2, static_cast<int>(minHz / binHz));
    const int hi = std::min(n - 2, static_cast<int>((sr * 0.45) / binHz));
    double maxDev = 0.0;
    for (int i = lo; i <= hi; ++i)
    {
        const int a = std::max(1, i / 2);          // ±1 octave smoothing window
        const int b = std::min(n - 1, i * 2);
        double sum = 0.0; int cnt = 0;
        for (int k = a; k <= b; ++k) { sum += logm[static_cast<size_t>(k)]; ++cnt; }
        const double env = sum / std::max(1, cnt);
        maxDev = std::max(maxDev, logm[static_cast<size_t>(i)] - env);
    }
    return maxDev;
}

// Spectral centroid (Hz) — brightness measure, for A/B timbre matching.
inline double centroidHz(const std::vector<float>& x, double sr, int fftSize, int offset = 0)
{
    const auto mag = magSpectrum(x, fftSize, offset);
    const double binHz = sr / fftSize;
    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < mag.size(); ++i) { num += (i * binHz) * mag[i]; den += mag[i]; }
    return den > 0.0 ? num / den : 0.0;
}
} // namespace spec
