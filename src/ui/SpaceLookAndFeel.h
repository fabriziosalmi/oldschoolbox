#pragma once
#include <JuceHeader.h>
#include "SpaceArt.h"

// ── 13-SKIN HARDWARE THEME ENGINE ──
namespace SpaceColours
{
    static constexpr int NUM_THEMES = 13;

    inline int& activeTheme()
    {
        static int t = 0;
        return t;
    }

    struct ThemePalette
    {
        const char*  name;
        juce::Colour bg;           // recessed chassis between modules
        juce::Colour headerBg;     // top bar background
        juce::Colour panelBg;      // default drum / core faceplate
        juce::Colour percPanelBg;  // deeper carbon-slate perc faceplate (ch 4-5)
        juce::Colour acidPanelBg;  // brushed silver-graphite 303 faceplate (ch 6-7)
        juce::Colour stripHeader;  // 30px channel header
        juce::Colour rowBgMuted;   // muted channel plate
        juce::Colour sectionLine;  // module dividers
        juce::Colour crtCenter;    // CRT glass radial center tint
        float        crtScanAlpha; // CRT scanline intensity
        juce::Colour masterAccent; // Master FX bus knob arc accent
        juce::Colour ch[8];        // 8 distinct curated channel accents
        bool         isLight;      // Porcelain / Braun ceramic light chassis
        bool         isGlossy;     // Piano-black gleuomorphic glass + razor LEDs
        juce::Colour textBright;
        juce::Colour textMid;
        juce::Colour textDim;
    };

    inline const ThemePalette& theme()
    {
        static const ThemePalette kThemes[NUM_THEMES] = {
            // 0: OBSIDIAN — Elektron / Studio Dark
            {
                "OBSIDIAN",
                juce::Colour(0xff0d0e12), juce::Colour(0xff111318),
                juce::Colour(0xff14161b), juce::Colour(0xff111319), juce::Colour(0xff1d2028),
                juce::Colour(0xff191c22), juce::Colour(0xff0d0e12), juce::Colour(0xff242832),
                juce::Colour(0xff07110b), 0.038f, juce::Colour(0xffe5c158),
                {
                    juce::Colour(0xff46d98a), juce::Colour(0xffd4b483), juce::Colour(0xffc9a977), juce::Colour(0xff7ec49c),
                    juce::Colour(0xff3ec6f0), juce::Colour(0xff5ea8e6), juce::Colour(0xffe05882), juce::Colour(0xffeb8934)
                },
                false, false, juce::Colour(0xffe6ebf5), juce::Colour(0xff949cad), juce::Colour(0xff5f6675)
            },
            // 1: TR-909 — Vintage Roland Pewter & Orange
            {
                "TR-909",
                juce::Colour(0xff16171b), juce::Colour(0xff1c1d22),
                juce::Colour(0xff22242a), juce::Colour(0xff1c1e24), juce::Colour(0xff2c2f37),
                juce::Colour(0xff2a2c33), juce::Colour(0xff17181d), juce::Colour(0xff363944),
                juce::Colour(0xff120c07), 0.042f, juce::Colour(0xfff26d21),
                {
                    juce::Colour(0xfff26d21), juce::Colour(0xffe8dec8), juce::Colour(0xffd9a74a), juce::Colour(0xffdf7c4e),
                    juce::Colour(0xff4db5c2), juce::Colour(0xff769dc4), juce::Colour(0xffe84855), juce::Colour(0xfff0b429)
                },
                false, false, juce::Colour(0xffedeae4), juce::Colour(0xffa4a8b3), juce::Colour(0xff6b707d)
            },
            // 2: ACID 303 — Machined Silver-Graphite & Acid Yellow
            {
                "ACID 303",
                juce::Colour(0xff171a20), juce::Colour(0xff1f232b),
                juce::Colour(0xff242933), juce::Colour(0xff1e222b), juce::Colour(0xff313744),
                juce::Colour(0xff2c313d), juce::Colour(0xff181b22), juce::Colour(0xff3c4352),
                juce::Colour(0xff101206), 0.040f, juce::Colour(0xffe8d830),
                {
                    juce::Colour(0xffe8d830), juce::Colour(0xff6be0a6), juce::Colour(0xffc9d1df), juce::Colour(0xffa3e635),
                    juce::Colour(0xff38bdf8), juce::Colour(0xff60a5fa), juce::Colour(0xfff43f8e), juce::Colour(0xfffb923c)
                },
                false, false, juce::Colour(0xffe8ecf5), juce::Colour(0xff9ea6b8), juce::Colour(0xff6a7284)
            },
            // 3: CRT-98 — Free Tekno Phosphor Bunker
            {
                "CRT-98",
                juce::Colour(0xff070b08), juce::Colour(0xff0b110d),
                juce::Colour(0xff0e1510), juce::Colour(0xff0b120d), juce::Colour(0xff152119),
                juce::Colour(0xff131d16), juce::Colour(0xff080c09), juce::Colour(0xff1f3025),
                juce::Colour(0xff07190e), 0.072f, juce::Colour(0xff46e88a),
                {
                    juce::Colour(0xff3ce882), juce::Colour(0xff8ef2b4), juce::Colour(0xffe8a838), juce::Colour(0xff2ec474),
                    juce::Colour(0xff45e3c6), juce::Colour(0xff5cd6a8), juce::Colour(0xfff08830), juce::Colour(0xffa8f042)
                },
                false, false, juce::Colour(0xffd7f5e1), juce::Colour(0xff7bb08c), juce::Colour(0xff466952)
            },
            // 4: SPIRAL — Ultraviolet Underground Sound System
            {
                "SPIRAL",
                juce::Colour(0xff0c0a14), juce::Colour(0xff120f1e),
                juce::Colour(0xff151224), juce::Colour(0xff110e1d), juce::Colour(0xff201a34),
                juce::Colour(0xff1c1830), juce::Colour(0xff0d0b16), juce::Colour(0xff2e274a),
                juce::Colour(0xff0f081c), 0.045f, juce::Colour(0xffb374ff),
                {
                    juce::Colour(0xffa86bff), juce::Colour(0xffc49bff), juce::Colour(0xfff25ca2), juce::Colour(0xffd975ff),
                    juce::Colour(0xff2ee2ff), juce::Colour(0xff6699ff), juce::Colour(0xffff4d94), juce::Colour(0xffff7a59)
                },
                false, false, juce::Colour(0xffece6ff), juce::Colour(0xff9f93c2), juce::Colour(0xff645985)
            },
            // 5: PIANO LED — Gleuomorphic Piano-Black Gloss & Razor High-Contrast LEDs
            {
                "PIANO LED",
                juce::Colour(0xff030305), juce::Colour(0xff07080b),
                juce::Colour(0xff08090d), juce::Colour(0xff06070a), juce::Colour(0xff0c0d13),
                juce::Colour(0xff0c0e14), juce::Colour(0xff040406), juce::Colour(0xff1d202b),
                juce::Colour(0xff030c09), 0.025f, juce::Colour(0xff00f0ff),
                {
                    juce::Colour(0xff00ff88), juce::Colour(0xff00e5ff), juce::Colour(0xffffffff), juce::Colour(0xff29ffb8),
                    juce::Colour(0xff00b8ff), juce::Colour(0xff6e7bff), juce::Colour(0xffff2a7a), juce::Colour(0xffff8a00)
                },
                false, true, juce::Colour(0xfff5f8ff), juce::Colour(0xff9aa4b8), juce::Colour(0xff52596b)
            },
            // 6: PORCELAIN — Luxury Ceramic / Marble White (Dieter Rams / Teenage Engineering)
            {
                "PORCELAIN",
                juce::Colour(0xffe2e0da), juce::Colour(0xffebe9e3),
                juce::Colour(0xfff4f3ee), juce::Colour(0xffecebe6), juce::Colour(0xffe6e8ec),
                juce::Colour(0xffe7e6e0), juce::Colour(0xffdad8d2), juce::Colour(0xffcbc8bf),
                juce::Colour(0xff09120e), 0.032f, juce::Colour(0xffe05a1e),
                {
                    juce::Colour(0xff0f9d58), juce::Colour(0xffb57614), juce::Colour(0xff8c6d1f), juce::Colour(0xff1e8e7e),
                    juce::Colour(0xff0277bd), juce::Colour(0xff3949ab), juce::Colour(0xffc2185b), juce::Colour(0xffd84315)
                },
                true, false, juce::Colour(0xff171a21), juce::Colour(0xff4a505e), juce::Colour(0xff7e8494)
            },
            // 7: GAMEBOY — Classic DMG-01 Olive Dot-Matrix & Burgundy Buttons
            {
                "GAMEBOY",
                juce::Colour(0xffb8bf96), juce::Colour(0xffc5cca4),
                juce::Colour(0xffcfd6af), juce::Colour(0xffc2c9a1), juce::Colour(0xffbcc49a),
                juce::Colour(0xffb5bd93), juce::Colour(0xffa7af85), juce::Colour(0xff8c966a),
                juce::Colour(0xff1c2b12), 0.068f, juce::Colour(0xff9c1f54),
                {
                    juce::Colour(0xff235e28), juce::Colour(0xff9c1f54), juce::Colour(0xff3a5a40), juce::Colour(0xff2b6e44),
                    juce::Colour(0xff1f4e5f), juce::Colour(0xff4a3b6b), juce::Colour(0xff9c1f54), juce::Colour(0xff8a4b14)
                },
                true, false, juce::Colour(0xff14200e), juce::Colour(0xff344729), juce::Colour(0xff5a6e4b)
            },
            // 8: MIAMI 84 — Outrun Synthwave Twilight, Hot Pink & Laser Cyan
            {
                "MIAMI 84",
                juce::Colour(0xff0f081f), juce::Colour(0xff170d2e),
                juce::Colour(0xff190e33), juce::Colour(0xff140b29), juce::Colour(0xff231445),
                juce::Colour(0xff201240), juce::Colour(0xff0e071c), juce::Colour(0xff3b1f6e),
                juce::Colour(0xff180726), 0.048f, juce::Colour(0xffff2e97),
                {
                    juce::Colour(0xff00f5ff), juce::Colour(0xffff2e97), juce::Colour(0xffffb830), juce::Colour(0xff38ef7d),
                    juce::Colour(0xff00d2ff), juce::Colour(0xffa855f7), juce::Colour(0xffff2e97), juce::Colour(0xffff7b00)
                },
                false, true, juce::Colour(0xfffdeeff), juce::Colour(0xffb899e6), juce::Colour(0xff7558a6)
            },
            // 9: AMBER 82 — Vintage Nixie & 1982 Amber Phosphor Terminal
            {
                "AMBER 82",
                juce::Colour(0xff0f0a06), juce::Colour(0xff17100a),
                juce::Colour(0xff1a120b), juce::Colour(0xff150e08), juce::Colour(0xff24180e),
                juce::Colour(0xff21160d), juce::Colour(0xff0e0905), juce::Colour(0xff3d2916),
                juce::Colour(0xff1a0f04), 0.062f, juce::Colour(0xffffa000),
                {
                    juce::Colour(0xffffa600), juce::Colour(0xffffcc55), juce::Colour(0xffe87a24), juce::Colour(0xffd9912b),
                    juce::Colour(0xffffb84d), juce::Colour(0xffe06d1b), juce::Colour(0xffff5e1a), juce::Colour(0xffffd166)
                },
                false, false, juce::Colour(0xffffebd2), juce::Colour(0xffc99a67), juce::Colour(0xff7a5836)
            },
            // 10: TOXIC BIO — Biohazard Acid-Lime & Industrial Steel
            {
                "TOXIC BIO",
                juce::Colour(0xff080a07), juce::Colour(0xff0e120c),
                juce::Colour(0xff11160f), juce::Colour(0xff0c100a), juce::Colour(0xff171f14),
                juce::Colour(0xff151c12), juce::Colour(0xff070906), juce::Colour(0xff293822),
                juce::Colour(0xff0e1a05), 0.055f, juce::Colour(0xffb8ff00),
                {
                    juce::Colour(0xffb8ff00), juce::Colour(0xffffe600), juce::Colour(0xff52ff00), juce::Colour(0xff00ff9d),
                    juce::Colour(0xff80ff00), juce::Colour(0xff00e5ff), juce::Colour(0xffff3864), juce::Colour(0xffffaa00)
                },
                false, false, juce::Colour(0xffe9ffcf), juce::Colour(0xff92b372), juce::Colour(0xff536b3e)
            },
            // 11: SAKURA — Tokyo Boutique Blush-Ceramic & Matcha
            {
                "SAKURA",
                juce::Colour(0xffebd9df), juce::Colour(0xfff2e3e8),
                juce::Colour(0xfff8eef1), juce::Colour(0xfff1e2e7), juce::Colour(0xffede5ec),
                juce::Colour(0xfff0dfe5), juce::Colour(0xffe3cfd6), juce::Colour(0xffd6bcc6),
                juce::Colour(0xff170b14), 0.034f, juce::Colour(0xffe04378),
                {
                    juce::Colour(0xffe04378), juce::Colour(0xff2d9d6f), juce::Colour(0xffc47d2b), juce::Colour(0xff3c8dad),
                    juce::Colour(0xff7e57c2), juce::Colour(0xff427aa1), juce::Colour(0xffd81b60), juce::Colour(0xffe65100)
                },
                true, false, juce::Colour(0xff241724), juce::Colour(0xff5e485c), juce::Colour(0xff947c92)
            },
            // 12: GOLD MK2 — Limited 24K Ebony Lacquer & Champagne Gold
            {
                "GOLD MK2",
                juce::Colour(0xff090704), juce::Colour(0xff110d07),
                juce::Colour(0xff130f08), juce::Colour(0xff0f0b06), juce::Colour(0xff1c160c),
                juce::Colour(0xff19130a), juce::Colour(0xff080603), juce::Colour(0xff3b2e16),
                juce::Colour(0xff140f05), 0.030f, juce::Colour(0xfff2c94c),
                {
                    juce::Colour(0xffffd85c), juce::Colour(0xffe8b931), juce::Colour(0xfff5e0a3), juce::Colour(0xffd49b27),
                    juce::Colour(0xffffc847), juce::Colour(0xffe0a82e), juce::Colour(0xffff6b35), juce::Colour(0xfff2c94c)
                },
                false, true, juce::Colour(0xfffff6de), juce::Colour(0xffc7b07b), juce::Colour(0xff78663d)
            }
        };
        return kThemes[std::clamp(activeTheme(), 0, NUM_THEMES - 1)];
    }

