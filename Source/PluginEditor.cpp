#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    // ── Colours ───────────────────────────────────────────────────────────────
    const juce::Colour bgColour           { 0xff000000 };
    const juce::Colour sectionColour      { 0xff0c0c18 };
    const juce::Colour subStripColour     { 0xff080814 };
    const juce::Colour accentColour       { 0xffe94560 };
    const juce::Colour textColour         { 0xffe0e0e0 };
    const juce::Colour dimColour          { 0xff6a6a8a };
    const juce::Colour gateOnColour       { 0xff00d4aa };
    const juce::Colour gateOffColour      { 0xff161622 };
    const juce::Colour slideOnColour      { 0xffe94560 };
    const juce::Colour knobColour         { 0xffe09040 };
    const juce::Colour runColour          { 0xff00d4aa };
    const juce::Colour stopColour         { 0xffe94560 };
    const juce::Colour activeGateOnColour { 0xffffffff };
    const juce::Colour activeGateOffColour{ 0xff505070 };
    const juce::Colour playBtnOn          { 0xff2255aa };
    const juce::Colour playBtnOff         { 0xff161630 };
    const juce::Colour envPanelColour     { 0xff090912 };

    // ── Layout: sequencer ─────────────────────────────────────────────────────
    constexpr int seqX = 5, seqW = 1340, seqH = 215;
    constexpr int stepStride = 80;
    constexpr int stepSliderTop    = 32;
    constexpr int stepSliderHeight = 118;
    constexpr int stepSliderBottom = stepSliderTop + stepSliderHeight;  // 150

    // ── Sub-strip (SEQ LEN, play order, reset, bipolar) ──────────────────────
    constexpr int stripY = 246;
    constexpr int stripH = 42;

    // ── Control panels row ────────────────────────────────────────────────────
    constexpr int ctrlY = 295;
    constexpr int ctrlH = 360;

    constexpr int lY1 = ctrlY + 22;
    constexpr int lY2 = ctrlY + 66;
    constexpr int lY3 = ctrlY + 99;
    constexpr int lY4 = ctrlY + 131;

    constexpr int cy1 = ctrlY + 39;
    constexpr int cy2 = ctrlY + 83;
    constexpr int cy3 = ctrlY + 116;
    constexpr int cy4 = ctrlY + 148;

    // ── Complex envelope row ──────────────────────────────────────────────────
    constexpr int envY  = 663;
    constexpr int envH  = 225;
    constexpr int envPW = 660;   // panel width (two panels: x=5 and x=680)

    // Helper: x-coordinate of controls within an env panel given panel origin pX
    // knobs start at pX+15, spaced 60px
    // second row at envY+100, display at pX+390
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

    const int n = proc.scopeSize, writePos = proc.scopeWritePos;
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
        float s   = juce::jlimit (-1.0f, 1.0f, proc.oscScopeBuffer[(trigger + x * n / drawW) % n]);
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

    const float tPos = proc.osc2Position * (float)(proc.numWavetables - 1);
    const int tA = (int)tPos, tB = juce::jmin (tA + 1, proc.numWavetables - 1);
    const float blend = tPos - (float)tA;
    const int ws = proc.wavetableSize, drawW = b.getWidth() - 4;

    g.setColour (juce::Colour (0xffe09040));
    juce::Path wave;
    for (int x = 0; x < drawW; ++x)
    {
        float rp = (float)x / (float)(drawW - 1) * (float)(ws - 1);
        int ri = (int)rp; float frac = rp - ri; int riN = (ri + 1) % ws;
        float sA = proc.wavetables[tA][ri] + frac * (proc.wavetables[tA][riN] - proc.wavetables[tA][ri]);
        float sB = proc.wavetables[tB][ri] + frac * (proc.wavetables[tB][riN] - proc.wavetables[tB][ri]);
        float yPx = cy - (sA + blend * (sB - sA)) * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + x, yPx);
        else         wave.lineTo          (2.0f + x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.5f));
}

