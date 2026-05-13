#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    // ── Black colour scheme ───────────────────────────────────────────────────
    const juce::Colour bgColour      { 0xff000000 };
    const juce::Colour sectionColour { 0xff0c0c18 };
    const juce::Colour accentColour  { 0xffe94560 };
    const juce::Colour textColour    { 0xffe0e0e0 };
    const juce::Colour dimColour     { 0xff6a6a8a };
    const juce::Colour gateOnColour  { 0xff00d4aa };
    const juce::Colour gateOffColour { 0xff161622 };
    const juce::Colour slideOnColour { 0xffe94560 };
    const juce::Colour knobColour    { 0xffe09040 };
    const juce::Colour runColour     { 0xff00d4aa };
    const juce::Colour stopColour    { 0xffe94560 };

    // Active-step highlight colours
    const juce::Colour activeGateOnColour  { 0xffffffff };   // bright white  — gate on  + active
    const juce::Colour activeGateOffColour { 0xff505070 };   // dim slate     — gate off + active

    // ── Layout constants ──────────────────────────────────────────────────────
    constexpr int seqX = 5, seqW = 1340, seqH = 215;
    constexpr int stepStride = 80;

    constexpr int ctrlY = 250;
    constexpr int ctrlH = 400;

    constexpr int lY1 = ctrlY + 22;
    constexpr int lY2 = ctrlY + 66;
    constexpr int lY3 = ctrlY + 99;
    constexpr int lY4 = ctrlY + 131;

    constexpr int cy1 = ctrlY + 39;
    constexpr int cy2 = ctrlY + 83;
    constexpr int cy3 = ctrlY + 116;
    constexpr int cy4 = ctrlY + 148;

    // Sequencer slider geometry (needed to place 0 V line)
    constexpr int stepSliderTop    = 32;
    constexpr int stepSliderHeight = 118;
    constexpr int stepSliderBottom = stepSliderTop + stepSliderHeight;  // 150
}

//==============================================================================
// OscScopeComponent
//==============================================================================
void OscScopeComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.fillAll (juce::Colour (0xff020208));
    g.setColour (juce::Colour (0xff2a2a4a));
    g.drawRect (b, 1);

    const float cy = (float)b.getCentreY();
    g.setColour (juce::Colour (0xff1a1a30));
    g.drawHorizontalLine ((int)cy, 2.0f, (float)(b.getWidth() - 2));

    const int n        = proc.scopeSize;
    const int writePos = proc.scopeWritePos;
    int trigger = 0;
    for (int i = n / 2; i > 1; --i)
    {
        int idx  = (writePos - i     + n) % n;
        int idxP = (writePos - i - 1 + n) % n;
        if (proc.oscScopeBuffer[idxP] <= 0.0f && proc.oscScopeBuffer[idx] > 0.0f)
        { trigger = idx; break; }
    }

    g.setColour (juce::Colour (0xff00d4aa));
    juce::Path wave;
    const int drawW = b.getWidth() - 4;
    for (int x = 0; x < drawW; ++x)
    {
        int   idx = (trigger + (x * n / drawW)) % n;
        float s   = juce::jlimit (-1.0f, 1.0f, proc.oscScopeBuffer[idx]);
        float yPx = cy - s * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + x, yPx);
        else         wave.lineTo          (2.0f + x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.5f));
}

//==============================================================================
// WavetableDisplayComponent
//==============================================================================
void WavetableDisplayComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.fillAll (juce::Colour (0xff020208));
    g.setColour (juce::Colour (0xff2a2a4a));
    g.drawRect (b, 1);

    const float cy = (float)b.getCentreY();
    g.setColour (juce::Colour (0xff1a1a30));
    g.drawHorizontalLine ((int)cy, 2.0f, (float)(b.getWidth() - 2));

    const float tPos  = proc.osc2Position * (float)(proc.numWavetables - 1);
    const int   tA    = (int)tPos;
    const int   tB    = juce::jmin (tA + 1, proc.numWavetables - 1);
    const float blend = tPos - (float)tA;
    const int   ws    = proc.wavetableSize;
    const int   drawW = b.getWidth() - 4;

    g.setColour (juce::Colour (0xffe09040));
    juce::Path wave;
    for (int x = 0; x < drawW; ++x)
    {
        float phase = (float)x / (float)(drawW - 1);
        float rp    = phase * (float)(ws - 1);
        int   ri    = (int)rp;
        float frac  = rp - (float)ri;
        int   riN   = (ri + 1) % ws;

        float sA = proc.wavetables[tA][ri] + frac * (proc.wavetables[tA][riN] - proc.wavetables[tA][ri]);
        float sB = proc.wavetables[tB][ri] + frac * (proc.wavetables[tB][riN] - proc.wavetables[tB][ri]);
        float s  = sA + blend * (sB - sA);

        float yPx = cy - s * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + x, yPx);
        else         wave.lineTo          (2.0f + x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.5f));
}

