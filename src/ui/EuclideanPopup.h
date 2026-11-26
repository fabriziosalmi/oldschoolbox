#pragma once
#include <JuceHeader.h>
#include <functional>
#include "../Sequencer.h"
#include "../core/Euclidean.h"

// Modal popup invoked via CallOutBox: hits + rotate sliders + live preview.
// Apply replaces the channel's pattern with the euclidean distribution.
class EuclideanPopup : public juce::Component
{
public:
    EuclideanPopup(Sequencer& seq, int channel, std::function<void()> beforeApply = {})
        : sequencer(seq), ch(channel), onBeforeApply(std::move(beforeApply))
    {
        steps = sequencer.getPatternLength(ch);
        hits  = juce::jlimit(1, steps, popCount64(sequencer.getPattern(ch)));
        if (hits == 0) hits = std::max(1, steps / 4);

        hitsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        hitsSlider.setRange(1, steps, 1);
        hitsSlider.setValue(hits, juce::dontSendNotification);
        hitsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 16);
        hitsSlider.onValueChange = [this] { hits = static_cast<int>(hitsSlider.getValue()); repaint(); };
        addAndMakeVisible(hitsSlider);

        rotSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        rotSlider.setRange(0, steps - 1, 1);
        rotSlider.setValue(0, juce::dontSendNotification);
        rotSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 16);
        rotSlider.onValueChange = [this] { rotate = static_cast<int>(rotSlider.getValue()); repaint(); };
        addAndMakeVisible(rotSlider);

        applyButton.setButtonText("APPLY");
        applyButton.onClick = [this] {
            if (onBeforeApply) onBeforeApply();
            const uint64_t pat = Euclidean::generate(hits, steps, rotate);
            sequencer.setPattern(ch, pat);
            if (auto* p = findParentComponentOfClass<juce::CallOutBox>())
                p->dismiss();
        };
        addAndMakeVisible(applyButton);

        setSize(220, 92);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff181828));

        // Labels
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("EUCLIDEAN  (" + juce::String(steps) + " steps)", 4, 2, getWidth() - 8, 12,
                   juce::Justification::centred);

        g.setFont(8.5f);
        g.drawText("HITS",   4, 18, 36, 12, juce::Justification::centredLeft);
        g.drawText("ROTATE", 4, 38, 40, 12, juce::Justification::centredLeft);

        // Preview pattern
        const uint64_t pat = Euclidean::generate(hits, steps, rotate);
        const int visible = std::min(steps, 32);
        const int cellW = (getWidth() - 8) / visible;
        const int y = 64;
        for (int s = 0; s < visible; ++s)
        {
            const bool on = (pat >> s) & 1ULL;
            g.setColour(on ? juce::Colour(0xff4a9eff) : juce::Colour(0xff2a2a3e));
            g.fillRoundedRectangle(static_cast<float>(4 + s * cellW),
                                   static_cast<float>(y),
                                   static_cast<float>(cellW - 1),
                                   8.0f, 1.5f);
        }
    }

    void resized() override
    {
        hitsSlider.setBounds(46, 18, getWidth() - 50, 14);
        rotSlider .setBounds(46, 38, getWidth() - 50, 14);
        applyButton.setBounds(getWidth() - 50, 76, 44, 14);
    }

private:
    static int popCount64(uint64_t x)
    {
        int n = 0; while (x) { n += int(x & 1ULL); x >>= 1; } return n;
    }

    Sequencer& sequencer;
    int ch     = 0;
    std::function<void()> onBeforeApply;
    int hits   = 4;
    int steps  = 16;
    int rotate = 0;

    juce::Slider     hitsSlider, rotSlider;
    juce::TextButton applyButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EuclideanPopup)
};
