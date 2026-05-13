
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Oscilloscope display — shows the pre-filter OSC mix in real-time
//==============================================================================
class OscScopeComponent : public juce::Component,
                          public juce::Timer
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
// Wavetable display — shows OSC 2's morphed waveform mathematically
//==============================================================================
class WavetableDisplayComponent : public juce::Component,
                                  public juce::Timer
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
class VoltageSeq2AudioProcessorEditor  : public juce::AudioProcessorEditor,
                                         public juce::Timer
{
public:
    VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor&);
    ~VoltageSeq2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    VoltageSeq2AudioProcessor& audioProcessor;

    // ── Sequencer ─────────────────────────────────────────────────────────────
    juce::Slider     stepKnob[16];
    juce::TextButton gateBtn[16];
    juce::TextButton slideBtn[16];

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
    juce::Slider   osc1PWMSlider;    // base pulse width

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   osc2PosSlider;
    juce::Slider   osc2LevelSlider;
    juce::ComboBox osc2OctaveBox;

    // ── Filter ────────────────────────────────────────────────────────────────
    juce::Slider   cutoffSlider;
    juce::Slider   resonanceSlider;
    juce::Slider   filterEnvAmtSlider;

    // ── Amp Envelope (larger knobs) ───────────────────────────────────────────
    juce::Slider   attackSlider;
    juce::Slider   decaySlider;
    juce::Slider   sustainSlider;
    juce::Slider   releaseSlider;

    // ── Filter Envelope (larger knobs) ────────────────────────────────────────
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

    // ── Seq length / reset / bipolar ─────────────────────────────────────────
    juce::Slider     seqLengthSlider;
    juce::TextButton resetBtn;
    juce::TextButton bipolarBtn;

    // ── Visualisers ───────────────────────────────────────────────────────────
    OscScopeComponent         oscScope;
    WavetableDisplayComponent wavetableDisplay;

    void setupKnob (juce::Slider& s, double min, double max, double val,
                    double skewMidpoint = 0.0);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessorEditor)
};
