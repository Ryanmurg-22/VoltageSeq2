cmake_minimum_required(VERSION 3.22)
project(VoltageSeq2 VERSION 3.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch JUCE 8.0.12 ─────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(VoltageSeq2
    COMPANY_NAME            "Murgatroyd Instruments"
    COMPANY_EMAIL           "ryan@soulcandi.co.za"
    PLUGIN_MANUFACTURER_CODE Volt
    PLUGIN_CODE             VoS2
    FORMATS                 VST3 AU
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    PRODUCES_MIDI_OUTPUT    TRUE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PRODUCT_NAME            "VoltageSeq2"
    DESCRIPTION             "Dual-voice 16-step voltage sequencer with FM synthesis"
)

juce_generate_juce_header(VoltageSeq2)

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(VoltageSeq2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    # Plaits engine wrappers — each unity-includes the relevant Mutable
    # Instruments plaits/stmlib .cc sources, so only these four need listing.
    Source/PlaitsDSP.cpp
    Source/PlaitsWrapper.cpp
    Source/PlaitsEngines.cpp
    Source/PlaitsEngines2.cpp
)

target_include_directories(VoltageSeq2 PRIVATE
    Source
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(VoltageSeq2 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # Mutable Instruments stmlib/plaits: TEST selects the portable desktop code
    # paths (no ARM Cortex-M inline asm, no STM32 hardware headers). The Xcode
    # project passes -DTEST; CMake must do the same or the build won't compile.
    TEST=1
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(VoltageSeq2 PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