    inline bool isLight()  { return theme().isLight; }
    inline bool isGlossy() { return theme().isGlossy; }
    inline juce::Colour txtBright() { return theme().textBright; }
    inline juce::Colour txtMid()    { return theme().textMid; }
    inline juce::Colour txtDim()    { return theme().textDim; }

    // Optional Beat-Synced Visual FX mode (100% UI-thread, zero audio-thread overhead)
    static constexpr int VIZ_OFF    = 0;
    static constexpr int VIZ_SUBTLE = 1;
    static constexpr int VIZ_RAVE   = 2;
    inline int& vizMode() { static int m = VIZ_SUBTLE; return m; }
    inline const char* vizModeName(int m)
    {
        return (m == VIZ_RAVE) ? "VIZ RAVE" : (m == VIZ_SUBTLE ? "VIZ SUB" : "VIZ OFF");
    }

    // Compatibility base constants (Obsidian defaults)
    const juce::Colour bg          { 0xff0d0e12 };
    const juce::Colour panelBg     { 0xff14161b };
    const juce::Colour stripHeader { 0xff191c22 };
    const juce::Colour rowBg       { 0xff14161b };
    const juce::Colour rowBgAlt    { 0xff111318 };
    const juce::Colour rowBgMuted  { 0xff0d0e12 };
    const juce::Colour paramBg     { 0xff0f1116 };
    const juce::Colour gridCell    { 0xff1b1e25 };
    const juce::Colour gridCellAlt { 0xff14161c };
    const juce::Colour subRowBg    { 0xff0b0c10 };

    // Refined Studio Accents
    const juce::Colour neonGreen   { 0xff46d98a };
    const juce::Colour neonCyan    { 0xff3ec6f0 };
    const juce::Colour neonMagenta { 0xffe04e85 };
    const juce::Colour neonOrange  { 0xffeb8934 };
    const juce::Colour neonPurple  { 0xff9b72f2 };
    const juce::Colour neonYellow  { 0xffe5c158 };
    const juce::Colour neonRed     { 0xffe64553 };

    // Subdued neutrals
    const juce::Colour gridLine    { 0xff222630 };
    const juce::Colour groupBg     { 0xff0f1115 };
    const juce::Colour dimGreen    { 0xff143323 };
    const juce::Colour textDim     { 0xff5f6675 };
    const juce::Colour textMid     { 0xff949cad };
    const juce::Colour textBright  { 0xffe6ebf5 };

    const juce::Colour knobBg      { 0xff14161b };
    const juce::Colour knobTrack   { 0xff252933 };
    const juce::Colour sectionLine { 0xff242832 };

    inline juce::Colour themed(juce::Colour c)
    {
        const int t = activeTheme();
        if (t == 3) // CRT-98 phosphor harmony
            return juce::Colour(0xff46e88a).withMultipliedBrightness(0.58f + 0.42f * c.getBrightness()).withAlpha(c.getFloatAlpha());
        if (t == 1 && c == neonYellow) return juce::Colour(0xfff26d21);
        if (t == 2 && c == neonGreen)  return juce::Colour(0xffe8d830);
        if (t == 4 && c == neonGreen)  return juce::Colour(0xffa86bff);
        return c;
    }

