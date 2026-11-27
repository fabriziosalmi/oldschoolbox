#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

// Underground Freetekno / Space Invaders Tekno Sound System (EST. 1998)
// MKII "Dirty 3D Underground Real" Hardware Visual Engine:
// - Weathered brushed steel + flightcase grime + micro-scratches tile
// - Procedural scuffed bare-metal edge wear chips along module corners
// - Countersunk Torx screws in milled wells
// - Molded 3D CRT/OLED housing with curved glass sheen & phosphor bloom
// - Physical LED bars with ambient light bleed & central lens hotspot
namespace SpaceArt
{
    inline juce::Font monoFont(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                            size,
                                            bold ? juce::Font::bold : juce::Font::plain));
    }

    // Pro geometric sans-serif for hardware titles & silkscreen labels (DIN Alternate / Helvetica Neue on macOS)
    inline juce::Font uiFont(float size, bool bold = true)
    {
        return juce::Font(juce::FontOptions("Helvetica Neue", size,
                                            bold ? juce::Font::bold : juce::Font::plain));
    }

    // Hardware silkscreen micro-typography with +0.07em tracking (#7 / #26)
    inline juce::Font silkFont(float size, bool bold = true)
    {
        return uiFont(size, bold).withExtraKerningFactor(0.07f);
    }

    // De-emphasized engineering unit renderer (#8): bright white number + 50% opacity smaller unit
    inline void drawValueWithUnit(juce::Graphics& g, const juce::String& formatted,
                                  juce::Rectangle<int> bounds, float numSize = 8.5f,
                                  juce::Colour numCol = juce::Colours::white)
    {
        const int sp = formatted.lastIndexOfChar(' ');
        if (sp > 0 && sp < formatted.length() - 1)
        {
            const auto numStr  = formatted.substring(0, sp);
            const auto unitStr = " " + formatted.substring(sp + 1);
            const auto fNum  = monoFont(numSize, true);
            const auto fUnit = monoFont(numSize * 0.84f, false);
            const float wNum  = juce::GlyphArrangement::getStringWidth(fNum, numStr);
            const float wUnit = juce::GlyphArrangement::getStringWidth(fUnit, unitStr);
            const float startX = static_cast<float>(bounds.getCentreX()) - (wNum + wUnit) * 0.5f;

            g.setFont(fNum);
            g.setColour(numCol);
            g.drawText(numStr, static_cast<int>(std::floor(startX)), bounds.getY(),
                       static_cast<int>(std::ceil(wNum)) + 2, bounds.getHeight(), juce::Justification::centredLeft);

            g.setFont(fUnit);
            g.setColour(numCol.withAlpha(0.50f));
            g.drawText(unitStr, static_cast<int>(std::floor(startX + wNum)), bounds.getY(),
                       static_cast<int>(std::ceil(wUnit)) + 3, bounds.getHeight(), juce::Justification::centredLeft);
        }
        else
        {
            g.setFont(monoFont(numSize, true));
            g.setColour(numCol);
            g.drawText(formatted, bounds, juce::Justification::centred);
        }
    }

    // Tileable weathered dark anodized steel + oil/dust mottling + fine flightcase scratches
    inline juce::Image makeNoiseTile(int size = 192)
    {
        juce::Image img(juce::Image::ARGB, size, size, true);
        uint32_t rng = 0x23199823u;
        auto nextRand = [&]() -> uint32_t {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return rng;
        };

        // 1. Brushed horizontal steel grain + dark grime flecks
        for (int y = 0; y < size; ++y)
        {
            const uint8_t rowStreak = static_cast<uint8_t>(nextRand() & 0x1Fu);
            for (int x = 0; x < size; ++x)
            {
                const uint8_t v = static_cast<uint8_t>(nextRand() & 0xFFu);
                if (v > 215)
                    img.setPixelAt(x, y, juce::Colour::fromRGBA(225, 228, 218, 20));
                else if (v < 52)
                    img.setPixelAt(x, y, juce::Colour::fromRGBA(0, 0, 0, 34));
                else if (rowStreak < 7 && (v & 3) == 0)
                    img.setPixelAt(x, y, juce::Colour::fromRGBA(195, 202, 190, 12));
            }
        }

        // 2. Subtle oil/grime blotches + metallic flightcase scratches
        juce::Graphics g(img);
        for (int b = 0; b < 10; ++b)
        {
            const float bx = static_cast<float>(nextRand() % static_cast<uint32_t>(size));
            const float by = static_cast<float>(nextRand() % static_cast<uint32_t>(size));
            const float br = 12.0f + static_cast<float>(nextRand() % 28u);
            juce::ColourGradient grime(juce::Colour::fromRGBA(0, 0, 0, 26), bx, by,
                                       juce::Colours::transparentBlack, bx + br, by + br, true);
            g.setGradientFill(grime);
            g.fillEllipse(bx - br, by - br, br * 2.0f, br * 2.0f);
        }
        for (int s = 0; s < 26; ++s)
        {
            const float x0 = static_cast<float>(nextRand() % static_cast<uint32_t>(size));
            const float y0 = static_cast<float>(nextRand() % static_cast<uint32_t>(size));
            const float len = 5.0f + static_cast<float>(nextRand() % 28u);
            const float ang = static_cast<float>(nextRand() % 360u) * 0.0174533f;
            const bool light = (nextRand() % 3u) != 0;
            g.setColour(light ? juce::Colour::fromRGBA(215, 222, 212, 24)
                              : juce::Colour::fromRGBA(0, 0, 0, 42));
            g.drawLine(x0, y0, x0 + std::cos(ang) * len, y0 + std::sin(ang) * len, 0.75f);
        }
        return img;
    }

    // Procedural bare-metal scuffed paint chips along module edges & corners (flightcase wear!)
    inline void drawEdgeWear(juce::Graphics& g, juce::Rectangle<float> r, uint32_t seed, float intensity = 1.0f)
    {
        uint32_t rng = seed * 2654435761u ^ 0x9E3779B9u;
        auto nextR = [&]() -> uint32_t {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return rng;
        };

        // Corner scuffs (bare brushed silver peeking through chipped black anodizing)
        const juce::Point<float> corners[4] = {
            { r.getX() + 2.0f, r.getY() + 2.0f },
            { r.getRight() - 2.0f, r.getY() + 2.0f },
            { r.getX() + 2.0f, r.getBottom() - 2.0f },
            { r.getRight() - 2.0f, r.getBottom() - 2.0f }
        };
        for (int c = 0; c < 4; ++c)
        {
            if ((nextR() & 3u) == 0) continue; // natural asymmetry
            const float dx = (c % 2 == 0) ? 1.0f : -1.0f;
            const float dy = (c < 2) ? 1.0f : -1.0f;
            const float len = (4.0f + static_cast<float>(nextR() % 9u)) * intensity;
            g.setColour(juce::Colour::fromRGBA(210, 218, 210, static_cast<uint8_t>(42 * intensity)));
            g.drawLine(corners[c].x, corners[c].y, corners[c].x + dx * len, corners[c].y, 1.1f);
            g.drawLine(corners[c].x, corners[c].y, corners[c].x, corners[c].y + dy * (len * 0.7f), 1.0f);
            // Tiny bright metallic speck at corner apex
            g.setColour(juce::Colour::fromRGBA(235, 240, 232, static_cast<uint8_t>(55 * intensity)));
            g.fillRect(corners[c].x - 0.5f, corners[c].y - 0.5f, 2.0f, 1.5f);
        }

        // Edge nicks along top & bottom bezels
        const int nicks = 6;
        for (int i = 0; i < nicks; ++i)
        {
            const bool top = (nextR() & 1u) != 0;
            const float nx = r.getX() + 14.0f + static_cast<float>(nextR() % static_cast<uint32_t>(std::max(16, static_cast<int>(r.getWidth() - 28))));
            const float ny = top ? (r.getY() + 0.8f) : (r.getBottom() - 1.2f);
            const float nw = 2.0f + static_cast<float>(nextR() % 7u);
            g.setColour(juce::Colour::fromRGBA(200, 208, 198, static_cast<uint8_t>(30 * intensity)));
            g.fillRect(nx, ny, nw, 1.2f);
        }
    }

    // 3D Ambient Occlusion & directional chassis bevel for raised steel module plates
    inline void draw3DModuleFrame(juce::Graphics& g, juce::Rectangle<float> r, float cornerR = 4.0f)
    {
        // Outer dark ambient occlusion seam (recessed gap between modules)
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.drawRoundedRectangle(r.expanded(0.8f), cornerR + 0.5f, 1.8f);
        // Bottom-right cast shadow
        g.setColour(juce::Colours::black.withAlpha(0.42f));
        g.drawHorizontalLine(static_cast<int>(r.getBottom()) + 1, r.getX() + 3.0f, r.getRight() - 2.0f);
        // Top-left directional studio specular bevel
        g.setColour(juce::Colours::white.withAlpha(0.11f));
        g.drawHorizontalLine(static_cast<int>(r.getY()) + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawVerticalLine(static_cast<int>(r.getX()) + 1, r.getY() + 4.0f, r.getBottom() - 4.0f);
    }

    // Razor-sharp integer-aligned Space Invaders sprites
    inline void drawInvader(juce::Graphics& g,
                            int variant,
                            int frame,
                            float x,
                            float y,
                            int px,
                            juce::Colour colour,
                            float glowAlpha = 0.18f)
    {
        static constexpr uint16_t kCrab[2][8] = {
            { 0x084, 0x048, 0x0FC, 0x1B6, 0x3FF, 0x2FD, 0x284, 0x066 },
            { 0x084, 0x248, 0x2FC, 0x3B6, 0x3FF, 0x1FE, 0x084, 0x102 }
        };
        static constexpr uint16_t kOcto[2][8] = {
            { 0x0F0, 0x3FC, 0x7FE, 0x6D6, 0x7FE, 0x198, 0x324, 0x606 },
            { 0x0F0, 0x3FC, 0x7FE, 0x6D6, 0x7FE, 0x39C, 0x666, 0x30C }
        };
        static constexpr uint16_t kSquid[2][8] = {
            { 0x030, 0x078, 0x0FC, 0x1B6, 0x1FE, 0x048, 0x0B4, 0x14A },
            { 0x030, 0x078, 0x0FC, 0x1B6, 0x1FE, 0x090, 0x168, 0x0B4 }
        };

        const int f = (frame & 1);
        const uint16_t* rows = (variant == 0) ? kCrab[f]
                             : (variant == 1) ? kOcto[f]
                                              : kSquid[f];
        const int cols = (variant == 1) ? 12 : (variant == 0 ? 11 : 9);
        const int ix = static_cast<int>(std::round(x));
        const int iy = static_cast<int>(std::round(y));
        const int ipx = std::max(1, px);

        if (glowAlpha > 0.01f)
        {
            g.setColour(colour.withAlpha(glowAlpha));
            g.fillRect(ix - 1, iy - 1, cols * ipx + 2, 8 * ipx + 2);
        }

        g.setColour(colour);
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < cols; ++c)
                if ((rows[r] >> (cols - 1 - c)) & 1u)
                    g.fillRect(ix + c * ipx, iy + r * ipx, ipx, ipx);
    }

    // Constant-stroke (1.25px) geometric channel engine icon (#27):
    // 0=Kick sine, 1=Snare burst, 2=Hats diamond, 3=Tom arc, 4=FM wave, 5=Noise step, 6=303 SAW, 7=303 SQR
    inline void drawChannelGlyph(juce::Graphics& g, int ch, float x, float y, float w, float h, juce::Colour col)
    {
        juce::Path p;
        const float cy = y + h * 0.5f;
        switch (ch)
        {
            case 0: // 909 Kick damped sine
                for (int i = 0; i <= 12; ++i)
                {
                    const float t = static_cast<float>(i) / 12.0f;
                    const float py = cy - std::sin(t * 6.28318f * 1.5f) * (1.0f - t * 0.7f) * (h * 0.44f);
                    if (i == 0) p.startNewSubPath(x, py); else p.lineTo(x + t * w, py);
                }
                break;
            case 1: // Snare snap + noise
                p.startNewSubPath(x, cy);
                p.lineTo(x + w * 0.22f, y + 1.0f);
                p.lineTo(x + w * 0.42f, y + h - 1.0f);
                p.lineTo(x + w * 0.62f, cy - h * 0.28f);
                p.lineTo(x + w * 0.80f, cy + h * 0.24f);
                p.lineTo(x + w, cy);
                break;
            case 2: // Hats metallic diamond/cymbal
                p.startNewSubPath(x + w * 0.5f, y + 1.0f);
                p.lineTo(x + w - 1.0f, cy);
                p.lineTo(x + w * 0.5f, y + h - 1.0f);
                p.lineTo(x + 1.0f, cy);
                p.closeSubPath();
                break;
            case 3: // Tom pitch-drop curve
                p.startNewSubPath(x + 1.0f, y + 1.5f);
                p.quadraticTo(x + w * 0.35f, y + h * 0.85f, x + w - 1.0f, y + h - 1.5f);
                break;
            case 4: // FM Perc double-frequency wave
                for (int i = 0; i <= 16; ++i)
                {
                    const float t = static_cast<float>(i) / 16.0f;
                    const float py = cy - std::sin(t * 12.566f + 0.9f * std::sin(t * 25.13f)) * (h * 0.42f);
                    if (i == 0) p.startNewSubPath(x, py); else p.lineTo(x + t * w, py);
                }
                break;
            case 5: // Noise Perc stepped burst
                p.startNewSubPath(x, cy);
                p.lineTo(x + w * 0.2f, y + 2.0f);
                p.lineTo(x + w * 0.35f, y + h - 2.0f);
                p.lineTo(x + w * 0.55f, y + 1.5f);
                p.lineTo(x + w * 0.75f, y + h - 2.5f);
                p.lineTo(x + w, cy);
                break;
            case 6: // 303 SAW wave
                p.startNewSubPath(x, y + h - 1.5f);
                p.lineTo(x + w * 0.5f, y + 1.5f);
                p.lineTo(x + w * 0.5f, y + h - 1.5f);
                p.lineTo(x + w, y + 1.5f);
                break;
            default: // 303 SQR wave
                p.startNewSubPath(x, y + h - 1.5f);
                p.lineTo(x, y + 1.5f);
                p.lineTo(x + w * 0.5f, y + 1.5f);
                p.lineTo(x + w * 0.5f, y + h - 1.5f);
                p.lineTo(x + w, y + h - 1.5f);
                p.lineTo(x + w, y + 1.5f);
                break;
        }
        g.setColour(col.withAlpha(0.75f));
        g.strokePath(p, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Recessed molded 3D CRT/OLED housing: concentric radii (R_inner = R_outer - pad),
    // deep radial glass (#07110B -> #020403), top inset shadow, 1px 7% bottom lip, subtle 3.5% scanlines (#25/#30)
    inline void drawCrtBezel(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, bool scanlines = true, float cornerR = 6.0f,
                             juce::Colour glassCenter = juce::Colour(0xff07110b), float scanAlpha = 0.038f)
    {
        // Concentric outer housing: padding = 3.0f -> outerR = cornerR + 3.0f
        const float pad = 3.0f;
        const float outerR = cornerR + pad;
        const auto outer = r.expanded(pad);
        g.setColour(juce::Colours::black.withAlpha(0.70f));
        g.fillRoundedRectangle(outer.translated(0.8f, 1.4f), outerR);

        juce::ColourGradient housing(juce::Colour(0xff252930), outer.getX(), outer.getY(),
                                     juce::Colour(0xff0d0f13), outer.getX(), outer.getBottom(), false);
        g.setGradientFill(housing);
        g.fillRoundedRectangle(outer, outerR);
        g.setColour(juce::Colours::black.withAlpha(0.82f));
        g.drawRoundedRectangle(outer, outerR, 1.0f);

        // 1px light catch (white @ 7%) on the bottom outer lip of the recessed metal bezel
        g.setColour(juce::Colours::white.withAlpha(0.075f));
        g.drawHorizontalLine(static_cast<int>(outer.getBottom()), outer.getX() + outerR, outer.getRight() - outerR);

        // Deep recessed CRT glass interior: radial gradient glassCenter (accent-tinted) -> #020403
        juce::ColourGradient crtBg(glassCenter.interpolatedWith(accent, 0.09f),
                                   r.getCentreX(), r.getCentreY(),
                                   juce::Colour(0xff020403), r.getX(), r.getY(), true);
        g.setGradientFill(crtBg);
        g.fillRoundedRectangle(r, cornerR);

        // Directional top inset shadow inside the recessed glass (simulates chassis depth)
        {
            juce::Graphics::ScopedSaveState ss(g);
            juce::Path clipP;
            clipP.addRoundedRectangle(r, cornerR);
            g.reduceClipRegion(clipP);
            juce::ColourGradient topInset(juce::Colours::black.withAlpha(0.68f), r.getX(), r.getY(),
                                          juce::Colours::transparentBlack, r.getX(), r.getY() + 14.0f, false);
            g.setGradientFill(topInset);
            g.fillRect(r.getX(), r.getY(), r.getWidth(), 14.0f);

            if (scanlines)
            {
                // Whisper-subtle theme-calibrated CRT scanlines
                g.setColour(juce::Colours::white.withAlpha(scanAlpha));
                for (int y = static_cast<int>(r.getY()) + 2; y < static_cast<int>(r.getBottom()) - 2; y += 3)
                    g.drawHorizontalLine(y, r.getX() + 2.0f, r.getRight() - 2.0f);
            }
        }

        // Subtle curved glass studio reflection
        juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.045f), r.getX(), r.getY(),
                                   juce::Colours::transparentWhite, r.getX() + r.getWidth() * 0.50f, r.getY() + r.getHeight() * 0.55f, false);
        g.setGradientFill(sheen);
        g.fillRoundedRectangle(r.reduced(1.5f), std::max(2.0f, cornerR - 1.5f));

        // Clean inner accent rim
        g.setColour(accent.withAlpha(0.34f));
        g.drawRoundedRectangle(r, cornerR, 0.9f);
    }

    // Industrial diagonal hazard stripes (///) with weathered contrast
    inline void drawHazardStripes(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float alpha = 0.45f)
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(r.toNearestInt());
        g.setColour(juce::Colour(0xff090b0a).withAlpha(0.88f));
        g.fillRect(r);
        g.setColour(col.withAlpha(alpha));
        const float step = 9.0f;
        for (float x = r.getX() - r.getHeight(); x < r.getRight() + r.getHeight(); x += step)
        {
            juce::Path stripe;
            stripe.startNewSubPath(x, r.getBottom());
            stripe.lineTo(x + 4.2f, r.getBottom());
            stripe.lineTo(x + 4.2f + r.getHeight(), r.getY());
            stripe.lineTo(x + r.getHeight(), r.getY());
            stripe.closeSubPath();
            g.fillPath(stripe);
        }
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRect(r, 0.8f);
    }

    // Countersunk Torx screw in milled chamfered steel well
    inline void drawTorxScrew(juce::Graphics& g, float cx, float cy, float r = 4.2f)
    {
        // Countersunk well shadow + outer chamfer highlight
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillEllipse(cx - r - 1.6f, cy - r - 0.6f, (r + 1.6f) * 2.0f, (r + 1.6f) * 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.92f));
        g.fillEllipse(cx - r - 1.4f, cy - r - 1.4f, (r + 1.4f) * 2.0f, (r + 1.4f) * 2.0f);

        // Brushed steel screw head
        juce::ColourGradient metal(juce::Colour(0xff6b726a), cx - r * 0.45f, cy - r * 0.45f,
                                   juce::Colour(0xff181b18), cx + r * 0.65f, cy + r * 0.65f, true);
        g.setGradientFill(metal);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

        // Torx / hex socket recess
        g.setColour(juce::Colours::black.withAlpha(0.82f));
        g.fillEllipse(cx - r * 0.44f, cy - r * 0.44f, r * 0.88f, r * 0.88f);
        g.setColour(juce::Colour(0xff454a45));
        g.drawLine(cx - r * 0.35f, cy, cx + r * 0.35f, cy, 0.7f);
        g.drawLine(cx, cy - r * 0.35f, cx, cy + r * 0.35f, 0.7f);

        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.drawEllipse(cx - r + 0.5f, cy - r + 0.5f, (r - 0.5f) * 2.0f, (r - 0.5f) * 2.0f, 0.6f);
    }

    // Physical LED bar with lens hotspot & ambient light bleed
    inline void drawLedBar(juce::Graphics& g,
                           juce::Rectangle<float> area,
                           float value,
                           int numSegments,
                           bool vertical = false,
                           bool cyanBottom = false)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        const int lit = static_cast<int>(std::round(value * static_cast<float>(numSegments)));
        const float gap = 1.6f;

        for (int i = 0; i < numSegments; ++i)
        {
            const float frac = static_cast<float>(i) / static_cast<float>(std::max(1, numSegments - 1));
            juce::Colour c = cyanBottom
                ? (frac < 0.30f ? juce::Colour(0xff14d8ff) : juce::Colour(0xff52ff2a))
                : juce::Colour(0xff52ff2a);
            if (frac > 0.84f)      c = juce::Colour(0xffff2944);
            else if (frac > 0.68f) c = juce::Colour(0xffff841a);
            else if (frac > 0.50f) c = juce::Colour(0xfff5e027);

            juce::Rectangle<float> seg;
            if (vertical)
            {
                const float segH = (area.getHeight() - gap * static_cast<float>(numSegments - 1)) / static_cast<float>(numSegments);
                const float sy = area.getBottom() - static_cast<float>(i + 1) * segH - static_cast<float>(i) * gap;
                seg = { area.getX(), sy, area.getWidth(), std::max(1.5f, segH) };
            }
            else
            {
                const float segW = (area.getWidth() - gap * static_cast<float>(numSegments - 1)) / static_cast<float>(numSegments);
                const float sx = area.getX() + static_cast<float>(i) * (segW + gap);
                seg = { sx, area.getY(), std::max(1.5f, segW), area.getHeight() };
            }

            if (i < lit)
            {
                // Soft ambient light bleed
                g.setColour(c.withAlpha(0.28f));
                g.fillRoundedRectangle(seg.expanded(1.2f), 1.5f);
                // Core LED body
                g.setColour(c);
                g.fillRoundedRectangle(seg, 1.0f);
                // Central physical lens hotspot
                g.setColour(juce::Colours::white.withAlpha(0.32f));
                g.fillRect(seg.reduced(seg.getWidth() * 0.28f, seg.getHeight() * 0.28f));
            }
            else
            {
                g.setColour(c.withAlpha(0.08f));
                g.fillRoundedRectangle(seg, 1.0f);
            }
        }
    }
}
