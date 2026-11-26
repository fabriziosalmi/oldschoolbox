#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

// Underground Freetekno / Space Invaders Tekno Sound System (EST. 1998)
// Razor-sharp pixel-art sprites, custom stencil bitmap title renderer,
// procedural weathered-metal noise texture, Tekno spirals, and hardware LED bars.
namespace SpaceArt
{
    inline juce::Font monoFont(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                            size,
                                            bold ? juce::Font::bold : juce::Font::plain));
    }

    // Generate a tileable dark industrial grain / dust noise texture (cached once).
    inline juce::Image makeNoiseTile(int size = 128)
    {
        juce::Image img(juce::Image::ARGB, size, size, true);
        uint32_t rng = 0x23199823u;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                const uint8_t v = static_cast<uint8_t>(rng & 0xFFu);
                // Subtle light/dark dust specks (~3% alpha when drawn)
                const uint8_t a = (v > 180) ? 14 : (v < 45 ? 18 : 0);
                const uint8_t lum = (v > 180) ? 235 : 0;
                img.setPixelAt(x, y, juce::Colour::fromRGBA(lum, lum, lum, a));
            }
        }
        return img;
    }

    // Razor-sharp integer-aligned Space Invaders sprites (zero bilinear blur).
    // variant 0 = Crab (11x8), variant 1 = Octopus (12x8), variant 2 = Squid (9x8)
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
            { 0x084, 0x048, 0x0FC, 0x1B6, 0x3FF, 0x2FD, 0x284, 0x066 }, // arms down
            { 0x084, 0x248, 0x2FC, 0x3B6, 0x3FF, 0x1FE, 0x084, 0x102 }  // arms up
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

        // Crisp subtle halo behind sprite (never softens foreground edges)
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

    // Custom 5x7 razor-sharp pixel-arcade bitmap font for the hero title
    // (replaces generic OS font with authentic arcade/stencil industrial lettering).
    inline void drawPixelTitle(juce::Graphics& g,
                               const char* text,
                               int centreX,
                               int y,
                               int px,
                               juce::Colour col)
    {
        auto glyph = [](char ch) -> const uint8_t* {
            static constexpr uint8_t kO[7] = { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E };
            static constexpr uint8_t kL[7] = { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F };
            static constexpr uint8_t kD[7] = { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E };
            static constexpr uint8_t kS[7] = { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E };
            static constexpr uint8_t kC[7] = { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E };
            static constexpr uint8_t kH[7] = { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 };
            static constexpr uint8_t kB[7] = { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E };
            static constexpr uint8_t kX[7] = { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 };
            static constexpr uint8_t kSp[7]= { 0,0,0,0,0,0,0 };
            switch (ch)
            {
                case 'O': return kO; case 'L': return kL; case 'D': return kD;
                case 'S': return kS; case 'C': return kC; case 'H': return kH;
                case 'B': return kB; case 'X': return kX;
                default:  return kSp;
            }
        };

        int len = 0;
        while (text[len] != '\0') ++len;
        const int charW = 5 * px;
        const int gap   = px + 1;
        const int totalW = len * charW + std::max(0, len - 1) * gap;
        int curX = centreX - totalW / 2;

        g.setColour(col);
        for (int i = 0; i < len; ++i)
        {
            const uint8_t* rows = glyph(text[i]);
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if ((rows[r] >> (4 - c)) & 1u)
                        g.fillRect(curX + c * px, y + r * px, px, px);
            curX += charW + gap;
        }
    }

    // Spiral Tribe / Freetekno hypnotic double-arm spiral
    inline void drawTeknoSpiral(juce::Graphics& g,
                                float cx,
                                float cy,
                                float rx,
                                float ry,
                                float angleOffset,
                                juce::Colour colA,
                                juce::Colour colB,
                                float alpha,
                                float thickness = 1.4f)
    {
        for (int arm = 0; arm < 2; ++arm)
        {
            juce::Path p;
            constexpr int STEPS = 44;
            const float armPhase = angleOffset + static_cast<float>(arm) * juce::MathConstants<float>::pi;
            for (int i = 0; i <= STEPS; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(STEPS);
                const float theta = armPhase + t * juce::MathConstants<float>::twoPi * 2.4f;
                const float px = cx + std::cos(theta) * rx * t;
                const float py = cy + std::sin(theta) * ry * t;
                if (i == 0) p.startNewSubPath(px, py);
                else        p.lineTo(px, py);
            }
            const auto col = (arm == 0) ? colA : colB;
            g.setColour(col.withAlpha(alpha * 0.30f));
            g.strokePath(p, juce::PathStrokeType(thickness * 2.4f));
            g.setColour(col.withAlpha(alpha));
            g.strokePath(p, juce::PathStrokeType(thickness));
        }
    }

    // Recessed CRT monitor bezel with inner shadow, scanlines, and subtle diagonal glass sheen
    inline void drawCrtBezel(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, bool scanlines = true)
    {
        g.setColour(juce::Colour(0xff040507));
        g.fillRoundedRectangle(r, 3.0f);

        // Inner recessed shadow (3px top/left dark lip)
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.50f));
        g.drawRoundedRectangle(r.reduced(2.0f), 2.0f, 1.5f);

        if (scanlines)
        {
            g.setColour(juce::Colours::black.withAlpha(0.18f));
            for (int y = static_cast<int>(r.getY()) + 2; y < static_cast<int>(r.getBottom()) - 2; y += 3)
                g.drawHorizontalLine(y, r.getX() + 2.0f, r.getRight() - 2.0f);
        }

        // Subtle CRT glass diagonal reflection
        juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.045f), r.getX(), r.getY(),
                                   juce::Colours::transparentWhite, r.getX() + r.getWidth() * 0.45f, r.getBottom(), false);
        g.setGradientFill(sheen);
        g.fillRoundedRectangle(r.reduced(1.5f), 2.0f);

        // Outer metallic highlight + accent edge
        g.setColour(accent.withAlpha(0.45f));
        g.drawRoundedRectangle(r, 3.0f, 0.9f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawHorizontalLine(static_cast<int>(r.getBottom()), r.getX() + 3.0f, r.getRight() - 3.0f);
    }

    // Industrial diagonal hazard stripes (///) for PRESSURE // WALL & CHAOS BUS
    inline void drawHazardStripes(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float alpha = 0.28f)
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(r.toNearestInt());
        g.setColour(col.withAlpha(alpha));
        const float step = 8.0f;
        for (float x = r.getX() - r.getHeight(); x < r.getRight() + r.getHeight(); x += step)
        {
            juce::Path stripe;
            stripe.startNewSubPath(x, r.getBottom());
            stripe.lineTo(x + 3.5f, r.getBottom());
            stripe.lineTo(x + 3.5f + r.getHeight(), r.getY());
            stripe.lineTo(x + r.getHeight(), r.getY());
            stripe.closeSubPath();
            g.fillPath(stripe);
        }
    }

    // Industrial Torx / hex rack-corner screw
    inline void drawTorxScrew(juce::Graphics& g, float cx, float cy, float r = 4.2f)
    {
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.fillEllipse(cx - r - 0.8f, cy - r - 0.5f, (r + 0.8f) * 2.0f, (r + 0.8f) * 2.0f);
        juce::ColourGradient metal(juce::Colour(0xff4a4e58), cx - r * 0.4f, cy - r * 0.4f,
                                   juce::Colour(0xff1c1e24), cx + r * 0.6f, cy + r * 0.6f, true);
        g.setGradientFill(metal);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.fillEllipse(cx - r * 0.42f, cy - r * 0.42f, r * 0.84f, r * 0.84f);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawEllipse(cx - r + 0.5f, cy - r + 0.5f, (r - 0.5f) * 2.0f, (r - 0.5f) * 2.0f, 0.6f);
    }

    // Hardware segmented LED bar (green -> yellow -> magenta/red)
    inline void drawLedBar(juce::Graphics& g,
                           juce::Rectangle<float> area,
                           float value,
                           int numSegments,
                           bool vertical = false)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        const int lit = static_cast<int>(std::round(value * static_cast<float>(numSegments)));
        const float gap = 1.5f;

        for (int i = 0; i < numSegments; ++i)
        {
            const float frac = static_cast<float>(i) / static_cast<float>(std::max(1, numSegments - 1));
            juce::Colour c = juce::Colour(0xff00ff55);
            if (frac > 0.78f)      c = juce::Colour(0xffff0055);
            else if (frac > 0.55f) c = juce::Colour(0xffffdd00);

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
                g.setColour(c.withAlpha(0.24f));
                g.fillRect(seg.expanded(0.8f));
                g.setColour(c);
                g.fillRect(seg);
            }
            else
            {
                g.setColour(c.withAlpha(0.08f));
                g.fillRect(seg);
            }
        }
    }
}