//==============================================================================
// ComplexEnvDisplay — draws a proportional ADSR curve from the param struct
//==============================================================================
void ComplexEnvDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().reduced (1);
    g.fillAll (juce::Colour (0xff020208));
    g.setColour (juce::Colour (0xff2a2a4a));
    g.drawRect (getLocalBounds(), 1);

    // Proportional widths: A + D + [sustain display] + R
    const float aSec = params.attack;
    const float dSec = params.decay;
    const float rSec = params.release;
    const float sSec = 0.3f;                         // fixed sustain display weight
    const float total = aSec + dSec + sSec + rSec;

    const float w  = (float)b.getWidth()  - 2.0f;
    const float h  = (float)b.getHeight() - 4.0f;
    const float x0 = (float)b.getX()      + 1.0f;
    const float y0 = (float)b.getY()      + 2.0f;

    const float aW = (aSec / total) * w;
    const float dW = (dSec / total) * w;
    const float sW = (sSec / total) * w;
    const float rW = (rSec / total) * w;

    const float bot    = y0 + h;                     // y = silence
    const float top    = y0;                         // y = peak
    const float susY   = top + h * (1.0f - params.sustain); // y = sustain level

    // Dim centre line at silence
    g.setColour (juce::Colour (0xff1a1a30));
    g.drawHorizontalLine ((int)bot, x0, x0 + w);

    // Envelope path
    g.setColour (juce::Colour (0xff00aaff));          // blue-cyan for complex envs
    juce::Path env;
    env.startNewSubPath (x0,            bot);         // start at zero
    env.lineTo          (x0 + aW,       top);         // attack → peak
    env.lineTo          (x0 + aW + dW,  susY);        // decay  → sustain
    env.lineTo          (x0 + aW + dW + sW, susY);    // sustain hold
    env.lineTo          (x0 + w,        bot);         // release → zero

    g.strokePath (env, juce::PathStrokeType (1.5f));

    // Fill lightly under curve
    juce::Path fill = env;
    fill.lineTo (x0, bot);
    fill.closeSubPath();
    g.setColour (juce::Colour (0xff00aaff).withAlpha (0.08f));
    g.fillPath (fill);

    // Loop indicator dot
    if (params.looping)
    {
        g.setColour (juce::Colour (0xff00d4aa));
        g.fillEllipse (x0 + 3.0f, y0 + 3.0f, 5.0f, 5.0f);
    }
    // Clock-sync indicator
    if (params.clockSync)
    {
        g.setColour (juce::Colour (0xffe09040));
        g.fillEllipse (x0 + 10.0f, y0 + 3.0f, 5.0f, 5.0f);
    }
}

//==============================================================================
// Helper to add clock division items to a ComboBox
//==============================================================================
static void addCenvDivItems (juce::ComboBox& box)
{
    box.addItem ("8/1",  1);
    box.addItem ("4/1",  2);
    box.addItem ("2/1",  3);
    box.addItem ("1/1",  4);
    box.addItem ("1/2",  5);
    box.addItem ("1/4",  6);
    box.addItem ("1/8",  7);
    box.addItem ("1/16", 8);
}

