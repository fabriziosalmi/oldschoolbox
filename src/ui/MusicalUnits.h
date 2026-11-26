#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "../core/ParamIds.h"

// Translates raw 0..1 / APVTS parameter values into real sound-engineer musical units
// (Hz + musical note, ms, kHz, +dB [MACKIE SAT], L/C/R pan, LP/HP isolator Hz).
namespace MusicalUnits
{
    inline juce::String hzToNoteName(double hz)
    {
        if (hz <= 8.0) return "";
        static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const double midiF = 69.0 + 12.0 * std::log2(hz / 440.0);
        const int midi = std::clamp(static_cast<int>(std::llround(midiF)), 0, 127);
        return juce::String(names[midi % 12]) + juce::String((midi / 12) - 1);
    }

    inline juce::String format(const juce::String& paramId, double val)
    {
        // Kick / Tom / Perc Tune -> Hz + musical note (so you can tune Kick to your 303!)
        if (paramId == ParamId::KICK_TUNE)
        {
            const double hz = (val > 20.0) ? val : (35.0 + val * 65.0);
            return juce::String(hz, 1) + " Hz (" + hzToNoteName(hz) + ")";
        }
        if (paramId == ParamId::SNARE_TUNE || paramId == ParamId::TOM_TUNE
            || paramId == ParamId::PERC1_TUNE || paramId == ParamId::PERC2_TUNE)
        {
            const double hz = (val > 20.0) ? val : (60.0 + val * 340.0);
            return juce::String(hz, 0) + " Hz (" + hzToNoteName(hz) + ")";
        }

        // 303 Bass Cutoff -> Hz / kHz
        if (paramId == ParamId::BASS1_CUTOFF || paramId == ParamId::BASS2_CUTOFF)
        {
            const double hz = (val > 20.0) ? val : (40.0 * std::pow(200.0, std::clamp(val, 0.0, 1.0)));
            return (hz >= 1000.0) ? (juce::String(hz / 1000.0, 2) + " kHz")
                                  : (juce::String(hz, 0) + " Hz");
        }

        // Master Drive -> +dB [MACKIE SAT]
        if (paramId == ParamId::MASTER_DRIVE)
        {
            const double db = std::clamp(val, 0.0, 1.0) * 18.0;
            return "+" + juce::String(db, 1) + " dB" + (db > 6.0 ? " [MACKIE SAT]" : "");
        }
        if (paramId == ParamId::KICK_DRIVE || paramId == ParamId::PERC1_DRIVE)
        {
            const double db = std::clamp(val, 0.0, 1.0) * 15.0;
            return "+" + juce::String(db, 1) + " dB";
        }

        // Bipolar DJ Wash Isolator
        if (paramId == ParamId::DJ_FILTER)
        {
            const double a = std::abs(val);
            if (a < 0.015) return "BYPASS";
            if (val < 0.0)
            {
                const double fc = 18000.0 * std::pow(160.0 / 18000.0, a);
                return "LP " + (fc >= 1000.0 ? juce::String(fc / 1000.0, 2) + " kHz"
                                             : juce::String(fc, 0) + " Hz");
            }
            const double fc = 35.0 * std::pow(215.0, a);
            return "HP " + (fc >= 1000.0 ? juce::String(fc / 1000.0, 2) + " kHz"
                                         : juce::String(fc, 0) + " Hz");
        }

        // Decays / Delay Time -> ms
        if (paramId.contains("decay") || paramId == ParamId::DELAY_TIME)
        {
            const double ms = (val <= 2.5) ? (val * 1000.0) : val;
            if (paramId.contains("decay") && val <= 1.0)
                return juce::String(static_cast<int>(std::round(30.0 + val * 620.0))) + " ms";
            return juce::String(static_cast<int>(std::round(ms))) + " ms";
        }

        // Volume -> dB
        if (paramId.startsWith("vol_") || paramId == ParamId::CHAOS_VOL)
        {
            if (val <= 0.001) return "-inf dB";
            const double db = 20.0 * std::log10(val / 0.8);
            return (db >= 0.0 ? "+" : "") + juce::String(db, 1) + " dB";
        }

        // Pan -> L / C / R
        if (paramId.startsWith("pan_"))
        {
            const double p = (val - 0.5) * 200.0;
            if (std::abs(p) < 2.0) return "C";
            return (p < 0.0 ? "L " : "R ") + juce::String(static_cast<int>(std::round(std::abs(p)))) + "%";
        }

        // Swing / Percentages
        if (val >= 0.0 && val <= 1.0)
            return juce::String(static_cast<int>(std::round(val * 100.0))) + "%";

        return juce::String(val, 2);
    }

    // Shared atomic-like HUD notification hook (UI thread only) so hovering/dragging any DjKnob
    // displays its live engineering readout inside the top CRT Marquee terminal.
    struct HudState
    {
        juce::String text;
        double       lastUpdateMs = 0.0;
    };

    inline HudState& hud()
    {
        static HudState s;
        return s;
    }

    inline void publishHud(const juce::String& label, const juce::String& paramId, double val)
    {
        hud().text = label.toUpperCase() + " // " + format(paramId, val);
        hud().lastUpdateMs = juce::Time::getMillisecondCounterHiRes();
    }
} // namespace MusicalUnits