//==============================================================================
VoltageSeq2AudioProcessorEditor::VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      oscScope (p), wavetableDisplay (p)
{
    setSize (1350, 660);

    //==========================================================================
    // STEP SLIDERS + GATE + SLIDE BUTTONS
    //==========================================================================
    for (int i = 0; i < 16; ++i)
    {
        stepKnob[i].setSliderStyle (juce::Slider::LinearVertical);
        stepKnob[i].setRange (-5.0, 5.0, 0.01);
        stepKnob[i].setValue (audioProcessor.stepVoltages[i], juce::dontSendNotification);
        stepKnob[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        stepKnob[i].setColour (juce::Slider::trackColourId,      knobColour);
        stepKnob[i].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
        stepKnob[i].onValueChange = [this, i]() {
            audioProcessor.stepVoltages[i] = (float)stepKnob[i].getValue();
        };
        addAndMakeVisible (stepKnob[i]);

        bool gOn = audioProcessor.stepGates[i];
        gateBtn[i].setButtonText ("");
        gateBtn[i].setToggleState (gOn, juce::dontSendNotification);
        gateBtn[i].setClickingTogglesState (true);
        gateBtn[i].setColour (juce::TextButton::buttonColourId,   gOn ? gateOnColour : gateOffColour);
        gateBtn[i].setColour (juce::TextButton::buttonOnColourId, gateOnColour);
        gateBtn[i].onClick = [this, i]() {
            bool s = gateBtn[i].getToggleState();
            audioProcessor.stepGates[i] = s;
            // colour will be corrected on next timer tick
        };
        addAndMakeVisible (gateBtn[i]);

        bool sOn = audioProcessor.stepGlides[i];
        slideBtn[i].setButtonText ("");
        slideBtn[i].setToggleState (sOn, juce::dontSendNotification);
        slideBtn[i].setClickingTogglesState (true);
        slideBtn[i].setColour (juce::TextButton::buttonColourId,   sOn ? slideOnColour : gateOffColour);
        slideBtn[i].setColour (juce::TextButton::buttonOnColourId, slideOnColour);
        slideBtn[i].onClick = [this, i]() {
            bool s = slideBtn[i].getToggleState();
            audioProcessor.stepGlides[i] = s;
            slideBtn[i].setColour (juce::TextButton::buttonColourId, s ? slideOnColour : gateOffColour);
        };
        addAndMakeVisible (slideBtn[i]);
    }

    //==========================================================================
    // SEQ TRANSPORT
    //==========================================================================
    setupKnob (bpmSlider, 60.0, 200.0, audioProcessor.internalBPM);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    bpmSlider.onValueChange = [this]() { audioProcessor.internalBPM = bpmSlider.getValue(); };

    setupKnob (rangeSlider, 0.0, 1.0, audioProcessor.rangeVCA);
    rangeSlider.onValueChange = [this]() { audioProcessor.rangeVCA = (float)rangeSlider.getValue(); };

    setupKnob (portaSlider, 0.0, 2.0, audioProcessor.portamentoTime);
    portaSlider.onValueChange = [this]() { audioProcessor.portamentoTime = (float)portaSlider.getValue(); };

    clockDivBox.addItem ("1/4",   1);
    clockDivBox.addItem ("1/8",   2);
    clockDivBox.addItem ("1/16",  3);
    clockDivBox.addItem ("1/8T",  4);
    clockDivBox.addItem ("1/16T", 5);
    clockDivBox.addItem ("1/8.",  6);
    clockDivBox.addItem ("1/16.", 7);
    clockDivBox.setSelectedItemIndex (audioProcessor.clockDivision, juce::dontSendNotification);
    clockDivBox.onChange = [this]() { audioProcessor.clockDivision = clockDivBox.getSelectedItemIndex(); };
    addAndMakeVisible (clockDivBox);

    bool isRunning = audioProcessor.sequencerRunning.load();
    runStopBtn.setButtonText (isRunning ? "STOP" : "RUN");
    runStopBtn.setColour (juce::TextButton::buttonColourId, isRunning ? stopColour : runColour);
    runStopBtn.onClick = [this]()
    {
        bool nowRunning = !audioProcessor.sequencerRunning.load();
        audioProcessor.sequencerRunning.store (nowRunning);
        if (nowRunning)
            audioProcessor.resetOnNextBlock.store (true);
        runStopBtn.setButtonText (nowRunning ? "STOP" : "RUN");
        runStopBtn.setColour (juce::TextButton::buttonColourId, nowRunning ? stopColour : runColour);
    };
    addAndMakeVisible (runStopBtn);

    bool isAuto = audioProcessor.autoRun.load();
    autoBtn.setButtonText ("AUTO");
    autoBtn.setToggleState (isAuto, juce::dontSendNotification);
    autoBtn.setClickingTogglesState (true);
    autoBtn.setColour (juce::TextButton::buttonColourId,   isAuto ? gateOnColour : gateOffColour);
    autoBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    autoBtn.onClick = [this]()
    {
        bool s = autoBtn.getToggleState();
        audioProcessor.autoRun.store (s);
        autoBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addAndMakeVisible (autoBtn);

    // ── Sequence length ────────────────────────────────────────────────────────
    seqLengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    seqLengthSlider.setRange (2.0, 16.0, 1.0);
    seqLengthSlider.setValue (audioProcessor.sequenceLength, juce::dontSendNotification);
    seqLengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 24, 18);
    seqLengthSlider.setColour (juce::Slider::trackColourId,      knobColour);
    seqLengthSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
    seqLengthSlider.setColour (juce::Slider::textBoxTextColourId,       textColour);
    seqLengthSlider.setColour (juce::Slider::textBoxBackgroundColourId, bgColour);
    seqLengthSlider.setColour (juce::Slider::textBoxOutlineColourId,    bgColour);
    seqLengthSlider.onValueChange = [this]()
    {
        audioProcessor.sequenceLength = (int)seqLengthSlider.getValue();
        audioProcessor.resetOnNextBlock.store (true);  // restart cleanly
    };
    addAndMakeVisible (seqLengthSlider);

    // ── Reset voltages to 0 V ─────────────────────────────────────────────────
    resetBtn.setButtonText ("RESET");
    resetBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2050));
    resetBtn.onClick = [this]()
    {
        for (int i = 0; i < 16; ++i)
        {
            audioProcessor.stepVoltages[i] = 0.0f;
            stepKnob[i].setValue (0.0, juce::dontSendNotification);
        }
    };
    addAndMakeVisible (resetBtn);

    // ── Bipolar / Unipolar toggle ─────────────────────────────────────────────
    bool isUni = audioProcessor.unipolar;
    bipolarBtn.setButtonText (isUni ? "UNIPOLAR" : "BIPOLAR");
    bipolarBtn.setToggleState (isUni, juce::dontSendNotification);
    bipolarBtn.setClickingTogglesState (true);
    bipolarBtn.setColour (juce::TextButton::buttonColourId,   isUni ? juce::Colour(0xff305050) : juce::Colour(0xff2a2050));
    bipolarBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff305050));
    bipolarBtn.onClick = [this]()
    {
        bool uni = bipolarBtn.getToggleState();
        audioProcessor.unipolar = uni;
        bipolarBtn.setButtonText (uni ? "UNIPOLAR" : "BIPOLAR");
        bipolarBtn.setColour (juce::TextButton::buttonColourId,
                              uni ? juce::Colour(0xff305050) : juce::Colour(0xff2a2050));

        // Update all step slider ranges; clamp existing values
        for (int i = 0; i < 16; ++i)
        {
            if (uni)
            {
                stepKnob[i].setRange (0.0, 5.0, 0.01);
                float clamped = juce::jmax (0.0f, audioProcessor.stepVoltages[i]);
                audioProcessor.stepVoltages[i] = clamped;
                stepKnob[i].setValue (clamped, juce::dontSendNotification);
            }
            else
            {
                stepKnob[i].setRange (-5.0, 5.0, 0.01);
                stepKnob[i].setValue (audioProcessor.stepVoltages[i], juce::dontSendNotification);
            }
        }
        repaint();   // redraw 0 V line in new position
    };
    addAndMakeVisible (bipolarBtn);

    //==========================================================================
    // QUANTIZER
    //==========================================================================
    rootBox.addItem ("C",  1); rootBox.addItem ("C#", 2);
    rootBox.addItem ("D",  3); rootBox.addItem ("D#", 4);
    rootBox.addItem ("E",  5); rootBox.addItem ("F",  6);
    rootBox.addItem ("F#", 7); rootBox.addItem ("G",  8);
    rootBox.addItem ("G#", 9); rootBox.addItem ("A",  10);
    rootBox.addItem ("A#",11); rootBox.addItem ("B",  12);
    rootBox.setSelectedItemIndex (audioProcessor.rootNote, juce::dontSendNotification);
    rootBox.onChange = [this]() { audioProcessor.rootNote = rootBox.getSelectedItemIndex(); };
    addAndMakeVisible (rootBox);

    scaleBox.addItem ("Major",       1); scaleBox.addItem ("Natural Minor", 2);
    scaleBox.addItem ("Dorian",      3); scaleBox.addItem ("Phrygian",      4);
    scaleBox.addItem ("Lydian",      5); scaleBox.addItem ("Mixolydian",    6);
    scaleBox.addItem ("Penta Major", 7); scaleBox.addItem ("Penta Minor",   8);
    scaleBox.addItem ("Chromatic",   9);
    scaleBox.setSelectedItemIndex (audioProcessor.currentScale, juce::dontSendNotification);
    scaleBox.onChange = [this]() { audioProcessor.currentScale = scaleBox.getSelectedItemIndex(); };
    addAndMakeVisible (scaleBox);

    //==========================================================================
    // OSC 1
    //==========================================================================
    osc1WaveBox.addItem ("Sine",     1); osc1WaveBox.addItem ("Saw",      2);
    osc1WaveBox.addItem ("Square",   3); osc1WaveBox.addItem ("Triangle", 4);
    osc1WaveBox.setSelectedItemIndex (audioProcessor.osc1Waveform, juce::dontSendNotification);
    osc1WaveBox.onChange = [this]() { audioProcessor.osc1Waveform = osc1WaveBox.getSelectedItemIndex(); };
    addAndMakeVisible (osc1WaveBox);

    setupKnob (osc1LevelSlider, 0.0, 1.0, audioProcessor.osc1Level);
    osc1LevelSlider.onValueChange = [this]() { audioProcessor.osc1Level = (float)osc1LevelSlider.getValue(); };

    osc1OctaveBox.addItem ("-2", 1); osc1OctaveBox.addItem ("-1", 2);
    osc1OctaveBox.addItem ("0",  3);
    osc1OctaveBox.addItem ("+1", 4); osc1OctaveBox.addItem ("+2", 5);
    osc1OctaveBox.setSelectedItemIndex (audioProcessor.osc1Octave + 2, juce::dontSendNotification);
    osc1OctaveBox.onChange = [this]() {
        audioProcessor.osc1Octave = osc1OctaveBox.getSelectedItemIndex() - 2;
    };
    addAndMakeVisible (osc1OctaveBox);

    setupKnob (osc1PWMSlider, 0.05, 0.95, audioProcessor.osc1PulseWidth);
    osc1PWMSlider.onValueChange = [this]() {
        audioProcessor.osc1PulseWidth = (float)osc1PWMSlider.getValue();
    };

    //==========================================================================
    // OSC 2
    //==========================================================================
    setupKnob (osc2PosSlider, 0.0, 1.0, audioProcessor.osc2Position);
    osc2PosSlider.onValueChange = [this]() { audioProcessor.osc2Position = (float)osc2PosSlider.getValue(); };

    setupKnob (osc2LevelSlider, 0.0, 1.0, audioProcessor.osc2Level);
    osc2LevelSlider.onValueChange = [this]() { audioProcessor.osc2Level = (float)osc2LevelSlider.getValue(); };

    osc2OctaveBox.addItem ("-2", 1); osc2OctaveBox.addItem ("-1", 2);
    osc2OctaveBox.addItem ("0",  3);
    osc2OctaveBox.addItem ("+1", 4); osc2OctaveBox.addItem ("+2", 5);
    osc2OctaveBox.setSelectedItemIndex (audioProcessor.osc2Octave + 2, juce::dontSendNotification);
    osc2OctaveBox.onChange = [this]() {
        audioProcessor.osc2Octave = osc2OctaveBox.getSelectedItemIndex() - 2;
    };
    addAndMakeVisible (osc2OctaveBox);

    //==========================================================================
    // FILTER
    //==========================================================================
    setupKnob (cutoffSlider, 20.0, 16000.0, audioProcessor.filterCutoff, 1000.0);
    cutoffSlider.onValueChange = [this]() { audioProcessor.filterCutoff = (float)cutoffSlider.getValue(); };

    setupKnob (resonanceSlider, 0.0, 1.0, audioProcessor.filterResonance);
    resonanceSlider.onValueChange = [this]() { audioProcessor.filterResonance = (float)resonanceSlider.getValue(); };

    setupKnob (filterEnvAmtSlider, 0.0, 1.0, audioProcessor.filterEnvAmount);
    filterEnvAmtSlider.onValueChange = [this]() { audioProcessor.filterEnvAmount = (float)filterEnvAmtSlider.getValue(); };

    //==========================================================================
    // AMP ENVELOPE
    //==========================================================================
    setupKnob (attackSlider,  0.001, 2.0, audioProcessor.adsrParams.attack,  0.3);
    attackSlider.onValueChange  = [this]() { audioProcessor.adsrParams.attack  = (float)attackSlider.getValue(); };

    setupKnob (decaySlider,   0.001, 2.0, audioProcessor.adsrParams.decay,   0.3);
    decaySlider.onValueChange   = [this]() { audioProcessor.adsrParams.decay   = (float)decaySlider.getValue(); };

    setupKnob (sustainSlider, 0.0,   1.0, audioProcessor.adsrParams.sustain);
    sustainSlider.onValueChange = [this]() { audioProcessor.adsrParams.sustain = (float)sustainSlider.getValue(); };

    setupKnob (releaseSlider, 0.001, 3.0, audioProcessor.adsrParams.release,  0.3);
    releaseSlider.onValueChange = [this]() { audioProcessor.adsrParams.release = (float)releaseSlider.getValue(); };

    //==========================================================================
    // FILTER ENVELOPE
    //==========================================================================
    setupKnob (fAttackSlider,  0.001, 4.0, audioProcessor.filterEnvParams.attack,  0.3);
    fAttackSlider.onValueChange  = [this]() { audioProcessor.filterEnvParams.attack  = (float)fAttackSlider.getValue(); };

    setupKnob (fDecaySlider,   0.001, 4.0, audioProcessor.filterEnvParams.decay,   0.3);
    fDecaySlider.onValueChange   = [this]() { audioProcessor.filterEnvParams.decay   = (float)fDecaySlider.getValue(); };

    setupKnob (fSustainSlider, 0.0,   1.0, audioProcessor.filterEnvParams.sustain);
    fSustainSlider.onValueChange = [this]() { audioProcessor.filterEnvParams.sustain = (float)fSustainSlider.getValue(); };

    setupKnob (fReleaseSlider, 0.001, 4.0, audioProcessor.filterEnvParams.release,  0.3);
    fReleaseSlider.onValueChange = [this]() { audioProcessor.filterEnvParams.release = (float)fReleaseSlider.getValue(); };

    //==========================================================================
    // LFO 1
    //==========================================================================
    setupKnob (lfoRateSlider, 0.1, 20.0, audioProcessor.lfoRate, 4.0);
    lfoRateSlider.onValueChange  = [this]() { audioProcessor.lfoRate  = (float)lfoRateSlider.getValue(); };

    setupKnob (lfoDepthSlider, 0.0, 1.0, audioProcessor.lfoDepth);
    lfoDepthSlider.onValueChange = [this]() { audioProcessor.lfoDepth = (float)lfoDepthSlider.getValue(); };

    lfoTargetBox.addItem ("PWM",    1);
    lfoTargetBox.addItem ("Cutoff", 2);
    lfoTargetBox.addItem ("Pitch",  3);
    lfoTargetBox.setSelectedItemIndex (audioProcessor.lfoTarget, juce::dontSendNotification);
    lfoTargetBox.onChange = [this]() { audioProcessor.lfoTarget = lfoTargetBox.getSelectedItemIndex(); };
    addAndMakeVisible (lfoTargetBox);

    //==========================================================================
    // LFO 2
    //==========================================================================
    setupKnob (lfo2RateSlider, 0.1, 20.0, audioProcessor.lfo2Rate, 4.0);
    lfo2RateSlider.onValueChange  = [this]() { audioProcessor.lfo2Rate  = (float)lfo2RateSlider.getValue(); };

    setupKnob (lfo2DepthSlider, 0.0, 1.0, audioProcessor.lfo2Depth);
    lfo2DepthSlider.onValueChange = [this]() { audioProcessor.lfo2Depth = (float)lfo2DepthSlider.getValue(); };

    lfo2TargetBox.addItem ("PWM",    1);
    lfo2TargetBox.addItem ("Cutoff", 2);
    lfo2TargetBox.addItem ("Pitch",  3);
    lfo2TargetBox.setSelectedItemIndex (audioProcessor.lfo2Target, juce::dontSendNotification);
    lfo2TargetBox.onChange = [this]() { audioProcessor.lfo2Target = lfo2TargetBox.getSelectedItemIndex(); };
    addAndMakeVisible (lfo2TargetBox);

    //==========================================================================
    // VISUALISERS
    //==========================================================================
    addAndMakeVisible (oscScope);
    addAndMakeVisible (wavetableDisplay);

    // Start the editor-level timer for step highlight + inactive-step dimming
    startTimerHz (30);
}

