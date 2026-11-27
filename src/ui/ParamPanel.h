#pragma once
#include <JuceHeader.h>
#include <cstddef>
#include "DjKnob.h"

// A row of up to 6 labelled rotary knobs, each bound to an APVTS parameter.
struct ParamKnobGroup
{
    static constexpr int MAX_PARAMS = 6;

    struct Entry {
        std::unique_ptr<DjKnob>                                                knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  attachment;
        juce::String label;
    };

    std::array<Entry, MAX_PARAMS> entries;
    int count = 0;

    void add(juce::AudioProcessorValueTreeState& apvts,
             const juce::String& paramId,
             const juce::String& label,
             juce::Component& parent,
             juce::Colour accentColour = juce::Colour(0xff00ff47),
             MidiLearnManager* midiLearn = nullptr,
             const juce::String& trackPrefix = {})
    {
        if (count >= MAX_PARAMS) return;
        auto& e = entries[static_cast<std::size_t>(count++)];
        e.label = label;
        const juce::String hudName = trackPrefix.isNotEmpty() ? (trackPrefix + " " + label) : label;
        e.knob  = std::make_unique<DjKnob>(paramId, &apvts, midiLearn, hudName);
        e.knob->setTooltip(hudName + "  (Alt+drag: spring-back | Shift+drag: fine | Right-click: MIDI Learn)");
        e.knob->setColour(juce::Slider::rotarySliderFillColourId, accentColour);
        e.knob->setPopupDisplayEnabled(false, false, nullptr);
        e.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, *e.knob);
        parent.addAndMakeVisible(*e.knob);
    }

    void syncGhosts(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& savedState)
    {
        for (int i = 0; i < count; ++i)
        {
            auto* k = entries[static_cast<std::size_t>(i)].knob.get();
            if (!k) continue;
            const auto pid = k->getParamId();
            if (auto* p = apvts.getParameter(pid))
            {
                float norm = p->getDefaultValue();
                for (int c = 0; c < savedState.getNumChildren(); ++c)
                {
                    const auto child = savedState.getChild(c);
                    if (child.getProperty("id").toString() == pid)
                    {
                        norm = p->convertTo0to1(static_cast<float>(child.getProperty("value")));
                        break;
                    }
                }
                k->setGhostNormalized(norm);
            }
        }
    }

    void refreshMidiLearnVisuals()
    {
        for (int i = 0; i < count; ++i)
            if (entries[static_cast<std::size_t>(i)].knob)
                entries[static_cast<std::size_t>(i)].knob->refreshLearnVisual();
    }
};
