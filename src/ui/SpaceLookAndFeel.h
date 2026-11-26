#pragma once
#include <JuceHeader.h>
#include "SpaceArt.h"

// ── "23 UNDERGROUND" 4-Family Palette + Live Theme Switch (RAVE / CRT-GREEN / AMBER-98) ──
namespace SpaceColours
{
    // Active theme: 0 = RAVE (4-family underground), 1 = CRT-GREEN (1978 phosphor), 2 = AMBER-98 (industrial)
    inline int& activeTheme()
    {
        static int t = 0;
        return t;
    }

    // Chassis — Anodized Dark Gunmetal (#111317) + deep matte rack surfaces
    const juce::Colour bg          { 0xff111317 };
    const juce::Colour rowBg       { 0xff0d0f14 };
    const juce::Colour rowBgAlt    { 0xff0a0c10 };
    const juce::Colour rowBgMuted  { 0xff06070a };
    const juce::Colour paramBg     { 0xff0b0d11 };
    const juce::Colour gridCell    { 0xff1c1f28 };   // TR-909 dark-grey beat group A
    const juce::Colour gridCellAlt { 0xff13151d };   // TR-909 darker beat group B
    const juce::Colour subRowBg    { 0xff08090d };   // darker bg for 303 sub-rows

    // Primaries — 23 Underground
    const juce::Colour neonGreen   { 0xff1bff6a };   // Toxic Acid Green (Low-end)
    const juce::Colour neonCyan    { 0xff00e5ff };   // Ice Cyan (Metallic drums & FX)
    const juce::Colour neonMagenta { 0xffff1e8e };   // Rave Magenta (Perc & Chaos Bus)
    const juce::Colour neonOrange  { 0xffff7a00 };   // Nuclear Amber (Acid 303 & Pressure)
    const juce::Colour neonPurple  { 0xffb15cff };
    const juce::Colour neonYellow  { 0xffffb800 };
    const juce::Colour neonRed     { 0xffff2944 };

    // Subdued
    const juce::Colour gridLine    { 0xff222633 };
    const juce::Colour groupBg     { 0xff0c0e13 };
    const juce::Colour dimGreen    { 0xff0a3a1a };
    const juce::Colour textDim     { 0xff666d7a };
    const juce::Colour textMid     { 0xff9aa5b5 };
    const juce::Colour textBright  { 0xffe6f4ff };

    // Knob/slider
    const juce::Colour knobBg      { 0xff14171f };
    const juce::Colour knobTrack   { 0xff242836 };

    // Section dividers
    const juce::Colour sectionLine { 0xff2f3545 };

    // Theme-aware colour remapping: preserves hue families in RAVE, shifts to phosphor in CRT/AMBER
    inline juce::Colour themed(juce::Colour c)
    {
        const int t = activeTheme();
        if (t == 1) // CRT-GREEN 1978 phosphor
            return juce::Colour(0xff1bff6a).withMultipliedBrightness(0.55f + 0.45f * c.getBrightness()).withAlpha(c.getFloatAlpha());
        if (t == 2) // AMBER-98 industrial
            return juce::Colour(0xffff9a00).withMultipliedBrightness(0.55f + 0.45f * c.getBrightness()).withAlpha(c.getFloatAlpha());
        return c;
    }

    // Per-channel 4-family palette (KICK=Green, SNARE/HATS/TOM=Cyan scale, PERC=Magenta, BASS=Amber)
    inline juce::Colour channelColor(int ch)
    {
        static const juce::Colour cols[8] = {
            juce::Colour(0xff1bff6a),   // 0 KICK   - Toxic Acid Green
            juce::Colour(0xff00e5ff),   // 1 SNARE  - Ice Cyan
            juce::Colour(0xff1cc5ff),   // 2 HATS   - Electric Teal
            juce::Colour(0xff38a3ff),   // 3 TOM    - Cold Steel Blue
            juce::Colour(0xffff1e8e),   // 4 PERC1  - Rave Magenta
            juce::Colour(0xffff4da6),   // 5 PERC2  - Laser Pink
            juce::Colour(0xffffb800),   // 6 BASS1  - Warm Nuclear Amber
            juce::Colour(0xffff7a00),   // 7 BASS2  - Acid 303 Orange
        };
        return themed(cols[std::clamp(ch, 0, 7)]);
    }
}

