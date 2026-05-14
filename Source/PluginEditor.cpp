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
    const juce::Colour voiceAColour       { 0xff00aaff };   // blue — Voice A accent
    const juce::Colour voiceBColour       { 0xffaa44ff };   // purple — Voice B accent

    // ── Layout constants: all Y coords are relative to a per-voice origin ─────
    constexpr int seqX = 5, seqW = 1340, seqH = 215;
    constexpr int stepStride = 80;
    constexpr int stepSliderTop    = 32;
    constexpr int stepSliderHeight = 118;
    constexpr int stepSliderBottom = stepSliderTop + stepSliderHeight;  // 150

    constexpr int stripRelY = 246;   // relative to voice yOff
    constexpr int stripH    =  42;

    constexpr int ctrlRelY = 295;
    constexpr int ctrlH    = 360;

    constexpr int envRelY  = 663;
    constexpr int envH     = 225;
    constexpr int envPW    = 660;   // each of the two env panels

    // Derived: label + control Y coords (relative to ctrlRelY)
    constexpr int lRowOff1 = 22, lRowOff2 = 66, lRowOff3 = 99, lRowOff4 = 131;
    constexpr int cRowOff1 = 39, cRowOff2 = 83, cRowOff3 = 116, cRowOff4 = 148;

    // Total pixel height of one voice area (seqPanel starts at y=28 relative to yOff)
    constexpr int voiceAreaH = 888;

    // Gap between Voice A area and Voice B area
    constexpr int voiceGap = 12;
    constexpr int voiceBOffset = voiceAreaH + voiceGap;   // = 900
}

//==============================================================================
// OscScopeComponent — uses voice index to read correct ring buffer
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
    const int writePos = proc.scopeWritePos[vi];

    // Zero-crossing trigger
    int trigger = 0;
    for (int i = n / 2; i > 1; --i)
    {
        int idx  = (writePos - i     + n) % n;
        int idxP = (writePos - i - 1 + n) % n;
        if (proc.oscScopeBuffer[vi][idxP] <= 0.0f && proc.oscScopeBuffer[vi][idx] > 0.0f)
        { trigger = idx; break; }
    }

    g.setColour (juce::Colour (0xff00d4aa));
    juce::Path wave;
    const int drawW = b.getWidth() - 4;
    for (int x = 0; x < drawW; ++x)
    {
        float s   = juce::jlimit (-1.0f, 1.0f, proc.oscScopeBuffer[vi][(trigger + x * n / drawW) % n]);
        float yPx = cy - s * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + (float)x, yPx);
        else         wave.lineTo          (2.0f + (float)x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.5f));
}

//==============================================================================
// WavetableDisplayComponent — uses voice index for osc2 position
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

    const float tPos  = proc.voice[vi].osc2Position * (float)(proc.numWavetables - 1);
    const int   tA    = (int)tPos;
    const int   tB    = juce::jmin (tA + 1, proc.numWavetables - 1);
    const float blend = tPos - (float)tA;
    const int   ws    = proc.wavetableSize;
    const int   drawW = b.getWidth() - 4;

    g.setColour (juce::Colour (0xffe09040));
    juce::Path wave;
    for (int x = 0; x < drawW; ++x)
    {
        float rp  = (float)x / (float)(drawW - 1) * (float)(ws - 1);
        int   ri  = (int)rp;
        float frac = rp - (float)ri;
        int   riN = (ri + 1) % ws;
        float sA  = proc.wavetables[tA][ri] + frac * (proc.wavetables[tA][riN] - proc.wavetables[tA][ri]);
        float sB  = proc.wavetables[tB][ri] + frac * (proc.wavetables[tB][riN] - proc.wavetables[tB][ri]);
        float yPx = cy - (sA + blend * (sB - sA)) * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + (float)x, yPx);
        else         wave.lineTo          (2.0f + (float)x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.5f));
}

//==============================================================================
// ComplexEnvDisplay — draws proportional ADSR curve from parameter struct
//==============================================================================
void ComplexEnvDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().reduced (1);
    g.fillAll (juce::Colour (0xff020208));
    g.setColour (juce::Colour (0xff2a2a4a));
    g.drawRect (getLocalBounds(), 1);

    const float aSec  = params.attack;
    const float dSec  = params.decay;
    const float rSec  = params.release;
    const float sSec  = 0.3f;
    const float total = aSec + dSec + sSec + rSec;

    const float w  = (float)b.getWidth()  - 2.0f;
    const float h  = (float)b.getHeight() - 4.0f;
    const float x0 = (float)b.getX()      + 1.0f;
    const float y0 = (float)b.getY()      + 2.0f;

    const float aW  = (aSec / total) * w;
    const float dW  = (dSec / total) * w;
    const float sW  = (sSec / total) * w;

    const float bot  = y0 + h;
    const float top  = y0;
    const float susY = top + h * (1.0f - params.sustain);

    g.setColour (juce::Colour (0xff1a1a30));
    g.drawHorizontalLine ((int)bot, x0, x0 + w);

    g.setColour (juce::Colour (0xff00aaff));
    juce::Path env;
    env.startNewSubPath (x0,            bot);
    env.lineTo          (x0 + aW,       top);
    env.lineTo          (x0 + aW + dW,  susY);
    env.lineTo          (x0 + aW + dW + sW, susY);
    env.lineTo          (x0 + w,        bot);
    g.strokePath (env, juce::PathStrokeType (1.5f));

    juce::Path fill = env;
    fill.lineTo (x0, bot);
    fill.closeSubPath();
    g.setColour (juce::Colour (0xff00aaff).withAlpha (0.08f));
    g.fillPath (fill);

    if (params.looping)
    {
        g.setColour (juce::Colour (0xff00d4aa));
        g.fillEllipse (x0 + 3.0f, y0 + 3.0f, 5.0f, 5.0f);
    }
    if (params.clockSync)
    {
        g.setColour (juce::Colour (0xffe09040));
        g.fillEllipse (x0 + 10.0f, y0 + 3.0f, 5.0f, 5.0f);
    }
}

//==============================================================================
// Static helpers
//==============================================================================
static void addCenvDivItems (juce::ComboBox& box)
{
    box.addItem ("8/1",  1); box.addItem ("4/1",  2); box.addItem ("2/1",  3);
    box.addItem ("1/1",  4); box.addItem ("1/2",  5); box.addItem ("1/4",  6);
    box.addItem ("1/8",  7); box.addItem ("1/16", 8);
}

