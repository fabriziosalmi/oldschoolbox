# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in OldSchoolBox (such as a crash or memory corruption triggered by malformed WAV/AIFF files, MIDI input, or plugin state XML), please report it privately rather than opening a public issue.

- **Contact**: Use GitHub Security Advisories on this repository (`Security` tab -> `Report a vulnerability`).
- **Response**: We aim to acknowledge reports within 72 hours and coordinate a fix before public disclosure.

## Scope

OldSchoolBox is an offline audio plugin (VST3/AU/Standalone) with no network access (`JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`). Security-relevant boundaries are:
- Custom WAV/AIFF file parsing in `ChaosSampleBus` (8-second cap, stereo/mono decoding)
- Saved plugin state XML/base64 deserialization in `PluginProcessor::setStateInformation`
- Incoming MIDI parsing in `MidiRouter` and `MidiLearnManager`
