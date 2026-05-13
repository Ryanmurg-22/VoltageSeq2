
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VoltageSeq2AudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor&);
    ~VoltageSeq2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    VoltageSeq2AudioProcessor& audioProcessor;

    // ── Sequencer ─────────────────────────────────────────────────────────────
    juce::Slider     stepKnob[16];
    juce::TextButton gateBtn[16];    // row 1 — gate on/off  (teal when on)
    juce::TextButton slideBtn[16];   // row 2 — slide on/off (accent when on)

    // ── SEQ transport ─────────────────────────────────────────────────────────
    juce::Slider   bpmSlider;
    juce::Slider   rangeSlider;
    juce::Slider   portaSlider;      // global portamento time

    // ── Quantizer ─────────────────────────────────────────────────────────────
    juce::ComboBox rootBox;
    juce::ComboBox scaleBox;

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    juce::ComboBox osc1WaveBox;
    juce::Slider   osc1LevelSlider;
    juce::ComboBox osc1OctaveBox;

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   osc2PosSlider;
    juce::Slider   osc2LevelSlider;
    juce::ComboBox osc2OctaveBox;

    // ── Filter ────────────────────────────────────────────────────────────────
    juce::Slider   cutoffSlider;
    juce::Slider   resonanceSlider;
    juce::Slider   filterEnvAmtSlider;

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    juce::Slider   attackSlider;
    juce::Slider   decaySlider;
    juce::Slider   sustainSlider;
    juce::Slider   releaseSlider;

    // ── Filter Envelope ───────────────────────────────────────────────────────
    juce::Slider   fAttackSlider;
    juce::Slider   fDecaySlider;
    juce::Slider   fSustainSlider;
    juce::Slider   fReleaseSlider;

    // ── LFO ───────────────────────────────────────────────────────────────────
    juce::Slider   lfoRateSlider;
    juce::Slider   lfoDepthSlider;
    juce::ComboBox lfoTargetBox;

    void setupKnob (juce::Slider& s, double min, double max, double val, double skew = 0.0);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessorEditor)
};
