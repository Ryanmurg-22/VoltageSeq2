cmake_minimum_required(VERSION 3.22)
project(VoltageSeq2 VERSION 5.1.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Windows: statically link the MSVC runtime ─────────────────────────────────
# With the default dynamic runtime (/MD) the shipped VST3 DLL depends on the
# Visual C++ Redistributable; on a machine that lacks it the DLL silently fails
# to load and the plugin never appears in the DAW. Static linking (/MT) bakes the
# runtime in so the plugin loads on any clean Windows machine. Requires policy
# CMP0091 = NEW, which is the default for our cmake_minimum_required (3.22).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    # Explicit, valid bundle id. The JUCE default (com.<company>.<target>) would
    # otherwise embed the space from the company name and produce an INVALID
    # CFBundleIdentifier. CI further suffixes this per-format (.vst3 / .component)
    # so the two payloads don't collide in the installer.
    BUNDLE_ID               "com.murgatroydinstruments.voltageseq2"
    PLUGIN_MANUFACTURER_CODE Volt
    PLUGIN_CODE             VoS2
    FORMATS                 VST3 AU
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    # NOTE: the keyword is NEEDS_MIDI_OUTPUT, NOT "PRODUCES_MIDI_OUTPUT".
    # juce_add_plugin silently ignores unknown keywords, so the old typo left
    # JucePlugin_ProducesMidiOutput=0 and the VST3/AU MIDI-output (event) bus was
    # never created — the plugin stopped appearing as a MIDI source in DAWs once
    # builds moved from Projucer to CMake.
    NEEDS_MIDI_OUTPUT       TRUE
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
    # MSVC does not define M_PI without this (used by plaits sine_oscillator etc.)
    _USE_MATH_DEFINES=1
)

# ── VST3 identity / backwards-compatibility ───────────────────────────────────
# The VST3 class ID derives from the manufacturer + plugin codes. Historical IDs:
#   'Volt'/'VoS2'  abcdef019182faeb566f6c74566f5332  — natural CMake ID; shipped in PUBLIC releases v5.0 & v5.1
#   'Manu'/'Dftm'  abcdef019182faeb4d616e754466746d  — JUCE defaults (old Projucer builds); pinned in v5.1.1–v5.1.3
#   'SnAc'/'SAK3'  abcdef019182faeb536e416353414b33  — early 2026
#   'Skys'/'Uyzp'  abcdef019182faeb536b797355797a70  — 2025
#
# LAUNCH DECISION: the primary component ID is the NATURAL Volt/VoS2 (NO pin), so
# it matches the public v5.0/v5.1 releases that customers/testers actually
# installed — their projects load by DIRECT ID. Pinning to Manu/Dftm (v5.1.1)
# fixed the author's own old Projucer sessions but orphaned every public user, so
# it is reverted. Volt/VoS2 is also stable going forward (no pin to drift). The
# older IDs — incl. Manu/Dftm — are declared compatible so remap-capable hosts
# can still load pre-v5.0 / author sessions. With no COMPONENT_CLASS pin, both
# the component AND controller derive from Volt/VoS2 (matched pair, as in v5.0/v5.1).
target_compile_definitions(VoltageSeq2 PUBLIC
    "JUCE_VST3_COMPATIBLE_CLASSES=VST3Interface::hexStringToId(\"abcdef019182faeb4d616e754466746d\"),VST3Interface::hexStringToId(\"abcdef019182faeb536e416353414b33\"),VST3Interface::hexStringToId(\"abcdef019182faeb536b797355797a70\")"
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