//==============================================================================
VoltageSeq2AudioProcessorEditor::VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      cenv1Display (p.cenv1), cenv2Display (p.cenv2),
      oscScope (p), wavetableDisplay (p)
{
    setSize (1350, 900);

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
            audioProcessor.stepGates[i] = gateBtn[i].getToggleState();
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
    // SUB-STRIP: Sequence length
    //==========================================================================
    seqLengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    seqLengthSlider.setRange (2.0, 16.0, 1.0);
    seqLengthSlider.setValue (audioProcessor.sequenceLength, juce::dontSendNotification);
    seqLengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 22, 18);
    seqLengthSlider.setColour (juce::Slider::trackColourId,              knobColour);
    seqLengthSlider.setColour (juce::Slider::backgroundColourId,         juce::Colour (0xff252540));
    seqLengthSlider.setColour (juce::Slider::textBoxTextColourId,        textColour);
    seqLengthSlider.setColour (juce::Slider::textBoxBackgroundColourId,  bgColour);
    seqLengthSlider.setColour (juce::Slider::textBoxOutlineColourId,     bgColour);
    seqLengthSlider.onValueChange = [this]() {
        audioProcessor.sequenceLength = (int)seqLengthSlider.getValue();
        audioProcessor.resetOnNextBlock.store (true);
    };
    addAndMakeVisible (seqLengthSlider);

    //==========================================================================
    // SUB-STRIP: Play order radio buttons
    //==========================================================================
    auto setupPlayBtn = [&](juce::TextButton& btn, const juce::String& label, int orderIdx)
    {
        btn.setButtonText (label);
        bool active = (audioProcessor.playOrder == orderIdx);
        btn.setColour (juce::TextButton::buttonColourId, active ? playBtnOn : playBtnOff);
        btn.onClick = [this, &btn, orderIdx]()
        {
            audioProcessor.playOrder = orderIdx;
            audioProcessor.resetOnNextBlock.store (true);
            // Reset all button colours then highlight this one
            juce::TextButton* btns[4] = { &playFwdBtn, &playRevBtn, &playConvBtn, &playRndBtn };
            for (auto* b : btns)
                b->setColour (juce::TextButton::buttonColourId, playBtnOff);
            btn.setColour (juce::TextButton::buttonColourId, playBtnOn);
        };
        addAndMakeVisible (btn);
    };
    setupPlayBtn (playFwdBtn,  "FWD",  0);
    setupPlayBtn (playRevBtn,  "REV",  1);
    setupPlayBtn (playConvBtn, "CONV", 2);
    setupPlayBtn (playRndBtn,  "RND",  3);

    //==========================================================================
    // SUB-STRIP: Reset voltages
    //==========================================================================
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

    //==========================================================================
    // SUB-STRIP: Bipolar / Unipolar
    //==========================================================================
    {
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
            for (int i = 0; i < 16; ++i)
            {
                if (uni) {
                    stepKnob[i].setRange (0.0, 5.0, 0.01);
                    float clamped = juce::jmax (0.0f, audioProcessor.stepVoltages[i]);
                    audioProcessor.stepVoltages[i] = clamped;
                    stepKnob[i].setValue (clamped, juce::dontSendNotification);
                } else {
                    stepKnob[i].setRange (-5.0, 5.0, 0.01);
                    stepKnob[i].setValue (audioProcessor.stepVoltages[i], juce::dontSendNotification);
                }
            }
            repaint();
        };
        addAndMakeVisible (bipolarBtn);
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

    clockDivBox.addItem ("1/4",   1); clockDivBox.addItem ("1/8",   2);
    clockDivBox.addItem ("1/16",  3); clockDivBox.addItem ("1/8T",  4);
    clockDivBox.addItem ("1/16T", 5); clockDivBox.addItem ("1/8.",  6);
    clockDivBox.addItem ("1/16.", 7);
    clockDivBox.setSelectedItemIndex (audioProcessor.clockDivision, juce::dontSendNotification);
    clockDivBox.onChange = [this]() { audioProcessor.clockDivision = clockDivBox.getSelectedItemIndex(); };
    addAndMakeVisible (clockDivBox);

    {
        bool isRunning = audioProcessor.sequencerRunning.load();
        runStopBtn.setButtonText (isRunning ? "STOP" : "RUN");
        runStopBtn.setColour (juce::TextButton::buttonColourId, isRunning ? stopColour : runColour);
        runStopBtn.onClick = [this]()
        {
            bool nowRunning = !audioProcessor.sequencerRunning.load();
            audioProcessor.sequencerRunning.store (nowRunning);
            if (nowRunning) audioProcessor.resetOnNextBlock.store (true);
            runStopBtn.setButtonText (nowRunning ? "STOP" : "RUN");
            runStopBtn.setColour (juce::TextButton::buttonColourId, nowRunning ? stopColour : runColour);
        };
        addAndMakeVisible (runStopBtn);
    }

    {
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
    }

    //==========================================================================
    // QUANTIZER
    //==========================================================================
    rootBox.addItem ("C",  1); rootBox.addItem ("C#", 2); rootBox.addItem ("D",  3);
    rootBox.addItem ("D#", 4); rootBox.addItem ("E",  5); rootBox.addItem ("F",  6);
    rootBox.addItem ("F#", 7); rootBox.addItem ("G",  8); rootBox.addItem ("G#", 9);
    rootBox.addItem ("A", 10); rootBox.addItem ("A#",11); rootBox.addItem ("B", 12);
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

    osc1OctaveBox.addItem ("-2",1); osc1OctaveBox.addItem ("-1",2); osc1OctaveBox.addItem ("0",3);
    osc1OctaveBox.addItem ("+1",4); osc1OctaveBox.addItem ("+2",5);
    osc1OctaveBox.setSelectedItemIndex (audioProcessor.osc1Octave + 2, juce::dontSendNotification);
    osc1OctaveBox.onChange = [this]() { audioProcessor.osc1Octave = osc1OctaveBox.getSelectedItemIndex() - 2; };
    addAndMakeVisible (osc1OctaveBox);

    setupKnob (osc1PWMSlider, 0.05, 0.95, audioProcessor.osc1PulseWidth);
    osc1PWMSlider.onValueChange = [this]() { audioProcessor.osc1PulseWidth = (float)osc1PWMSlider.getValue(); };

    //==========================================================================
    // OSC 2
    //==========================================================================
    setupKnob (osc2PosSlider, 0.0, 1.0, audioProcessor.osc2Position);
    osc2PosSlider.onValueChange = [this]() { audioProcessor.osc2Position = (float)osc2PosSlider.getValue(); };

    setupKnob (osc2LevelSlider, 0.0, 1.0, audioProcessor.osc2Level);
    osc2LevelSlider.onValueChange = [this]() { audioProcessor.osc2Level = (float)osc2LevelSlider.getValue(); };

    osc2OctaveBox.addItem ("-2",1); osc2OctaveBox.addItem ("-1",2); osc2OctaveBox.addItem ("0",3);
    osc2OctaveBox.addItem ("+1",4); osc2OctaveBox.addItem ("+2",5);
    osc2OctaveBox.setSelectedItemIndex (audioProcessor.osc2Octave + 2, juce::dontSendNotification);
    osc2OctaveBox.onChange = [this]() { audioProcessor.osc2Octave = osc2OctaveBox.getSelectedItemIndex() - 2; };
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
    setupKnob (releaseSlider, 0.001, 3.0, audioProcessor.adsrParams.release, 0.3);
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
    setupKnob (fReleaseSlider, 0.001, 4.0, audioProcessor.filterEnvParams.release, 0.3);
    fReleaseSlider.onValueChange = [this]() { audioProcessor.filterEnvParams.release = (float)fReleaseSlider.getValue(); };

    //==========================================================================
    // LFO 1
    //==========================================================================
    setupKnob (lfoRateSlider,  0.1, 20.0, audioProcessor.lfoRate,  4.0);
    lfoRateSlider.onValueChange  = [this]() { audioProcessor.lfoRate  = (float)lfoRateSlider.getValue(); };
    setupKnob (lfoDepthSlider, 0.0, 1.0,  audioProcessor.lfoDepth);
    lfoDepthSlider.onValueChange = [this]() { audioProcessor.lfoDepth = (float)lfoDepthSlider.getValue(); };
    lfoTargetBox.addItem ("PWM",1); lfoTargetBox.addItem ("Cutoff",2); lfoTargetBox.addItem ("Pitch",3);
    lfoTargetBox.setSelectedItemIndex (audioProcessor.lfoTarget, juce::dontSendNotification);
    lfoTargetBox.onChange = [this]() { audioProcessor.lfoTarget = lfoTargetBox.getSelectedItemIndex(); };
    addAndMakeVisible (lfoTargetBox);

    //==========================================================================
    // LFO 2
    //==========================================================================
    setupKnob (lfo2RateSlider,  0.1, 20.0, audioProcessor.lfo2Rate,  4.0);
    lfo2RateSlider.onValueChange  = [this]() { audioProcessor.lfo2Rate  = (float)lfo2RateSlider.getValue(); };
    setupKnob (lfo2DepthSlider, 0.0, 1.0,  audioProcessor.lfo2Depth);
    lfo2DepthSlider.onValueChange = [this]() { audioProcessor.lfo2Depth = (float)lfo2DepthSlider.getValue(); };
    lfo2TargetBox.addItem ("PWM",1); lfo2TargetBox.addItem ("Cutoff",2); lfo2TargetBox.addItem ("Pitch",3);
    lfo2TargetBox.setSelectedItemIndex (audioProcessor.lfo2Target, juce::dontSendNotification);
    lfo2TargetBox.onChange = [this]() { audioProcessor.lfo2Target = lfo2TargetBox.getSelectedItemIndex(); };
    addAndMakeVisible (lfo2TargetBox);

    //==========================================================================
    // COMPLEX ENVELOPE 1
    //==========================================================================
    auto& c1 = audioProcessor.cenv1;

    setupKnob (cenv1AtkSlider,   0.001, 4.0, c1.attack,  0.3);
    cenv1AtkSlider.onValueChange   = [this]() { audioProcessor.cenv1.attack  = (float)cenv1AtkSlider.getValue(); };
    setupKnob (cenv1DecSlider,   0.001, 4.0, c1.decay,   0.3);
    cenv1DecSlider.onValueChange   = [this]() { audioProcessor.cenv1.decay   = (float)cenv1DecSlider.getValue(); };
    setupKnob (cenv1SusSlider,   0.0,   1.0, c1.sustain);
    cenv1SusSlider.onValueChange   = [this]() { audioProcessor.cenv1.sustain = (float)cenv1SusSlider.getValue(); };
    setupKnob (cenv1RelSlider,   0.001, 4.0, c1.release, 0.3);
    cenv1RelSlider.onValueChange   = [this]() { audioProcessor.cenv1.release = (float)cenv1RelSlider.getValue(); };
    setupKnob (cenv1DepthSlider, 0.0,   1.0, c1.depth);
    cenv1DepthSlider.onValueChange = [this]() { audioProcessor.cenv1.depth   = (float)cenv1DepthSlider.getValue(); };

    cenv1DestBox.addItem ("Amplitude", 1); cenv1DestBox.addItem ("Filter", 2); cenv1DestBox.addItem ("Pitch", 3);
    cenv1DestBox.setSelectedItemIndex (c1.dest, juce::dontSendNotification);
    cenv1DestBox.onChange = [this]() { audioProcessor.cenv1.dest = cenv1DestBox.getSelectedItemIndex(); };
    addAndMakeVisible (cenv1DestBox);

    addCenvDivItems (cenv1DivBox);
    cenv1DivBox.setSelectedItemIndex (c1.clockDiv, juce::dontSendNotification);
    cenv1DivBox.onChange = [this]() { audioProcessor.cenv1.clockDiv = cenv1DivBox.getSelectedItemIndex(); };
    addAndMakeVisible (cenv1DivBox);

    cenv1LoopBtn.setButtonText ("LOOP");
    cenv1LoopBtn.setToggleState (c1.looping, juce::dontSendNotification);
    cenv1LoopBtn.setClickingTogglesState (true);
    cenv1LoopBtn.setColour (juce::TextButton::buttonColourId,   c1.looping ? gateOnColour : gateOffColour);
    cenv1LoopBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    cenv1LoopBtn.onClick = [this]() {
        bool s = cenv1LoopBtn.getToggleState();
        audioProcessor.cenv1.looping = s;
        cenv1LoopBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addAndMakeVisible (cenv1LoopBtn);

    cenv1SyncBtn.setButtonText ("CLK SYNC");
    cenv1SyncBtn.setToggleState (c1.clockSync, juce::dontSendNotification);
    cenv1SyncBtn.setClickingTogglesState (true);
    cenv1SyncBtn.setColour (juce::TextButton::buttonColourId,   c1.clockSync ? knobColour : gateOffColour);
    cenv1SyncBtn.setColour (juce::TextButton::buttonOnColourId, knobColour);
    cenv1SyncBtn.onClick = [this]() {
        bool s = cenv1SyncBtn.getToggleState();
        audioProcessor.cenv1.clockSync = s;
        cenv1SyncBtn.setColour (juce::TextButton::buttonColourId, s ? knobColour : gateOffColour);
    };
    addAndMakeVisible (cenv1SyncBtn);
    addAndMakeVisible (cenv1Display);

    //==========================================================================
    // COMPLEX ENVELOPE 2
    //==========================================================================
    auto& c2 = audioProcessor.cenv2;

    setupKnob (cenv2AtkSlider,   0.001, 4.0, c2.attack,  0.3);
    cenv2AtkSlider.onValueChange   = [this]() { audioProcessor.cenv2.attack  = (float)cenv2AtkSlider.getValue(); };
    setupKnob (cenv2DecSlider,   0.001, 4.0, c2.decay,   0.3);
    cenv2DecSlider.onValueChange   = [this]() { audioProcessor.cenv2.decay   = (float)cenv2DecSlider.getValue(); };
    setupKnob (cenv2SusSlider,   0.0,   1.0, c2.sustain);
    cenv2SusSlider.onValueChange   = [this]() { audioProcessor.cenv2.sustain = (float)cenv2SusSlider.getValue(); };
    setupKnob (cenv2RelSlider,   0.001, 4.0, c2.release, 0.3);
    cenv2RelSlider.onValueChange   = [this]() { audioProcessor.cenv2.release = (float)cenv2RelSlider.getValue(); };
    setupKnob (cenv2DepthSlider, 0.0,   1.0, c2.depth);
    cenv2DepthSlider.onValueChange = [this]() { audioProcessor.cenv2.depth   = (float)cenv2DepthSlider.getValue(); };

    cenv2DestBox.addItem ("Amplitude", 1); cenv2DestBox.addItem ("Filter", 2); cenv2DestBox.addItem ("Pitch", 3);
    cenv2DestBox.setSelectedItemIndex (c2.dest, juce::dontSendNotification);
    cenv2DestBox.onChange = [this]() { audioProcessor.cenv2.dest = cenv2DestBox.getSelectedItemIndex(); };
    addAndMakeVisible (cenv2DestBox);

    addCenvDivItems (cenv2DivBox);
    cenv2DivBox.setSelectedItemIndex (c2.clockDiv, juce::dontSendNotification);
    cenv2DivBox.onChange = [this]() { audioProcessor.cenv2.clockDiv = cenv2DivBox.getSelectedItemIndex(); };
    addAndMakeVisible (cenv2DivBox);

    cenv2LoopBtn.setButtonText ("LOOP");
    cenv2LoopBtn.setToggleState (c2.looping, juce::dontSendNotification);
    cenv2LoopBtn.setClickingTogglesState (true);
    cenv2LoopBtn.setColour (juce::TextButton::buttonColourId,   c2.looping ? gateOnColour : gateOffColour);
    cenv2LoopBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    cenv2LoopBtn.onClick = [this]() {
        bool s = cenv2LoopBtn.getToggleState();
        audioProcessor.cenv2.looping = s;
        cenv2LoopBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addAndMakeVisible (cenv2LoopBtn);

    cenv2SyncBtn.setButtonText ("CLK SYNC");
    cenv2SyncBtn.setToggleState (c2.clockSync, juce::dontSendNotification);
    cenv2SyncBtn.setClickingTogglesState (true);
    cenv2SyncBtn.setColour (juce::TextButton::buttonColourId,   c2.clockSync ? knobColour : gateOffColour);
    cenv2SyncBtn.setColour (juce::TextButton::buttonOnColourId, knobColour);
    cenv2SyncBtn.onClick = [this]() {
        bool s = cenv2SyncBtn.getToggleState();
        audioProcessor.cenv2.clockSync = s;
        cenv2SyncBtn.setColour (juce::TextButton::buttonColourId, s ? knobColour : gateOffColour);
    };
    addAndMakeVisible (cenv2SyncBtn);
    addAndMakeVisible (cenv2Display);

    //==========================================================================
    // VISUALISERS
    //==========================================================================
    addAndMakeVisible (oscScope);
    addAndMakeVisible (wavetableDisplay);

    startTimerHz (30);
}

VoltageSeq2AudioProcessorEditor::~VoltageSeq2AudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
// Timer — active step highlight + dim steps beyond sequence length
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

        juce::Colour col;
        if      (isActive && gateOn)  col = activeGateOnColour;
        else if (isActive && !gateOn) col = activeGateOffColour;
        else if (gateOn)              col = gateOnColour;
        else                          col = gateOffColour;
        gateBtn[i].setColour (juce::TextButton::buttonColourId, col);

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
    if (skewMidpoint > 0.0) s.setSkewFactorFromMidPoint (skewMidpoint);
    addAndMakeVisible (s);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    // ── Header ────────────────────────────────────────────────────────────────
    g.setColour (accentColour);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText ("MURGATROYD INSTRUMENTS", 10, 4, 380, 18, juce::Justification::centredLeft);
    g.setColour (textColour);
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText ("VOLTAGE SEQ 2", 0, 4, getWidth(), 18, juce::Justification::centred);

    // ── Sequencer panel ───────────────────────────────────────────────────────
    g.setColour (sectionColour);
    g.fillRoundedRectangle ((float)seqX, 28.0f, (float)seqW, (float)seqH, 5.0f);

    // 0 V reference line
    const float zeroY = audioProcessor.unipolar
                        ? (float)stepSliderBottom
                        : (float)(stepSliderTop + stepSliderHeight / 2);
    g.setColour (juce::Colour (0xff333366));
    g.drawLine (10.0f, zeroY, (float)(seqX + seqW - 5), zeroY, 1.0f);
    g.setColour (dimColour);
    g.setFont (juce::Font (8.0f));
    g.drawText ("0V", seqX + seqW - 32, (int)zeroY - 8, 30, 10, juce::Justification::left);

    g.setFont (juce::Font (8.5f, juce::Font::bold));
    g.setColour (gateOnColour);
    g.drawText ("GATE",  1291, 155, 50, 17, juce::Justification::centredLeft);
    g.setColour (slideOnColour);
    g.drawText ("SLIDE", 1288, 174, 53, 17, juce::Justification::centredLeft);

    // ── Sub-strip (below sequencer) ───────────────────────────────────────────
    g.setColour (subStripColour);
    g.fillRoundedRectangle (5.0f, (float)stripY, (float)seqW, (float)stripH, 4.0f);

    g.setColour (dimColour);
    g.setFont (juce::Font (9.0f, juce::Font::bold));
    g.drawText ("SEQ LEN",    12,  stripY + 2, 100, 12, juce::Justification::centredLeft);
    g.drawText ("PLAY ORDER", 130, stripY + 2, 180, 12, juce::Justification::centredLeft);

    // ── Control panels row ────────────────────────────────────────────────────
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

    g.setColour (dimColour.withAlpha (0.5f));
    g.drawLine (1259.0f, (float)(ctrlY + 18), 1259.0f, (float)(ctrlY + ctrlH - 10), 1.0f);

    // ── Control labels ────────────────────────────────────────────────────────
    g.setColour (textColour);
    g.setFont (juce::Font (10.0f));

    // SEQ
    g.drawText ("BPM",       7,  lY1,         55, 14, juce::Justification::centred);
    g.drawText ("RANGE",    67,  lY1,         68, 14, juce::Justification::centred);
    g.drawText ("CLOCK DIV", 5, ctrlY +  87, 140, 14, juce::Justification::centred);
    g.drawText ("PORTA",     5, ctrlY + 129, 140, 14, juce::Justification::centred);

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
    g.drawText ("SCOPE",   315, ctrlY + 183, 170, 12, juce::Justification::centred);
    // OSC 2
    g.drawText ("WT POS",  490, lY1,  75, 14, juce::Justification::centred);
    g.drawText ("LEVEL",   563, lY1,  82, 14, juce::Justification::centred);
    g.drawText ("OCTAVE",  490, lY3, 155, 14, juce::Justification::centred);
    g.drawText ("WT VIEW", 490, ctrlY + 143, 155, 12, juce::Justification::centred);
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
    g.drawText ("RATE",   1175, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("DEPTH",  1218, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("TARGET", 1172, lY3, 84, 14, juce::Justification::centred);
    // LFO 2
    g.drawText ("RATE",   1263, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("DEPTH",  1306, lY1, 40, 14, juce::Justification::centred);
    g.drawText ("TARGET", 1260, lY3, 84, 14, juce::Justification::centred);

    // Step numbers
    g.setColour (dimColour);
    g.setFont (juce::Font (9.0f));
    for (int i = 0; i < 16; ++i)
        g.drawText (juce::String (i + 1), seqX + i * stepStride + 4, 196, 72, 12,
                    juce::Justification::centred);

    // ── Complex envelope panels ───────────────────────────────────────────────
    auto drawEnvPanel = [&](int pX, const juce::String& title)
    {
        g.setColour (envPanelColour);
        g.fillRoundedRectangle ((float)pX, (float)envY, (float)envPW, (float)envH, 5.0f);
        g.setColour (dimColour);
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.drawText (title, pX, envY + 4, envPW, 14, juce::Justification::centred);

        g.setColour (textColour);
        g.setFont (juce::Font (10.0f));
        // Knob labels
        g.drawText ("ATK",   pX + 15, envY + 20, 48, 13, juce::Justification::centred);
        g.drawText ("DEC",   pX + 73, envY + 20, 48, 13, juce::Justification::centred);
        g.drawText ("SUS",   pX +131, envY + 20, 48, 13, juce::Justification::centred);
        g.drawText ("REL",   pX +189, envY + 20, 48, 13, juce::Justification::centred);
        g.drawText ("DEPTH", pX +250, envY + 20, 58, 13, juce::Justification::centred);
        // Row 2 labels
        g.setColour (dimColour);
        g.setFont (juce::Font (9.0f));
        g.drawText ("DEST",     pX + 15, envY + 88,  120, 12, juce::Justification::centredLeft);
        g.drawText ("CLK DIV",  pX +270, envY + 88,   88, 12, juce::Justification::centredLeft);
        // Display label
        g.drawText ("ENVELOPE SHAPE", pX + 385, envY + 12, 270, 12, juce::Justification::centred);
    };
    drawEnvPanel (  5, "ENVELOPE 1");
    drawEnvPanel (680, "ENVELOPE 2");
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

    // ── Sub-strip ─────────────────────────────────────────────────────────────
    const int sY = stripY + 14;   // control row inside strip
    seqLengthSlider.setBounds (12,  sY, 100, 22);
    playFwdBtn     .setBounds (128, sY,  44, 22);
    playRevBtn     .setBounds (175, sY,  44, 22);
    playConvBtn    .setBounds (222, sY,  50, 22);
    playRndBtn     .setBounds (275, sY,  44, 22);
    resetBtn       .setBounds (335, sY,  80, 22);
    bipolarBtn     .setBounds (425, sY, 110, 22);

    // ── SEQ transport ─────────────────────────────────────────────────────────
    bpmSlider  .setBounds (10, cy1,          52, 62);
    rangeSlider.setBounds (70, cy1 + 6,      62, 50);
    clockDivBox.setBounds (10, ctrlY + 100, 125, 24);
    portaSlider.setBounds (44, ctrlY + 145,  50, 50);
    runStopBtn .setBounds (10, ctrlY + 210, 125, 30);
    autoBtn    .setBounds (10, ctrlY + 250, 125, 24);

    // ── Quantizer ─────────────────────────────────────────────────────────────
    rootBox .setBounds (155, cy1, 150, 24);
    scaleBox.setBounds (155, cy2, 150, 24);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    osc1WaveBox    .setBounds (320, cy1,       160, 24);
    osc1LevelSlider.setBounds (320, cy2,        45, 45);
    osc1OctaveBox  .setBounds (373, cy2 + 10,  105, 24);
    osc1PWMSlider  .setBounds (375, cy4,        45, 45);
    oscScope       .setBounds (317, ctrlY + 195, 163, 155);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    osc2PosSlider   .setBounds (495, cy1,  50, 50);
    osc2LevelSlider .setBounds (560, cy1,  50, 50);
    osc2OctaveBox   .setBounds (500, cy3, 140, 24);
    wavetableDisplay.setBounds (492, ctrlY + 155, 151, 195);

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

    // ── Complex Envelope panels ───────────────────────────────────────────────
    // ENV 1 (panel at x=5)
    const int e1 = 5;
    cenv1AtkSlider  .setBounds (e1 + 15,  envY + 33, 48, 48);
    cenv1DecSlider  .setBounds (e1 + 73,  envY + 33, 48, 48);
    cenv1SusSlider  .setBounds (e1 +131,  envY + 33, 48, 48);
    cenv1RelSlider  .setBounds (e1 +189,  envY + 33, 48, 48);
    cenv1DepthSlider.setBounds (e1 +253,  envY + 33, 48, 48);
    cenv1DestBox    .setBounds (e1 + 15,  envY +100, 120, 22);
    cenv1LoopBtn    .setBounds (e1 +145,  envY +100,  60, 22);
    cenv1SyncBtn    .setBounds (e1 +215,  envY +100,  80, 22);
    cenv1DivBox     .setBounds (e1 +305,  envY +100,  75, 22);
    cenv1Display    .setBounds (e1 +390,  envY + 25, 265, 188);

    // ENV 2 (panel at x=680)
    const int e2 = 680;
    cenv2AtkSlider  .setBounds (e2 + 15,  envY + 33, 48, 48);
    cenv2DecSlider  .setBounds (e2 + 73,  envY + 33, 48, 48);
    cenv2SusSlider  .setBounds (e2 +131,  envY + 33, 48, 48);
    cenv2RelSlider  .setBounds (e2 +189,  envY + 33, 48, 48);
    cenv2DepthSlider.setBounds (e2 +253,  envY + 33, 48, 48);
    cenv2DestBox    .setBounds (e2 + 15,  envY +100, 120, 22);
    cenv2LoopBtn    .setBounds (e2 +145,  envY +100,  60, 22);
    cenv2SyncBtn    .setBounds (e2 +215,  envY +100,  80, 22);
    cenv2DivBox     .setBounds (e2 +305,  envY +100,  75, 22);
    cenv2Display    .setBounds (e2 +390,  envY + 25, 265, 188);
}