//==============================================================================
// CONSTRUCTOR
//==============================================================================
VoltageSeq2AudioProcessorEditor::VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (1350, 1800);

    // ── Load backplate SVG ────────────────────────────────────────────────────
    if (auto svgXml = juce::XmlDocument::parse (juce::String (kBackplateSVG)))
        backplate = juce::Drawable::createFromSVG (*svgXml);

    // ── Instantiate heap-allocated components for both voices ─────────────────
    for (int v = 0; v < 2; ++v)
    {
        oscScope[v]       = std::make_unique<OscScopeComponent>        (audioProcessor, v);
        wavetableDisplay[v] = std::make_unique<WavetableDisplayComponent>(audioProcessor, v);
        cenvDisplay[v][0] = std::make_unique<ComplexEnvDisplay>(audioProcessor.voice[v].cenv1);
        cenvDisplay[v][1] = std::make_unique<ComplexEnvDisplay>(audioProcessor.voice[v].cenv2);
    }

    // ── Per-voice control setup ───────────────────────────────────────────────
    setupVoice (0);
    setupVoice (1);

    //==========================================================================
    // SHARED CONTROLS
    //==========================================================================

    // BPM (shared tempo — sits in Voice A SEQ panel)
    setupKnob (bpmSlider, 60.0, 200.0, audioProcessor.internalBPM);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    bpmSlider.onValueChange = [this]() { audioProcessor.internalBPM = bpmSlider.getValue(); };

    // AUTO run button
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

    // Preset Save / Load
    savePresetBtn.setButtonText ("SAVE");
    savePresetBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a2840));
    savePresetBtn.onClick = [this]()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Murgatroyd Instruments/VoltageSeq2/Presets");
        dir.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Save Preset", dir, "*.vs2");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result != juce::File{})
                {
                    auto file = result.withFileExtension (".vs2");
                    juce::MemoryBlock state;
                    audioProcessor.getStateInformation (state);
                    file.replaceWithData (state.getData(), state.getSize());
                }
            });
    };
    addAndMakeVisible (savePresetBtn);

    loadPresetBtn.setButtonText ("LOAD");
    loadPresetBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a2840));
    loadPresetBtn.onClick = [this]()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Murgatroyd Instruments/VoltageSeq2/Presets");
        dir.createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Load Preset", dir, "*.vs2");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    juce::MemoryBlock state;
                    result.loadFileAsData (state);
                    audioProcessor.setStateInformation (state.getData(), (int)state.getSize());
                    syncUIFromProcessor();
                }
            });
    };
    addAndMakeVisible (loadPresetBtn);

    startTimerHz (30);
}

