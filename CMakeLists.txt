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
    FORMATS                 VST3
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    PRODUCES_MIDI_OUTPUT    FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME            "VoltageSeq2"
    DESCRIPTION             "Dual-voice 16-step voltage sequencer with FM synthesis"
)

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(VoltageSeq2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
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
