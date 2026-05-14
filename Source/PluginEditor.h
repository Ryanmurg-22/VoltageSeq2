
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
// Complex envelope display — draws ADSR shape from parameter values
//==============================================================================
class ComplexEnvDisplay : public juce::Component, public juce::Timer
{
public:
    ComplexEnvDisplay (const VoltageSeq2AudioProcessor::ComplexEnvParams& p)
        : params (p) { startTimerHz (15); }
    ~ComplexEnvDisplay() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    const VoltageSeq2AudioProcessor::ComplexEnvParams& params;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComplexEnvDisplay)
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

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   osc2PosSlider   [2];
    juce::Slider   osc2LevelSlider [2];
    juce::ComboBox osc2OctaveBox   [2];

    // ── Filter ────────────────────────────────────────────────────────────────
    juce::Slider   cutoffSlider       [2];
    juce::Slider   resonanceSlider    [2];
    juce::Slider   filterEnvAmtSlider [2];

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

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo2RateSlider  [2];
    juce::Slider   lfo2DepthSlider [2];
    juce::ComboBox lfo2TargetBox   [2];

    // ── Complex Envelopes (2 envelopes × 2 voices) ────────────────────────────
    juce::Slider     cenvAtkSlider  [2][2];
    juce::Slider     cenvDecSlider  [2][2];
    juce::Slider     cenvSusSlider  [2][2];
    juce::Slider     cenvRelSlider  [2][2];
    juce::Slider     cenvDepthSlider[2][2];
    juce::ComboBox   cenvDestBox    [2][2];
    juce::ComboBox   cenvDivBox     [2][2];
    juce::TextButton cenvLoopBtn    [2][2];
    juce::TextButton cenvSyncBtn    [2][2];

    // ComplexEnvDisplay holds a const-reference — must be heap-allocated
    std::unique_ptr<ComplexEnvDisplay> cenvDisplay[2][2];

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
    void setupVoice (int v);           // wire up all controls for one voice
    void layoutVoice (int v, int yOff); // position all controls for one voice
    void setupKnob (juce::Slider& s, double min, double max, double val,
                    double skewMidpoint = 0.0);
    void syncUIFromProcessor();        // refresh all widget values after preset load

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessorEditor)
};