VoltageSeq2AudioProcessorEditor::~VoltageSeq2AudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
// setupVoice — wire up all controls for voice index v
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupVoice (int v)
{
    auto& vp = audioProcessor.voice[v];

    //==========================================================================
    // STEP SLIDERS + GATE + SLIDE BUTTONS
    //==========================================================================
    for (int i = 0; i < 16; ++i)
    {
        stepKnob[v][i].setSliderStyle (juce::Slider::LinearVertical);
        stepKnob[v][i].setRange (-5.0, 5.0, 0.01);
        stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
        stepKnob[v][i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        stepKnob[v][i].setColour (juce::Slider::trackColourId,      knobColour);
        stepKnob[v][i].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
        stepKnob[v][i].onValueChange = [this, v, i]()
        {
            audioProcessor.voice[v].stepVoltages[i] = (float)stepKnob[v][i].getValue();
        };
        addAndMakeVisible (stepKnob[v][i]);

        bool gOn = vp.stepGates[i];
        gateBtn[v][i].setButtonText ("");
        gateBtn[v][i].setToggleState (gOn, juce::dontSendNotification);
        gateBtn[v][i].setClickingTogglesState (true);
        gateBtn[v][i].setColour (juce::TextButton::buttonColourId,   gOn ? gateOnColour : gateOffColour);
        gateBtn[v][i].setColour (juce::TextButton::buttonOnColourId, gateOnColour);
        gateBtn[v][i].onClick = [this, v, i]()
        {
            audioProcessor.voice[v].stepGates[i] = gateBtn[v][i].getToggleState();
        };
        addAndMakeVisible (gateBtn[v][i]);

        bool sOn = vp.stepGlides[i];
        slideBtn[v][i].setButtonText ("");
        slideBtn[v][i].setToggleState (sOn, juce::dontSendNotification);
        slideBtn[v][i].setClickingTogglesState (true);
        slideBtn[v][i].setColour (juce::TextButton::buttonColourId,   sOn ? slideOnColour : gateOffColour);
        slideBtn[v][i].setColour (juce::TextButton::buttonOnColourId, slideOnColour);
        slideBtn[v][i].onClick = [this, v, i]()
        {
            bool s = slideBtn[v][i].getToggleState();
            audioProcessor.voice[v].stepGlides[i] = s;
            slideBtn[v][i].setColour (juce::TextButton::buttonColourId, s ? slideOnColour : gateOffColour);
        };
        addAndMakeVisible (slideBtn[v][i]);
    }

    //==========================================================================
    // SUB-STRIP: Sequence length
    //==========================================================================
    seqLengthSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    seqLengthSlider[v].setRange (2.0, 16.0, 1.0);
    seqLengthSlider[v].setValue (vp.sequenceLength, juce::dontSendNotification);
    seqLengthSlider[v].setTextBoxStyle (juce::Slider::TextBoxRight, false, 22, 18);
    seqLengthSlider[v].setColour (juce::Slider::trackColourId,             knobColour);
    seqLengthSlider[v].setColour (juce::Slider::backgroundColourId,        juce::Colour (0xff252540));
    seqLengthSlider[v].setColour (juce::Slider::textBoxTextColourId,       textColour);
    seqLengthSlider[v].setColour (juce::Slider::textBoxBackgroundColourId, bgColour);
    seqLengthSlider[v].setColour (juce::Slider::textBoxOutlineColourId,    bgColour);
    seqLengthSlider[v].onValueChange = [this, v]()
    {
        audioProcessor.voice[v].sequenceLength = (int)seqLengthSlider[v].getValue();
        audioProcessor.voice[v].resetOnNextBlock.store (true);
    };
    addAndMakeVisible (seqLengthSlider[v]);

    //==========================================================================
    // SUB-STRIP: Play order radio buttons
    //==========================================================================
    auto setupPlayBtn = [&](juce::TextButton& btn, const juce::String& label, int orderIdx)
    {
        btn.setButtonText (label);
        bool active = (vp.playOrder == orderIdx);
        btn.setColour (juce::TextButton::buttonColourId, active ? playBtnOn : playBtnOff);
        btn.onClick = [this, v, &btn, orderIdx]()
        {
            audioProcessor.voice[v].playOrder = orderIdx;
            audioProcessor.voice[v].resetOnNextBlock.store (true);
            juce::TextButton* btns[4] = { &playFwdBtn[v], &playRevBtn[v], &playConvBtn[v], &playRndBtn[v] };
            for (auto* b : btns)
                b->setColour (juce::TextButton::buttonColourId, playBtnOff);
            btn.setColour (juce::TextButton::buttonColourId, playBtnOn);
        };
        addAndMakeVisible (btn);
    };
    setupPlayBtn (playFwdBtn[v],  "FWD",  0);
    setupPlayBtn (playRevBtn[v],  "REV",  1);
    setupPlayBtn (playConvBtn[v], "CONV", 2);
    setupPlayBtn (playRndBtn[v],  "RND",  3);

    //==========================================================================
    // SUB-STRIP: Reset voltages
    //==========================================================================
    resetBtn[v].setButtonText ("RESET");
    resetBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2050));
    resetBtn[v].onClick = [this, v]()
    {
        for (int i = 0; i < 16; ++i)
        {
            audioProcessor.voice[v].stepVoltages[i] = 0.0f;
            stepKnob[v][i].setValue (0.0, juce::dontSendNotification);
        }
    };
    addAndMakeVisible (resetBtn[v]);

    //==========================================================================
    // SUB-STRIP: Bipolar / Unipolar toggle
    //==========================================================================
    {
        bool isUni = vp.unipolar;
        bipolarBtn[v].setButtonText (isUni ? "UNIPOLAR" : "BIPOLAR");
        bipolarBtn[v].setToggleState (isUni, juce::dontSendNotification);
        bipolarBtn[v].setClickingTogglesState (true);
        bipolarBtn[v].setColour (juce::TextButton::buttonColourId,   isUni ? juce::Colour(0xff305050) : juce::Colour(0xff2a2050));
        bipolarBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff305050));
        bipolarBtn[v].onClick = [this, v]()
        {
            bool uni = bipolarBtn[v].getToggleState();
            audioProcessor.voice[v].unipolar = uni;
            bipolarBtn[v].setButtonText (uni ? "UNIPOLAR" : "BIPOLAR");
            bipolarBtn[v].setColour (juce::TextButton::buttonColourId,
                                     uni ? juce::Colour(0xff305050) : juce::Colour(0xff2a2050));
            for (int i = 0; i < 16; ++i)
            {
                if (uni) {
                    stepKnob[v][i].setRange (0.0, 5.0, 0.01);
                    float clamped = juce::jmax (0.0f, audioProcessor.voice[v].stepVoltages[i]);
                    audioProcessor.voice[v].stepVoltages[i] = clamped;
                    stepKnob[v][i].setValue (clamped, juce::dontSendNotification);
                } else {
                    stepKnob[v][i].setRange (-5.0, 5.0, 0.01);
                    stepKnob[v][i].setValue (audioProcessor.voice[v].stepVoltages[i], juce::dontSendNotification);
                }
            }
            repaint();
        };
        addAndMakeVisible (bipolarBtn[v]);
    }

    //==========================================================================
    // SUB-STRIP: Swing
    //==========================================================================
    swingSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider[v].setRange (0.5, 0.75, 0.001);
    swingSlider[v].setValue (vp.swingAmount, juce::dontSendNotification);
    swingSlider[v].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    swingSlider[v].setColour (juce::Slider::trackColourId,      knobColour);
    swingSlider[v].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
    swingSlider[v].onValueChange = [this, v]()
    {
        audioProcessor.voice[v].swingAmount = (float)swingSlider[v].getValue();
    };
    addAndMakeVisible (swingSlider[v]);

    //==========================================================================
    // SEQ TRANSPORT: Range, Porta, Clock div, Run/Stop
    //==========================================================================
    setupKnob (rangeSlider[v], 0.0, 1.0, vp.rangeVCA);
    rangeSlider[v].onValueChange = [this, v]()
    {
        audioProcessor.voice[v].rangeVCA = (float)rangeSlider[v].getValue();
    };

    setupKnob (portaSlider[v], 0.0, 2.0, vp.portamentoTime);
    portaSlider[v].onValueChange = [this, v]()
    {
        audioProcessor.voice[v].portamentoTime = (float)portaSlider[v].getValue();
    };

    clockDivBox[v].addItem ("1/4",   1); clockDivBox[v].addItem ("1/8",   2);
    clockDivBox[v].addItem ("1/16",  3); clockDivBox[v].addItem ("1/8T",  4);
    clockDivBox[v].addItem ("1/16T", 5); clockDivBox[v].addItem ("1/8.",  6);
    clockDivBox[v].addItem ("1/16.", 7);
    clockDivBox[v].setSelectedItemIndex (vp.clockDivision, juce::dontSendNotification);
    clockDivBox[v].onChange = [this, v]()
    {
        audioProcessor.voice[v].clockDivision = clockDivBox[v].getSelectedItemIndex();
    };
    addAndMakeVisible (clockDivBox[v]);

    {
        bool isRunning = vp.sequencerRunning.load();
        runStopBtn[v].setButtonText (isRunning ? "STOP" : "RUN");
        runStopBtn[v].setColour (juce::TextButton::buttonColourId, isRunning ? stopColour : runColour);
        runStopBtn[v].onClick = [this, v]()
        {
            bool nowRunning = !audioProcessor.voice[v].sequencerRunning.load();
            audioProcessor.voice[v].sequencerRunning.store (nowRunning);
            if (nowRunning) audioProcessor.voice[v].resetOnNextBlock.store (true);
            runStopBtn[v].setButtonText (nowRunning ? "STOP" : "RUN");
            runStopBtn[v].setColour (juce::TextButton::buttonColourId, nowRunning ? stopColour : runColour);
        };
        addAndMakeVisible (runStopBtn[v]);
    }

    //==========================================================================
    // QUANTIZER
    //==========================================================================
    rootBox[v].addItem ("C", 1);  rootBox[v].addItem ("C#",2);  rootBox[v].addItem ("D", 3);
    rootBox[v].addItem ("D#",4);  rootBox[v].addItem ("E", 5);  rootBox[v].addItem ("F", 6);
    rootBox[v].addItem ("F#",7);  rootBox[v].addItem ("G", 8);  rootBox[v].addItem ("G#",9);
    rootBox[v].addItem ("A", 10); rootBox[v].addItem ("A#",11); rootBox[v].addItem ("B", 12);
    rootBox[v].setSelectedItemIndex (vp.rootNote, juce::dontSendNotification);
    rootBox[v].onChange = [this, v]() { audioProcessor.voice[v].rootNote = rootBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (rootBox[v]);

    scaleBox[v].addItem ("Major",       1); scaleBox[v].addItem ("Natural Minor", 2);
    scaleBox[v].addItem ("Dorian",      3); scaleBox[v].addItem ("Phrygian",      4);
    scaleBox[v].addItem ("Lydian",      5); scaleBox[v].addItem ("Mixolydian",    6);
    scaleBox[v].addItem ("Penta Major", 7); scaleBox[v].addItem ("Penta Minor",   8);
    scaleBox[v].addItem ("Chromatic",   9);
    scaleBox[v].setSelectedItemIndex (vp.currentScale, juce::dontSendNotification);
    scaleBox[v].onChange = [this, v]() { audioProcessor.voice[v].currentScale = scaleBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (scaleBox[v]);

    //==========================================================================
    // OSC 1
    //==========================================================================
    osc1WaveBox[v].addItem ("Sine",1); osc1WaveBox[v].addItem ("Saw",2);
    osc1WaveBox[v].addItem ("Square",3); osc1WaveBox[v].addItem ("Triangle",4);
    osc1WaveBox[v].setSelectedItemIndex (vp.osc1Waveform, juce::dontSendNotification);
    osc1WaveBox[v].onChange = [this, v]() { audioProcessor.voice[v].osc1Waveform = osc1WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (osc1WaveBox[v]);

    setupKnob (osc1LevelSlider[v], 0.0, 1.0, vp.osc1Level);
    osc1LevelSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc1Level = (float)osc1LevelSlider[v].getValue(); };

    osc1OctaveBox[v].addItem ("-2",1); osc1OctaveBox[v].addItem ("-1",2); osc1OctaveBox[v].addItem ("0",3);
    osc1OctaveBox[v].addItem ("+1",4); osc1OctaveBox[v].addItem ("+2",5);
    osc1OctaveBox[v].setSelectedItemIndex (vp.osc1Octave + 2, juce::dontSendNotification);
    osc1OctaveBox[v].onChange = [this, v]() { audioProcessor.voice[v].osc1Octave = osc1OctaveBox[v].getSelectedItemIndex() - 2; };
    addAndMakeVisible (osc1OctaveBox[v]);

    setupKnob (osc1PWMSlider[v], 0.05, 0.95, vp.osc1PulseWidth);
    osc1PWMSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc1PulseWidth = (float)osc1PWMSlider[v].getValue(); };

    //==========================================================================
    // OSC 2
    //==========================================================================
    setupKnob (osc2PosSlider[v], 0.0, 1.0, vp.osc2Position);
    osc2PosSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc2Position = (float)osc2PosSlider[v].getValue(); };

    setupKnob (osc2LevelSlider[v], 0.0, 1.0, vp.osc2Level);
    osc2LevelSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc2Level = (float)osc2LevelSlider[v].getValue(); };

    osc2OctaveBox[v].addItem ("-2",1); osc2OctaveBox[v].addItem ("-1",2); osc2OctaveBox[v].addItem ("0",3);
    osc2OctaveBox[v].addItem ("+1",4); osc2OctaveBox[v].addItem ("+2",5);
    osc2OctaveBox[v].setSelectedItemIndex (vp.osc2Octave + 2, juce::dontSendNotification);
    osc2OctaveBox[v].onChange = [this, v]() { audioProcessor.voice[v].osc2Octave = osc2OctaveBox[v].getSelectedItemIndex() - 2; };
    addAndMakeVisible (osc2OctaveBox[v]);

    //==========================================================================
    // FILTER
    //==========================================================================
    setupKnob (cutoffSlider[v], 20.0, 16000.0, vp.filterCutoff, 1000.0);
    cutoffSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterCutoff = (float)cutoffSlider[v].getValue(); };

    setupKnob (resonanceSlider[v], 0.0, 1.0, vp.filterResonance);
    resonanceSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterResonance = (float)resonanceSlider[v].getValue(); };

    setupKnob (filterEnvAmtSlider[v], 0.0, 1.0, vp.filterEnvAmount);
    filterEnvAmtSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterEnvAmount = (float)filterEnvAmtSlider[v].getValue(); };

    //==========================================================================
    // AMP ENVELOPE
    //==========================================================================
    setupKnob (attackSlider[v],  0.001, 2.0, vp.adsrParams.attack,  0.3);
    attackSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].adsrParams.attack  = (float)attackSlider[v].getValue(); };
    setupKnob (decaySlider[v],   0.001, 2.0, vp.adsrParams.decay,   0.3);
    decaySlider[v].onValueChange   = [this, v]() { audioProcessor.voice[v].adsrParams.decay   = (float)decaySlider[v].getValue(); };
    setupKnob (sustainSlider[v], 0.0,   1.0, vp.adsrParams.sustain);
    sustainSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].adsrParams.sustain = (float)sustainSlider[v].getValue(); };
    setupKnob (releaseSlider[v], 0.001, 3.0, vp.adsrParams.release, 0.3);
    releaseSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].adsrParams.release = (float)releaseSlider[v].getValue(); };

    //==========================================================================
    // FILTER ENVELOPE
    //==========================================================================
    setupKnob (fAttackSlider[v],  0.001, 4.0, vp.filterEnvParams.attack,  0.3);
    fAttackSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].filterEnvParams.attack  = (float)fAttackSlider[v].getValue(); };
    setupKnob (fDecaySlider[v],   0.001, 4.0, vp.filterEnvParams.decay,   0.3);
    fDecaySlider[v].onValueChange   = [this, v]() { audioProcessor.voice[v].filterEnvParams.decay   = (float)fDecaySlider[v].getValue(); };
    setupKnob (fSustainSlider[v], 0.0,   1.0, vp.filterEnvParams.sustain);
    fSustainSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterEnvParams.sustain = (float)fSustainSlider[v].getValue(); };
    setupKnob (fReleaseSlider[v], 0.001, 4.0, vp.filterEnvParams.release, 0.3);
    fReleaseSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterEnvParams.release = (float)fReleaseSlider[v].getValue(); };

    //==========================================================================
    // LFO 1
    //==========================================================================
    setupKnob (lfoRateSlider[v],  0.1, 20.0, vp.lfoRate,  4.0);
    lfoRateSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].lfoRate  = (float)lfoRateSlider[v].getValue(); };
    setupKnob (lfoDepthSlider[v], 0.0, 1.0,  vp.lfoDepth);
    lfoDepthSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].lfoDepth = (float)lfoDepthSlider[v].getValue(); };
    lfoTargetBox[v].addItem ("PWM",1); lfoTargetBox[v].addItem ("Cutoff",2); lfoTargetBox[v].addItem ("Pitch",3);
    lfoTargetBox[v].setSelectedItemIndex (vp.lfoTarget, juce::dontSendNotification);
    lfoTargetBox[v].onChange = [this, v]() { audioProcessor.voice[v].lfoTarget = lfoTargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfoTargetBox[v]);

    //==========================================================================
    // LFO 2
    //==========================================================================
    setupKnob (lfo2RateSlider[v],  0.1, 20.0, vp.lfo2Rate,  4.0);
    lfo2RateSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].lfo2Rate  = (float)lfo2RateSlider[v].getValue(); };
    setupKnob (lfo2DepthSlider[v], 0.0, 1.0,  vp.lfo2Depth);
    lfo2DepthSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].lfo2Depth = (float)lfo2DepthSlider[v].getValue(); };
    lfo2TargetBox[v].addItem ("PWM",1); lfo2TargetBox[v].addItem ("Cutoff",2); lfo2TargetBox[v].addItem ("Pitch",3);
    lfo2TargetBox[v].setSelectedItemIndex (vp.lfo2Target, juce::dontSendNotification);
    lfo2TargetBox[v].onChange = [this, v]() { audioProcessor.voice[v].lfo2Target = lfo2TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo2TargetBox[v]);

    //==========================================================================
    // COMPLEX ENVELOPES (e=0 → cenv1, e=1 → cenv2)
    //==========================================================================
    for (int e = 0; e < 2; ++e)
    {
        auto& cep = (e == 0) ? vp.cenv1 : vp.cenv2;

        setupKnob (cenvAtkSlider  [v][e], 0.001, 4.0, cep.attack,  0.3);
        cenvAtkSlider  [v][e].onValueChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.attack = (float)cenvAtkSlider[v][e].getValue();
        };

        setupKnob (cenvDecSlider  [v][e], 0.001, 4.0, cep.decay,   0.3);
        cenvDecSlider  [v][e].onValueChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.decay = (float)cenvDecSlider[v][e].getValue();
        };

        setupKnob (cenvSusSlider  [v][e], 0.0,   1.0, cep.sustain);
        cenvSusSlider  [v][e].onValueChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.sustain = (float)cenvSusSlider[v][e].getValue();
        };

        setupKnob (cenvRelSlider  [v][e], 0.001, 4.0, cep.release, 0.3);
        cenvRelSlider  [v][e].onValueChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.release = (float)cenvRelSlider[v][e].getValue();
        };

        setupKnob (cenvDepthSlider[v][e], 0.0,   1.0, cep.depth);
        cenvDepthSlider[v][e].onValueChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.depth = (float)cenvDepthSlider[v][e].getValue();
        };

        cenvDestBox[v][e].addItem ("Amplitude",1); cenvDestBox[v][e].addItem ("Filter",2);
        cenvDestBox[v][e].addItem ("Pitch",3);
        cenvDestBox[v][e].setSelectedItemIndex (cep.dest, juce::dontSendNotification);
        cenvDestBox[v][e].onChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.dest = cenvDestBox[v][e].getSelectedItemIndex();
        };
        addAndMakeVisible (cenvDestBox[v][e]);

        addCenvDivItems (cenvDivBox[v][e]);
        cenvDivBox[v][e].setSelectedItemIndex (cep.clockDiv, juce::dontSendNotification);
        cenvDivBox[v][e].onChange = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.clockDiv = cenvDivBox[v][e].getSelectedItemIndex();
        };
        addAndMakeVisible (cenvDivBox[v][e]);

        cenvLoopBtn[v][e].setButtonText ("LOOP");
        cenvLoopBtn[v][e].setToggleState (cep.looping, juce::dontSendNotification);
        cenvLoopBtn[v][e].setClickingTogglesState (true);
        cenvLoopBtn[v][e].setColour (juce::TextButton::buttonColourId,   cep.looping ? gateOnColour : gateOffColour);
        cenvLoopBtn[v][e].setColour (juce::TextButton::buttonOnColourId, gateOnColour);
        cenvLoopBtn[v][e].onClick = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.looping = cenvLoopBtn[v][e].getToggleState();
            cenvLoopBtn[v][e].setColour (juce::TextButton::buttonColourId, c.looping ? gateOnColour : gateOffColour);
        };
        addAndMakeVisible (cenvLoopBtn[v][e]);

        cenvSyncBtn[v][e].setButtonText ("CLK SYNC");
        cenvSyncBtn[v][e].setToggleState (cep.clockSync, juce::dontSendNotification);
        cenvSyncBtn[v][e].setClickingTogglesState (true);
        cenvSyncBtn[v][e].setColour (juce::TextButton::buttonColourId,   cep.clockSync ? knobColour : gateOffColour);
        cenvSyncBtn[v][e].setColour (juce::TextButton::buttonOnColourId, knobColour);
        cenvSyncBtn[v][e].onClick = [this, v, e]()
        {
            auto& c = (e == 0) ? audioProcessor.voice[v].cenv1 : audioProcessor.voice[v].cenv2;
            c.clockSync = cenvSyncBtn[v][e].getToggleState();
            cenvSyncBtn[v][e].setColour (juce::TextButton::buttonColourId, c.clockSync ? knobColour : gateOffColour);
        };
        addAndMakeVisible (cenvSyncBtn[v][e]);

        addAndMakeVisible (*cenvDisplay[v][e]);
    }

    // Visualisers
    addAndMakeVisible (*oscScope[v]);
    addAndMakeVisible (*wavetableDisplay[v]);
}

