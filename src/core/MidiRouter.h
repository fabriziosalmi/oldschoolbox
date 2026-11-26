#pragma once
#include <JuceHeader.h>
#include "VoiceBank.h"
#include "BusLayout.h"

// Routes incoming MIDI messages to VoiceBank. Stateless — header-only.
struct MidiRouter
{
    static void process(const juce::MidiBuffer& midi, VoiceBank& voices)
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int  ch  = msg.getChannel();

            if (msg.isNoteOn(false))
            {
                const int   note = msg.getNoteNumber();
                const float vel  = msg.getFloatVelocity();

                // Drum notes: any channel except dedicated bass channels
                if (ch != MidiMap::BASS1_CH && ch != MidiMap::BASS2_CH)
                {
                    switch (note)
                    {
                        case MidiMap::KICK:  voices.trigger(OsbBus::KICK,  vel); break;
                        case MidiMap::SNARE: voices.trigger(OsbBus::SNARE, vel); break;
                        case MidiMap::HAT:   voices.trigger(OsbBus::HAT,   vel); break;
                        case MidiMap::TOM:   voices.trigger(OsbBus::TOM,   vel); break;
                        case MidiMap::PERC1: voices.trigger(OsbBus::PERC1, vel); break;
                        case MidiMap::PERC2: voices.trigger(OsbBus::PERC2, vel); break;
                        default: break;
                    }
                }

                if (ch == MidiMap::BASS1_CH) voices.triggerBass(0, note, vel);
                if (ch == MidiMap::BASS2_CH) voices.triggerBass(1, note, vel);
            }

            if (msg.isNoteOff())
            {
                if (ch == MidiMap::BASS1_CH) voices.noteOffBass(0);
                if (ch == MidiMap::BASS2_CH) voices.noteOffBass(1);
            }
        }
    }
};
