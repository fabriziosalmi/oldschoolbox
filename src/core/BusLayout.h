#pragma once
#include <JuceHeader.h>

namespace OsbBus
{
    constexpr int KICK  = 0;
    constexpr int SNARE = 1;
    constexpr int HAT   = 2;
    constexpr int TOM   = 3;
    constexpr int PERC1 = 4;
    constexpr int PERC2 = 5;
    constexpr int BASS1 = 6;
    constexpr int BASS2 = 7;
    constexpr int COUNT = 8;

    constexpr const char* NAMES[] = {
        "Kick","Snare","Hats","Tom","Perc1","Perc2","Bass1","Bass2"
    };


}

namespace MidiMap
{
    constexpr int KICK     = 36;
    constexpr int SNARE    = 38;
    constexpr int HAT      = 42;
    constexpr int TOM      = 45;
    constexpr int PERC1    = 48;
    constexpr int PERC2    = 50;
    constexpr int BASS1_CH = 1;
    constexpr int BASS2_CH = 2;
}