//==============================================================================
// setupKnob helper
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
// PAINT
//==============================================================================
void VoltageSeq2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    for (int v = 0; v < 2; ++v)
    {
        const int yOff  = (v == 0) ? 0 : voiceBOffset;
        const juce::Colour& voiceAccent = (v == 0) ? voiceAColour : voiceBColour;
        const juce::String voiceLabel = (v == 0) ? "VOICE  A" : "VOICE  B";

        // ── Backplate (each voice gets its own copy scaled to one voice area) ─
        if (backplate != nullptr)
            backplate->drawWithin (g,
                juce::Rectangle<float> (0.0f, (float)yOff, 1350.0f, (float)voiceAreaH),
                juce::RectanglePlacement::stretchToFit, 0.88f);

        // ── Voice title bar ───────────────────────────────────────────────────
        g.setColour (voiceAccent.withAlpha (0.15f));
        g.fillRect (0, yOff, 1350, 26);
        g.setColour (voiceAccent);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (voiceLabel, 8, yOff + 4, 200, 18, juce::Justification::centredLeft);

        // Shared controls label (Voice A only)
        if (v == 0)
        {
            g.setColour (dimColour.withAlpha (0.8f));
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.drawText ("PRESET", 1072, yOff + 4, 42, 10, juce::Justification::centredLeft);
        }

        // ── Sequencer panel ───────────────────────────────────────────────────
        g.setColour (sectionColour.withAlpha (0.78f));
        g.fillRoundedRectangle ((float)seqX, (float)(yOff + 28), (float)seqW, (float)seqH, 5.0f);

        // 0V reference line
        const bool isUni = audioProcessor.voice[v].unipolar;
        const float zeroY = (float)(yOff + (isUni ? stepSliderBottom : stepSliderTop + stepSliderHeight / 2));
        g.setColour (juce::Colour (0xff333366));
        g.drawLine (10.0f, zeroY, (float)(seqX + seqW - 5), zeroY, 1.0f);
        g.setColour (dimColour);
        g.setFont (juce::Font (8.0f));
        g.drawText ("0V", seqX + seqW - 32, (int)zeroY - 8, 30, 10, juce::Justification::left);

        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.setColour (gateOnColour);
        g.drawText ("GATE",  1291, yOff + 155, 50, 17, juce::Justification::centredLeft);
        g.setColour (slideOnColour);
        g.drawText ("SLIDE", 1288, yOff + 174, 53, 17, juce::Justification::centredLeft);

        // ── Sub-strip ─────────────────────────────────────────────────────────
        g.setColour (subStripColour.withAlpha (0.82f));
        g.fillRoundedRectangle (5.0f, (float)(yOff + stripRelY), (float)seqW, (float)stripH, 4.0f);
        g.setColour (dimColour);
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.drawText ("SEQ LEN",    12,  yOff + stripRelY + 2, 100, 12, juce::Justification::centredLeft);
        g.drawText ("PLAY ORDER", 130, yOff + stripRelY + 2, 180, 12, juce::Justification::centredLeft);
        g.drawText ("SWING",      560, yOff + stripRelY + 2,  80, 12, juce::Justification::centredLeft);

        // ── Control panels row ────────────────────────────────────────────────
        const int cY = yOff + ctrlRelY;
        auto drawPanel = [&](int x, int w, const juce::String& title)
        {
            g.setColour (sectionColour.withAlpha (0.80f));
            g.fillRoundedRectangle ((float)x, (float)cY, (float)w, (float)ctrlH, 5.0f);
            g.setColour (dimColour);
            g.setFont (juce::Font (9.5f, juce::Font::bold));
            g.drawText (title, x, cY + 4, w, 14, juce::Justification::centred);
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
        g.drawLine (1259.0f, (float)(cY + 18), 1259.0f, (float)(cY + ctrlH - 10), 1.0f);

        // ── Control labels ────────────────────────────────────────────────────
        auto lY1 = cY + lRowOff1, lY2 = cY + lRowOff2;
        auto lY3 = cY + lRowOff3, lY4 = cY + lRowOff4;

        g.setColour (textColour);
        g.setFont (juce::Font (10.0f));
        // SEQ
        if (v == 0)
            g.drawText ("BPM",       7,   lY1,          55, 14, juce::Justification::centred);
        g.drawText ("RANGE",    67,  lY1,          68, 14, juce::Justification::centred);
        g.drawText ("CLOCK DIV", 5,  cY + 87,     140, 14, juce::Justification::centred);
        g.drawText ("PORTA",     5,  cY + 129,    140, 14, juce::Justification::centred);

        // LFO sub-labels
        g.setColour (dimColour);
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.drawText ("LFO 1", 1172, cY + 19,  84, 12, juce::Justification::centred);
        g.drawText ("LFO 2", 1261, cY + 19,  82, 12, juce::Justification::centred);

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
        g.drawText ("SCOPE",   315, cY + 183, 170, 12, juce::Justification::centred);
        // OSC 2
        g.drawText ("WT POS",  490, lY1,  75, 14, juce::Justification::centred);
        g.drawText ("LEVEL",   563, lY1,  82, 14, juce::Justification::centred);
        g.drawText ("OCTAVE",  490, lY3, 155, 14, juce::Justification::centred);
        g.drawText ("WT VIEW", 490, cY + 143, 155, 12, juce::Justification::centred);
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
        g.drawText ("ATK",   995, lY1, 40, 14, juce::Justification::centred);
        g.drawText ("DEC",  1037, lY1, 40, 14, juce::Justification::centred);
        g.drawText ("SUS",  1079, lY1, 40, 14, juce::Justification::centred);
        g.drawText ("REL",  1121, lY1, 40, 14, juce::Justification::centred);
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
            g.drawText (juce::String (i + 1), seqX + i * stepStride + 4, yOff + 196, 72, 12,
                        juce::Justification::centred);

        // ── Complex envelope panels ───────────────────────────────────────────
        const int eY = yOff + envRelY;
        auto drawEnvPanel = [&](int pX, const juce::String& title)
        {
            g.setColour (envPanelColour.withAlpha (0.80f));
            g.fillRoundedRectangle ((float)pX, (float)eY, (float)envPW, (float)envH, 5.0f);
            g.setColour (dimColour);
            g.setFont (juce::Font (9.5f, juce::Font::bold));
            g.drawText (title, pX, eY + 4, envPW, 14, juce::Justification::centred);
            g.setColour (textColour);
            g.setFont (juce::Font (10.0f));
            g.drawText ("ATK",   pX + 15, eY + 20,  48, 13, juce::Justification::centred);
            g.drawText ("DEC",   pX + 73, eY + 20,  48, 13, juce::Justification::centred);
            g.drawText ("SUS",   pX +131, eY + 20,  48, 13, juce::Justification::centred);
            g.drawText ("REL",   pX +189, eY + 20,  48, 13, juce::Justification::centred);
            g.drawText ("DEPTH", pX +250, eY + 20,  58, 13, juce::Justification::centred);
            g.setColour (dimColour);
            g.setFont (juce::Font (9.0f));
            g.drawText ("DEST",    pX + 15,  eY + 88, 120, 12, juce::Justification::centredLeft);
            g.drawText ("CLK DIV", pX + 270, eY + 88,  88, 12, juce::Justification::centredLeft);
            g.drawText ("ENVELOPE SHAPE", pX + 385, eY + 12, 270, 12, juce::Justification::centred);
        };
        drawEnvPanel (  5, "ENVELOPE 1");
        drawEnvPanel (680, "ENVELOPE 2");

        // ── Divider between voices ────────────────────────────────────────────
        if (v == 0)
        {
            g.setColour (voiceAColour.withAlpha (0.25f));
            g.fillRect (0, voiceAreaH, 1350, voiceGap);
            g.setColour (voiceAColour.withAlpha (0.5f));
            g.drawLine (0.0f, (float)voiceAreaH, 1350.0f, (float)voiceAreaH, 1.5f);
            g.setColour (voiceBColour.withAlpha (0.5f));
            g.drawLine (0.0f, (float)(voiceAreaH + voiceGap), 1350.0f, (float)(voiceAreaH + voiceGap), 1.5f);
        }
    }
}

