#include "VoiceBank.h"
#include "ParamIds.h"

VoiceBank::VoiceBank(juce::AudioProcessorValueTreeState& a) : apvts(a)
{
    for (auto& t : uiTriggers) t.store(false);
    for (auto& n : uiBassNotes) n.store(48);
    channelVelocity.fill(1.0f);

    // Bass1 = warm sub-reinforced 303 with phase-reset for rock-solid low end
    bass[0].setWaveMix(0.0f);         // pure saw
    bass[0].setSubMix(0.25f);         // clean -1 oct sub layer
    bass[0].setPhaseReset(true);      // consistent phase vs kick
    bass[0].setOutputHighPassHz(0.0f);

    // Bass2 = aggressive acid 303 with 135 Hz HPF so it never clashes with Bass1 sub
    bass[1].setWaveMix(0.65f);        // asymmetric hollow 303 square lean
    bass[1].setSubMix(0.0f);
    bass[1].setPhaseReset(false);
    bass[1].setOutputHighPassHz(135.0f);
}

float VoiceBank::raw(const char* id) const
{
    return apvts.getRawParameterValue(id)->load();
}

void VoiceBank::prepare(double sr)
{
    storedSampleRate = sr;
    kick.prepare(sr);  snare.prepare(sr); hats.prepare(sr);
    tom.prepare(sr);   perc1.prepare(sr); perc2.prepare(sr);
    for (auto& b : bass) b.prepare(sr);

    // Init sidechain attack/release from current param
    const float scRel = raw(ParamId::SC_RELEASE);
    for (auto& sc : sidechain) sc.prepare(sr, scRel);
    fxDuck.prepare(sr, 0.12f); // ~120 ms wet ducking on Master Reverb/Delay
}

// UI thread → sets atomic flag
void VoiceBank::requestTrigger(int ch)
{
    if (validBus(ch))
        uiTriggers[busIndex(ch)].store(true);
}

void VoiceBank::requestBassNote(int idx, int midiNote)
{
    if (validBass(idx))
    {
        uiBassNotes[bassIndex(idx)].store(midiNote);
        uiTriggers[busIndex(OsbBus::BASS1 + idx)].store(true);
    }
}

int VoiceBank::getBassNote(int bassIdx) const
{
    if (validBass(bassIdx))
        return uiBassNotes[bassIndex(bassIdx)].load(std::memory_order_relaxed);
    return 48;
}

// Audio thread → consumes pending UI requests
void VoiceBank::consumeUiTriggers()
{
    for (int i = 0; i < 6; ++i)
        if (uiTriggers[busIndex(i)].exchange(false)) trigger(i, 1.0f);

    for (int i = 0; i < 2; ++i)
        if (uiTriggers[busIndex(OsbBus::BASS1 + i)].exchange(false))
            triggerBass(i, uiBassNotes[bassIndex(i)].load(), 1.0f);
}

// Audio thread → trigger a drum voice (reads fresh params)
void VoiceBank::trigger(int ch, float vel)
{
    if (validBus(ch))
        channelVelocity[busIndex(ch)] = vel;
    switch (ch)
    {
        case OsbBus::KICK:
            kick.setParams({ raw(ParamId::KICK_TUNE), raw(ParamId::KICK_DECAY),
                             raw(ParamId::KICK_PUNCH), raw(ParamId::KICK_PITCHENV),
                             raw(ParamId::KICK_DRIVE), raw(ParamId::KICK_SUB) });
            kick.trigger(vel);

            // Trigger sidechain duckers for both bass channels
            {
                const float scAmt1 = raw(ParamId::SC_BASS1);
                const float scAmt2 = raw(ParamId::SC_BASS2);
                const float scRel  = raw(ParamId::SC_RELEASE);
                sidechain[0].setRelease(storedSampleRate, scRel);
                sidechain[1].setRelease(storedSampleRate, scRel);
                if (scAmt1 > 0.001f) sidechain[0].trigger(scAmt1);
                if (scAmt2 > 0.001f) sidechain[1].trigger(scAmt2);
                fxDuck.trigger(0.45f * vel); // ~-5 dB wet duck on Master Reverb/Delay
            }
            break;

        case OsbBus::SNARE:
            snare.setParams({ raw(ParamId::SNARE_TUNE), raw(ParamId::SNARE_DECAY),
                              raw(ParamId::SNARE_SNAP), raw(ParamId::SNARE_NOISE) });
            snare.trigger(vel); break;

        case OsbBus::HAT:
            hats.setParams({ raw(ParamId::HATS_DECAY), raw(ParamId::HATS_TONE),
                             raw(ParamId::HATS_BODY) });
            hats.trigger(vel); break;

        case OsbBus::TOM:
            tom.setParams({ raw(ParamId::TOM_TUNE), raw(ParamId::TOM_DECAY),
                            raw(ParamId::TOM_PITCHENV), raw(ParamId::TOM_ATTACK) });
            tom.trigger(vel); break;

        case OsbBus::PERC1:
            perc1.setParams({ raw(ParamId::PERC1_TUNE), raw(ParamId::PERC1_DECAY),
                              raw(ParamId::PERC1_DETUNE), raw(ParamId::PERC1_DRIVE) });
            perc1.trigger(vel); break;

        case OsbBus::PERC2:
            perc2.setParams({ raw(ParamId::PERC2_TUNE), raw(ParamId::PERC2_DECAY),
                              raw(ParamId::PERC2_NOISE), raw(ParamId::PERC2_PITCH) });
            perc2.trigger(vel); break;

        default: break;
    }
}

