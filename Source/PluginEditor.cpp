#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    const juce::Colour bgColour       { 0xff1a1a2e };
    const juce::Colour sectionColour  { 0xff16213e };
    const juce::Colour accentColour   { 0xffe94560 };
    const juce::Colour textColour     { 0xffe0e0e0 };
    const juce::Colour dimColour      { 0xff6a6a8a };
    const juce::Colour gateOnColour   { 0xff00d4aa };
    const juce::Colour gateOffColour  { 0xff2a2a4a };
    const juce::Colour slideOnColour  { 0xffe94560 };   // accent red for slide
    const juce::Colour knobColour     { 0xffe09040 };
}

// ── Layout constants ──────────────────────────────────────────────────────────
// Sequencer panel  (full-width strip at top)
static constexpr int seqPanelX = 5,   seqPanelW = 1240, seqPanelH = 215;
// Step spacing: 16 steps across 1184 px of the seq panel (74 px each)
static constexpr int stepStride = 74;
// Control panels start below the sequencer
static constexpr int ctrlY = 250;    // top of all section panels
static constexpr int ctrlH = 400;    // panel height (fills to bottom of 660 px window)

//==============================================================================
VoltageSeq2AudioProcessorEditor::VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (1250, 660);

    //==========================================================================
    // STEP SLIDERS + GATE BUTTONS + SLIDE BUTTONS
    //==========================================================================
    for (int i = 0; i < 16; ++i)
    {
        // Voltage fader
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

        // Gate button (row 1 — teal)
        bool gateOn = audioProcessor.stepGates[i];
        gateBtn[i].setButtonText ("");
        gateBtn[i].setToggleState (gateOn, juce::dontSendNotification);
        gateBtn[i].setClickingTogglesState (true);
        gateBtn[i].setColour (juce::TextButton::buttonColourId,   gateOn ? gateOnColour  : gateOffColour);
        gateBtn[i].setColour (juce::TextButton::buttonOnColourId, gateOnColour);
        gateBtn[i].onClick = [this, i]() {
            bool s = gateBtn[i].getToggleState();
            audioProcessor.stepGates[i] = s;
            gateBtn[i].setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
        };
        addAndMakeVisible (gateBtn[i]);

        // Slide button (row 2 — accent red)
        bool slideOn = audioProcessor.stepGlides[i];
        slideBtn[i].setButtonText ("");
        slideBtn[i].setToggleState (slideOn, juce::dontSendNotification);
        slideBtn[i].setClickingTogglesState (true);
        slideBtn[i].setColour (juce::TextButton::buttonColourId,   slideOn ? slideOnColour : gateOffColour);
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
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 55, 16);
    bpmSlider.onValueChange = [this]() { audioProcessor.internalBPM = bpmSlider.getValue(); };

    setupKnob (rangeSlider, 0.0, 1.0, audioProcessor.rangeVCA);
    rangeSlider.onValueChange = [this]() { audioProcessor.rangeVCA = (float)rangeSlider.getValue(); };

    setupKnob (portaSlider, 0.0, 2.0, audioProcessor.portamentoTime);
    portaSlider.onValueChange = [this]() { audioProcessor.portamentoTime = (float)portaSlider.getValue(); };

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

    scaleBox.addItem ("Major",         1);
    scaleBox.addItem ("Natural Minor", 2);
    scaleBox.addItem ("Dorian",        3);
    scaleBox.addItem ("Phrygian",      4);
    scaleBox.addItem ("Lydian",        5);
    scaleBox.addItem ("Mixolydian",    6);
    scaleBox.addItem ("Penta Major",   7);
    scaleBox.addItem ("Penta Minor",   8);
    scaleBox.addItem ("Chromatic",     9);
    scaleBox.setSelectedItemIndex (audioProcessor.currentScale, juce::dontSendNotification);
    scaleBox.onChange = [this]() { audioProcessor.currentScale = scaleBox.getSelectedItemIndex(); };
    addAndMakeVisible (scaleBox);

    //==========================================================================
    // OSC 1
    //==========================================================================
    osc1WaveBox.addItem ("Sine",     1);
    osc1WaveBox.addItem ("Saw",      2);
    osc1WaveBox.addItem ("Square",   3);
    osc1WaveBox.addItem ("Triangle", 4);
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
    setupKnob (attackSlider,  0.001, 2.0, audioProcessor.adsrParams.attack);
    attackSlider.onValueChange  = [this]() { audioProcessor.adsrParams.attack  = (float)attackSlider.getValue(); };

    setupKnob (decaySlider,   0.001, 2.0, audioProcessor.adsrParams.decay);
    decaySlider.onValueChange   = [this]() { audioProcessor.adsrParams.decay   = (float)decaySlider.getValue(); };

    setupKnob (sustainSlider, 0.0,   1.0, audioProcessor.adsrParams.sustain);
    sustainSlider.onValueChange = [this]() { audioProcessor.adsrParams.sustain = (float)sustainSlider.getValue(); };

    setupKnob (releaseSlider, 0.001, 3.0, audioProcessor.adsrParams.release);
    releaseSlider.onValueChange = [this]() { audioProcessor.adsrParams.release = (float)releaseSlider.getValue(); };

    //==========================================================================
    // FILTER ENVELOPE
    //==========================================================================
    setupKnob (fAttackSlider,  0.001, 4.0, audioProcessor.filterEnvParams.attack);
    fAttackSlider.onValueChange  = [this]() { audioProcessor.filterEnvParams.attack  = (float)fAttackSlider.getValue(); };

    setupKnob (fDecaySlider,   0.001, 4.0, audioProcessor.filterEnvParams.decay);
    fDecaySlider.onValueChange   = [this]() { audioProcessor.filterEnvParams.decay   = (float)fDecaySlider.getValue(); };

    setupKnob (fSustainSlider, 0.0,   1.0, audioProcessor.filterEnvParams.sustain);
    fSustainSlider.onValueChange = [this]() { audioProcessor.filterEnvParams.sustain = (float)fSustainSlider.getValue(); };

    setupKnob (fReleaseSlider, 0.001, 4.0, audioProcessor.filterEnvParams.release);
    fReleaseSlider.onValueChange = [this]() { audioProcessor.filterEnvParams.release = (float)fReleaseSlider.getValue(); };

    //==========================================================================
    // LFO
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
}