//==============================================================================
// RESIZED
//==============================================================================
void VoltageSeq2AudioProcessorEditor::resized()
{
    // Shared controls
    bpmSlider    .setBounds (10, 39,          52, 62);  // Voice A SEQ area — absolute
    savePresetBtn.setBounds (1100, 5,  60, 22);
    loadPresetBtn.setBounds (1168, 5,  60, 22);

    layoutVoice (0, 0);
    layoutVoice (1, voiceBOffset);
}

//==============================================================================
// layoutVoice — position all per-voice controls with yOff applied
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutVoice (int v, int yOff)
{
    const int cY = yOff + ctrlRelY;
    const int sY = yOff + stripRelY + 14;   // control row inside sub-strip
    const int eY = yOff + envRelY;

    auto cy1 = cY + cRowOff1, cy2 = cY + cRowOff2;
    auto cy3 = cY + cRowOff3, cy4 = cY + cRowOff4;

    // ── Step controls ─────────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        const int bx = seqX + i * stepStride;
        stepKnob[v][i].setBounds (bx + 4,  yOff + stepSliderTop, 72, stepSliderHeight);
        gateBtn [v][i].setBounds (bx + 11, yOff + 155, 54, 17);
        slideBtn[v][i].setBounds (bx + 11, yOff + 174, 54, 17);
    }

    // ── Sub-strip ─────────────────────────────────────────────────────────────
    seqLengthSlider[v].setBounds (12,  sY, 100, 22);
    playFwdBtn [v]    .setBounds (128, sY,  44, 22);
    playRevBtn [v]    .setBounds (175, sY,  44, 22);
    playConvBtn[v]    .setBounds (222, sY,  50, 22);
    playRndBtn [v]    .setBounds (275, sY,  44, 22);
    resetBtn   [v]    .setBounds (335, sY,  80, 22);
    bipolarBtn [v]    .setBounds (425, sY, 110, 22);
    swingSlider[v]    .setBounds (560, sY, 130, 22);

    // ── SEQ transport ─────────────────────────────────────────────────────────
    // BPM is shared — only positioned for voice A (handled in resized())
    if (v == 1)
    {
        // Voice B: show a small BPM readout label instead (no duplicate knob)
        // (no control to position — the bpmSlider stays in voice A's area)
    }
    rangeSlider[v]  .setBounds (70, cY + cRowOff1 + 6,  62, 50);
    clockDivBox[v]  .setBounds (10, cY + 100, 125, 24);
    portaSlider[v]  .setBounds (44, cY + 145,  50, 50);
    runStopBtn [v]  .setBounds (10, cY + 210, 125, 30);

    if (v == 0)
        autoBtn.setBounds (10, cY + 250, 125, 24);   // auto btn is shared, shown in voice A

    // ── Quantizer ─────────────────────────────────────────────────────────────
    rootBox [v].setBounds (155, cy1, 150, 24);
    scaleBox[v].setBounds (155, cy2, 150, 24);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    osc1WaveBox    [v].setBounds (320, cy1,       160, 24);
    osc1LevelSlider[v].setBounds (320, cy2,        45, 45);
    osc1OctaveBox  [v].setBounds (373, cy2 + 10,  105, 24);
    osc1PWMSlider  [v].setBounds (375, cy4,        45, 45);
    oscScope       [v]->setBounds (317, cY + 195, 163, 155);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    osc2PosSlider   [v].setBounds (495, cy1,  50, 50);
    osc2LevelSlider [v].setBounds (560, cy1,  50, 50);
    osc2OctaveBox   [v].setBounds (500, cy3, 140, 24);
    wavetableDisplay[v]->setBounds (492, cY + 155, 151, 195);

    // ── Filter ────────────────────────────────────────────────────────────────
    cutoffSlider      [v].setBounds (655, cy1,  50, 50);
    resonanceSlider   [v].setBounds (720, cy1,  50, 50);
    filterEnvAmtSlider[v].setBounds (688, cy3,  50, 50);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    attackSlider [v].setBounds (815, cy1, 40, 40);
    decaySlider  [v].setBounds (857, cy1, 40, 40);
    sustainSlider[v].setBounds (899, cy1, 40, 40);
    releaseSlider[v].setBounds (941, cy1, 40, 40);

    // ── Filter Envelope ───────────────────────────────────────────────────────
    fAttackSlider [v].setBounds ( 995, cy1, 40, 40);
    fDecaySlider  [v].setBounds (1037, cy1, 40, 40);
    fSustainSlider[v].setBounds (1079, cy1, 40, 40);
    fReleaseSlider[v].setBounds (1121, cy1, 40, 40);

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    lfoRateSlider [v].setBounds (1175, cy1, 38, 38);
    lfoDepthSlider[v].setBounds (1218, cy1, 38, 38);
    lfoTargetBox  [v].setBounds (1173, cy3, 83, 24);

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    lfo2RateSlider [v].setBounds (1263, cy1, 38, 38);
    lfo2DepthSlider[v].setBounds (1306, cy1, 38, 38);
    lfo2TargetBox  [v].setBounds (1261, cy3, 83, 24);

    // ── Complex Envelopes ─────────────────────────────────────────────────────
    const int envPanelX[2] = { 5, 680 };
    for (int e = 0; e < 2; ++e)
    {
        const int pX = envPanelX[e];
        cenvAtkSlider  [v][e].setBounds (pX + 15,  eY + 33, 48, 48);
        cenvDecSlider  [v][e].setBounds (pX + 73,  eY + 33, 48, 48);
        cenvSusSlider  [v][e].setBounds (pX +131,  eY + 33, 48, 48);
        cenvRelSlider  [v][e].setBounds (pX +189,  eY + 33, 48, 48);
        cenvDepthSlider[v][e].setBounds (pX +253,  eY + 33, 48, 48);
        cenvDestBox    [v][e].setBounds (pX + 15,  eY +100, 120, 22);
        cenvLoopBtn    [v][e].setBounds (pX +145,  eY +100,  60, 22);
        cenvSyncBtn    [v][e].setBounds (pX +215,  eY +100,  80, 22);
        cenvDivBox     [v][e].setBounds (pX +305,  eY +100,  75, 22);
        cenvDisplay    [v][e]->setBounds(pX +390,  eY + 25, 265, 188);
    }
}

