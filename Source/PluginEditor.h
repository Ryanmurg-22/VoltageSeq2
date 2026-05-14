
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "BackplateData.h"

//==============================================================================
// Oscilloscope — live pre-filter OSC mix with zero-crossing trigger
//==============================================================================
class OscScopeComponent : public juce::Component, public juce::Timer
{
public:
    OscScopeComponent (VoltageSeq2AudioProcessor& p) : proc (p) { startTimerHz (30); }
    ~OscScopeComponent() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    VoltageSeq2AudioProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscScopeComponent)
};

//==============================================================================
// Wavetable display — mathematical OSC 2 morph render
//==============================================================================
class WavetableDisplayComponent : public juce::Component, public juce::Timer
{
public:
    WavetableDisplayComponent (VoltageSeq2AudioProcessor& p) : proc (p) { startTimerHz (30); }
    ~WavetableDisplayComponent() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    VoltageSeq2AudioProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableDisplayComponent)
};

//==============================================================================
// Complex envelope display — draws the ADSR curve from parameter values
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
    void timerCallback() override;   // step highlight + alpha dim

private:
    VoltageSeq2AudioProcessor& audioProcessor;

    // ── Sequencer ─────────────────────────────────────────────────────────────
    juce::Slider     stepKnob[16];
    juce::TextButton gateBtn[16];
    juce::TextButton slideBtn[16];

    // ── Sub-strip controls (below sequencer) ─────────────────────────────────
    juce::Slider     seqLengthSlider;
    juce::Slider     swingSlider;
    juce::TextButton playFwdBtn, playRevBtn, playConvBtn, playRndBtn;
    juce::TextButton resetBtn;
    juce::TextButton bipolarBtn;

    // ── Preset save / load ────────────────────────────────────────────────────
    juce::TextButton savePresetBtn, loadPresetBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── SEQ transport ─────────────────────────────────────────────────────────
    juce::Slider     bpmSlider;
    juce::Slider     rangeSlider;
    juce::Slider     portaSlider;
    juce::ComboBox   clockDivBox;
    juce::TextButton runStopBtn;
    juce::TextButton autoBtn;

    // ── Quantizer ─────────────────────────────────────────────────────────────
    juce::ComboBox rootBox;
    juce::ComboBox scaleBox;

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    juce::ComboBox osc1WaveBox;
    juce::Slider   osc1LevelSlider;
    juce::ComboBox osc1OctaveBox;
    juce::Slider   osc1PWMSlider;

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   osc2PosSlider;
    juce::Slider   osc2LevelSlider;
    juce::ComboBox osc2OctaveBox;

    // ── Filter ────────────────────────────────────────────────────────────────
    juce::Slider   cutoffSlider;
    juce::Slider   resonanceSlider;
    juce::Slider   filterEnvAmtSlider;

    // ── Amp Envelope ─────────────────────────────────────────────────────────
    juce::Slider   attackSlider;
    juce::Slider   decaySlider;
    juce::Slider   sustainSlider;
    juce::Slider   releaseSlider;

    // ── Filter Envelope ───────────────────────────────────────────────────────
    juce::Slider   fAttackSlider;
    juce::Slider   fDecaySlider;
    juce::Slider   fSustainSlider;
    juce::Slider   fReleaseSlider;

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfoRateSlider;
    juce::Slider   lfoDepthSlider;
    juce::ComboBox lfoTargetBox;

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo2RateSlider;
    juce::Slider   lfo2DepthSlider;
    juce::ComboBox lfo2TargetBox;

    // ── Complex Envelope 1 ────────────────────────────────────────────────────
    juce::Slider     cenv1AtkSlider, cenv1DecSlider, cenv1SusSlider,
                     cenv1RelSlider, cenv1DepthSlider;
    juce::ComboBox   cenv1DestBox, cenv1DivBox;
    juce::TextButton cenv1LoopBtn, cenv1SyncBtn;
    ComplexEnvDisplay cenv1Display;

    // ── Complex Envelope 2 ────────────────────────────────────────────────────
    juce::Slider     cenv2AtkSlider, cenv2DecSlider, cenv2SusSlider,
                     cenv2RelSlider, cenv2DepthSlider;
    juce::ComboBox   cenv2DestBox, cenv2DivBox;
    juce::TextButton cenv2LoopBtn, cenv2SyncBtn;
    ComplexEnvDisplay cenv2Display;

    // ── OSC scope + WT display ────────────────────────────────────────────────
    OscScopeComponent         oscScope;
    WavetableDisplayComponent wavetableDisplay;

    // ── Backplate ─────────────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> backplate;

    void setupKnob (juce::Slider& s, double min, double max, double val,
                    double skewMidpoint = 0.0);
    void syncUIFromProcessor();   // refresh all widget values after preset load

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessorEditor)
};
