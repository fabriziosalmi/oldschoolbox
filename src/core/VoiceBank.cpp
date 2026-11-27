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

float VoiceBank::modded(int ch, int targetSlot, const char* id) const
{
    if (!lfoBank) return raw(id);
    const float off = lfoBank->getOffsetFor(ch, targetSlot);
    if (std::abs(off) < 1.0e-5f) return raw(id);
    if (auto* p = apvts.getParameter(id))
    {
        const float norm = std::clamp(p->convertTo0to1(raw(id)) + off, 0.0f, 1.0f);
        return p->convertFrom0to1(norm);
    }
    return raw(id);
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
    // TIGHT performance macro: tightens all 6 drum decays to 0.62x for instant pre-drop punch!
    const float tightDec = (raw(ParamId::TIGHT) > 0.5f) ? 0.62f : 1.0f;
    switch (ch)
    {
        case OsbBus::KICK:
            kick.setParams({ modded(0, 0, ParamId::KICK_TUNE), modded(0, 1, ParamId::KICK_DECAY) * tightDec,
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
            snare.setParams({ modded(1, 0, ParamId::SNARE_TUNE), raw(ParamId::SNARE_DECAY) * tightDec,
                              modded(1, 1, ParamId::SNARE_SNAP), raw(ParamId::SNARE_NOISE) });
            snare.trigger(vel); break;

        case OsbBus::HAT:
            hats.setParams({ modded(2, 0, ParamId::HATS_DECAY) * tightDec, modded(2, 1, ParamId::HATS_TONE),
                             raw(ParamId::HATS_BODY) });
            hats.trigger(vel); break;

        case OsbBus::TOM:
            tom.setParams({ modded(3, 0, ParamId::TOM_TUNE), modded(3, 1, ParamId::TOM_DECAY) * tightDec,
                            raw(ParamId::TOM_PITCHENV), raw(ParamId::TOM_ATTACK) });
            tom.trigger(vel); break;

        case OsbBus::PERC1:
            perc1.setParams({ modded(4, 0, ParamId::PERC1_TUNE), raw(ParamId::PERC1_DECAY) * tightDec,
                              raw(ParamId::PERC1_DETUNE), modded(4, 1, ParamId::PERC1_DRIVE) });
            perc1.trigger(vel); break;

        case OsbBus::PERC2:
            perc2.setParams({ modded(5, 0, ParamId::PERC2_TUNE), raw(ParamId::PERC2_DECAY) * tightDec,
                              raw(ParamId::PERC2_NOISE), modded(5, 1, ParamId::PERC2_PITCH) });
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

        // Read and apply 303 params for this bass channel (with BeatLfo modulation!)
        if (idx == 0)
            bass[0].setParams({ modded(6, 0, ParamId::BASS1_CUTOFF), modded(6, 1, ParamId::BASS1_RESO),
                                raw(ParamId::BASS1_ENVMOD), raw(ParamId::BASS1_DECAY),
                                raw(ParamId::BASS1_ACCENT) });
        else
            bass[1].setParams({ modded(7, 0, ParamId::BASS2_CUTOFF), modded(7, 1, ParamId::BASS2_RESO),
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

    // Refresh bass params every buffer so automation & BeatLfo are tracked smoothly
    if (ch == OsbBus::BASS1)
        bass[0].setParams({ modded(6, 0, ParamId::BASS1_CUTOFF), modded(6, 1, ParamId::BASS1_RESO),
                            raw(ParamId::BASS1_ENVMOD), raw(ParamId::BASS1_DECAY),
                            raw(ParamId::BASS1_ACCENT) });
    else if (ch == OsbBus::BASS2)
        bass[1].setParams({ modded(7, 0, ParamId::BASS2_CUTOFF), modded(7, 1, ParamId::BASS2_RESO),
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