//==============================================================================
// TIMER CALLBACK — step highlight + alpha dim for both voices
//==============================================================================
void VoltageSeq2AudioProcessorEditor::timerCallback()
{
    for (int v = 0; v < 2; ++v)
    {
        auto& vp = audioProcessor.voice[v];
        const int  active  = vp.currentStep;
        const int  seqLen  = vp.sequenceLength;
        const bool running = vp.sequencerRunning.load();

        for (int i = 0; i < 16; ++i)
        {
            const bool inRange  = (i < seqLen);
            const bool isActive = running && inRange && (i == active);
            const bool gOn      = vp.stepGates[i];

            juce::Colour col;
            if      (isActive && gOn)  col = activeGateOnColour;
            else if (isActive && !gOn) col = activeGateOffColour;
            else if (gOn)              col = gateOnColour;
            else                       col = gateOffColour;
            gateBtn[v][i].setColour (juce::TextButton::buttonColourId, col);

            const float alpha = inRange ? 1.0f : 0.25f;
            stepKnob[v][i].setAlpha (alpha);
            gateBtn [v][i].setAlpha (alpha);
            slideBtn[v][i].setAlpha (alpha);
        }

        // Keep run/stop button colours in sync
        const bool isRunning = vp.sequencerRunning.load();
        runStopBtn[v].setColour (juce::TextButton::buttonColourId, isRunning ? stopColour : runColour);
        runStopBtn[v].setButtonText (isRunning ? "STOP" : "RUN");
    }
}