void VoiceBank::triggerBass(int idx, int midiNote, float vel)
{
    triggerBass303(idx, midiNote, vel, false, false);
}

void VoiceBank::triggerBass303(int idx, int midiNote, float vel, bool slide, bool accent)
{
    if (validBass(idx))
    {
        channelVelocity[busIndex(OsbBus::BASS1 + idx)] = vel;
        uiBassNotes[bassIndex(idx)].store(midiNote, std::memory_order_relaxed);

        // Read and apply 303 params for this bass channel
        if (idx == 0)
            bass[0].setParams({ raw(ParamId::BASS1_CUTOFF), raw(ParamId::BASS1_RESO),
                                raw(ParamId::BASS1_ENVMOD), raw(ParamId::BASS1_DECAY),
                                raw(ParamId::BASS1_ACCENT) });
        else
            bass[1].setParams({ raw(ParamId::BASS2_CUTOFF), raw(ParamId::BASS2_RESO),
                                raw(ParamId::BASS2_ENVMOD), raw(ParamId::BASS2_DECAY),
                                raw(ParamId::BASS2_ACCENT) });

        bass[bassIndex(idx)].trigger303(midiNote, vel, slide, accent);
    }
}

void VoiceBank::noteOffBass(int idx)
{
    if (validBass(idx))
        bass[bassIndex(idx)].noteOff();
}

bool VoiceBank::isActive(int ch) const
{
    switch (ch)
    {
        case OsbBus::KICK:  return kick.isActive();
        case OsbBus::SNARE: return snare.isActive();
        case OsbBus::HAT:   return hats.isActive();
        case OsbBus::TOM:   return tom.isActive();
        case OsbBus::PERC1: return perc1.isActive();
        case OsbBus::PERC2: return perc2.isActive();
        case OsbBus::BASS1: return bass[0].isActive();
        case OsbBus::BASS2: return bass[1].isActive();
        default:            return false;
    }
}

void VoiceBank::renderBus(int ch, float* L, float* R, int n, float vol)
{
    const float gain = vol * (validBus(ch) ? channelVelocity[busIndex(ch)] : 1.0f);

    // For bass channels: apply sidechain ducking per-sample
    const bool isBass = (ch == OsbBus::BASS1 || ch == OsbBus::BASS2);
    const int scIdx = ch - OsbBus::BASS1;  // 0 or 1
    const auto sc = isBass ? bassIndex(scIdx) : std::size_t {};

    auto fill = [&](auto& v) {
        for (int i = 0; i < n; ++i)
        {
            float s = v.tick() * gain;
            if (isBass) s *= sidechain[sc].tick();
            L[i] = s;
            R[i] = s;
        }
    };

    // When bass voice is inactive, just tick the sidechain ducker so it decays
    if (isBass && !isActive(ch))
    {
        for (int i = 0; i < n; ++i) sidechain[sc].tick();
        return;  // no audio to render
    }

    // Safety: if called with null buffers, nothing to do
    if (L == nullptr) return;

    // Refresh bass params every buffer so automation is tracked (smoothing is per-sample inside BassVoice)
    if (ch == OsbBus::BASS1)
        bass[0].setParams({ raw(ParamId::BASS1_CUTOFF), raw(ParamId::BASS1_RESO),
                            raw(ParamId::BASS1_ENVMOD), raw(ParamId::BASS1_DECAY),
                            raw(ParamId::BASS1_ACCENT) });
    else if (ch == OsbBus::BASS2)
        bass[1].setParams({ raw(ParamId::BASS2_CUTOFF), raw(ParamId::BASS2_RESO),
                            raw(ParamId::BASS2_ENVMOD), raw(ParamId::BASS2_DECAY),
                            raw(ParamId::BASS2_ACCENT) });

    switch (ch)
    {
        case OsbBus::KICK:  fill(kick);     break;
        case OsbBus::SNARE: fill(snare);    break;
        case OsbBus::HAT:   fill(hats);     break;
        case OsbBus::TOM:   fill(tom);      break;
        case OsbBus::PERC1: fill(perc1);    break;
        case OsbBus::PERC2: fill(perc2);    break;
        case OsbBus::BASS1: fill(bass[0]);  break;
        case OsbBus::BASS2: fill(bass[1]);  break;
        default: break;
    }
}
