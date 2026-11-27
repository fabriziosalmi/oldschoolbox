#pragma once
#include <JuceHeader.h>
#include "../core/MidiLearn.h"
#include "MusicalUnits.h"

// Elektron / FabFilter-grade DJ & Live Performance rotary knob:
// - Cursor Lock (unbounded infinite drag without hitting screen edges)
// - Bipolar 12-o'clock mode + Virtual Magnetic Center Detent (PAN / WASH)
// - In-Place Label <-> Value readout support (no noisy floating JUCE popups!)
// - Alt + Drag: Spring-Back Tweaking (snaps back on mouseUp)
// - Shift + Drag: Surgical fine precision
// - Double-Click: Smart musical default (APVTS defaultValue)
// - Mouse Wheel: Instant micro-adjust
// - Right-Click: 1-click MIDI Learn (Shift+R: clear CC)
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
        setPopupDisplayEnabled(false, false, nullptr); // In-place Label <-> Value replaces floating popups!

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

    // Enable bipolar 12-o'clock LED arc + magnetic virtual center detent
    void setBipolar(bool b)
    {
        bipolar = b;
        getProperties().set("bipolar", b);
        repaint();
    }

    bool isShowingValue() const { return isDraggingNow || isHoveredNow; }

    juce::String getCompactValueText() const
    {
        if (paramId.isNotEmpty())
            return MusicalUnits::formatCompact(paramId, getValue());
        return juce::String(getValue(), 2);
    }

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

    // Virtual Magnetic Center Detent (#14): gently snaps to center when dragging across neutral (unless Shift held)
    double snapValue(double attemptedValue, DragMode dragMode) override
    {
        if (bipolar && dragMode == notDragging)
            return attemptedValue;
        if (bipolar && !juce::ModifierKeys::currentModifiers.isShiftDown())
        {
            const double center = 0.5 * (getMinimum() + getMaximum());
            const double tol    = (getMaximum() - getMinimum()) * 0.035;
            if (std::abs(attemptedValue - center) < tol)
                return center;
        }
        return attemptedValue;
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
        isHoveredNow = true;
        juce::Slider::mouseEnter(e);
        if (paramId.isNotEmpty())
            MusicalUnits::publishHud(hudLabel.isNotEmpty() ? hudLabel : paramId, paramId, getValue());
        if (auto* p = getParentComponent()) p->repaint();
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        isHoveredNow = false;
        juce::Slider::mouseExit(e);
        if (auto* p = getParentComponent()) p->repaint();
    }

    void valueChanged() override
    {
        juce::Slider::valueChanged();
        if ((isMouseOverOrDragging() || isDraggingNow) && paramId.isNotEmpty())
            MusicalUnits::publishHud(hudLabel.isNotEmpty() ? hudLabel : paramId, paramId, getValue());
        if (isDraggingNow)
            if (auto* p = getParentComponent()) p->repaint();
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

        isDraggingNow = true;
        // Cursor Lock (#13): hide cursor & allow infinite drag without hitting screen edges
        e.source.enableUnboundedMouseMovement(true, false);

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

        setMouseDragSensitivity(e.mods.isShiftDown() ? 650 : 150);
        juce::Slider::mouseDown(e);
        if (auto* p = getParentComponent()) p->repaint();
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
        e.source.enableUnboundedMouseMovement(false);
        isDraggingNow = false;
        juce::Slider::mouseUp(e);
        if (springActive)
        {
            springActive = false;
            getProperties().set("springActive", false);
            setValue(springReturnVal, juce::sendNotificationSync);
            repaint();
        }
        setMouseDragSensitivity(150);
        if (auto* p = getParentComponent()) p->repaint();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs(wheel.deltaY) < 1.0e-5f) return;
        // Step in normalized [0..1] proportion so logarithmic/skewed knobs (e.g. 303 Cutoff)
        // move smoothly by equal musical intervals instead of jumping hundreds of Hz!
        const double prop  = valueToProportionOfLength(getValue());
        const double delta = (e.mods.isShiftDown() ? 0.005 : 0.025) * (wheel.deltaY > 0.0f ? 1.0 : -1.0);
        const double next  = proportionOfLengthToValue(juce::jlimit(0.0, 1.0, prop + delta));
        setValue(juce::jlimit(getMinimum(), getMaximum(), next), juce::sendNotificationSync);
        if (paramId.isNotEmpty())
            MusicalUnits::publishHud(hudLabel.isNotEmpty() ? hudLabel : paramId, paramId, getValue());
        if (auto* p = getParentComponent()) p->repaint();
    }

private:
    juce::String paramId;
    juce::String hudLabel;
    MidiLearnManager* midiLearn = nullptr;
    bool   bipolar         = false;
    bool   isHoveredNow    = false;
    bool   isDraggingNow   = false;
    bool   springActive    = false;
    double springReturnVal = 0.0;
};
