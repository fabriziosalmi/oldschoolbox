cmake_minimum_required(VERSION 3.22)
project(OldSchoolBox VERSION 0.7.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_plugin(OldSchoolBox
    COMPANY_NAME "FabLab"
    PLUGIN_MANUFACTURER_CODE Fblb
    PLUGIN_CODE Osb0
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "OldSchoolBox"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(OldSchoolBox PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/StepGrid.cpp
    src/core/ParamLayout.cpp
    src/core/VoiceBank.cpp
)

target_include_directories(OldSchoolBox PRIVATE src)

target_compile_definitions(OldSchoolBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_OGGVORBIS=0
)

juce_generate_juce_header(OldSchoolBox)

juce_add_binary_data(OldSchoolBoxAssets
    SOURCES
        assets/space_invaders_logo.png
)

target_link_libraries(OldSchoolBox PRIVATE
    OldSchoolBoxAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

add_executable(acid303_probe tests/acid303_probe.cpp)
target_include_directories(acid303_probe PRIVATE src)
target_compile_features(acid303_probe PRIVATE cxx_std_17)

add_executable(kick_probe tests/kick_probe.cpp)
target_include_directories(kick_probe PRIVATE src)
target_compile_features(kick_probe PRIVATE cxx_std_17)

add_executable(hats_probe tests/hats_probe.cpp)
target_include_directories(hats_probe PRIVATE src)
target_compile_features(hats_probe PRIVATE cxx_std_17)

add_executable(aa_probe tests/aa_probe.cpp)
target_include_directories(aa_probe PRIVATE src tests)
target_compile_features(aa_probe PRIVATE cxx_std_17)

# A/B fidelity probe vs real 303 reference WAVs (references/ is gitignored;
# the probe no-ops gracefully if the samples aren't present).
add_executable(ref303_probe tests/ref303_probe.cpp)
target_include_directories(ref303_probe PRIVATE src tests)
target_compile_features(ref303_probe PRIVATE cxx_std_17)

add_executable(master_output_probe tests/master_output_probe.cpp)
target_include_directories(master_output_probe PRIVATE src)
target_compile_features(master_output_probe PRIVATE cxx_std_17)

add_executable(state_probe
    tests/state_probe.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/StepGrid.cpp
    src/core/ParamLayout.cpp
    src/core/VoiceBank.cpp
)
target_include_directories(state_probe PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/OldSchoolBox_artefacts/JuceLibraryCode)
target_compile_features(state_probe PRIVATE cxx_std_17)
target_compile_definitions(state_probe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_OGGVORBIS=0
    JucePlugin_Name="OldSchoolBox"
)
target_link_libraries(state_probe PRIVATE
    OldSchoolBoxAssets
    juce::juce_audio_utils
    juce::juce_dsp
)
add_dependencies(state_probe OldSchoolBox)
