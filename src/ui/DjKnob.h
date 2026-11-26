#pragma once
#include <JuceHeader.h>
#include "../core/MidiLearn.h"
#include "MusicalUnits.h"

// DJ & Live Performance rotary knob:
// - Alt + Drag: Spring-Back Tweaking (snaps back to pre-drag value on mouseUp)
// - Shift + Drag: Surgical fine precision
// - Double-Click: Reset to musical default (APVTS parameter defaultValue)
// - Mouse Wheel: Instant micro-adjust without clicking
// - Right-Click: 1-click MIDI Learn (Shift+Right-Click clears CC mapping)
// - Real Engineering Units in popup & top CRT HUD + Ghost Dot + Live Mod Ring
class DjKnob : public juce::Slider
{
public:
    DjKnob(const juce::String& paramIdIn = {},
           juce::AudioProcessorValueTreeState* apvtsIn = nullptr,
           MidiLearnManager* midiLearnIn = nullptr,
           const juce::String& hudLabelIn = {})
        : paramId(paramIdIn), hudLabel(hudLabelIn), midiLearn(midiLearnIn)
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setScrollWheelEnabled(true);
        setMouseDragSensitivity(150);
        setPopupDisplayEnabled(true, false, nullptr);

        if (apvtsIn && paramId.isNotEmpty())
        {
            if (auto* p = apvtsIn->getParameter(paramId))
            {
                const double defVal = static_cast<double>(p->convertFrom0to1(p->getDefaultValue()));
                setDoubleClickReturnValue(true, defVal);
                setGhostNormalized(p->getDefaultValue());
            }
        }
    }

    const juce::String& getParamId() const { return paramId; }

    void setHudLabel(const juce::String& lbl) { hudLabel = lbl; }

    void setGhostNormalized(float norm01)
    {
        getProperties().set("ghostNorm", std::clamp(norm01, 0.0f, 1.0f));
        repaint();
    }

    void setModOffset(float offset)
    {
        const float cur = static_cast<float>(getProperties()["modOffset"]);
        if (std::abs(cur - offset) > 0.004f)
        {
            getProperties().set("modOffset", offset);
            repaint();
        }
    }

    juce::String getTextFromValue(double value) override
    {
        if (paramId.isNotEmpty())
            return MusicalUnits::format(paramId, value);
        return juce::Slider::getTextFromValue(value);
    }

    void refreshLearnVisual()
    {
        const bool armed = (midiLearn != nullptr && paramId.isNotEmpty() && midiLearn->isLearning(paramId));
        if (static_cast<bool>(getProperties()["midiLearnArmed"]) != armed)
        {
            getProperties().set("midiLearnArmed", armed);
            repaint();
        }
        else if (armed)
        {
            repaint();
        }
    }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseEnter(e);
        if (paramId.isNotEmpty())
            MusicalUnits::publishHud(hudLabel.isNotEmpty() ? hudLabel : paramId, paramId, getValue());
    }

    void valueChanged() override
    {
        juce::Slider::valueChanged();
        if (isMouseOverOrDragging() && paramId.isNotEmpty())
            MusicalUnits::publishHud(hudLabel.isNotEmpty() ? hudLabel : paramId, paramId, getValue());
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (midiLearn && paramId.isNotEmpty())
            {
                if (e.mods.isShiftDown())
                    midiLearn->clearMapping(paramId);
                else
                    midiLearn->toggleLearn(paramId);
                refreshLearnVisual();
            }
            return;
        }

        if (e.mods.isAltDown())
        {
            springActive = true;
            springReturnVal = getValue();
            getProperties().set("springActive", true);
            repaint();
        }
        else
        {
            springActive = false;
        }

        // Surgical precision when Shift is held
        setMouseDragSensitivity(e.mods.isShiftDown() ? 650 : 150);
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;
        setMouseDragSensitivity(e.mods.isShiftDown() ? 650 : 150);
        juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;
        juce::Slider::mouseUp(e);
        if (springActive)
        {
            springActive = false;
            getProperties().set("springActive", false);
            setValue(springReturnVal, juce::sendNotificationSync);
            repaint();
        }
        setMouseDragSensitivity(150);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs(wheel.deltaY) < 1.0e-5f) return;
        const double step = (getMaximum() - getMinimum()) * (e.mods.isShiftDown() ? 0.005 : 0.025);
        const double dir  = (wheel.deltaY > 0.0f) ? 1.0 : -1.0;
        setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + dir * step), juce::sendNotificationSync);
        if (paramId.isNotEmpty())
            MusicalUnits::publishHud(hudLabel.isNotEmpty() ? hudLabel : paramId, paramId, getValue());
    }

private:
    juce::String paramId;
    juce::String hudLabel;
    MidiLearnManager* midiLearn = nullptr;
    bool   springActive    = false;
    double springReturnVal = 0.0;
};