VoltageSeq2AudioProcessorEditor::~VoltageSeq2AudioProcessorEditor() {}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupKnob (juce::Slider& s, double min, double max,
                                                  double val, double skew)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRange (min, max);
    s.setValue (val, juce::dontSendNotification);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, knobColour);
    if (skew > 0.0) s.setSkewFactorFromMidPoint (skew);
    addAndMakeVisible (s);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    // Title
    g.setColour (accentColour);
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText ("VOLTAGE SEQ", 0, 5, getWidth(), 18, juce::Justification::centred);

    // ── Sequencer panel ───────────────────────────────────────────────────────
    g.setColour (sectionColour);
    g.fillRoundedRectangle ((float)seqPanelX, 28.0f, (float)seqPanelW, (float)seqPanelH, 5.0f);

    // 0 V reference line (mid-point of slider range)
    g.setColour (juce::Colour (0xff333366));
    g.drawLine (10.0f, 91.0f, (float)(seqPanelX + seqPanelW - 5), 91.0f, 1.0f);
    g.setColour (dimColour);
    g.setFont (juce::Font (8.0f));
    g.drawText ("0V", seqPanelX + seqPanelW - 30, 83, 28, 10, juce::Justification::left);

    // Row labels for the two button rows
    g.setFont (juce::Font (8.5f, juce::Font::bold));
    g.setColour (gateOnColour);
    g.drawText ("GATE",  1192, 155, 52, 17, juce::Justification::centredLeft);
    g.setColour (slideOnColour);
    g.drawText ("SLIDE", 1192, 174, 52, 17, juce::Justification::centredLeft);

    // ── Section panels ────────────────────────────────────────────────────────
    auto drawPanel = [&](int x, int w, const juce::String& title)
    {
        g.setColour (sectionColour);
        g.fillRoundedRectangle ((float)x, (float)ctrlY, (float)w, (float)ctrlH, 5.0f);
        g.setColour (dimColour);
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.drawText (title, x, ctrlY + 4, w, 14, juce::Justification::centred);
    };

    drawPanel (   5, 130, "SEQ");
    drawPanel ( 140, 165, "QUANTIZER");
    drawPanel ( 310, 155, "OSC 1");
    drawPanel ( 470, 155, "OSC 2");
    drawPanel ( 630, 155, "FILTER");
    drawPanel ( 790, 140, "AMP ENV");
    drawPanel ( 935, 140, "FILTER ENV");
    drawPanel (1080, 165, "LFO");

    // ── Control labels ────────────────────────────────────────────────────────
    const int labelY1 = ctrlY + 22;   // first label row
    const int labelY2 = ctrlY + 66;   // second label row (below first knob set)
    const int labelY3 = ctrlY + 98;   // third label row

    g.setColour (textColour);
    g.setFont (juce::Font (10.0f));

    // SEQ panel
    g.drawText ("BPM",    7,   labelY1,  62, 14, juce::Justification::centred);
    g.drawText ("RANGE",  68,  labelY1,  62, 14, juce::Justification::centred);
    g.drawText ("PORTA",   5,  labelY2, 130, 14, juce::Justification::centred);

    // Quantizer
    g.drawText ("ROOT",  140, labelY1, 165, 14, juce::Justification::centred);
    g.drawText ("SCALE", 140, labelY2, 165, 14, juce::Justification::centred);

    // OSC 1
    g.drawText ("WAVE",  310, labelY1, 155, 14, juce::Justification::centred);
    g.drawText ("LEVEL", 310, labelY2,  70, 14, juce::Justification::centred);
    g.drawText ("OCT",   382, labelY2 + 14, 78, 14, juce::Justification::centred);

    // OSC 2
    g.drawText ("WT POS", 470, labelY1,  80, 14, juce::Justification::centred);
    g.drawText ("LEVEL",  548, labelY1,  77, 14, juce::Justification::centred);
    g.drawText ("OCTAVE", 470, labelY2 + 33, 155, 14, juce::Justification::centred);

    // Filter
    g.drawText ("CUTOFF", 630, labelY1,  78, 14, juce::Justification::centred);
    g.drawText ("RES",    706, labelY1,  79, 14, juce::Justification::centred);
    g.drawText ("ENV AMT",630, labelY2 + 33, 155, 14, juce::Justification::centred);

    // Amp Env
    g.drawText ("ATK",  793, labelY1, 34, 14, juce::Justification::centred);
    g.drawText ("DEC",  825, labelY1, 34, 14, juce::Justification::centred);
    g.drawText ("SUS",  857, labelY1, 34, 14, juce::Justification::centred);
    g.drawText ("REL",  889, labelY1, 34, 14, juce::Justification::centred);

    // Filter Env
    g.drawText ("ATK",  938, labelY1, 34, 14, juce::Justification::centred);
    g.drawText ("DEC",  970, labelY1, 34, 14, juce::Justification::centred);
    g.drawText ("SUS", 1002, labelY1, 34, 14, juce::Justification::centred);
    g.drawText ("REL", 1034, labelY1, 34, 14, juce::Justification::centred);

    // LFO
    g.drawText ("RATE",   1085, labelY1,  55, 14, juce::Justification::centred);
    g.drawText ("DEPTH",  1155, labelY1,  55, 14, juce::Justification::centred);
    g.drawText ("TARGET", 1080, labelY2 + 33, 165, 14, juce::Justification::centred);

    // Step numbers
    g.setColour (dimColour);
    g.setFont (juce::Font (9.0f));
    for (int i = 0; i < 16; ++i)
        g.drawText (juce::String (i + 1),
                    seqPanelX + i * stepStride + 4, 196, 66, 12,
                    juce::Justification::centred);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::resized()
{
    // ── Sequencer strip ───────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        const int bx = seqPanelX + i * stepStride;

        stepKnob[i].setBounds (bx + 4,  32, 66, 118);
        gateBtn[i] .setBounds (bx + 11, 155, 50,  17);
        slideBtn[i].setBounds (bx + 11, 174, 50,  17);
    }

    // ── SEQ transport controls ────────────────────────────────────────────────
    const int cy1 = ctrlY + 39;   // first knob row
    const int cy2 = ctrlY + 83;   // second knob row
    const int cy3 = ctrlY + 115;  // third knob row

    bpmSlider  .setBounds ( 10, cy1,  55, 65);   // includes text box
    rangeSlider.setBounds ( 73, cy1 + 6, 50, 50);
    portaSlider.setBounds ( 38, cy3,  55, 55);

    // ── Quantizer ─────────────────────────────────────────────────────────────
    rootBox .setBounds (145, cy1, 155, 24);
    scaleBox.setBounds (145, cy2, 155, 24);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    osc1WaveBox    .setBounds (315, cy1,       140, 24);
    osc1LevelSlider.setBounds (315, cy2,        55, 55);
    osc1OctaveBox  .setBounds (378, cy2 + 16,   80, 24);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    osc2PosSlider  .setBounds (475, cy1,  55, 55);
    osc2LevelSlider.setBounds (545, cy1,  55, 55);
    osc2OctaveBox  .setBounds (487, cy3, 130, 24);

    // ── Filter ────────────────────────────────────────────────────────────────
    cutoffSlider      .setBounds (635, cy1,  55, 55);
    resonanceSlider   .setBounds (707, cy1,  55, 55);
    filterEnvAmtSlider.setBounds (668, cy3,  55, 55);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    attackSlider .setBounds (795, cy1, 30, 55);
    decaySlider  .setBounds (827, cy1, 30, 55);
    sustainSlider.setBounds (859, cy1, 30, 55);
    releaseSlider.setBounds (891, cy1, 30, 55);

    // ── Filter Envelope ───────────────────────────────────────────────────────
    fAttackSlider .setBounds ( 940, cy1, 30, 55);
    fDecaySlider  .setBounds ( 972, cy1, 30, 55);
    fSustainSlider.setBounds (1004, cy1, 30, 55);
    fReleaseSlider.setBounds (1036, cy1, 30, 55);

    // ── LFO ───────────────────────────────────────────────────────────────────
    lfoRateSlider .setBounds (1085, cy1,  55, 55);
    lfoDepthSlider.setBounds (1155, cy1,  55, 55);
    lfoTargetBox  .setBounds (1090, cy3, 145, 24);
}