VoltageSeq2AudioProcessorEditor::~VoltageSeq2AudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
// Timer — updates step button colours and dims steps outside sequence length
//==============================================================================
void VoltageSeq2AudioProcessorEditor::timerCallback()
{
    const int  active  = audioProcessor.currentStep;
    const int  seqLen  = audioProcessor.sequenceLength;
    const bool running = audioProcessor.sequencerRunning.load();

    for (int i = 0; i < 16; ++i)
    {
        const bool inRange  = (i < seqLen);
        const bool isActive = running && inRange && (i == active);
        const bool gateOn   = audioProcessor.stepGates[i];

        // Gate button colour
        juce::Colour col;
        if      (isActive && gateOn)  col = activeGateOnColour;
        else if (isActive && !gateOn) col = activeGateOffColour;
        else if (gateOn)              col = gateOnColour;
        else                          col = gateOffColour;
        gateBtn[i].setColour (juce::TextButton::buttonColourId, col);

        // Dim everything beyond the active sequence length
        const float alpha = inRange ? 1.0f : 0.25f;
        stepKnob[i].setAlpha (alpha);
        gateBtn[i] .setAlpha (alpha);
        slideBtn[i].setAlpha (alpha);
    }
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupKnob (juce::Slider& s, double min, double max,
                                                  double val, double skewMidpoint)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRange (min, max);
    s.setValue (val, juce::dontSendNotification);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, knobColour);
    if (skewMidpoint > 0.0)
        s.setSkewFactorFromMidPoint (skewMidpoint);
    addAndMakeVisible (s);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    // ── Header strip ──────────────────────────────────────────────────────────
    g.setColour (accentColour);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText ("MURGATROYD INSTRUMENTS", 10, 4, 380, 18, juce::Justification::centredLeft);

    g.setColour (textColour);
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText ("VOLTAGE SEQ 2", 0, 4, getWidth(), 18, juce::Justification::centred);

    // ── Sequencer panel ───────────────────────────────────────────────────────
    g.setColour (sectionColour);
    g.fillRoundedRectangle ((float)seqX, 28.0f, (float)seqW, (float)seqH, 5.0f);

    // 0 V reference line — moves to bottom in unipolar mode
    const float zeroY = audioProcessor.unipolar
                        ? (float)stepSliderBottom           // 150 — bottom of sliders
                        : (float)(stepSliderTop + stepSliderHeight / 2);  // 91 — middle
    g.setColour (juce::Colour (0xff333366));
    g.drawLine (10.0f, zeroY, (float)(seqX + seqW - 5), zeroY, 1.0f);
    g.setColour (dimColour);
    g.setFont (juce::Font (8.0f));
    g.drawText ("0V", seqX + seqW - 32, (int)zeroY - 8, 30, 10, juce::Justification::left);

    // Row labels
    g.setFont (juce::Font (8.5f, juce::Font::bold));
    g.setColour (gateOnColour);
    g.drawText ("GATE",  1291, 155, 50, 17, juce::Justification::centredLeft);
    g.setColour (slideOnColour);
    g.drawText ("SLIDE", 1288, 174, 53, 17, juce::Justification::centredLeft);

    // ── Section panels ────────────────────────────────────────────────────────
    auto drawPanel = [&](int x, int w, const juce::String& title)
    {
        g.setColour (sectionColour);
        g.fillRoundedRectangle ((float)x, (float)ctrlY, (float)w, (float)ctrlH, 5.0f);
        g.setColour (dimColour);
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.drawText (title, x, ctrlY + 4, w, 14, juce::Justification::centred);
    };

    drawPanel (   5, 140, "SEQ");
    drawPanel ( 150, 160, "QUANTIZER");
    drawPanel ( 315, 170, "OSC 1");
    drawPanel ( 490, 155, "OSC 2");
    drawPanel ( 650, 155, "FILTER");
    drawPanel ( 810, 175, "AMP ENV");
    drawPanel ( 990, 175, "FILTER ENV");
    drawPanel (1170, 175, "LFO");

    // LFO panel divider
    g.setColour (dimColour.withAlpha (0.5f));
    g.drawLine (1259.0f, (float)(ctrlY + 18), 1259.0f, (float)(ctrlY + ctrlH - 10), 1.0f);

    // ── Control labels ────────────────────────────────────────────────────────
    g.setColour (textColour);
    g.setFont (juce::Font (10.0f));

    // SEQ panel
    g.drawText ("BPM",       7,  lY1,              55, 14, juce::Justification::centred);
    g.drawText ("RANGE",    67,  lY1,              68, 14, juce::Justification::centred);
    g.drawText ("CLOCK DIV", 5,  ctrlY +  87,     140, 14, juce::Justification::centred);
    g.drawText ("PORTA",     5,  ctrlY + 129,     140, 14, juce::Justification::centred);
    g.drawText ("SEQ LEN",   5,  ctrlY + 213,     140, 14, juce::Justification::centred);

    // LFO sub-labels
    g.setColour (dimColour);
    g.setFont (juce::Font (8.5f, juce::Font::bold));
    g.drawText ("LFO 1", 1172, ctrlY + 19,  84, 12, juce::Justification::centred);
    g.drawText ("LFO 2", 1261, ctrlY + 19,  82, 12, juce::Justification::centred);

    g.setColour (textColour);
    g.setFont (juce::Font (10.0f));

    // Quantizer
    g.drawText ("ROOT",  150, lY1, 160, 14, juce::Justification::centred);
    g.drawText ("SCALE", 150, lY2, 160, 14, juce::Justification::centred);

    // OSC 1
    g.drawText ("WAVE",    315, lY1, 170, 14, juce::Justification::centred);
    g.drawText ("LEVEL",   315, lY2,  60, 14, juce::Justification::centred);
    g.drawText ("OCT",     374, lY2, 108, 14, juce::Justification::centred);
    g.drawText ("BASE PW", 315, lY4, 170, 14, juce::Justification::centred);
    g.drawText ("SCOPE",   315, ctrlY + 197, 170, 12, juce::Justification::centred);

    // OSC 2
    g.drawText ("WT POS",  490, lY1,  75, 14, juce::Justification::centred);
    g.drawText ("LEVEL",   563, lY1,  82, 14, juce::Justification::centred);
    g.drawText ("OCTAVE",  490, lY3, 155, 14, juce::Justification::centred);
    g.drawText ("WT VIEW", 490, ctrlY + 148, 155, 12, juce::Justification::centred);

    // Filter
    g.drawText ("CUTOFF",  650, lY1,  72, 14, juce::Justification::centred);
    g.drawText ("RES",     720, lY1,  85, 14, juce::Justification::centred);
    g.drawText ("ENV AMT", 650, lY3, 155, 14, juce::Justification::centred);

    // Amp Env
    g.drawText ("ATK",  815, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("DEC",  857, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("SUS",  899, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("REL",  941, lY1, 40, 14, juce::Justification::centred);

    // Filter Env
    g.drawText ("ATK",  995,  lY1, 40, 14, juce::Justification::centred);
    g.drawText ("DEC", 1037,  lY1, 40, 14, juce::Justification::centred);
    g.drawText ("SUS", 1079,  lY1, 40, 14, juce::Justification::centred);
    g.drawText ("REL", 1121,  lY1, 40, 14, juce::Justification::centred);

    // LFO 1
    g.drawText ("RATE",   1175, lY1,  40, 14, juce::Justification::centred);
    g.drawText ("DEPTH",  1218, lY1,  40, 14, juce::Justification::centred);
    g.drawText ("TARGET", 1172, lY3,  84, 14, juce::Justification::centred);

    // LFO 2
    g.drawText ("RATE",   1263, lY1,  40, 14, juce::Justification::centred);
    g.drawText ("DEPTH",  1306, lY1,  40, 14, juce::Justification::centred);
    g.drawText ("TARGET", 1260, lY3,  84, 14, juce::Justification::centred);

    // Step numbers
    g.setColour (dimColour);
    g.setFont (juce::Font (9.0f));
    for (int i = 0; i < 16; ++i)
        g.drawText (juce::String (i + 1),
                    seqX + i * stepStride + 4, 196, 72, 12,
                    juce::Justification::centred);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::resized()
{
    // ── Sequencer ─────────────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        const int bx = seqX + i * stepStride;
        stepKnob[i].setBounds (bx + 4,  stepSliderTop, 72, stepSliderHeight);
        gateBtn[i] .setBounds (bx + 11, 155, 54, 17);
        slideBtn[i].setBounds (bx + 11, 174, 54, 17);
    }

    // ── SEQ transport ─────────────────────────────────────────────────────────
    bpmSlider      .setBounds (10,  cy1,           52, 62);
    rangeSlider    .setBounds (70,  cy1 + 6,       62, 50);
    clockDivBox    .setBounds (10,  ctrlY + 100,  125, 24);
    portaSlider    .setBounds (44,  ctrlY + 145,   50, 50);
    runStopBtn     .setBounds (10,  ctrlY + 210,  125, 30);
    autoBtn        .setBounds (10,  ctrlY + 250,  125, 24);

    // Sequence length slider — full width of SEQ panel, shows integer value
    seqLengthSlider.setBounds (10,  ctrlY + 228,   95, 24);

    // Reset and bipolar buttons stacked below
    resetBtn       .setBounds (10,  ctrlY + 300,  125, 26);
    bipolarBtn     .setBounds (10,  ctrlY + 334,  125, 26);

    // ── Quantizer ─────────────────────────────────────────────────────────────
    rootBox .setBounds (155, cy1, 150, 24);
    scaleBox.setBounds (155, cy2, 150, 24);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    osc1WaveBox    .setBounds (320, cy1,       160, 24);
    osc1LevelSlider.setBounds (320, cy2,        45, 45);
    osc1OctaveBox  .setBounds (373, cy2 + 10,  105, 24);
    osc1PWMSlider  .setBounds (375, cy4,        45, 45);
    oscScope       .setBounds (317, ctrlY + 210, 163, 178);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    osc2PosSlider    .setBounds (495, cy1,  50, 50);
    osc2LevelSlider  .setBounds (560, cy1,  50, 50);
    osc2OctaveBox    .setBounds (500, cy3, 140, 24);
    wavetableDisplay .setBounds (492, ctrlY + 160, 151, 225);

    // ── Filter ────────────────────────────────────────────────────────────────
    cutoffSlider      .setBounds (655, cy1,  50, 50);
    resonanceSlider   .setBounds (720, cy1,  50, 50);
    filterEnvAmtSlider.setBounds (688, cy3,  50, 50);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    attackSlider .setBounds (815, cy1, 40, 40);
    decaySlider  .setBounds (857, cy1, 40, 40);
    sustainSlider.setBounds (899, cy1, 40, 40);
    releaseSlider.setBounds (941, cy1, 40, 40);

    // ── Filter Envelope ───────────────────────────────────────────────────────
    fAttackSlider .setBounds ( 995, cy1, 40, 40);
    fDecaySlider  .setBounds (1037, cy1, 40, 40);
    fSustainSlider.setBounds (1079, cy1, 40, 40);
    fReleaseSlider.setBounds (1121, cy1, 40, 40);

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    lfoRateSlider .setBounds (1175, cy1, 38, 38);
    lfoDepthSlider.setBounds (1218, cy1, 38, 38);
    lfoTargetBox  .setBounds (1173, cy3, 83, 24);

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    lfo2RateSlider .setBounds (1263, cy1, 38, 38);
    lfo2DepthSlider.setBounds (1306, cy1, 38, 38);
    lfo2TargetBox  .setBounds (1261, cy3, 83, 24);
}