//==============================================================================
// syncUIFromProcessor — refresh all widget values after preset load
//==============================================================================
void VoltageSeq2AudioProcessorEditor::syncUIFromProcessor()
{
    bpmSlider.setValue (audioProcessor.internalBPM, juce::dontSendNotification);

    const bool isAuto = audioProcessor.autoRun.load();
    autoBtn.setToggleState (isAuto, juce::dontSendNotification);
    autoBtn.setColour (juce::TextButton::buttonColourId, isAuto ? gateOnColour : gateOffColour);

    for (int v = 0; v < 2; ++v)
    {
        auto& vp = audioProcessor.voice[v];

        // Step controls
        for (int i = 0; i < 16; ++i)
        {
            stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
            gateBtn [v][i].setToggleState (vp.stepGates [i], juce::dontSendNotification);
            slideBtn[v][i].setToggleState (vp.stepGlides[i], juce::dontSendNotification);
        }

        // Sub-strip
        seqLengthSlider[v].setValue (vp.sequenceLength, juce::dontSendNotification);
        swingSlider    [v].setValue (vp.swingAmount,    juce::dontSendNotification);

        juce::TextButton* pBtns[4] = { &playFwdBtn[v], &playRevBtn[v], &playConvBtn[v], &playRndBtn[v] };
        for (int i = 0; i < 4; ++i)
            pBtns[i]->setColour (juce::TextButton::buttonColourId,
                                  vp.playOrder == i ? playBtnOn : playBtnOff);

        bipolarBtn[v].setToggleState (vp.unipolar, juce::dontSendNotification);
        bipolarBtn[v].setButtonText  (vp.unipolar ? "UNIPOLAR" : "BIPOLAR");
        bipolarBtn[v].setColour (juce::TextButton::buttonColourId,
                                  vp.unipolar ? juce::Colour (0xff305050) : juce::Colour (0xff2a2050));
        for (int i = 0; i < 16; ++i)
        {
            if (vp.unipolar) stepKnob[v][i].setRange (0.0, 5.0, 0.01);
            else             stepKnob[v][i].setRange (-5.0, 5.0, 0.01);
            stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
        }

        // SEQ transport
        rangeSlider[v].setValue (vp.rangeVCA,        juce::dontSendNotification);
        portaSlider[v].setValue (vp.portamentoTime,  juce::dontSendNotification);
        clockDivBox[v].setSelectedItemIndex (vp.clockDivision, juce::dontSendNotification);

        // Quantizer
        rootBox [v].setSelectedItemIndex (vp.rootNote,     juce::dontSendNotification);
        scaleBox[v].setSelectedItemIndex (vp.currentScale, juce::dontSendNotification);

        // OSC 1
        osc1WaveBox    [v].setSelectedItemIndex (vp.osc1Waveform,   juce::dontSendNotification);
        osc1LevelSlider[v].setValue (vp.osc1Level,                   juce::dontSendNotification);
        osc1OctaveBox  [v].setSelectedItemIndex (vp.osc1Octave + 2, juce::dontSendNotification);
        osc1PWMSlider  [v].setValue (vp.osc1PulseWidth,              juce::dontSendNotification);

        // OSC 2
        osc2PosSlider  [v].setValue (vp.osc2Position,                juce::dontSendNotification);
        osc2LevelSlider[v].setValue (vp.osc2Level,                   juce::dontSendNotification);
        osc2OctaveBox  [v].setSelectedItemIndex (vp.osc2Octave + 2, juce::dontSendNotification);

        // Filter
        cutoffSlider      [v].setValue (vp.filterCutoff,           juce::dontSendNotification);
        resonanceSlider   [v].setValue (vp.filterResonance,        juce::dontSendNotification);
        filterEnvAmtSlider[v].setValue (vp.filterEnvAmount,        juce::dontSendNotification);

        // Filter Env
        fAttackSlider [v].setValue (vp.filterEnvParams.attack,  juce::dontSendNotification);
        fDecaySlider  [v].setValue (vp.filterEnvParams.decay,   juce::dontSendNotification);
        fSustainSlider[v].setValue (vp.filterEnvParams.sustain, juce::dontSendNotification);
        fReleaseSlider[v].setValue (vp.filterEnvParams.release, juce::dontSendNotification);

        // Amp Env
        attackSlider [v].setValue (vp.adsrParams.attack,  juce::dontSendNotification);
        decaySlider  [v].setValue (vp.adsrParams.decay,   juce::dontSendNotification);
        sustainSlider[v].setValue (vp.adsrParams.sustain, juce::dontSendNotification);
        releaseSlider[v].setValue (vp.adsrParams.release, juce::dontSendNotification);

        // LFO 1
        lfoRateSlider [v].setValue (vp.lfoRate,  juce::dontSendNotification);
        lfoDepthSlider[v].setValue (vp.lfoDepth, juce::dontSendNotification);
        lfoTargetBox  [v].setSelectedItemIndex (vp.lfoTarget, juce::dontSendNotification);

        // LFO 2
        lfo2RateSlider [v].setValue (vp.lfo2Rate,  juce::dontSendNotification);
        lfo2DepthSlider[v].setValue (vp.lfo2Depth, juce::dontSendNotification);
        lfo2TargetBox  [v].setSelectedItemIndex (vp.lfo2Target, juce::dontSendNotification);

        // Complex Envelopes
        for (int e = 0; e < 2; ++e)
        {
            const auto& cep = (e == 0) ? vp.cenv1 : vp.cenv2;
            cenvAtkSlider  [v][e].setValue (cep.attack,  juce::dontSendNotification);
            cenvDecSlider  [v][e].setValue (cep.decay,   juce::dontSendNotification);
            cenvSusSlider  [v][e].setValue (cep.sustain, juce::dontSendNotification);
            cenvRelSlider  [v][e].setValue (cep.release, juce::dontSendNotification);
            cenvDepthSlider[v][e].setValue (cep.depth,   juce::dontSendNotification);
            cenvDestBox[v][e].setSelectedItemIndex (cep.dest,     juce::dontSendNotification);
            cenvDivBox [v][e].setSelectedItemIndex (cep.clockDiv, juce::dontSendNotification);
            cenvLoopBtn[v][e].setToggleState (cep.looping,   juce::dontSendNotification);
            cenvLoopBtn[v][e].setColour (juce::TextButton::buttonColourId,
                                         cep.looping   ? gateOnColour : gateOffColour);
            cenvSyncBtn[v][e].setToggleState (cep.clockSync, juce::dontSendNotification);
            cenvSyncBtn[v][e].setColour (juce::TextButton::buttonColourId,
                                         cep.clockSync ? knobColour   : gateOffColour);
        }
    }

    repaint();
}