// ── Custom LookAndFeel ────────────────────────────────────────────────────────
class SpaceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpaceLookAndFeel()
    {
        setColour(juce::TextButton::buttonColourId,   SpaceColours::knobBg);
        setColour(juce::TextButton::textColourOffId,  SpaceColours::neonCyan);
        setColour(juce::TextButton::textColourOnId,   SpaceColours::neonGreen);
        setColour(juce::TextButton::buttonOnColourId, SpaceColours::dimGreen);
        setColour(juce::Slider::rotarySliderFillColourId, SpaceColours::neonGreen);
    }

    // ── Rotary slider (tactile machined hardware knob with external tick marks + knurled cap) ──
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const float minDim  = static_cast<float>(std::min(w, h));
        const float outerR  = minDim * 0.46f;
        const float radius  = minDim * 0.36f;
        const float centreX = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
        const float angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float diam    = radius * 2.0f;

        // Inherit the channel's assigned colour (and honour active THEME switch)
        juce::Colour arcCol = SpaceColours::themed(
            slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                : SpaceColours::neonCyan);

        // 1. External graduated tick marks (11 hardware LED/silkscreen notches)
        constexpr int NUM_TICKS = 11;
        for (int i = 0; i < NUM_TICKS; ++i)
        {
            const float frac = static_cast<float>(i) / static_cast<float>(NUM_TICKS - 1);
            const float a    = rotaryStartAngle + frac * (rotaryEndAngle - rotaryStartAngle);
            const bool lit   = (frac <= sliderPos + 0.001f) && (sliderPos > 0.005f);
            const float r0   = radius + 1.4f;
            const float r1   = (i == 0 || i == NUM_TICKS / 2 || i == NUM_TICKS - 1) ? outerR : (outerR - 1.1f);
            const float sx   = std::sin(a), cy = -std::cos(a);

            g.setColour(lit ? arcCol.withAlpha(0.95f) : juce::Colour(0xff383b48));
            g.drawLine(centreX + r0 * sx, centreY + r0 * cy,
                       centreX + r1 * sx, centreY + r1 * cy,
                       lit ? 1.35f : 0.9f);
        }

        // 2. Active LED ring (tight & crisp)
        if (sliderPos > 0.002f)
        {
            juce::Path arc;
            arc.addCentredArc(centreX, centreY, radius + 0.8f, radius + 0.8f,
                              0.0f, rotaryStartAngle, angle, true);
            g.setColour(arcCol.withAlpha(0.18f));
            g.strokePath(arc, juce::PathStrokeType(3.6f));
            g.setColour(arcCol);
            g.strokePath(arc, juce::PathStrokeType(1.5f));
        }

        // 3. Recessed bezel shadow
        g.setColour(juce::Colours::black.withAlpha(0.65f));
        g.fillEllipse(centreX - radius - 0.8f, centreY - radius + 0.8f, diam + 1.6f, diam + 1.6f);

        // 4. Machined aluminum skirt (outer ring)
        {
            juce::ColourGradient skirt(juce::Colour(0xff2c2f3a), centreX - radius * 0.4f, centreY - radius * 0.4f,
                                       juce::Colour(0xff111218), centreX + radius * 0.7f, centreY + radius * 0.7f, true);
            g.setGradientFill(skirt);
            g.fillEllipse(centreX - radius, centreY - radius, diam, diam);
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawEllipse(centreX - radius, centreY - radius, diam, diam, 0.7f);
        }

        // 5. Inner knurled cap (concentric machined disc)
        {
            const float capR = radius * 0.72f;
            juce::ColourGradient cap(juce::Colour(0xff242730), centreX - capR * 0.3f, centreY - capR * 0.3f,
                                     juce::Colour(0xff0d0e13), centreX + capR * 0.7f, centreY + capR * 0.7f, true);
            g.setGradientFill(cap);
            g.fillEllipse(centreX - capR, centreY - capR, capR * 2.0f, capR * 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.drawEllipse(centreX - capR, centreY - capR, capR * 2.0f, capR * 2.0f, 0.8f);
        }

        // 6. High-contrast machined pointer line
        {
            const float pInner = radius * 0.16f;
            const float pOuter = radius * 0.88f;
            const float sx = std::sin(angle), cy = -std::cos(angle);
            g.setColour(arcCol.withAlpha(0.35f));
            g.drawLine(centreX + pInner * sx, centreY + pInner * cy,
                       centreX + pOuter * sx, centreY + pOuter * cy, 2.8f);
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.drawLine(centreX + pInner * sx, centreY + pInner * cy,
                       centreX + pOuter * sx, centreY + pOuter * cy, 1.4f);
        }

        // 7. Ghost Dot (saved Scene value indicator @ 38% opacity) + Live Modulation Ring
        const float arcR = radius + 2.2f;
        if (slider.getProperties().contains("ghostNorm"))
        {
            const float gNorm = static_cast<float>(slider.getProperties()["ghostNorm"]);
            if (std::abs(gNorm - sliderPos) > 0.015f)
            {
                const float gAng = rotaryStartAngle + gNorm * (rotaryEndAngle - rotaryStartAngle);
                const float gx = centreX + arcR * std::sin(gAng);
                const float gy = centreY - arcR * std::cos(gAng);
                g.setColour(juce::Colours::white.withAlpha(0.18f));
                g.fillEllipse(gx - 3.0f, gy - 3.0f, 6.0f, 6.0f);
                g.setColour(juce::Colours::white.withAlpha(0.45f));
                g.fillEllipse(gx - 1.5f, gy - 1.5f, 3.0f, 3.0f);
            }
        }

        if (slider.getProperties().contains("modOffset"))
        {
            const float mOff = static_cast<float>(slider.getProperties()["modOffset"]);
            if (std::abs(mOff) > 0.005f)
            {
                const float mAng = std::clamp(angle + mOff * (rotaryEndAngle - rotaryStartAngle),
                                              rotaryStartAngle, rotaryEndAngle);
                juce::Path modArc;
                modArc.addCentredArc(centreX, centreY, outerR + 0.6f, outerR + 0.6f, 0.0f,
                                     std::min(angle, mAng), std::max(angle, mAng), true);
                g.setColour(juce::Colours::white.withAlpha(0.82f));
                g.strokePath(modArc, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }
        }

        // 8. MIDI Learn armed white pulse OR Alt+Drag spring-back glow
        if (static_cast<bool>(slider.getProperties()["midiLearnArmed"]))
        {
            const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(juce::Time::getMillisecondCounter() % 600) * 0.01047f);
            g.setColour(juce::Colours::white.withAlpha(pulse));
            g.drawEllipse(centreX - radius - 2.0f, centreY - radius - 2.0f, diam + 4.0f, diam + 4.0f, 1.8f);
        }
        else if (static_cast<bool>(slider.getProperties()["springActive"]))
        {
            g.setColour(SpaceColours::neonCyan.withAlpha(0.85f));
            g.drawEllipse(centreX - radius - 2.0f, centreY - radius - 2.0f, diam + 4.0f, diam + 4.0f, 1.6f);
        }
    }

    // ── Buttons ──
    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour& buttonColour, bool hover, bool down) override
    {
        auto r = btn.getLocalBounds().toFloat().reduced(0.5f);
        const bool toggled = btn.getToggleState();
        const auto text = btn.getButtonText();

        // Live controls honour their assigned state colours from PluginEditor.
        if (text.length() == 1 && text.containsOnly("ABCD"))
        {
            const auto liveCol = buttonColour.contrasting(0.10f);
            const bool active = buttonColour.getBrightness() > 0.45f;
            g.setColour(buttonColour.withAlpha(active ? 0.82f : (hover ? 0.34f : 0.22f)));
            g.fillRoundedRectangle(r, 4.0f);
            if (active)
            {
                g.setColour(buttonColour.withAlpha(0.18f));
                g.fillRoundedRectangle(r.expanded(3.0f), 6.0f);
            }
            g.setColour((active ? liveCol : SpaceColours::neonCyan).withAlpha(active ? 0.95f : 0.42f));
            g.drawRoundedRectangle(r, 4.0f, active ? 1.5f : 0.9f);
            return;
        }

        if (text == "FILL")
        {
            const bool armed = buttonColour.getBrightness() > 0.35f;
            const auto col = SpaceColours::themed(armed ? SpaceColours::neonGreen : SpaceColours::neonMagenta);
            g.setColour(col.withAlpha(down ? 0.55f : armed ? 0.42f : (hover ? 0.30f : 0.18f)));
            g.fillRoundedRectangle(r, 4.0f);
            g.setColour(col.withAlpha(armed ? 0.28f : 0.14f));
            g.fillRoundedRectangle(r.expanded(2.5f, 1.5f), 5.0f);
            g.setColour(col.withAlpha(armed ? 1.0f : 0.85f));
            g.drawRoundedRectangle(r, 4.0f, 1.6f);
            return;
        }

        if (text == "CHAOS" || text == "CHAOS ON")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonMagenta);
            g.setColour(toggled ? col.withAlpha(0.45f) : (hover ? col.withAlpha(0.18f) : juce::Colour(0xff101218)));
            g.fillRoundedRectangle(r, 3.0f);
            if (toggled)
            {
                g.setColour(col.withAlpha(0.22f));
                g.fillRoundedRectangle(r.expanded(2.0f), 4.0f);
            }
            g.setColour(col.withAlpha(toggled ? 1.0f : 0.45f));
            g.drawRoundedRectangle(r, 3.0f, toggled ? 1.5f : 0.9f);
            return;
        }

        if (text == "DUB THROW")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonCyan);
            g.setColour(down ? col.withAlpha(0.45f) : (hover ? col.withAlpha(0.18f) : juce::Colour(0xff101319)));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col.withAlpha(down ? 1.0f : (hover ? 0.85f : 0.50f)));
            g.drawRoundedRectangle(r, 3.0f, down ? 1.5f : 1.0f);
            return;
        }

        if (text == "MONO <120")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonOrange);
            g.setColour(toggled ? col.withAlpha(0.32f) : juce::Colour(0xff0f1116));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col.withAlpha(toggled ? 0.92f : 0.30f));
            g.drawRoundedRectangle(r, 3.0f, toggled ? 1.2f : 0.7f);
            return;
        }

        if (text == "AUTO")
        {
            const auto col = SpaceColours::themed(SpaceColours::neonYellow);
            g.setColour(toggled ? col.withAlpha(0.30f) : hover ? col.withAlpha(0.14f) : juce::Colour(0xff101218));
            g.fillRoundedRectangle(r, 4.0f);
            g.setColour(col.withAlpha(toggled ? 0.95f : 0.32f));
            g.drawRoundedRectangle(r, 4.0f, toggled ? 1.5f : 0.8f);
            return;
        }

        if (text == "KILL SUB")
        {
            const auto col = SpaceColours::neonRed;
            g.setColour(down ? col.withAlpha(0.55f) : (hover ? col.withAlpha(0.20f) : juce::Colour(0xff140c0e)));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col.withAlpha(down ? 1.0f : (hover ? 0.88f : 0.55f)));
            g.drawRoundedRectangle(r, 3.0f, down ? 1.6f : 1.0f);
            return;
        }

        if (text == "REVERT")
        {
            const bool armed = buttonColour.getBrightness() > 0.35f;
            const auto col = SpaceColours::themed(armed ? SpaceColours::neonRed : SpaceColours::neonOrange);
            g.setColour(armed ? col.withAlpha(0.45f) : (hover ? col.withAlpha(0.20f) : juce::Colour(0xff111218)));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col.withAlpha(armed ? 1.0f : (hover ? 0.85f : 0.45f)));
            g.drawRoundedRectangle(r, 3.0f, armed ? 1.5f : 0.9f);
            return;
        }

        if (text.startsWith("QNT:") || text == "WAV>" || text == "MID>")
        {
            const auto col = SpaceColours::themed(text.startsWith("QNT:") ? SpaceColours::neonGreen : SpaceColours::neonCyan);
            g.setColour(hover ? col.withAlpha(0.22f) : juce::Colour(0xff101319));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col.withAlpha(hover ? 0.92f : 0.48f));
            g.drawRoundedRectangle(r, 3.0f, 0.9f);
            return;
        }

        // Matte recessed chassis buttons (M/S/CLR/RND/C/P/H/VAR) — ZERO neon border clutter when inactive!
        // They light up like hardware LEDs ONLY when toggled ON, queued, or hovered.
        if (text == "M" || text == "S" || text == "VAR" || text == "H"
            || text == "CLR" || text == "RND" || text == "C" || text == "P" || text == "ACID")
        {
            const bool queued = (text == "M") && static_cast<bool>(btn.getProperties()["queuedMute"]);
            juce::Colour activeCol = SpaceColours::neonCyan;
            if (text == "M")         activeCol = queued ? SpaceColours::neonYellow : SpaceColours::neonOrange;
            else if (text == "S")    activeCol = SpaceColours::neonCyan;
            else if (text == "VAR")  activeCol = SpaceColours::neonYellow;
            else if (text == "H")    activeCol = SpaceColours::neonOrange;
            else if (text == "ACID") activeCol = SpaceColours::neonMagenta;
            activeCol = SpaceColours::themed(activeCol);

            if (queued)
            {
                const bool flash = ((juce::Time::getMillisecondCounter() / 160) & 1) != 0;
                g.setColour(activeCol.withAlpha(flash ? 0.55f : 0.18f));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(juce::Colours::white.withAlpha(flash ? 0.95f : 0.45f));
                g.drawRoundedRectangle(r, 2.5f, 1.4f);
            }
            else if (toggled || down)
            {
                g.setColour(activeCol.withAlpha(0.32f));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(activeCol.withAlpha(0.95f));
                g.drawRoundedRectangle(r, 2.5f, 1.3f);
            }
            else if (hover)
            {
                g.setColour(activeCol.withAlpha(0.14f));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(activeCol.withAlpha(0.60f));
                g.drawRoundedRectangle(r, 2.5f, 0.9f);
            }
            else
            {
                // Matte recessed chassis engraving (no glowing outline)
                g.setColour(juce::Colour(0xff0b0d11));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(juce::Colours::black.withAlpha(0.65f));
                g.drawHorizontalLine(static_cast<int>(r.getY()), r.getX() + 2.0f, r.getRight() - 2.0f);
                g.setColour(juce::Colours::white.withAlpha(0.05f));
                g.drawHorizontalLine(static_cast<int>(r.getBottom()), r.getX() + 2.0f, r.getRight() - 2.0f);
            }
            return;
        }

        // INIT button — danger red accent
        if (text == "INIT")
        {
            g.setColour(down ? SpaceColours::neonRed.withAlpha(0.3f) :
                         hover ? SpaceColours::neonRed.withAlpha(0.12f) :
                         SpaceColours::knobBg);
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(hover ? SpaceColours::neonRed.withAlpha(0.6f) : SpaceColours::gridLine.brighter(0.1f));
            g.drawRoundedRectangle(r, 3.0f, 0.8f);
            return;
        }

        // SYNC button — cyan accent
        if (text.containsAnyOf("1/FREE"))
        {
            g.setColour(down ? SpaceColours::neonCyan.withAlpha(0.25f) :
                         hover ? SpaceColours::neonCyan.withAlpha(0.10f) :
                         SpaceColours::knobBg);
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(hover ? SpaceColours::neonCyan.withAlpha(0.6f) : SpaceColours::neonCyan.withAlpha(0.25f));
            g.drawRoundedRectangle(r, 3.0f, 0.8f);
            return;
        }

        // PLAY / STOP internal clock button
        if (text == "PLAY" || text == "STOP")
        {
            const auto col = toggled ? SpaceColours::neonGreen : SpaceColours::neonCyan;
            g.setColour(toggled ? col.withAlpha(0.30f) : hover ? col.withAlpha(0.14f) : SpaceColours::knobBg.brighter(0.06f));
            g.fillRoundedRectangle(r, 3.0f);
            if (toggled)
            {
                g.setColour(col.withAlpha(0.18f));
                g.fillRoundedRectangle(r.expanded(2.5f), 4.5f);
            }
            g.setColour(col.withAlpha(toggled ? 0.95f : 0.55f));
            g.drawRoundedRectangle(r, 3.0f, toggled ? 1.5f : 1.0f);
            return;
        }

        // Default button styling
        juce::Colour bg = SpaceColours::knobBg;
        if (down)       bg = SpaceColours::neonCyan.withAlpha(0.20f);
        else if (hover) bg = SpaceColours::neonCyan.withAlpha(0.06f);

        g.setColour(bg);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(hover ? SpaceColours::neonCyan.withAlpha(0.5f) : SpaceColours::gridLine.brighter(0.12f));
        g.drawRoundedRectangle(r, 3.0f, 0.7f);
        // Micro-bevel: top highlight + bottom shadow
        if (!down)
        {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.drawHorizontalLine(static_cast<int>(r.getY()) + 1,
                                  r.getX() + 3.0f, r.getRight() - 3.0f);
            g.setColour(juce::Colours::black.withAlpha(0.08f));
            g.drawHorizontalLine(static_cast<int>(r.getBottom()) - 1,
                                  r.getX() + 3.0f, r.getRight() - 3.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool hover, bool) override
    {
        const auto text = btn.getButtonText();
        const bool toggled = btn.getToggleState();

        float fontSize = 9.5f;
        if ((text.length() == 1 && text.containsOnly("ABCD")) || text == "FILL" || text == "AUTO")
            fontSize = 11.0f;
        g.setFont(SpaceArt::monoFont(fontSize, true));

        if (text == "PLAY" || text == "STOP")
            g.setColour(toggled ? SpaceColours::neonGreen : SpaceColours::neonCyan.withAlpha(hover ? 1.0f : 0.85f));
        else if (text.length() == 1 && text.containsOnly("ABCD"))
        {
            const auto buttonColour = btn.findColour(juce::TextButton::buttonColourId);
            const bool active = buttonColour.getBrightness() > 0.45f;
            g.setColour(active ? juce::Colours::black : SpaceColours::textBright.withAlpha(hover ? 1.0f : 0.78f));
        }
        else if (text == "FILL")
        {
            const auto buttonColour = btn.findColour(juce::TextButton::buttonColourId);
            const bool armed = buttonColour.getBrightness() > 0.35f;
            g.setColour(armed ? juce::Colours::black : SpaceColours::neonMagenta.withAlpha(hover ? 1.0f : 0.82f));
        }
        else if (text == "AUTO")
            g.setColour(toggled ? juce::Colours::black : SpaceColours::neonYellow.withAlpha(hover ? 1.0f : 0.72f));
        else if (text == "CHAOS" || text == "CHAOS ON")
            g.setColour(toggled ? juce::Colours::white : SpaceColours::themed(SpaceColours::neonMagenta).withAlpha(hover ? 1.0f : 0.80f));
        else if (text == "DUB THROW")
            g.setColour(SpaceColours::themed(SpaceColours::neonCyan).withAlpha(hover ? 1.0f : 0.90f));
        else if (text == "MONO <120")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonOrange) : SpaceColours::textDim);
        else if (text == "M")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonOrange) : (hover ? SpaceColours::textBright : SpaceColours::textDim));
        else if (text == "S")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonCyan) : (hover ? SpaceColours::textBright : SpaceColours::textDim));
        else if (text == "VAR")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonYellow) : (hover ? SpaceColours::textBright : SpaceColours::textDim));
        else if (text == "H")
            g.setColour(toggled ? SpaceColours::themed(SpaceColours::neonOrange) : (hover ? SpaceColours::textBright : SpaceColours::textDim));
        else if (text == "CLR" || text == "RND" || text == "C" || text == "P" || text == "ACID")
            g.setColour(hover ? SpaceColours::textBright : SpaceColours::textDim);
        else if (text == "INIT")
            g.setColour(hover ? SpaceColours::neonRed.withAlpha(0.9f) : SpaceColours::textMid);
        else if (text.containsAnyOf("1/FREE"))
            g.setColour(SpaceColours::neonCyan.withAlpha(hover ? 0.95f : 0.7f));
        else
            g.setColour(hover ? SpaceColours::neonCyan : SpaceColours::textMid);

        g.drawText(text, btn.getLocalBounds(), juce::Justification::centred);
    }
};