    inline juce::Colour channelColor(int ch)
    {
        return theme().ch[std::clamp(ch, 0, 7)];
    }

    inline juce::Colour stripPanelColor(int ch)
    {
        const auto& th = theme();
        if (ch >= 6) return th.acidPanelBg;  // Silver-graphite TB-303 faceplate!
        if (ch >= 4) return th.percPanelBg;  // Deep carbon-slate Elektron Perc faceplate!
        return th.panelBg;                   // 909 Drum faceplate!
    }
}

class SpaceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpaceLookAndFeel()
    {
        setColour(juce::TextButton::buttonColourId,   SpaceColours::knobBg);
        setColour(juce::TextButton::textColourOffId,  SpaceColours::textMid);
        setColour(juce::TextButton::textColourOnId,   SpaceColours::neonGreen);
        setColour(juce::TextButton::buttonOnColourId, SpaceColours::dimGreen);
        setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonGreen);
    }

    // ── Photographic Satin-Aluminum Hardware Knob (mixi + Elektron inspired) ──
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const float minDim  = static_cast<float>(std::min(w, h));
        const float arcR    = minDim * 0.46f;
        const float radius  = minDim * 0.35f;
        const float centreX = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
        const float angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float diam    = radius * 2.0f;

        juce::Colour arcCol = SpaceColours::themed(
            slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                : SpaceColours::neonGreen);

        // 1. Complete 270-degree recessed machined track (adapts to Light Porcelain vs Dark/Piano-Gloss!)
        const bool lightTheme  = SpaceColours::isLight();
        const bool glossyTheme = SpaceColours::isGlossy();
        {
            juce::Path bgArc;
            bgArc.addCentredArc(centreX, centreY, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(lightTheme ? juce::Colour(0xffc8c6be) : (glossyTheme ? juce::Colour(0xff020204) : juce::Colour(0xff0a0b0e)));
            g.strokePath(bgArc, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(lightTheme ? juce::Colour(0xffdcdad3) : (glossyTheme ? juce::Colour(0xff161922) : juce::Colour(0xff252932)));
            g.strokePath(bgArc, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // 2. Razor-crisp 2.0px rounded LED arc (bipolar 12-o'clock for PAN & WASH!)
        const bool isBipolar = static_cast<bool>(slider.getProperties()["bipolar"]);
        const float midAngle = 0.5f * (rotaryStartAngle + rotaryEndAngle);
        if (isBipolar)
        {
            g.setColour(SpaceColours::textMid.withAlpha(0.70f));
            g.fillEllipse(centreX - 1.2f, centreY - arcR - 1.2f, 2.4f, 2.4f);
            if (std::abs(angle - midAngle) > 0.015f)
            {
                juce::Path arc;
                arc.addCentredArc(centreX, centreY, arcR, arcR, 0.0f, std::min(midAngle, angle), std::max(midAngle, angle), true);
                g.setColour(arcCol.withAlpha(0.18f));
                g.strokePath(arc, juce::PathStrokeType(4.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(arcCol.withAlpha(0.95f));
                g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        else if (sliderPos > 0.002f)
        {
            juce::Path arc;
            arc.addCentredArc(centreX, centreY, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
            g.setColour(arcCol.withAlpha(0.18f));
            g.strokePath(arc, juce::PathStrokeType(4.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(arcCol.withAlpha(0.94f));
            g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // 3. Soft multi-layer studio shadow (Porcelain uses delicate ceramic contact shadow; Piano uses deep mirror shadow)
        g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.08f : 0.16f));
        g.fillEllipse(centreX - radius + 2.5f, centreY - radius + 4.5f, diam + 1.0f, diam + 1.0f);
        g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.14f : 0.32f));
        g.fillEllipse(centreX - radius + 1.4f, centreY - radius + 2.6f, diam, diam);
        g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.22f : 0.65f));
        g.fillEllipse(centreX - radius - 0.5f, centreY - radius + 0.8f, diam + 1.0f, diam + 1.0f);

        // 4. Per-Section Hardware Knob Material ("909" / "elektron" / "303" / "mackie" + Porcelain / Piano LED)
        const auto kStyle = slider.getProperties()["knobStyle"].toString();
        const bool is303    = (kStyle == "303");
        const bool is909    = (kStyle == "909");
        const bool isMackie = (kStyle == "mackie");

        // Optional crisp 11-dot radial console scale ticks for 909 / 303 / Mackie
        if (radius >= 14.0f && (is303 || is909 || isMackie))
        {
            const float tickR = arcR + 2.8f;
            g.setColour((lightTheme ? juce::Colour(0xff3a3e48) : juce::Colours::white).withAlpha(is303 ? 0.26f : 0.16f));
            for (int i = 0; i <= 10; ++i)
            {
                const float ta = rotaryStartAngle + (static_cast<float>(i) / 10.0f) * (rotaryEndAngle - rotaryStartAngle);
                const float tx = centreX + tickR * std::sin(ta);
                const float ty = centreY - tickR * std::cos(ta);
                g.fillEllipse(tx - 0.7f, ty - 0.7f, 1.4f, 1.4f);
            }
        }

        {
            const juce::Colour bodyTop = lightTheme  ? (is303 ? juce::Colour(0xffeceef2) : juce::Colour(0xffffffff))
                                       : glossyTheme ? juce::Colour(0xff232630)
                                       : is303       ? juce::Colour(0xff4a505e)
                                       : is909       ? juce::Colour(0xff3c404a)
                                                     : juce::Colour(0xff343943);
            const juce::Colour bodyBot = lightTheme  ? (is303 ? juce::Colour(0xffc5c8d0) : juce::Colour(0xffd2d0c8))
                                       : glossyTheme ? juce::Colour(0xff030305)
                                       : is303       ? juce::Colour(0xff171a22)
                                                     : juce::Colour(0xff101216);
            juce::ColourGradient body(bodyTop, centreX - radius * 0.50f, centreY - radius * 0.55f,
                                      bodyBot, centreX + radius * 0.65f, centreY + radius * 0.65f, true);
            g.setGradientFill(body);
            g.fillEllipse(centreX - radius, centreY - radius, diam, diam);

            // Subtle knurling micro-texture around outer collar
            if (radius >= 12.0f)
            {
                const int ridges = is303 ? 16 : 24;
                const float rIn  = radius * (is303 ? 0.76f : 0.82f);
                const float rOut = radius * 0.96f;
                for (int i = 0; i < ridges; ++i)
                {
                    const float ka = angle + static_cast<float>(i) * (juce::MathConstants<float>::twoPi / static_cast<float>(ridges));
                    g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.12f : (is303 ? 0.36f : 0.28f)));
                    g.drawLine(centreX + rIn * std::sin(ka), centreY - rIn * std::cos(ka),
                               centreX + rOut * std::sin(ka), centreY - rOut * std::cos(ka), is303 ? 1.0f : 0.8f);
                }
            }

            // Machined 1px top-edge specular chamfer arc
            g.setColour(lightTheme ? juce::Colours::black.withAlpha(0.14f) : juce::Colours::white.withAlpha(glossyTheme ? 0.22f : (is303 ? 0.14f : 0.08f)));
            g.drawEllipse(centreX - radius + 0.5f, centreY - radius + 0.5f, diam - 1.0f, diam - 1.0f, 0.9f);
            juce::Path topRim;
            topRim.addCentredArc(centreX, centreY, radius - 0.6f, radius - 0.6f, 0.0f, -1.05f, 1.05f, true);
            g.setColour(juce::Colours::white.withAlpha(lightTheme ? 0.85f : (glossyTheme ? 0.42f : (is303 ? 0.26f : 0.16f))));
            g.strokePath(topRim, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // 5. Recessed top cap (Porcelain satin ceramic | Piano-black glass gloss | 303/909/Obsidian)
        {
            const float capR = radius * (is303 ? 0.68f : 0.74f);
            const juce::Colour capTop = lightTheme  ? juce::Colour(0xfffcfbf8)
                                      : glossyTheme ? juce::Colour(0xff1f222c)
                                      : is303       ? juce::Colour(0xff525866)
                                      : is909       ? juce::Colour(0xff383c46)
                                                    : juce::Colour(0xff303540);
            const juce::Colour capBot = lightTheme  ? juce::Colour(0xffe2e0d8)
                                      : glossyTheme ? juce::Colour(0xff020204)
                                      : is303       ? juce::Colour(0xff232730)
                                                    : juce::Colour(0xff13151b);
            juce::ColourGradient cap(capTop, centreX, centreY - capR,
                                     capBot, centreX, centreY + capR, false);
            g.setGradientFill(cap);
            g.fillEllipse(centreX - capR, centreY - capR, capR * 2.0f, capR * 2.0f);

            if (glossyTheme)
            {
                // Crisp piano-lacquer top-half glass sheen
                juce::ColourGradient gloss(juce::Colours::white.withAlpha(0.18f), centreX, centreY - capR,
                                           juce::Colours::transparentWhite, centreX, centreY + capR * 0.1f, false);
                g.setGradientFill(gloss);
                g.fillEllipse(centreX - capR * 0.88f, centreY - capR * 0.88f, capR * 1.76f, capR * 1.10f);
            }

            if (isMackie)
            {
                g.setColour(arcCol.withAlpha(lightTheme ? 0.55f : 0.32f));
                g.drawEllipse(centreX - capR + 1.2f, centreY - capR + 1.2f, capR * 2.0f - 2.4f, capR * 2.0f - 2.4f, 1.0f);
            }
            else
            {
                g.setColour(juce::Colours::white.withAlpha(lightTheme ? 0.75f : (is303 ? 0.16f : 0.07f)));
                g.drawEllipse(centreX - capR + 0.5f, centreY - capR + 0.5f, capR * 2.0f - 1.0f, capR * 2.0f - 1.0f, 0.7f);
            }
            g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.18f : 0.60f));
            g.drawEllipse(centreX - capR, centreY - capR, capR * 2.0f, capR * 2.0f, 0.85f);
        }

        // 6. Rounded precision indicator notch
        {
            const float pInner = radius * 0.26f;
            const float pOuter = radius * 0.84f;
            const float sx = std::sin(angle), cy = -std::cos(angle);
            juce::Path notch;
            notch.startNewSubPath(centreX + pInner * sx, centreY + pInner * cy);
            notch.lineTo(centreX + pOuter * sx, centreY + pOuter * cy);
            const juce::Colour notchCol = lightTheme  ? (is303 ? arcCol.darker(0.15f) : juce::Colour(0xff171a21))
                                        : glossyTheme ? arcCol.brighter(0.35f)
                                        : is303       ? arcCol.brighter(0.30f)
                                        : is909       ? juce::Colour(0xfff5efe2)
                                                      : juce::Colours::white.withAlpha(0.95f);
            g.setColour(notchCol);
            g.strokePath(notch, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // 7. Ghost Dot (saved Scene value) & Live BeatLfo / Auto Modulation Ring (#11)
        if (slider.getProperties().contains("ghostNorm"))
        {
            const float gNorm = static_cast<float>(slider.getProperties()["ghostNorm"]);
            if (std::abs(gNorm - sliderPos) > 0.015f)
            {
                const float gAng = rotaryStartAngle + gNorm * (rotaryEndAngle - rotaryStartAngle);
                const float gx = centreX + arcR * std::sin(gAng);
                const float gy = centreY - arcR * std::cos(gAng);
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.fillEllipse(gx - 1.5f, gy - 1.5f, 3.0f, 3.0f);
            }
        }

        if (slider.getProperties().contains("modOffset"))
        {
            const float mOff = static_cast<float>(slider.getProperties()["modOffset"]);
            if (std::abs(mOff) > 0.004f)
            {
                const float mAng = std::clamp(angle + mOff * (rotaryEndAngle - rotaryStartAngle),
                                              rotaryStartAngle, rotaryEndAngle);
                juce::Path modArc;
                modArc.addCentredArc(centreX, centreY, arcR + 2.2f, arcR + 2.2f, 0.0f,
                                     std::min(angle, mAng), std::max(angle, mAng), true);
                g.setColour(SpaceColours::neonPurple.withAlpha(0.32f));
                g.strokePath(modArc, juce::PathStrokeType(3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(juce::Colours::white.withAlpha(0.90f));
                g.strokePath(modArc, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        if (static_cast<bool>(slider.getProperties()["midiLearnArmed"]))
        {
            const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(juce::Time::getMillisecondCounter() % 600) * 0.01047f);
            g.setColour(juce::Colours::white.withAlpha(pulse));
            g.drawEllipse(centreX - arcR - 1.5f, centreY - arcR - 1.5f, (arcR + 1.5f) * 2.0f, (arcR + 1.5f) * 2.0f, 1.4f);
        }
        else if (static_cast<bool>(slider.getProperties()["springActive"]))
        {
            g.setColour(SpaceColours::neonCyan.withAlpha(0.85f));
            g.drawEllipse(centreX - arcR - 1.5f, centreY - arcR - 1.5f, (arcR + 1.5f) * 2.0f, (arcR + 1.5f) * 2.0f, 1.3f);
        }
    }

    // ── Sleek mixi-style Vertical Hardware Fader ──
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const float cx = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
        const float topY = static_cast<float>(y) + 8.0f;
        const float botY = static_cast<float>(y + h) - 8.0f;
        const float trackH = std::max(10.0f, botY - topY);

        juce::Colour accent = SpaceColours::themed(
            slider.isColourSpecified(juce::Slider::trackColourId)
                ? slider.findColour(juce::Slider::trackColourId)
                : SpaceColours::neonGreen);

        // Subtle minimal ticks
        const bool lightTheme = SpaceColours::isLight();
        g.setColour((lightTheme ? juce::Colour(0xff3a3e48) : juce::Colours::white).withAlpha(lightTheme ? 0.22f : 0.09f));
        for (int i = 0; i <= 4; ++i)
        {
            const float ty = topY + (static_cast<float>(i) / 4.0f) * trackH;
            g.drawHorizontalLine(static_cast<int>(ty), cx - 9.0f, cx - 6.0f);
            g.drawHorizontalLine(static_cast<int>(ty), cx + 6.0f, cx + 9.0f);
        }

        // Recessed narrow slot
        const auto slotR = juce::Rectangle<float>(cx - 2.0f, topY, 4.0f, trackH);
        g.setColour(lightTheme ? juce::Colour(0xffc2c0b8) : juce::Colour(0xff08090c));
        g.fillRoundedRectangle(slotR, 2.0f);

        // Soft glowing level fill
        const float clampedPos = std::clamp(sliderPos, topY, botY);
        if (botY - clampedPos > 1.0f)
        {
            g.setColour(accent.withAlpha(lightTheme ? 0.65f : 0.35f));
            g.fillRoundedRectangle(cx - 1.2f, clampedPos, 2.4f, botY - clampedPos, 1.2f);
        }

        // Matte machined SSL/Roland fader cap (ceramic white on Porcelain!)
        const float capW = std::min(static_cast<float>(w) - 2.0f, 18.0f);
        const float capH = 15.0f;
        const auto capR = juce::Rectangle<float>(cx - capW * 0.5f, clampedPos - capH * 0.5f, capW, capH);

        g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.18f : 0.60f));
        g.fillRoundedRectangle(capR.translated(0.5f, 2.4f), 2.5f);

        juce::ColourGradient capGrad(lightTheme ? juce::Colour(0xffffffff) : juce::Colour(0xff424854), capR.getX(), capR.getY(),
                                     lightTheme ? juce::Colour(0xffd6d4cc) : juce::Colour(0xff171a20), capR.getX(), capR.getBottom(), false);
        g.setGradientFill(capGrad);
        g.fillRoundedRectangle(capR, 2.5f);

        g.setColour(juce::Colours::white.withAlpha(lightTheme ? 0.85f : 0.18f));
        g.drawHorizontalLine(static_cast<int>(capR.getY()) + 1, capR.getX() + 2.0f, capR.getRight() - 2.0f);
        g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.25f : 0.70f));
        g.drawRoundedRectangle(capR, 2.5f, 0.85f);

        // Recessed center groove + high-contrast indicator line
        g.setColour(juce::Colours::black.withAlpha(lightTheme ? 0.16f : 0.55f));
        g.fillRect(capR.getX() + 1.5f, clampedPos - 2.0f, capR.getWidth() - 3.0f, 4.0f);
        g.setColour(lightTheme ? juce::Colour(0xff171a21) : juce::Colours::white.withAlpha(0.96f));
        g.fillRoundedRectangle(capR.getX() + 2.0f, clampedPos - 0.9f, capR.getWidth() - 4.0f, 1.8f, 0.8f);
    }

    // ── Refined Borderless-When-Off Studio Buttons (Zero boxy border clutter! #23) ──
    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour& buttonColour, bool hover, bool down) override
    {
        auto r = btn.getLocalBounds().toFloat().reduced(0.5f);
        const bool toggled = btn.getToggleState();
        const auto text = btn.getButtonText();
        const bool lightTheme  = SpaceColours::isLight();
        const bool glossyTheme = SpaceColours::isGlossy();
        const juce::Colour keyOffBg   = lightTheme ? juce::Colour(0xffe6e5df) : (glossyTheme ? juce::Colour(0xff0d0f15) : juce::Colour(0xff181b22));
        const juce::Colour keyHoverBg = lightTheme ? juce::Colour(0xffdcdad2) : (glossyTheme ? juce::Colour(0xff171b26) : juce::Colour(0xff222630));
        const juce::Colour keyBorder  = lightTheme ? juce::Colour(0xffc5c3ba) : juce::Colours::white.withAlpha(glossyTheme ? 0.14f : 0.10f);

        auto drawSubtleBevel = [&](juce::Rectangle<float> b)
        {
            g.setColour(juce::Colours::white.withAlpha(lightTheme ? 0.75f : (glossyTheme ? 0.14f : 0.06f)));
            g.drawHorizontalLine(static_cast<int>(b.getY()) + 1, b.getX() + 2.0f, b.getRight() - 2.0f);
        };

        // Scene keys A / B / C / D
        if (text.length() == 1 && text.containsOnly("ABCD"))
        {
            const bool active = buttonColour.getBrightness() > 0.45f;
            const auto col = SpaceColours::themed(SpaceColours::neonGreen);
            g.setColour(active ? col.withAlpha(0.88f) : (hover ? keyHoverBg : keyOffBg));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            if (!active && lightTheme) { g.setColour(keyBorder); g.drawRoundedRectangle(r, 3.0f, 0.75f); }
            return;
        }

        // FILL & TIGHT
        if (text == "FILL" || text == "TIGHT")
        {
            const bool active = (text == "TIGHT") ? toggled : (buttonColour.getBrightness() > 0.35f);
            const auto col = SpaceColours::themed(active && text == "FILL" ? SpaceColours::neonGreen : SpaceColours::neonYellow);
            g.setColour(active ? col.withAlpha(0.88f) : (down ? col.withAlpha(0.45f) : (hover ? keyHoverBg : keyOffBg)));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            if (!active)
            {
                g.setColour(col.withAlpha(lightTheme ? 0.55f : 0.35f));
                g.drawRoundedRectangle(r, 3.0f, 0.8f);
            }
            return;
        }

        // Core screen tabs: SEQ / MATRIX / MIX / LFO / CHAOS / SCOPE (1:1 underline color with OLED CRT bezel!)
        if (text == "SEQ" || text == "MATRIX" || text == "MIX" || text == "LFO" || text == "CHAOS" || text == "SCOPE")
        {
            const bool isChaosTab = (text == "CHAOS") && (btn.getComponentID() == "tabChaos");
            const bool isChLfo    = (text == "LFO")   && (btn.getComponentID() == "chLfo");
            if ((text != "CHAOS" || isChaosTab) && !isChLfo)
            {
                juce::Colour col = SpaceColours::neonGreen;
                if      (text == "MATRIX") col = SpaceColours::neonCyan;
                else if (text == "MIX")    col = SpaceColours::neonYellow;
                else if (text == "LFO")    col = SpaceColours::neonPurple;
                else if (isChaosTab)       col = SpaceColours::neonMagenta;
                else if (text == "SCOPE")  col = SpaceColours::neonGreen;
                else if (text == "SEQ")    col = SpaceColours::neonGreen;
                col = SpaceColours::themed(col);

                g.setColour(toggled ? (lightTheme ? juce::Colour(0xffdcdad3) : juce::Colour(0xff1e222b))
                                    : (hover ? keyHoverBg : juce::Colours::transparentBlack));
                g.fillRoundedRectangle(r, 2.5f);
                if (toggled)
                {
                    g.setColour(col);
                    g.fillRect(r.getX() + 3.0f, r.getBottom() - 2.0f, r.getWidth() - 6.0f, 2.0f);
                }
                return;
            }
        }

        // SELECT pill (consistent machined micro-key when off, crisp accent frame when active)
        if (text == "SELECT")
        {
            const auto col = SpaceColours::themed(
                btn.isColourSpecified(juce::TextButton::buttonOnColourId)
                    ? btn.findColour(juce::TextButton::buttonOnColourId)
                    : SpaceColours::neonGreen);
            g.setColour(toggled ? col.withAlpha(lightTheme ? 0.18f : 0.22f) : (hover ? keyHoverBg : keyOffBg));
            g.fillRoundedRectangle(r, 2.5f);
            drawSubtleBevel(r);
            g.setColour(toggled ? col.withAlpha(0.85f) : keyBorder);
            g.drawRoundedRectangle(r, 2.5f, toggled ? 0.95f : 0.75f);
            return;
        }

        // PLAY / STOP
        if (text == "PLAY" || text == "STOP")
        {
            const auto col = (text == "STOP") ? SpaceColours::neonRed : SpaceColours::themed(SpaceColours::neonGreen);
            g.setColour(toggled ? col.withAlpha(0.90f) : (hover ? col.withAlpha(0.24f) : (lightTheme ? juce::Colour(0xffdfece4) : juce::Colour(0xff1a2420))));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            if (!toggled)
            {
                g.setColour(col.withAlpha(lightTheme ? 0.55f : 0.32f));
                g.drawRoundedRectangle(r, 3.0f, 0.8f);
            }
            return;
        }

        // REVERT / KILL SUB
        if (text == "REVERT" || text == "KILL SUB")
        {
            const bool armed = (text == "REVERT") ? (buttonColour.getBrightness() > 0.35f) : down;
            const auto col = SpaceColours::neonRed;
            g.setColour(armed ? col.withAlpha(0.90f) : (hover ? col.withAlpha(0.28f) : (lightTheme ? juce::Colour(0xfff0dfe2) : juce::Colour(0xff2c1b20))));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            if (!armed)
            {
                g.setColour(col.withAlpha(lightTheme ? 0.55f : 0.45f));
                g.drawRoundedRectangle(r, 3.0f, 0.85f);
            }
            return;
        }

        // CHAOS toggle
        if (text == "CHAOS" || text == "CHAOS ON")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonMagenta);
            g.setColour(toggled ? col.withAlpha(0.38f) : (hover ? col.withAlpha(0.16f) : juce::Colour(0xff1a1d24)));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            if (toggled)
            {
                g.setColour(col.withAlpha(0.85f));
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
            }
            return;
        }

        if (text == "DUB THROW" || text == "DUB")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonCyan);
            g.setColour(down ? col.withAlpha(0.45f) : (hover ? col.withAlpha(0.18f) : juce::Colour(0xff191d25)));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            return;
        }
        if (text == "SPIRAL")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonPurple);
            g.setColour((toggled || down) ? col.withAlpha(0.55f) : (hover ? col.withAlpha(0.22f) : juce::Colour(0xff1e1928)));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            g.setColour(col.withAlpha((toggled || down) ? 0.92f : 0.42f));
            g.drawRoundedRectangle(r, 3.0f, (toggled || down) ? 1.1f : 0.8f);
            return;
        }
        if (text == "MONO <120")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonOrange);
            g.setColour(toggled ? col.withAlpha(0.28f) : juce::Colour(0xff1a1d24));
            g.fillRoundedRectangle(r, 3.0f);
            drawSubtleBevel(r);
            return;
        }

        // MIDI OUT hardware rectangular key with integrated LED + 5-pin DIN vector icon!
        if (text == "MIDI")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonGreen);
            g.setColour(toggled ? col.withAlpha(0.24f) : (hover ? keyHoverBg : keyOffBg));
            g.fillRoundedRectangle(r, 2.5f);
            drawSubtleBevel(r);
            g.setColour(toggled ? col.withAlpha(0.85f) : keyBorder);
            g.drawRoundedRectangle(r, 2.5f, toggled ? 0.95f : 0.75f);

            const float ledX = r.getX() + 7.0f;
            const float ledY = r.getCentreY();
            if (toggled)
            {
                g.setColour(col.withAlpha(0.32f));
                g.fillEllipse(ledX - 3.8f, ledY - 3.8f, 7.6f, 7.6f);
                g.setColour(col);
            }
            else
            {
                g.setColour(lightTheme ? juce::Colour(0xffb5b3aa) : juce::Colour(0xff2a2f3a));
            }
            g.fillEllipse(ledX - 2.0f, ledY - 2.0f, 4.0f, 4.0f);

            // Crisp 5-pin DIN connector glyph
            const float dinX = r.getX() + 19.0f;
            const auto fg = toggled ? (lightTheme ? juce::Colour(0xff14171d) : juce::Colours::white) : SpaceColours::txtMid();
            g.setColour(fg.withAlpha(0.88f));
            g.drawEllipse(dinX - 4.8f, ledY - 4.8f, 9.6f, 9.6f, 1.1f);
            for (int pin = -2; pin <= 2; ++pin)
            {
                const float pa = static_cast<float>(pin) * 0.55f;
                g.fillEllipse(dinX + std::sin(pa) * 2.5f - 0.7f, ledY - std::cos(pa) * 2.5f - 0.7f, 1.4f, 1.4f);
            }

            g.setFont(SpaceArt::silkFont(7.6f, true));
            g.setColour(fg);
            g.drawText("MIDI", static_cast<int>(r.getX() + 26.0f), static_cast<int>(r.getY()),
                       static_cast<int>(r.getWidth() - 28.0f), static_cast<int>(r.getHeight()), juce::Justification::centred);
            return;
        }

        if (text == "AUTO")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonYellow);
            g.setColour(toggled ? col.withAlpha(0.28f) : (hover ? keyHoverBg : keyOffBg));
            g.fillRoundedRectangle(r, 3.0f);
            if (lightTheme && !toggled) { g.setColour(keyBorder); g.drawRoundedRectangle(r, 3.0f, 0.75f); }
            return;
        }

        // Matte recessed chassis micro-buttons (M / S / VAR / H / CLR / RND / C / P / ACID / FLIP / LFO / LOOP / REV / STUT)
        if (text == "M" || text == "S" || text == "VAR" || text == "H"
            || text == "CLR" || text == "RND" || text == "C" || text == "P" || text == "ACID"
            || text == "FLIP" || text == "REV" || text == "STUT" || text == "LOOP" || text.startsWith("LOOP ")
            || (text == "LFO" && btn.getComponentID() == "chLfo"))
        {
            const bool queued = (text == "M") && static_cast<bool>(btn.getProperties()["queuedMute"]);
            const bool loopActive = text.startsWith("LOOP ");
            juce::Colour activeCol = SpaceColours::neonCyan;
            if (text == "M")              activeCol = queued ? SpaceColours::neonYellow : SpaceColours::neonOrange;
            else if (text == "S")         activeCol = SpaceColours::neonCyan;
            else if (text == "VAR")       activeCol = SpaceColours::neonYellow;
            else if (text == "H")         activeCol = SpaceColours::neonOrange;
            else if (text == "ACID" || text == "STUT" || text == "REV" || text == "LOOP" || loopActive)
                activeCol = SpaceColours::neonMagenta;
            else if (text == "FLIP" || text == "LFO")  activeCol = SpaceColours::neonPurple;
            activeCol = SpaceColours::themed(activeCol);

            if (queued)
            {
                const bool flash = ((juce::Time::getMillisecondCounter() / 160) & 1) != 0;
                g.setColour(activeCol.withAlpha(flash ? 0.55f : 0.16f));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(activeCol.withAlpha(0.85f));
                g.drawRoundedRectangle(r, 2.5f, 0.9f);
            }
            else if (toggled || down || loopActive)
            {
                g.setColour(activeCol.withAlpha(0.32f));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(activeCol.withAlpha(0.82f));
                g.drawRoundedRectangle(r, 2.5f, 0.9f);
            }
            else
            {
                g.setColour(hover ? keyHoverBg : keyOffBg);
                g.fillRoundedRectangle(r, 2.5f);
                drawSubtleBevel(r);
                g.setColour(keyBorder);
                g.drawRoundedRectangle(r, 2.5f, 0.75f);
            }
            return;
        }

        // Top-bar semantic differentiation: INIT (destructive) & Theme button
        if (text == "INIT")
        {
            g.setColour(down ? SpaceColours::neonRed.withAlpha(0.38f)
                             : (hover ? SpaceColours::neonRed.withAlpha(0.20f) : (lightTheme ? juce::Colour(0xffefe3e5) : juce::Colour(0xff1a1619))));
            g.fillRoundedRectangle(r, 2.5f);
            drawSubtleBevel(r);
            g.setColour(hover ? SpaceColours::neonRed.withAlpha(0.55f) : SpaceColours::neonRed.withAlpha(0.32f));
            g.drawRoundedRectangle(r, 2.5f, 0.75f);
            return;
        }
        if (btn.getComponentID() == "themeBtn")
        {
            const auto tCol = SpaceColours::theme().masterAccent;
            g.setColour(down ? tCol.withAlpha(0.28f) : (hover ? tCol.withAlpha(0.16f) : keyOffBg));
            g.fillRoundedRectangle(r, 2.5f);
            drawSubtleBevel(r);
            g.setColour(tCol.withAlpha(hover ? 0.75f : 0.48f));
            g.drawRoundedRectangle(r, 2.5f, 0.85f);
            return;
        }
        if (btn.getComponentID() == "vizBtn")
        {
            const int vm = SpaceColours::vizMode();
            const auto vCol = (vm == SpaceColours::VIZ_RAVE)   ? SpaceColours::themed(SpaceColours::neonMagenta)
                            : (vm == SpaceColours::VIZ_SUBTLE) ? SpaceColours::themed(SpaceColours::neonCyan)
                                                               : SpaceColours::txtDim();
            g.setColour(vm > 0 ? vCol.withAlpha(hover ? 0.28f : 0.16f) : (hover ? keyHoverBg : keyOffBg));
            g.fillRoundedRectangle(r, 2.5f);
            drawSubtleBevel(r);
            g.setColour(vm > 0 ? vCol.withAlpha(0.75f) : keyBorder);
            g.drawRoundedRectangle(r, 2.5f, vm > 0 ? 0.9f : 0.75f);
            return;
        }

        // Default studio key
        g.setColour(down ? keyHoverBg.darker(0.12f) : (hover ? keyHoverBg : keyOffBg));
        g.fillRoundedRectangle(r, 2.5f);
        drawSubtleBevel(r);
        if (lightTheme) { g.setColour(keyBorder); g.drawRoundedRectangle(r, 2.5f, 0.75f); }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool hover, bool) override
    {
        const auto text = btn.getButtonText();
        const bool toggled = btn.getToggleState();
        if (text == "MIDI") return;

        const auto b = btn.getLocalBounds().toFloat();
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();

        const bool lightTheme = SpaceColours::isLight();
        // ── Vector icons replacing repetitive text (lower cognitive load!) ──
        if (text == "UNDO" || text == "REDO")
        {
            const bool isUndo = (text == "UNDO");
            const auto col = btn.isEnabled()
                ? (hover ? SpaceColours::txtBright() : SpaceColours::txtMid())
                : SpaceColours::txtDim().withAlpha(0.42f);
            g.setColour(col);
            juce::Path arc;
            const float r = 4.3f;
            const float dir = isUndo ? -1.0f : 1.0f;
            // Curved arrow arc
            if (isUndo)
                arc.addCentredArc(cx + 0.8f, cy + 1.0f, r, r, 0.0f, -1.35f, 2.35f, true);
            else
                arc.addCentredArc(cx - 0.8f, cy + 1.0f, r, r, 0.0f, -2.35f, 1.35f, true);
            g.strokePath(arc, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Arrowhead
            const float tipX = cx + dir * 3.8f;
            const float tipY = cy - 2.4f;
            juce::Path head;
            head.startNewSubPath(tipX, tipY);
            head.lineTo(tipX - dir * 3.2f, tipY - 0.5f);
            head.startNewSubPath(tipX, tipY);
            head.lineTo(tipX - dir * 0.4f, tipY + 3.1f);
            g.strokePath(head, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        if (text == "PLAY" || text == "STOP")
        {
            const auto col = toggled ? juce::Colour(0xff0d0e12)
                                     : ((text == "STOP") ? juce::Colour(0xfff5a3a3) : SpaceColours::themed(SpaceColours::neonGreen));
            g.setColour(col);
            if (toggled || text == "STOP")
            {
                // Crisp hardware Stop square when playing
                g.fillRoundedRectangle(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f, 1.5f);
            }
            else
            {
                // Crisp hardware Play triangle when stopped
                juce::Path tri;
                tri.addTriangle(cx - 4.0f, cy - 5.2f, cx - 4.0f, cy + 5.2f, cx + 5.4f, cy);
                g.fillPath(tri);
            }
            return;
        }

        if (text == "C" || text == "P")
        {
            const auto col = hover ? SpaceColours::txtBright() : SpaceColours::txtMid();
            g.setColour(col);
            if (text == "C")
            {
                // Copy: two overlapping rounded sheets
                g.drawRoundedRectangle(cx - 4.5f, cy - 4.5f, 6.2f, 7.2f, 1.1f, 1.1f);
                g.setColour(lightTheme ? juce::Colour(0xffe6e5df) : juce::Colour(0xff171a22));
                g.fillRoundedRectangle(cx - 1.5f, cy - 2.0f, 6.2f, 7.2f, 1.1f);
                g.setColour(col);
                g.drawRoundedRectangle(cx - 1.5f, cy - 2.0f, 6.2f, 7.2f, 1.1f, 1.1f);
            }
            else
            {
                // Paste: clipboard with top clip
                g.drawRoundedRectangle(cx - 4.0f, cy - 3.5f, 8.0f, 8.5f, 1.2f, 1.1f);
                g.fillRoundedRectangle(cx - 2.0f, cy - 5.0f, 4.0f, 2.2f, 0.8f);
                g.drawHorizontalLine(static_cast<int>(cy + 0.5f), cx - 2.0f, cx + 2.0f);
                g.drawHorizontalLine(static_cast<int>(cy + 2.5f), cx - 2.0f, cx + 1.2f);
            }
            return;
        }

        if (text == "REVERT")
        {
            const bool armed = btn.findColour(juce::TextButton::buttonColourId).getBrightness() > 0.35f;
            const auto col = armed ? juce::Colours::white : (lightTheme ? juce::Colour(0xffb52332) : (hover ? juce::Colours::white : juce::Colour(0xfff2a2a2)));
            g.setColour(col);
            // Snap-back circular arrow icon on left + concise "REV"
            const float ix = b.getX() + 12.5f;
            juce::Path arc;
            arc.addCentredArc(ix, cy, 4.0f, 4.0f, 0.0f, -1.6f, 2.7f, true);
            g.strokePath(arc, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            head.startNewSubPath(ix - 3.8f, cy - 0.6f);
            head.lineTo(ix - 5.8f, cy - 2.8f);
            head.startNewSubPath(ix - 3.8f, cy - 0.6f);
            head.lineTo(ix - 1.4f, cy - 2.4f);
            g.strokePath(head, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont(SpaceArt::silkFont(8.4f, true));
            g.drawText("REV", static_cast<int>(b.getX() + 19.0f), static_cast<int>(b.getY()),
                       static_cast<int>(b.getWidth() - 21.0f), static_cast<int>(b.getHeight()), juce::Justification::centred);
            return;
        }

        // Export / Import arrows: WAV> / MID> / DROP WAV -> [↓ WAV] / [↓ MID]
        if (text == "WAV>" || text == "MID>" || text == "DROP WAV")
        {
            const bool isImport = (text == "DROP WAV");
            const auto col = isImport ? SpaceColours::themed(SpaceColours::neonMagenta).withAlpha(hover ? 1.0f : 0.90f)
                                      : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid());
            g.setColour(col);
            const float ax = b.getX() + 11.0f;
            juce::Path arr;
            arr.startNewSubPath(ax, cy - 3.8f);
            arr.lineTo(ax, cy + 2.5f);
            arr.startNewSubPath(ax - 2.8f, cy + 0.0f);
            arr.lineTo(ax, cy + 2.8f);
            arr.lineTo(ax + 2.8f, cy + 0.0f);
            g.strokePath(arr, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawHorizontalLine(static_cast<int>(cy + 4.3f), ax - 3.6f, ax + 3.6f);
            g.setFont(SpaceArt::silkFont(8.0f, true));
            const juce::String lbl = (text == "MID>") ? "MID" : (isImport ? "LOAD WAV" : "WAV");
            g.drawText(lbl, static_cast<int>(b.getX() + 18.0f), static_cast<int>(b.getY()),
                       static_cast<int>(b.getWidth() - 20.0f), static_cast<int>(b.getHeight()), juce::Justification::centred);
            return;
        }

        // INIT / Presets -> [☰ INIT]
        if (text == "INIT")
        {
            const auto col = hover ? (lightTheme ? juce::Colour(0xffb52332) : juce::Colour(0xfff5a5a5)) : SpaceColours::txtMid();
            g.setColour(col);
            const float lx = b.getX() + 7.5f;
            for (int i = -1; i <= 1; ++i)
                g.drawHorizontalLine(static_cast<int>(cy + static_cast<float>(i) * 2.8f), lx, lx + 7.0f);
            g.setFont(SpaceArt::silkFont(8.0f, true));
            g.drawText("INIT", static_cast<int>(b.getX() + 17.0f), static_cast<int>(b.getY()),
                       static_cast<int>(b.getWidth() - 19.0f), static_cast<int>(b.getHeight()), juce::Justification::centred);
            return;
        }

        // Theme Switcher -> [◐ THEME]
        if (btn.getComponentID() == "themeBtn")
        {
            const auto tCol = SpaceColours::theme().masterAccent;
            const auto fg   = lightTheme ? tCol.darker(0.15f) : tCol.brighter(hover ? 0.25f : 0.08f);
            const float sx = b.getX() + 9.5f;
            g.setColour(tCol);
            g.drawEllipse(sx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.2f);
            juce::Path half;
            half.addPieSegment(sx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 0.0f, juce::MathConstants<float>::pi, 0.0f);
            g.fillPath(half);
            g.setFont(SpaceArt::silkFont(7.8f, true));
            g.setColour(fg);
            g.drawText(text, static_cast<int>(b.getX() + 16.0f), static_cast<int>(b.getY()),
                       static_cast<int>(b.getWidth() - 18.0f), static_cast<int>(b.getHeight()), juce::Justification::centred);
            return;
        }

        // VIZ Mode -> [✦ SUB] / [✦ RAVE] / [○ OFF]
        if (btn.getComponentID() == "vizBtn")
        {
            const int vm = SpaceColours::vizMode();
            const auto vCol = (vm == SpaceColours::VIZ_RAVE)   ? SpaceColours::themed(SpaceColours::neonMagenta)
                            : (vm == SpaceColours::VIZ_SUBTLE) ? SpaceColours::themed(SpaceColours::neonCyan)
                                                               : SpaceColours::txtMid();
            const auto fg = vm > 0 ? (lightTheme ? vCol.darker(0.18f) : vCol.brighter(0.20f))
                                   : (hover ? SpaceColours::txtBright() : SpaceColours::txtDim());
            g.setColour(fg);
            const float ix = b.getX() + 10.0f;
            if (vm == SpaceColours::VIZ_OFF)
            {
                g.drawEllipse(ix - 3.6f, cy - 3.6f, 7.2f, 7.2f, 1.2f);
            }
            else
            {
                // 4-point crisp vector star/sparkle
                juce::Path star;
                const float rOut = (vm == SpaceColours::VIZ_RAVE) ? 4.8f : 4.1f;
                const float rIn  = 1.4f;
                for (int i = 0; i < 8; ++i)
                {
                    const float a = static_cast<float>(i) * (juce::MathConstants<float>::pi * 0.25f);
                    const float rr = (i & 1) ? rIn : rOut;
                    const float px = ix + std::cos(a) * rr;
                    const float py = cy + std::sin(a) * rr;
                    if (i == 0) star.startNewSubPath(px, py); else star.lineTo(px, py);
                }
                star.closeSubPath();
                g.fillPath(star);
            }
            g.setFont(SpaceArt::silkFont(7.8f, true));
            const juce::String shortLbl = (vm == SpaceColours::VIZ_RAVE) ? "RAVE" : (vm == SpaceColours::VIZ_SUBTLE ? "SUB" : "OFF");
            g.drawText(shortLbl, static_cast<int>(b.getX() + 16.0f), static_cast<int>(b.getY()),
                       static_cast<int>(b.getWidth() - 18.0f), static_cast<int>(b.getHeight()), juce::Justification::centred);
            return;
        }

        // Page navigation chevrons < / >
        if (text == "<" || text == ">")
        {
            const auto col = btn.isEnabled()
                ? (hover ? SpaceColours::txtBright() : SpaceColours::txtMid())
                : SpaceColours::txtDim().withAlpha(0.35f);
            g.setColour(col);
            const float d = (text == "<") ? -1.0f : 1.0f;
            juce::Path chv;
            chv.startNewSubPath(cx - d * 2.0f, cy - 3.6f);
            chv.lineTo(cx + d * 2.2f, cy);
            chv.lineTo(cx - d * 2.0f, cy + 3.6f);
            g.strokePath(chv, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        // FLIP (Face A <-> Face B) -> crisp ⇄ double-arrow
        if (text == "FLIP")
        {
            g.setColour(toggled ? (lightTheme ? juce::Colour(0xff14171d) : juce::Colours::white)
                                : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
            juce::Path p;
            p.startNewSubPath(cx - 5.0f, cy - 2.2f); p.lineTo(cx + 5.0f, cy - 2.2f);
            p.startNewSubPath(cx + 2.6f, cy - 4.3f); p.lineTo(cx + 5.0f, cy - 2.2f); p.lineTo(cx + 2.6f, cy - 0.2f);
            p.startNewSubPath(cx + 5.0f, cy + 2.2f); p.lineTo(cx - 5.0f, cy + 2.2f);
            p.startNewSubPath(cx - 2.6f, cy + 0.2f); p.lineTo(cx - 5.0f, cy + 2.2f); p.lineTo(cx - 2.6f, cy + 4.3f);
            g.strokePath(p, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        // RND (3-pip rounded die) & CLR (minimal trash can)
        if (text == "RND")
        {
            g.setColour(hover ? SpaceColours::txtBright() : SpaceColours::txtMid());
            g.drawRoundedRectangle(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 2.0f, 1.2f);
            g.fillEllipse(cx - 2.8f, cy - 2.8f, 1.8f, 1.8f);
            g.fillEllipse(cx - 0.9f, cy - 0.9f, 1.8f, 1.8f);
            g.fillEllipse(cx + 1.1f, cy + 1.1f, 1.8f, 1.8f);
            return;
        }
        if (text == "CLR")
        {
            g.setColour(hover ? SpaceColours::neonRed : SpaceColours::txtMid());
            g.drawHorizontalLine(static_cast<int>(cy - 3.6f), cx - 4.8f, cx + 4.8f);
            g.drawHorizontalLine(static_cast<int>(cy - 5.2f), cx - 2.0f, cx + 2.0f);
            g.drawRoundedRectangle(cx - 3.5f, cy - 3.2f, 7.0f, 8.2f, 1.0f, 1.15f);
            g.drawVerticalLine(static_cast<int>(cx - 1.2f), cy - 1.2f, cy + 3.4f);
            g.drawVerticalLine(static_cast<int>(cx + 1.2f), cy - 1.2f, cy + 3.4f);
            return;
        }

        // CHAOS Sampler icons: TRIG (lightning bolt), REV (reverse ◀|), LOOP (cycle ⟳ + division)
        if (text == "TRIG")
        {
            g.setColour(SpaceColours::themed(SpaceColours::neonMagenta).withAlpha(hover ? 1.0f : 0.92f));
            juce::Path bolt;
            bolt.startNewSubPath(cx + 1.2f, cy - 5.5f);
            bolt.lineTo(cx - 3.2f, cy + 0.4f);
            bolt.lineTo(cx + 0.2f, cy + 0.4f);
            bolt.lineTo(cx - 1.4f, cy + 5.5f);
            bolt.lineTo(cx + 3.4f, cy - 0.6f);
            bolt.lineTo(cx + 0.0f, cy - 0.6f);
            bolt.closeSubPath();
            g.fillPath(bolt);
            return;
        }
        if (text == "REV")
        {
            const bool active = toggled || btn.isDown();
            g.setColour(active ? juce::Colours::white : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
            g.fillRect(cx - 4.8f, cy - 4.2f, 1.6f, 8.4f);
            juce::Path tri;
            tri.addTriangle(cx + 4.4f, cy - 4.4f, cx + 4.4f, cy + 4.4f, cx - 2.4f, cy);
            g.fillPath(tri);
            return;
        }
        if (text == "LOOP" || text.startsWith("LOOP "))
        {
            const bool active = toggled || text.startsWith("LOOP ");
            g.setColour(active ? juce::Colours::white : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
            const bool hasDiv = text.startsWith("LOOP ");
            const float lx = hasDiv ? (b.getX() + 13.0f) : cx;
            juce::Path arc;
            arc.addCentredArc(lx, cy, 4.0f, 4.0f, 0.0f, 0.4f, 5.4f, true);
            g.strokePath(arc, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            head.startNewSubPath(lx + 1.2f, cy - 4.0f);
            head.lineTo(lx + 4.2f, cy - 3.6f);
            head.lineTo(lx + 2.8f, cy - 0.8f);
            g.strokePath(head, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (hasDiv)
            {
                g.setFont(SpaceArt::silkFont(8.2f, true));
                g.drawText(text.substring(5), static_cast<int>(b.getX() + 21.0f), static_cast<int>(b.getY()),
                           static_cast<int>(b.getWidth() - 24.0f), static_cast<int>(b.getHeight()), juce::Justification::centred);
            }
            return;
        }

        float fontSize = 9.0f;
        if ((text.length() == 1 && text.containsOnly("ABCD")) || text == "FILL" || text == "TIGHT")
            fontSize = 10.0f;
        else if (text == "SELECT" || text == "SEQ" || text == "MATRIX" || text == "MIX" || text == "LFO" || text == "SCOPE")
            fontSize = 8.5f;
        else if (text == "MONO <120" || text == "KILL SUB")
            fontSize = 7.8f;
        g.setFont(SpaceArt::silkFont(fontSize, true));

        if (text == "FILL" || text == "TIGHT")
        {
            const bool active = (text == "TIGHT") ? toggled : (btn.findColour(juce::TextButton::buttonColourId).getBrightness() > 0.35f);
            g.setColour(active ? juce::Colour(0xff0d0e12) : (lightTheme ? juce::Colour(0xff9c5210) : SpaceColours::themed(SpaceColours::neonYellow).withAlpha(0.88f)));
        }
        else if (text == "PLAY" || text == "STOP")
            g.setColour(toggled ? juce::Colour(0xff0d0e12)
                                : ((text == "STOP") ? juce::Colour(0xfff5a3a3) : SpaceColours::themed(SpaceColours::neonGreen)));
        else if (text == "REVERT" || text == "KILL SUB")
        {
            const bool armed = (text == "REVERT") ? (btn.findColour(juce::TextButton::buttonColourId).getBrightness() > 0.35f) : btn.isDown();
            g.setColour(armed ? juce::Colours::white : (lightTheme ? juce::Colour(0xffb52332) : (hover ? juce::Colours::white : juce::Colour(0xfff2a2a2))));
        }
        else if (text.length() == 1 && text.containsOnly("ABCD"))
        {
            const bool active = btn.findColour(juce::TextButton::buttonColourId).getBrightness() > 0.45f;
            g.setColour(active ? juce::Colour(0xff0d0e12) : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
        }
        else if (text == "SELECT")
        {
            const auto col = SpaceColours::themed(
                btn.isColourSpecified(juce::TextButton::buttonOnColourId)
                    ? btn.findColour(juce::TextButton::buttonOnColourId)
                    : SpaceColours::neonGreen);
            g.setColour(toggled ? col : (hover ? SpaceColours::txtBright() : SpaceColours::txtDim()));
        }
        else if (text == "SEQ" || text == "MATRIX" || text == "MIX" || text == "SCOPE"
                 || (text == "LFO" && btn.getComponentID() != "chLfo")
                 || (text == "CHAOS" && btn.getComponentID() == "tabChaos"))
            g.setColour(toggled ? SpaceColours::txtBright() : (hover ? SpaceColours::txtMid() : SpaceColours::txtDim()));
        else if (text == "CHAOS" || text == "CHAOS ON")
            g.setColour(toggled ? juce::Colours::white : SpaceColours::themed(SpaceColours::neonMagenta).withAlpha(0.90f));
        else if (text == "INIT")
            g.setColour(hover ? (lightTheme ? juce::Colour(0xffb52332) : juce::Colour(0xfff5a5a5)) : SpaceColours::txtMid());
        else if (btn.getComponentID() == "themeBtn")
            g.setColour(lightTheme ? SpaceColours::theme().masterAccent.darker(0.15f) : SpaceColours::theme().masterAccent.brighter(hover ? 0.25f : 0.08f));
        else if (btn.getComponentID() == "vizBtn")
        {
            const int vm = SpaceColours::vizMode();
            const auto vCol = (vm == SpaceColours::VIZ_RAVE)   ? SpaceColours::themed(SpaceColours::neonMagenta)
                            : (vm == SpaceColours::VIZ_SUBTLE) ? SpaceColours::themed(SpaceColours::neonCyan)
                                                               : SpaceColours::txtMid();
            g.setColour(vm > 0 ? (lightTheme ? vCol.darker(0.18f) : vCol.brighter(0.20f)) : (hover ? SpaceColours::txtBright() : SpaceColours::txtDim()));
        }
        else if (text == "DUB THROW" || text == "DUB")
            g.setColour(SpaceColours::themed(SpaceColours::neonCyan));
        else if (text == "SPIRAL")
            g.setColour((toggled || btn.isDown()) ? juce::Colours::white : SpaceColours::themed(SpaceColours::neonPurple).brighter(lightTheme ? -0.15f : 0.28f));
        else if (text == "MONO <120")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonOrange) : SpaceColours::txtMid());
        else if (text == "AUTO")
            g.setColour(toggled ? (lightTheme ? juce::Colour(0xff9c5210) : SpaceColours::themed(SpaceColours::neonYellow)) : SpaceColours::txtMid());
        else if (text == "M" || text == "S" || text == "FLIP" || (text == "LFO" && btn.getComponentID() == "chLfo"))
            g.setColour(toggled ? (lightTheme ? juce::Colour(0xff14171d) : juce::Colours::white) : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
        else if (text == "VAR")
            g.setColour(toggled ? (lightTheme ? juce::Colour(0xff9c5210) : SpaceColours::themed(SpaceColours::neonYellow)) : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
        else if (text == "H")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonOrange) : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
        else if (text == "REV" || text == "STUT" || text == "LOOP" || text.startsWith("LOOP "))
        {
            const bool active = toggled || btn.isDown() || text.startsWith("LOOP ");
            g.setColour(active ? juce::Colours::white : (hover ? SpaceColours::txtBright() : SpaceColours::txtMid()));
        }
        else if (text == "ACID" || text == "DROP WAV" || text == "TRIG")
            g.setColour(SpaceColours::themed(SpaceColours::neonMagenta).withAlpha(hover ? 1.0f : 0.90f));
        else
            g.setColour(hover ? SpaceColours::txtBright() : SpaceColours::txtMid());

        g.drawText(text, btn.getLocalBounds(), juce::Justification::centred);
    }
};
