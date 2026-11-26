# Contributing to OldSchoolBox

## Prerequisites

- macOS (Apple Silicon or Intel) with Xcode Command Line Tools
- CMake 3.22+ (`brew install cmake`)
- Git with submodules initialized (`git submodule update --init --recursive`)

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

This builds `OldSchoolBox.app` (Standalone), `OldSchoolBox.vst3`, `OldSchoolBox.component` (AU), and all verification probes.

## Verify

Run the full test and DSP probe suite after any change:

```bash
./build/state_probe
./build/acid303_probe
./build/kick_probe
./build/hats_probe
./build/aa_probe
./build/master_output_probe
```

## Pull Requests

- Use Conventional Commits with scope (for example: `feat(dsp):`, `fix(ui):`, `docs:`).
- Keep audio-thread code allocation-free and lock-free.
- Verify all probes pass after your final edit before opening a PR.
