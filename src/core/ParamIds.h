#pragma once

// Single source of truth for all APVTS parameter ID strings.
namespace ParamId
{
    // Volume (8 channels)
    inline const char* vol(int ch)
    {
        static const char* ids[] = {
            "vol_0","vol_1","vol_2","vol_3","vol_4","vol_5","vol_6","vol_7"
        };
        if (ch < 0 || ch > 7) return ids[0];
        return ids[ch];
    }

    // Pan (8 channels) — 0.0 = hard left, 0.5 = centre, 1.0 = hard right
    inline const char* pan(int ch)
    {
        static const char* ids[] = {
            "pan_0","pan_1","pan_2","pan_3","pan_4","pan_5","pan_6","pan_7"
        };
        if (ch < 0 || ch > 7) return ids[0];
        return ids[ch];
    }

    // Kick (6 params)
    constexpr const char* KICK_TUNE     = "kick_tune";
    constexpr const char* KICK_DECAY    = "kick_decay";
    constexpr const char* KICK_PUNCH    = "kick_punch";
    constexpr const char* KICK_PITCHENV = "kick_pitchenv";
    constexpr const char* KICK_DRIVE    = "kick_drive";
    constexpr const char* KICK_SUB      = "kick_sub";

    // Snare
    constexpr const char* SNARE_TUNE  = "snare_tune";
    constexpr const char* SNARE_DECAY = "snare_decay";
    constexpr const char* SNARE_SNAP  = "snare_snap";
    constexpr const char* SNARE_NOISE = "snare_noise";

    // Hats
    constexpr const char* HATS_DECAY = "hats_decay";
    constexpr const char* HATS_TONE  = "hats_tone";
    constexpr const char* HATS_BODY  = "hats_body";

    // Tom
    constexpr const char* TOM_TUNE     = "tom_tune";
    constexpr const char* TOM_DECAY    = "tom_decay";
    constexpr const char* TOM_PITCHENV = "tom_pitchenv";
    constexpr const char* TOM_ATTACK   = "tom_attack";

    // Perc1
    constexpr const char* PERC1_TUNE   = "perc1_tune";
    constexpr const char* PERC1_DECAY  = "perc1_decay";
    constexpr const char* PERC1_DETUNE = "perc1_detune";
    constexpr const char* PERC1_DRIVE  = "perc1_drive";

    // Perc2
    constexpr const char* PERC2_TUNE  = "perc2_tune";
    constexpr const char* PERC2_DECAY = "perc2_decay";
    constexpr const char* PERC2_NOISE = "perc2_noise";
    constexpr const char* PERC2_PITCH = "perc2_pitch";

    // Bass1 (round offbeat) — same param names as Bass2 but indexed
    constexpr const char* BASS1_CUTOFF = "bass1_cutoff";
    constexpr const char* BASS1_RESO   = "bass1_reso";
    constexpr const char* BASS1_ENVMOD = "bass1_envmod";
    constexpr const char* BASS1_DECAY  = "bass1_decay";
    constexpr const char* BASS1_ACCENT = "bass1_accent";

    // Bass2 (acid 303)
    constexpr const char* BASS2_CUTOFF = "bass2_cutoff";
    constexpr const char* BASS2_RESO   = "bass2_reso";
    constexpr const char* BASS2_ENVMOD = "bass2_envmod";
    constexpr const char* BASS2_DECAY  = "bass2_decay";
    constexpr const char* BASS2_ACCENT = "bass2_accent";

    // Sidechain (kick → bass ducking, per-bass amount)
    constexpr const char* SC_BASS1 = "sc_bass1";
    constexpr const char* SC_BASS2 = "sc_bass2";
    constexpr const char* SC_RELEASE = "sc_release";

    // Global
    constexpr const char* SWING = "swing";

    // Master FX
    constexpr const char* MASTER_DRIVE = "master_drive";
    constexpr const char* DELAY_TIME   = "delay_time";
    constexpr const char* DELAY_FB     = "delay_fb";
    constexpr const char* DELAY_MIX    = "delay_mix";
    constexpr const char* DELAY_SYNC   = "delay_sync"; // 0=off, 1=1/4, 2=1/8, 3=1/8d, 4=1/16, 5=1/16d, 6=1/32

    // Master Reverb + Compressor
    constexpr const char* REVERB_SIZE = "reverb_size";
    constexpr const char* REVERB_MIX  = "reverb_mix";
    constexpr const char* COMP_AMOUNT = "comp_amount";

    // MIDI output toggle
    constexpr const char* MIDI_OUT    = "midi_out";

    // Master "TIGHT" toggle — Elektron-style bass tightener (lower mono fold + low-shelf cut)
    constexpr const char* TIGHT       = "tight";

    // AUTO mode — heuristic cruise control (mutates patterns each bar)
    constexpr const char* AUTO_ENABLE   = "auto_enable";
    constexpr const char* AUTO_MUTATION = "auto_mutation";
    constexpr const char* AUTO_DRIFT    = "auto_drift";

    // CHAOS // UNDERGROUND SAMPLE BUS
    constexpr const char* CHAOS_ENABLE  = "chaos_enable";
    constexpr const char* CHAOS_BANK    = "chaos_bank";   // 0..7 factory, 8=custom WAV
    constexpr const char* CHAOS_DENSITY = "chaos_density";
    constexpr const char* CHAOS_GATE    = "chaos_gate";
    constexpr const char* CHAOS_GRIT    = "chaos_grit";
    constexpr const char* CHAOS_PITCH   = "chaos_pitch";
    constexpr const char* CHAOS_VOL     = "chaos_vol";

    // Live FX & Master Sound System controls
    constexpr const char* DUB_THROW     = "dub_throw";    // momentary delay feedback throw
    constexpr const char* MONO_SUB      = "mono_sub";     // M/S mono-maker < 120 Hz
    constexpr const char* DJ_FILTER     = "dj_filter";    // bipolar DJ isolator (-1 LP .. 0 bypass .. +1 HP)
}
