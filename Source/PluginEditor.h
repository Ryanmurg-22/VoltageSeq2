
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "BackplateData.h"

//==============================================================================
// Oscilloscope — live pre-filter waveform with zero-crossing trigger
//==============================================================================
class OscScopeComponent : public juce::Component, public juce::Timer
{
public:
    OscScopeComponent (VoltageSeq2AudioProcessor& p, int voiceIndex)
        : proc (p), vi (voiceIndex) { startTimerHz (30); }
    ~OscScopeComponent() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    VoltageSeq2AudioProcessor& proc;
    int vi;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscScopeComponent)
};

//==============================================================================
// Wavetable display — mathematical OSC 2 morph render
//==============================================================================
class WavetableDisplayComponent : public juce::Component, public juce::Timer
{
public:
    WavetableDisplayComponent (VoltageSeq2AudioProcessor& p, int voiceIndex)
        : proc (p), vi (voiceIndex) { startTimerHz (30); }
    ~WavetableDisplayComponent() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    VoltageSeq2AudioProcessor& proc;
    int vi;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableDisplayComponent)
};

//==============================================================================
class VoltageSeq2AudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::Timer
{
public:
    VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor&);
    ~VoltageSeq2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;   // step highlight + state poll

private:
    VoltageSeq2AudioProcessor& audioProcessor;

    // ── Per-voice controls  [vi][step] or [vi] ───────────────────────────────
    juce::Slider     stepKnob   [2][16];
    juce::TextButton gateBtn    [2][16];
    juce::TextButton slideBtn   [2][16];

    juce::Slider     seqLengthSlider [2];
    juce::Slider     swingSlider     [2];
    juce::TextButton playFwdBtn  [2], playRevBtn  [2];
    juce::TextButton playConvBtn [2], playRndBtn  [2];
    juce::TextButton resetBtn    [2];
    juce::TextButton bipolarBtn  [2];
    juce::TextButton runStopBtn  [2];

    juce::Slider     portaSlider     [2];
    juce::Slider     rangeSlider     [2];
    juce::ComboBox   clockDivBox     [2];

    juce::ComboBox   rootBox   [2];
    juce::ComboBox   scaleBox  [2];

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    juce::ComboBox osc1WaveBox   [2];
    juce::Slider   osc1LevelSlider [2];
    juce::ComboBox osc1OctaveBox [2];
    juce::Slider   osc1PWMSlider [2];
    juce::Slider   osc1FeedbackSlider [2];
    juce::Slider   driftSlider [2];

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   osc2PosSlider   [2];
    juce::Slider   osc2LevelSlider [2];
    juce::ComboBox osc2OctaveBox   [2];

    // ── FM ─────────────────────────────────────────────────────────────────────
    juce::Slider   fmDepthSlider  [2];
    juce::Slider   fmRatioSlider  [2];
    juce::Slider   crossModSlider [2];

    // ── Filter ────────────────────────────────────────────────────────────────
    juce::Slider     cutoffSlider       [2];
    juce::Slider     resonanceSlider    [2];
    juce::Slider     filterEnvAmtSlider [2];
    juce::Slider     filterDriveSlider  [2];
    juce::ComboBox   filterModeBox      [2];
    juce::TextButton filterSlopeBtn     [2];

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    juce::Slider   attackSlider  [2];
    juce::Slider   decaySlider   [2];
    juce::Slider   sustainSlider [2];
    juce::Slider   releaseSlider [2];

    // ── Filter Envelope ───────────────────────────────────────────────────────
    juce::Slider   fAttackSlider  [2];
    juce::Slider   fDecaySlider   [2];
    juce::Slider   fSustainSlider [2];
    juce::Slider   fReleaseSlider [2];

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfoRateSlider  [2];
    juce::Slider   lfoDepthSlider [2];
    juce::ComboBox lfoTargetBox   [2];
    juce::ComboBox lfoWaveBox     [2];
    juce::TextButton lfoSyncBtn   [2];
    juce::ComboBox lfoSyncDivBox  [2];

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo2RateSlider  [2];
    juce::Slider   lfo2DepthSlider [2];
    juce::ComboBox lfo2TargetBox   [2];
    juce::ComboBox lfo2WaveBox     [2];
    juce::TextButton lfo2SyncBtn   [2];
    juce::ComboBox lfo2SyncDivBox  [2];

    // ── LFO 3 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo3RateSlider  [2];
    juce::Slider   lfo3DepthSlider [2];
    juce::ComboBox lfo3TargetBox   [2];
    juce::ComboBox lfo3WaveBox     [2];
    juce::TextButton lfo3SyncBtn   [2];
    juce::ComboBox lfo3SyncDivBox  [2];

    // ── LFO 4 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo4RateSlider  [2];
    juce::Slider   lfo4DepthSlider [2];
    juce::ComboBox lfo4TargetBox   [2];
    juce::ComboBox lfo4WaveBox     [2];
    juce::TextButton lfo4SyncBtn   [2];
    juce::ComboBox lfo4SyncDivBox  [2];

    // ── Mod Envelope (per voice) ───────────────────────────────────────────────
    juce::Slider     modEnvAtkSlider  [2];
    juce::Slider     modEnvDecSlider  [2];
    juce::Slider     modEnvSusSlider  [2];
    juce::Slider     modEnvRelSlider  [2];
    juce::Slider     modEnvDepthSlider[2];
    juce::ComboBox   modEnvDestBox    [2];
    juce::TextButton modEnvSyncBtn    [2];
    juce::ComboBox   modEnvDivBox     [2];

    // ── Per-voice scope / WT displays ─────────────────────────────────────────
    std::unique_ptr<OscScopeComponent>         oscScope       [2];
    std::unique_ptr<WavetableDisplayComponent> wavetableDisplay [2];

    // ── Shared controls (single instance, sits in Voice A panel) ──────────────
    juce::Slider     bpmSlider;
    juce::TextButton autoBtn;
    juce::TextButton savePresetBtn, loadPresetBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── Backplate SVG ─────────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> backplate;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void setupVoice (int v);                          // wire up all controls for one voice
    void layoutVoice (int v, int seqTopY, int ctrlTopY); // position all controls for one voice
    void setupKnob (juce::Slider& s, double min, double max, double val,
                    double skewMidpoint = 0.0);
    void syncUIFromProcessor();        // refresh all widget values after preset load

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessorEditor)
};
