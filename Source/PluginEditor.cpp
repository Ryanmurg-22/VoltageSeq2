#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    // ── Colours ───────────────────────────────────────────────────────────────
    const juce::Colour bgColour           { 0xff000000 };
    const juce::Colour sectionColour      { 0xff0c0c18 };
    const juce::Colour subStripColour     { 0xff080814 };
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
    const juce::Colour voiceAColour       { 0xff00aaff };
    const juce::Colour voiceBColour       { 0xffaa44ff };

    // ── Single-window compact layout ──────────────────────────────────────────
    constexpr int headerH = 28;

    // Sequencer strips (same width for both voices)
    constexpr int seqX      = 5;
    constexpr int seqW      = 1340;
    constexpr int seqH      = 114;
    constexpr int stepStride = 80;

    // Step controls (relative to the strip's Y origin)
    constexpr int stepSliderTop  = 14;
    constexpr int stepSliderH    = 72;
    constexpr int gateRelY       = 88;
    constexpr int slideRelY      = 103;

    // Sub-strips
    constexpr int subH = 30;

    // Absolute Y positions for each region
    constexpr int seqAY  = headerH;                // 28
    constexpr int subAY  = seqAY + seqH;           // 142
    constexpr int seqBY  = subAY + subH;           // 172
    constexpr int subBY  = seqBY + seqH;           // 286
    constexpr int ctrlAY = subBY + subH;           // 316
    constexpr int ctrlH  = 174;
    constexpr int ctrlBY = ctrlAY + ctrlH;         // 490
    constexpr int winH   = ctrlBY + ctrlH + 8;    // 672

    // Control panel label/knob row offsets (relative to panel ctrlY)
    constexpr int lOff1 = 18, lOff2 = 70, lOff3 = 118;
    constexpr int cOff1 = 30, cOff2 = 82, cOff3 = 132;
    // Knob size throughout: 36 × 36 px
    constexpr int kSz = 36;

    // Control panel X positions (same as before — they fill the full width)
    constexpr int pSeqX  =    5, pSeqW  = 140;
    constexpr int pQntX  =  150, pQntW  = 160;
    constexpr int pO1X   =  315, pO1W   = 170;
    constexpr int pO2X   =  490, pO2W   = 155;
    constexpr int pFltX  =  650, pFltW  = 155;
    constexpr int pAEX   =  810, pAEW   = 175;
    constexpr int pFEX   =  990, pFEW   = 175;
    constexpr int pLfoX  = 1170, pLfoW  = 175;
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
    const int writePos = proc.scopeWritePos[vi];
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
    const int drawW = juce::jmax (1, b.getWidth() - 4);
    for (int x = 0; x < drawW; ++x)
    {
        float s   = juce::jlimit (-1.0f, 1.0f,
                                  proc.oscScopeBuffer[vi][(trigger + x * n / drawW) % n]);
        float yPx = cy - s * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + (float)x, yPx);
        else         wave.lineTo          (2.0f + (float)x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.2f));
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

    const float tPos  = proc.voice[vi].osc2Position * (float)(proc.numWavetables - 1);
    const int   tA    = (int)tPos;
    const int   tB    = juce::jmin (tA + 1, proc.numWavetables - 1);
    const float blend = tPos - (float)tA;
    const int   ws    = proc.wavetableSize;
    const int   drawW = juce::jmax (1, b.getWidth() - 4);

    g.setColour (juce::Colour (0xffe09040));
    juce::Path wave;
    for (int x = 0; x < drawW; ++x)
    {
        float rp   = (float)x / (float)(drawW - 1) * (float)(ws - 1);
        int   ri   = (int)rp;
        float frac = rp - (float)ri;
        int   riN  = (ri + 1) % ws;
        float sA   = proc.wavetables[tA][ri] + frac * (proc.wavetables[tA][riN] - proc.wavetables[tA][ri]);
        float sB   = proc.wavetables[tB][ri] + frac * (proc.wavetables[tB][riN] - proc.wavetables[tB][ri]);
        float yPx  = cy - (sA + blend * (sB - sA)) * (b.getHeight() * 0.4f);
        if (x == 0) wave.startNewSubPath (2.0f + (float)x, yPx);
        else         wave.lineTo          (2.0f + (float)x, yPx);
    }
    g.strokePath (wave, juce::PathStrokeType (1.2f));
}

//==============================================================================
static void addCenvDivItems (juce::ComboBox& box)
{
    box.addItem ("8/1",1); box.addItem ("4/1",2); box.addItem ("2/1",3);
    box.addItem ("1/1",4); box.addItem ("1/2",5); box.addItem ("1/4",6);
    box.addItem ("1/8",7); box.addItem ("1/16",8);
}

//==============================================================================
// CONSTRUCTOR
//==============================================================================
VoltageSeq2AudioProcessorEditor::VoltageSeq2AudioProcessorEditor (VoltageSeq2AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // ── Load backplate SVG ────────────────────────────────────────────────────
    if (auto svgXml = juce::XmlDocument::parse (juce::String (kBackplateSVG)))
        backplate = juce::Drawable::createFromSVG (*svgXml);

    // ── Heap-allocated components ─────────────────────────────────────────────
    for (int v = 0; v < 2; ++v)
    {
        oscScope[v]         = std::make_unique<OscScopeComponent>        (audioProcessor, v);
        wavetableDisplay[v] = std::make_unique<WavetableDisplayComponent>(audioProcessor, v);
    }

    // ── Per-voice controls ────────────────────────────────────────────────────
    setupVoice (0);
    setupVoice (1);

    //==========================================================================
    // SHARED / GLOBAL CONTROLS
    //==========================================================================

    // BPM slider — compact linear, lives in the global header
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setRange (40.0, 250.0);
    bpmSlider.setValue (audioProcessor.internalBPM, juce::dontSendNotification);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    bpmSlider.setColour (juce::Slider::trackColourId,             knobColour);
    bpmSlider.setColour (juce::Slider::backgroundColourId,        juce::Colour (0xff252540));
    bpmSlider.setColour (juce::Slider::textBoxTextColourId,        textColour);
    bpmSlider.setColour (juce::Slider::textBoxBackgroundColourId,  bgColour);
    bpmSlider.setColour (juce::Slider::textBoxOutlineColourId,     bgColour);
    bpmSlider.onValueChange = [this]() { audioProcessor.internalBPM = bpmSlider.getValue(); };
    addAndMakeVisible (bpmSlider);

    // AUTO button
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

    // Preset Save
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
                    juce::MemoryBlock state;
                    audioProcessor.getStateInformation (state);
                    result.withFileExtension (".vs2").replaceWithData (state.getData(), state.getSize());
                }
            });
    };
    addAndMakeVisible (savePresetBtn);

    // Preset Load
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

    // setSize LAST — triggers resized() which calls layoutVoice()
    setSize (1350, winH);
    startTimerHz (30);
}

VoltageSeq2AudioProcessorEditor::~VoltageSeq2AudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
// setupVoice — wire controls for one voice
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupVoice (int v)
{
    auto& vp = audioProcessor.voice[v];

    // Step sliders + gate + slide
    for (int i = 0; i < 16; ++i)
    {
        stepKnob[v][i].setSliderStyle (juce::Slider::LinearVertical);
        stepKnob[v][i].setRange (-5.0, 5.0, 0.01);
        stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
        stepKnob[v][i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        stepKnob[v][i].setColour (juce::Slider::trackColourId,      knobColour);
        stepKnob[v][i].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
        stepKnob[v][i].onValueChange = [this, v, i]()
        { audioProcessor.voice[v].stepVoltages[i] = (float)stepKnob[v][i].getValue(); };
        addAndMakeVisible (stepKnob[v][i]);

        bool gOn = vp.stepGates[i];
        gateBtn[v][i].setButtonText ("");
        gateBtn[v][i].setToggleState (gOn, juce::dontSendNotification);
        gateBtn[v][i].setClickingTogglesState (true);
        gateBtn[v][i].setColour (juce::TextButton::buttonColourId,   gOn ? gateOnColour : gateOffColour);
        gateBtn[v][i].setColour (juce::TextButton::buttonOnColourId, gateOnColour);
        gateBtn[v][i].onClick = [this, v, i]()
        { audioProcessor.voice[v].stepGates[i] = gateBtn[v][i].getToggleState(); };
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

    // Sequence length
    seqLengthSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    seqLengthSlider[v].setRange (2.0, 16.0, 1.0);
    seqLengthSlider[v].setValue (vp.sequenceLength, juce::dontSendNotification);
    seqLengthSlider[v].setTextBoxStyle (juce::Slider::TextBoxRight, false, 18, 18);
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

    // Play order buttons
    auto setupPlayBtn = [&](juce::TextButton& btn, const juce::String& label, int orderIdx)
    {
        btn.setButtonText (label);
        btn.setColour (juce::TextButton::buttonColourId, vp.playOrder == orderIdx ? playBtnOn : playBtnOff);
        btn.onClick = [this, v, &btn, orderIdx]()
        {
            audioProcessor.voice[v].playOrder = orderIdx;
            audioProcessor.voice[v].resetOnNextBlock.store (true);
            juce::TextButton* all[4] = { &playFwdBtn[v], &playRevBtn[v], &playConvBtn[v], &playRndBtn[v] };
            for (auto* b : all) b->setColour (juce::TextButton::buttonColourId, playBtnOff);
            btn.setColour (juce::TextButton::buttonColourId, playBtnOn);
        };
        addAndMakeVisible (btn);
    };
    setupPlayBtn (playFwdBtn[v],  "FWD",  0);
    setupPlayBtn (playRevBtn[v],  "REV",  1);
    setupPlayBtn (playConvBtn[v], "CONV", 2);
    setupPlayBtn (playRndBtn[v],  "RND",  3);

    // Reset
    resetBtn[v].setButtonText ("RST");
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

    // Bipolar toggle
    {
        bool isUni = vp.unipolar;
        bipolarBtn[v].setButtonText (isUni ? "UNI" : "BI");
        bipolarBtn[v].setToggleState (isUni, juce::dontSendNotification);
        bipolarBtn[v].setClickingTogglesState (true);
        bipolarBtn[v].setColour (juce::TextButton::buttonColourId,   isUni ? juce::Colour(0xff305050) : juce::Colour(0xff2a2050));
        bipolarBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff305050));
        bipolarBtn[v].onClick = [this, v]()
        {
            bool uni = bipolarBtn[v].getToggleState();
            audioProcessor.voice[v].unipolar = uni;
            bipolarBtn[v].setButtonText (uni ? "UNI" : "BI");
            bipolarBtn[v].setColour (juce::TextButton::buttonColourId,
                                     uni ? juce::Colour(0xff305050) : juce::Colour(0xff2a2050));
            for (int i = 0; i < 16; ++i)
            {
                if (uni) {
                    stepKnob[v][i].setRange (0.0, 5.0, 0.01);
                    float c = juce::jmax (0.0f, audioProcessor.voice[v].stepVoltages[i]);
                    audioProcessor.voice[v].stepVoltages[i] = c;
                    stepKnob[v][i].setValue (c, juce::dontSendNotification);
                } else {
                    stepKnob[v][i].setRange (-5.0, 5.0, 0.01);
                    stepKnob[v][i].setValue (audioProcessor.voice[v].stepVoltages[i], juce::dontSendNotification);
                }
            }
            repaint();
        };
        addAndMakeVisible (bipolarBtn[v]);
    }

    // Swing
    swingSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider[v].setRange (0.5, 0.75, 0.001);
    swingSlider[v].setValue (vp.swingAmount, juce::dontSendNotification);
    swingSlider[v].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    swingSlider[v].setColour (juce::Slider::trackColourId,      knobColour);
    swingSlider[v].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
    swingSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].swingAmount = (float)swingSlider[v].getValue(); };
    addAndMakeVisible (swingSlider[v]);

    // Run / Stop
    {
        bool isRunning = vp.sequencerRunning.load();
        runStopBtn[v].setButtonText (isRunning ? "STOP" : "RUN");
        runStopBtn[v].setColour (juce::TextButton::buttonColourId, isRunning ? stopColour : runColour);
        runStopBtn[v].onClick = [this, v]()
        {
            bool now = !audioProcessor.voice[v].sequencerRunning.load();
            audioProcessor.voice[v].sequencerRunning.store (now);
            if (now) audioProcessor.voice[v].resetOnNextBlock.store (true);
            runStopBtn[v].setButtonText (now ? "STOP" : "RUN");
            runStopBtn[v].setColour (juce::TextButton::buttonColourId, now ? stopColour : runColour);
        };
        addAndMakeVisible (runStopBtn[v]);
    }

    // SEQ transport knobs
    setupKnob (rangeSlider[v], 0.0, 1.0, vp.rangeVCA);
    rangeSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].rangeVCA = (float)rangeSlider[v].getValue(); };

    setupKnob (portaSlider[v], 0.0, 2.0, vp.portamentoTime);
    portaSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].portamentoTime = (float)portaSlider[v].getValue(); };

    clockDivBox[v].addItem ("1/4",1); clockDivBox[v].addItem ("1/8",2); clockDivBox[v].addItem ("1/16",3);
    clockDivBox[v].addItem ("1/8T",4); clockDivBox[v].addItem ("1/16T",5);
    clockDivBox[v].addItem ("1/8.",6); clockDivBox[v].addItem ("1/16.",7);
    clockDivBox[v].setSelectedItemIndex (vp.clockDivision, juce::dontSendNotification);
    clockDivBox[v].onChange = [this, v]() { audioProcessor.voice[v].clockDivision = clockDivBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (clockDivBox[v]);

    // Quantizer
    rootBox[v].addItem ("C",1); rootBox[v].addItem ("C#",2); rootBox[v].addItem ("D",3);
    rootBox[v].addItem ("D#",4); rootBox[v].addItem ("E",5); rootBox[v].addItem ("F",6);
    rootBox[v].addItem ("F#",7); rootBox[v].addItem ("G",8); rootBox[v].addItem ("G#",9);
    rootBox[v].addItem ("A",10); rootBox[v].addItem ("A#",11); rootBox[v].addItem ("B",12);
    rootBox[v].setSelectedItemIndex (vp.rootNote, juce::dontSendNotification);
    rootBox[v].onChange = [this, v]() { audioProcessor.voice[v].rootNote = rootBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (rootBox[v]);

    scaleBox[v].addItem ("Major",1); scaleBox[v].addItem ("Nat Minor",2);
    scaleBox[v].addItem ("Dorian",3); scaleBox[v].addItem ("Phrygian",4);
    scaleBox[v].addItem ("Lydian",5); scaleBox[v].addItem ("Mixolyd",6);
    scaleBox[v].addItem ("Penta Maj",7); scaleBox[v].addItem ("Penta Min",8);
    scaleBox[v].addItem ("Chromatic",9);
    scaleBox[v].setSelectedItemIndex (vp.currentScale, juce::dontSendNotification);
    scaleBox[v].onChange = [this, v]() { audioProcessor.voice[v].currentScale = scaleBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (scaleBox[v]);

    // OSC 1
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

    // OSC1 feedback
    setupKnob (osc1FeedbackSlider[v], 0.0, 1.0, vp.osc1Feedback);
    osc1FeedbackSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc1Feedback = (float)osc1FeedbackSlider[v].getValue(); };

    // Drift
    setupKnob (driftSlider[v], 0.0, 1.0, vp.driftAmount);
    driftSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].driftAmount = (float)driftSlider[v].getValue(); };

    // OSC 2
    setupKnob (osc2PosSlider[v], 0.0, 1.0, vp.osc2Position);
    osc2PosSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc2Position = (float)osc2PosSlider[v].getValue(); };

    setupKnob (osc2LevelSlider[v], 0.0, 1.0, vp.osc2Level);
    osc2LevelSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc2Level = (float)osc2LevelSlider[v].getValue(); };

    osc2OctaveBox[v].addItem ("-2",1); osc2OctaveBox[v].addItem ("-1",2); osc2OctaveBox[v].addItem ("0",3);
    osc2OctaveBox[v].addItem ("+1",4); osc2OctaveBox[v].addItem ("+2",5);
    osc2OctaveBox[v].setSelectedItemIndex (vp.osc2Octave + 2, juce::dontSendNotification);
    osc2OctaveBox[v].onChange = [this, v]() { audioProcessor.voice[v].osc2Octave = osc2OctaveBox[v].getSelectedItemIndex() - 2; };
    addAndMakeVisible (osc2OctaveBox[v]);

    // FM
    setupKnob (fmDepthSlider[v], 0.0, 1.0, vp.fmDepth);
    fmDepthSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].fmDepth = (float)fmDepthSlider[v].getValue(); };

    // FM Ratio — full-width LinearHorizontal, -8 to +8, shows exact value
    fmRatioSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    fmRatioSlider[v].setRange (-8.0, 8.0, 0.01);
    fmRatioSlider[v].setValue (vp.fmRatio, juce::dontSendNotification);
    fmRatioSlider[v].setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 20);
    fmRatioSlider[v].setNumDecimalPlacesToDisplay (2);
    fmRatioSlider[v].setColour (juce::Slider::trackColourId,             knobColour);
    fmRatioSlider[v].setColour (juce::Slider::backgroundColourId,        juce::Colour (0xff252540));
    fmRatioSlider[v].setColour (juce::Slider::textBoxTextColourId,       textColour);
    fmRatioSlider[v].setColour (juce::Slider::textBoxBackgroundColourId, bgColour);
    fmRatioSlider[v].setColour (juce::Slider::textBoxOutlineColourId,    bgColour);
    fmRatioSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].fmRatio = (float)fmRatioSlider[v].getValue(); };
    addAndMakeVisible (fmRatioSlider[v]);

    // Cross-mod
    setupKnob (crossModSlider[v], 0.0, 1.0, vp.crossModDepth);
    crossModSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].crossModDepth = (float)crossModSlider[v].getValue(); };

    // Filter
    setupKnob (cutoffSlider[v], 20.0, 16000.0, vp.filterCutoff, 1000.0);
    cutoffSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterCutoff = (float)cutoffSlider[v].getValue(); };

    setupKnob (resonanceSlider[v], 0.0, 1.0, vp.filterResonance);
    resonanceSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterResonance = (float)resonanceSlider[v].getValue(); };

    setupKnob (filterEnvAmtSlider[v], 0.0, 1.0, vp.filterEnvAmount);
    filterEnvAmtSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterEnvAmount = (float)filterEnvAmtSlider[v].getValue(); };

    // Filter drive
    setupKnob (filterDriveSlider[v], 0.0, 1.0, vp.filterDrive);
    filterDriveSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterDrive = (float)filterDriveSlider[v].getValue(); };

    // Filter mode
    filterModeBox[v].addItem ("LP", 1); filterModeBox[v].addItem ("BP", 2); filterModeBox[v].addItem ("HP", 3);
    filterModeBox[v].setSelectedItemIndex (vp.filterMode, juce::dontSendNotification);
    filterModeBox[v].onChange = [this, v]() { audioProcessor.voice[v].filterMode = filterModeBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (filterModeBox[v]);

    // Filter slope
    filterSlopeBtn[v].setButtonText ("12");
    filterSlopeBtn[v].setClickingTogglesState (true);
    filterSlopeBtn[v].setToggleState (vp.filterSlope == 1, juce::dontSendNotification);
    filterSlopeBtn[v].setColour (juce::TextButton::buttonColourId,   vp.filterSlope ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
    filterSlopeBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff2255aa));
    filterSlopeBtn[v].onClick = [this, v]()
    {
        bool is24 = filterSlopeBtn[v].getToggleState();
        audioProcessor.voice[v].filterSlope = is24 ? 1 : 0;
        filterSlopeBtn[v].setButtonText (is24 ? "24" : "12");
        filterSlopeBtn[v].setColour (juce::TextButton::buttonColourId, is24 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
    };
    addAndMakeVisible (filterSlopeBtn[v]);

    // Amp Envelope
    setupKnob (attackSlider[v],  0.001, 2.0, vp.adsrParams.attack,  0.3);
    attackSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].adsrParams.attack  = (float)attackSlider[v].getValue(); };
    setupKnob (decaySlider[v],   0.001, 2.0, vp.adsrParams.decay,   0.3);
    decaySlider[v].onValueChange   = [this, v]() { audioProcessor.voice[v].adsrParams.decay   = (float)decaySlider[v].getValue(); };
    setupKnob (sustainSlider[v], 0.0,   1.0, vp.adsrParams.sustain);
    sustainSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].adsrParams.sustain = (float)sustainSlider[v].getValue(); };
    setupKnob (releaseSlider[v], 0.001, 3.0, vp.adsrParams.release, 0.3);
    releaseSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].adsrParams.release = (float)releaseSlider[v].getValue(); };

    // Filter Envelope
    setupKnob (fAttackSlider[v],  0.001, 4.0, vp.filterEnvParams.attack,  0.3);
    fAttackSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].filterEnvParams.attack  = (float)fAttackSlider[v].getValue(); };
    setupKnob (fDecaySlider[v],   0.001, 4.0, vp.filterEnvParams.decay,   0.3);
    fDecaySlider[v].onValueChange   = [this, v]() { audioProcessor.voice[v].filterEnvParams.decay   = (float)fDecaySlider[v].getValue(); };
    setupKnob (fSustainSlider[v], 0.0,   1.0, vp.filterEnvParams.sustain);
    fSustainSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterEnvParams.sustain = (float)fSustainSlider[v].getValue(); };
    setupKnob (fReleaseSlider[v], 0.001, 4.0, vp.filterEnvParams.release, 0.3);
    fReleaseSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterEnvParams.release = (float)fReleaseSlider[v].getValue(); };

    // LFO 1
    setupKnob (lfoRateSlider[v],  0.1, 20.0, vp.lfoRate,  4.0);
    lfoRateSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].lfoRate  = (float)lfoRateSlider[v].getValue(); };
    setupKnob (lfoDepthSlider[v], 0.0, 1.0,  vp.lfoDepth);
    lfoDepthSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].lfoDepth = (float)lfoDepthSlider[v].getValue(); };
    lfoTargetBox[v].addItem ("PWM",1); lfoTargetBox[v].addItem ("Cutoff",2); lfoTargetBox[v].addItem ("Pitch",3);
    lfoTargetBox[v].setSelectedItemIndex (vp.lfoTarget, juce::dontSendNotification);
    lfoTargetBox[v].onChange = [this, v]() { audioProcessor.voice[v].lfoTarget = lfoTargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfoTargetBox[v]);

    // LFO 2
    setupKnob (lfo2RateSlider[v],  0.1, 20.0, vp.lfo2Rate,  4.0);
    lfo2RateSlider[v].onValueChange  = [this, v]() { audioProcessor.voice[v].lfo2Rate  = (float)lfo2RateSlider[v].getValue(); };
    setupKnob (lfo2DepthSlider[v], 0.0, 1.0,  vp.lfo2Depth);
    lfo2DepthSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].lfo2Depth = (float)lfo2DepthSlider[v].getValue(); };
    lfo2TargetBox[v].addItem ("PWM",1); lfo2TargetBox[v].addItem ("Cutoff",2); lfo2TargetBox[v].addItem ("Pitch",3);
    lfo2TargetBox[v].setSelectedItemIndex (vp.lfo2Target, juce::dontSendNotification);
    lfo2TargetBox[v].onChange = [this, v]() { audioProcessor.voice[v].lfo2Target = lfo2TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo2TargetBox[v]);

    // Mod Envelope
    setupKnob (modEnvAtkSlider  [v], 0.001, 4.0, vp.modEnv.attack,  0.3);
    modEnvAtkSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.attack  = (float)modEnvAtkSlider  [v].getValue(); };
    setupKnob (modEnvDecSlider  [v], 0.001, 4.0, vp.modEnv.decay,   0.3);
    modEnvDecSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.decay   = (float)modEnvDecSlider  [v].getValue(); };
    setupKnob (modEnvSusSlider  [v], 0.0,   1.0, vp.modEnv.sustain);
    modEnvSusSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.sustain = (float)modEnvSusSlider  [v].getValue(); };
    setupKnob (modEnvRelSlider  [v], 0.001, 4.0, vp.modEnv.release, 0.3);
    modEnvRelSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.release = (float)modEnvRelSlider  [v].getValue(); };
    setupKnob (modEnvDepthSlider[v], 0.0,   1.0, vp.modEnv.depth);
    modEnvDepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.depth   = (float)modEnvDepthSlider[v].getValue(); };

    modEnvDestBox[v].addItem ("FM Depth", 1); modEnvDestBox[v].addItem ("Pitch", 2); modEnvDestBox[v].addItem ("Filter", 3);
    modEnvDestBox[v].setSelectedItemIndex (vp.modEnv.dest, juce::dontSendNotification);
    modEnvDestBox[v].onChange = [this,v]() { audioProcessor.voice[v].modEnv.dest = modEnvDestBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (modEnvDestBox[v]);

    // Default = GATE (retriggers on every sequencer gate, same as amp/filter envs)
    // Toggled  = SYNC (free-runs on a clock division)
    modEnvSyncBtn[v].setButtonText (vp.modEnv.clockSync ? "SYNC" : "GATE");
    modEnvSyncBtn[v].setClickingTogglesState (true);
    modEnvSyncBtn[v].setToggleState (vp.modEnv.clockSync, juce::dontSendNotification);
    modEnvSyncBtn[v].setColour (juce::TextButton::buttonColourId,   vp.modEnv.clockSync ? juce::Colour(0xffe09040) : gateOnColour);
    modEnvSyncBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xffe09040));
    modEnvSyncBtn[v].onClick = [this,v]()
    {
        bool s = modEnvSyncBtn[v].getToggleState();
        audioProcessor.voice[v].modEnv.clockSync = s;
        modEnvSyncBtn[v].setButtonText (s ? "SYNC" : "GATE");
        modEnvSyncBtn[v].setColour (juce::TextButton::buttonColourId, s ? juce::Colour(0xffe09040) : gateOnColour);
    };
    addAndMakeVisible (modEnvSyncBtn[v]);

    addCenvDivItems (modEnvDivBox[v]);
    modEnvDivBox[v].setSelectedItemIndex (vp.modEnv.clockDiv, juce::dontSendNotification);
    modEnvDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].modEnv.clockDiv = modEnvDivBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (modEnvDivBox[v]);

    addAndMakeVisible (*oscScope[v]);
    addAndMakeVisible (*wavetableDisplay[v]);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupKnob (juce::Slider& s, double mn, double mx,
                                                  double val, double skew)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRange (mn, mx);
    s.setValue (val, juce::dontSendNotification);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, knobColour);
    if (skew > 0.0) s.setSkewFactorFromMidPoint (skew);
    addAndMakeVisible (s);
}

//==============================================================================
// PAINT
//==============================================================================
void VoltageSeq2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    // ── Global header ─────────────────────────────────────────────────────────
    g.setColour (juce::Colour (0xff0a0a1a));
    g.fillRect (0, 0, 1350, headerH);

    // Branding — left side of header
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.setColour (juce::Colour (0xffe09040));                      // amber — product name
    g.drawText ("VoltageSEQ", 8, 3, 130, 22, juce::Justification::centredLeft);
    g.setFont (juce::Font (9.0f, juce::Font::plain));
    g.setColour (dimColour.withAlpha (0.7f));                     // dim — manufacturer
    g.drawText ("MURGATROYD INSTRUMENTS", 8, 18, 200, 11, juce::Justification::centredLeft);

    // BPM / Preset labels
    g.setFont (juce::Font (9.0f, juce::Font::bold));
    g.setColour (dimColour.withAlpha (0.6f));
    g.drawText ("BPM", 502, 4, 28, 20, juce::Justification::centredRight);
    g.drawText ("PRESET", 1090, 4, 42, 20, juce::Justification::centredLeft);

    // Backplate drawn once for the full content area
    if (backplate != nullptr)
        backplate->drawWithin (g,
            juce::Rectangle<float> (0.0f, (float)headerH, 1350.0f, (float)(winH - headerH)),
            juce::RectanglePlacement::stretchToFit, 0.70f);

    for (int v = 0; v < 2; ++v)
    {
        const int sY  = (v == 0) ? seqAY  : seqBY;
        const int sbY = (v == 0) ? subAY  : subBY;
        const int cY  = (v == 0) ? ctrlAY : ctrlBY;
        const juce::Colour& accent = (v == 0) ? voiceAColour : voiceBColour;
        const juce::String  vLabel = (v == 0) ? "VOICE  A" : "VOICE  B";

        // ── Sequencer strip ───────────────────────────────────────────────────
        g.setColour (sectionColour.withAlpha (0.82f));
        g.fillRoundedRectangle ((float)seqX, (float)sY, (float)seqW, (float)seqH, 4.0f);

        // Voice label + accent bar
        g.setColour (accent.withAlpha (0.18f));
        g.fillRect (seqX, sY, seqW, 13);
        g.setColour (accent);
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.drawText (vLabel, seqX + 4, sY + 1, 120, 11, juce::Justification::centredLeft);

        // 0 V reference line
        const bool isUni = audioProcessor.voice[v].unipolar;
        const float zeroY = (float)(sY + stepSliderTop + (isUni ? stepSliderH : stepSliderH / 2));
        g.setColour (juce::Colour (0xff333366));
        g.drawLine (10.0f, zeroY, (float)(seqX + seqW - 5), zeroY, 1.0f);

        // GATE / SLIDE labels (right edge)
        g.setFont (juce::Font (8.0f, juce::Font::bold));
        g.setColour (gateOnColour);
        g.drawText ("GATE",  1293, sY + gateRelY,  50, 13, juce::Justification::centredLeft);
        g.setColour (slideOnColour);
        g.drawText ("SLIDE", 1293, sY + slideRelY, 50, 13, juce::Justification::centredLeft);

        // Step numbers
        g.setColour (dimColour);
        g.setFont (juce::Font (8.0f));
        for (int i = 0; i < 16; ++i)
            g.drawText (juce::String (i + 1), seqX + i * stepStride + 4, sY + seqH - 10,
                        stepStride - 8, 10, juce::Justification::centred);

        // ── Sub-strip ─────────────────────────────────────────────────────────
        g.setColour (subStripColour.withAlpha (0.88f));
        g.fillRoundedRectangle ((float)seqX, (float)sbY, (float)seqW, (float)subH, 3.0f);
        g.setColour (dimColour);
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.drawText ("LEN",   14,  sbY + 3, 40, 10, juce::Justification::centredLeft);
        g.drawText ("ORDER", 112, sbY + 3, 50, 10, juce::Justification::centredLeft);
        g.drawText ("SWING", 492, sbY + 3, 50, 10, juce::Justification::centredLeft);

        // Voice-A-only labels
        if (v == 0)
            g.drawText ("AUTO", 665, sbY + 3, 40, 10, juce::Justification::centredLeft);

        // ── Control panels row ────────────────────────────────────────────────
        auto drawPanel = [&](int px, int pw, const juce::String& title)
        {
            g.setColour (sectionColour.withAlpha (0.85f));
            g.fillRoundedRectangle ((float)px, (float)cY, (float)pw, (float)ctrlH, 4.0f);
            g.setColour (dimColour);
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.drawText (title, px, cY + 3, pw, 12, juce::Justification::centred);
        };
        drawPanel (pSeqX,  pSeqW,  "SEQ");
        drawPanel (pQntX,  pQntW,  "QUANTIZER");
        drawPanel (pO1X,   pO1W,   "OSC 1");
        drawPanel (pO2X,   pO2W,   "OSC 2");
        drawPanel (pFltX,  pFltW,  "FILTER");
        drawPanel (pAEX,   pAEW,   "AMP ENV");
        drawPanel (pFEX,   pFEW,   "FILTER ENV");
        drawPanel (pLfoX,  pLfoW,  "LFO");

        // LFO sub-divider
        g.setColour (dimColour.withAlpha (0.4f));
        g.drawLine (1259.0f, (float)(cY + 16), 1259.0f, (float)(cY + ctrlH - 8), 1.0f);
        g.setFont (juce::Font (8.0f, juce::Font::bold));
        g.setColour (dimColour);
        g.drawText ("LFO 1", 1172, cY + 15, 84, 11, juce::Justification::centred);
        g.drawText ("LFO 2", 1261, cY + 15, 82, 11, juce::Justification::centred);

        // Control panel labels
        const int lY1 = cY + lOff1, lY2 = cY + lOff2, lY3 = cY + lOff3;
        g.setColour (textColour);
        g.setFont (juce::Font (9.0f));
        // SEQ
        g.drawText ("RANGE",    pSeqX,  lY1, pSeqW, 12, juce::Justification::centred);
        g.drawText ("CLOCK",    pSeqX,  lY2, pSeqW, 12, juce::Justification::centred);
        g.drawText ("PORTA",    pSeqX,  lY3, pSeqW, 12, juce::Justification::centred);
        // QUANT
        g.drawText ("ROOT",     pQntX,  lY1, pQntW, 12, juce::Justification::centred);
        g.drawText ("SCALE",    pQntX,  lY2, pQntW, 12, juce::Justification::centred);
        // OSC 1
        g.drawText ("WAVE",    pO1X,    cY+14,  pO1W, 12, juce::Justification::centred);
        g.drawText ("LEVEL",   pO1X,    cY+54,  46,   12, juce::Justification::centred);
        g.drawText ("OCT",     pO1X+46, cY+54,  78,   12, juce::Justification::centred);
        g.drawText ("FEEDBK",  pO1X+118,cY+54,  52,   12, juce::Justification::centred);
        g.drawText ("PWM",   pO1X+4,  cY+108, kSz, 12, juce::Justification::centred);
        g.drawText ("DRIFT", pO1X+46, cY+108, kSz, 12, juce::Justification::centred);
        g.drawText ("SCOPE", pO1X+88, cY+108, pO1W-93, 11, juce::Justification::centred);
        // OSC 2
        g.drawText ("WT POS",  pO2X,    cY+14,  50,   12, juce::Justification::centred);
        g.drawText ("LEVEL",   pO2X+55, cY+14,  kSz,  12, juce::Justification::centred);
        g.drawText ("FM DPT",  pO2X+100,cY+14,  55,   12, juce::Justification::centred);
        g.drawText ("RATIO",   pO2X,    cY+68,  100,  12, juce::Justification::centredLeft);
        g.drawText ("OCT",     pO2X,    cY+106, 65,   12, juce::Justification::centred);
        g.drawText ("XMOD",    pO2X+80, cY+106, kSz,  12, juce::Justification::centred);
        g.drawText ("WT",      pO2X,    cY+142, pO2W, 11, juce::Justification::centred);
        // Filter
        g.drawText ("CUTOFF",  pFltX+5,  lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("RES",     pFltX+48, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("DRIVE",   pFltX+91, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("MODE",    pFltX+5,  lY2, 65,  12, juce::Justification::centred);
        g.drawText ("SLOPE",   pFltX+74, lY2, 32,  12, juce::Justification::centred);
        g.drawText ("ENV",     pFltX+110,lY2, kSz, 12, juce::Justification::centred);
        // AMP ENV
        g.drawText ("A", pAEX+2,  lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("D", pAEX+46, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("S", pAEX+90, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("R", pAEX+134,lY1, kSz, 12, juce::Justification::centred);
        // FILTER ENV
        g.drawText ("A", pFEX+2,  lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("D", pFEX+46, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("S", pFEX+90, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("R", pFEX+134,lY1, kSz, 12, juce::Justification::centred);
        // LFO 1
        g.drawText ("RATE",  1175, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("DEPTH", 1218, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("TGT",   1173, lY2, 82,  12, juce::Justification::centred);
        // LFO 2
        g.drawText ("RATE",  1263, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("DEPTH", 1306, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("TGT",   1261, lY2, 82,  12, juce::Justification::centred);

        // Mod envelope labels (third row in filter env + LFO panels)
        g.setColour (dimColour);
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.drawText ("MOD ENV", pFEX, cY + lOff3 - 2, pFEW, 11, juce::Justification::centred);
        g.setFont (juce::Font (9.0f));
        g.setColour (textColour);
        g.drawText ("A",     pFEX + 2,   cY + lOff3 + 10, kSz, 11, juce::Justification::centred);
        g.drawText ("D",     pFEX + 46,  cY + lOff3 + 10, kSz, 11, juce::Justification::centred);
        g.drawText ("S",     pFEX + 90,  cY + lOff3 + 10, kSz, 11, juce::Justification::centred);
        g.drawText ("R",     pFEX + 134, cY + lOff3 + 10, kSz, 11, juce::Justification::centred);
        g.drawText ("DEPTH", pLfoX + 4,  cY + lOff3 + 10, kSz, 11, juce::Justification::centred);
        g.drawText ("DEST",  pLfoX + 46, cY + lOff3 + 10, 80,  11, juce::Justification::centred);
        g.drawText ("TRIG",  pLfoX + 130,cY + lOff3 + 10, 40,  11, juce::Justification::centred);

        // Accent stripe on control panel left edge
        g.setColour (accent.withAlpha (0.4f));
        g.fillRect (pSeqX, cY, 2, ctrlH);
    }

    // Divider between Voice A controls and Voice B controls
    g.setColour (voiceAColour.withAlpha (0.2f));
    g.fillRect (0, ctrlBY - 1, 1350, 2);
    g.setColour (voiceBColour.withAlpha (0.2f));
    g.fillRect (0, ctrlBY + 1, 1350, 1);
}

//==============================================================================
// RESIZED
//==============================================================================
void VoltageSeq2AudioProcessorEditor::resized()
{
    // Global header
    bpmSlider    .setBounds (530,  4, 190, 20);
    autoBtn      .setBounds (730,  3,  55, 22);
    savePresetBtn.setBounds (1155, 3,  60, 22);
    loadPresetBtn.setBounds (1220, 3,  60, 22);

    layoutVoice (0, seqAY, ctrlAY);
    layoutVoice (1, seqBY, ctrlBY);
}

//==============================================================================
// layoutVoice — position all per-voice controls
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutVoice (int v, int seqTopY, int ctrlTopY)
{
    const int sbY  = seqTopY + seqH;          // sub-strip Y
    const int sbCY = sbY + 6;                 // control row inside sub-strip
    const int cy1  = ctrlTopY + cOff1;
    const int cy2  = ctrlTopY + cOff2;
    const int cy3  = ctrlTopY + cOff3;

    // ── Step controls ─────────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        const int bx = seqX + i * stepStride;
        stepKnob[v][i].setBounds (bx + 4,  seqTopY + stepSliderTop, 72, stepSliderH);
        gateBtn [v][i].setBounds (bx + 8,  seqTopY + gateRelY,  64, 13);
        slideBtn[v][i].setBounds (bx + 8,  seqTopY + slideRelY, 64, 12);
    }

    // ── Sub-strip ─────────────────────────────────────────────────────────────
    seqLengthSlider[v].setBounds (12,  sbCY, 82, 18);
    playFwdBtn [v]    .setBounds (112, sbCY, 36, 18);
    playRevBtn [v]    .setBounds (151, sbCY, 36, 18);
    playConvBtn[v]    .setBounds (190, sbCY, 42, 18);
    playRndBtn [v]    .setBounds (235, sbCY, 36, 18);
    resetBtn   [v]    .setBounds (280, sbCY, 46, 18);
    bipolarBtn [v]    .setBounds (330, sbCY, 46, 18);
    swingSlider[v]    .setBounds (490, sbCY,110, 18);
    runStopBtn [v]    .setBounds (608, sbCY, 50, 18);

    if (v == 0)
        autoBtn.setBounds (665, sbCY, 50, 18);   // shared — only show in Voice A sub-strip

    // ── SEQ panel ─────────────────────────────────────────────────────────────
    rangeSlider[v]  .setBounds (pSeqX + 52, cy1, kSz, kSz);
    clockDivBox[v]  .setBounds (pSeqX + 8,  cy2, pSeqW - 16, 20);
    portaSlider[v]  .setBounds (pSeqX + 52, cy3, kSz, kSz);

    // ── Quantizer ─────────────────────────────────────────────────────────────
    rootBox [v].setBounds (pQntX + 8, cy1, pQntW - 16, 22);
    scaleBox[v].setBounds (pQntX + 8, cy2, pQntW - 16, 22);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    osc1WaveBox        [v].setBounds (pO1X + 8,   ctrlTopY + 26, pO1W - 16, 20);
    osc1LevelSlider    [v].setBounds (pO1X + 6,   ctrlTopY + 66, kSz, kSz);
    osc1OctaveBox      [v].setBounds (pO1X + 46,  ctrlTopY + 72, 78,  22);
    osc1FeedbackSlider [v].setBounds (pO1X + 130, ctrlTopY + 66, kSz, kSz);
    osc1PWMSlider      [v].setBounds (pO1X + 4,   ctrlTopY + 120, kSz, kSz);
    driftSlider        [v].setBounds (pO1X + 46,  ctrlTopY + 120, kSz, kSz);
    oscScope           [v]->setBounds(pO1X + 88,  ctrlTopY + 120, pO1W - 93, 50);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    // Row 1: WT Pos | Level | FM Depth
    osc2PosSlider   [v].setBounds (pO2X + 5,   ctrlTopY + 26, kSz, kSz);
    osc2LevelSlider [v].setBounds (pO2X + 55,  ctrlTopY + 26, kSz, kSz);
    fmDepthSlider   [v].setBounds (pO2X + 105, ctrlTopY + 26, kSz, kSz);
    // Row 2: FM Ratio linear slider (full width, shows value)
    fmRatioSlider   [v].setBounds (pO2X + 5,   ctrlTopY + 80, 145, 22);
    // Row 3: Octave | CrossMod (clearly separated)
    osc2OctaveBox   [v].setBounds (pO2X + 5,   ctrlTopY + 118, 60, 22);
    crossModSlider  [v].setBounds (pO2X + 80,  ctrlTopY + 114, kSz, kSz);
    // Mini WT display at bottom
    wavetableDisplay[v]->setBounds(pO2X + 5,   ctrlTopY + 152, pO2W - 10, 16);

    // ── Filter ────────────────────────────────────────────────────────────────
    cutoffSlider      [v].setBounds (pFltX + 5,  cy1, kSz, kSz);
    resonanceSlider   [v].setBounds (pFltX + 48, cy1, kSz, kSz);
    filterDriveSlider [v].setBounds (pFltX + 91, cy1, kSz, kSz);
    filterModeBox     [v].setBounds (pFltX + 5,  cy2, 65,  20);
    filterSlopeBtn    [v].setBounds (pFltX + 74, cy2, 32,  20);
    filterEnvAmtSlider[v].setBounds (pFltX + 110,cy2, kSz, kSz);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    const int aeStride = 44;
    attackSlider [v].setBounds (pAEX + 2,            cy1, kSz, kSz);
    decaySlider  [v].setBounds (pAEX + 2 + aeStride, cy1, kSz, kSz);
    sustainSlider[v].setBounds (pAEX + 2 + aeStride*2, cy1, kSz, kSz);
    releaseSlider[v].setBounds (pAEX + 2 + aeStride*3, cy1, kSz, kSz);

    // ── Filter Envelope ───────────────────────────────────────────────────────
    fAttackSlider [v].setBounds (pFEX + 2,            cy1, kSz, kSz);
    fDecaySlider  [v].setBounds (pFEX + 2 + aeStride, cy1, kSz, kSz);
    fSustainSlider[v].setBounds (pFEX + 2 + aeStride*2, cy1, kSz, kSz);
    fReleaseSlider[v].setBounds (pFEX + 2 + aeStride*3, cy1, kSz, kSz);

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    lfoRateSlider [v].setBounds (1175, cy1, kSz, kSz);
    lfoDepthSlider[v].setBounds (1218, cy1, kSz, kSz);
    lfoTargetBox  [v].setBounds (1173, cy2, 82,  20);

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    lfo2RateSlider [v].setBounds (1263, cy1, kSz, kSz);
    lfo2DepthSlider[v].setBounds (1306, cy1, kSz, kSz);
    lfo2TargetBox  [v].setBounds (1261, cy2, 82,  20);

    // ── Mod Envelope (third row, spans filter-env + LFO columns)
    modEnvAtkSlider  [v].setBounds (pFEX + 2,            cy3, kSz, kSz);
    modEnvDecSlider  [v].setBounds (pFEX + 46,           cy3, kSz, kSz);
    modEnvSusSlider  [v].setBounds (pFEX + 90,           cy3, kSz, kSz);
    modEnvRelSlider  [v].setBounds (pFEX + 134,          cy3, kSz, kSz);
    modEnvDepthSlider[v].setBounds (pLfoX + 4,           cy3, kSz, kSz);
    modEnvDestBox    [v].setBounds (pLfoX + 46,          cy3 + 6, 80, 20);
    modEnvSyncBtn    [v].setBounds (pLfoX + 130,         cy3 + 6, 40, 20);
    modEnvDivBox     [v].setBounds (pLfoX + 130,         cy3 + 27, 40, 20);
}

//==============================================================================
// TIMER CALLBACK
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

        const bool isRunning = vp.sequencerRunning.load();
        runStopBtn[v].setColour (juce::TextButton::buttonColourId, isRunning ? stopColour : runColour);
        runStopBtn[v].setButtonText (isRunning ? "STOP" : "RUN");
    }
}

//==============================================================================
// syncUIFromProcessor
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

        for (int i = 0; i < 16; ++i)
        {
            stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
            gateBtn [v][i].setToggleState (vp.stepGates [i], juce::dontSendNotification);
            slideBtn[v][i].setToggleState (vp.stepGlides[i], juce::dontSendNotification);
        }

        seqLengthSlider[v].setValue (vp.sequenceLength, juce::dontSendNotification);
        swingSlider    [v].setValue (vp.swingAmount,    juce::dontSendNotification);

        juce::TextButton* pBtns[4] = { &playFwdBtn[v], &playRevBtn[v], &playConvBtn[v], &playRndBtn[v] };
        for (int i = 0; i < 4; ++i)
            pBtns[i]->setColour (juce::TextButton::buttonColourId, vp.playOrder == i ? playBtnOn : playBtnOff);

        bipolarBtn[v].setToggleState (vp.unipolar, juce::dontSendNotification);
        bipolarBtn[v].setButtonText  (vp.unipolar ? "UNI" : "BI");
        bipolarBtn[v].setColour (juce::TextButton::buttonColourId,
                                  vp.unipolar ? juce::Colour (0xff305050) : juce::Colour (0xff2a2050));
        for (int i = 0; i < 16; ++i)
        {
            if (vp.unipolar) stepKnob[v][i].setRange (0.0, 5.0, 0.01);
            else             stepKnob[v][i].setRange (-5.0, 5.0, 0.01);
            stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
        }

        rangeSlider[v].setValue (vp.rangeVCA,       juce::dontSendNotification);
        portaSlider[v].setValue (vp.portamentoTime, juce::dontSendNotification);
        clockDivBox[v].setSelectedItemIndex (vp.clockDivision, juce::dontSendNotification);
        rootBox [v].setSelectedItemIndex (vp.rootNote,     juce::dontSendNotification);
        scaleBox[v].setSelectedItemIndex (vp.currentScale, juce::dontSendNotification);

        osc1WaveBox    [v].setSelectedItemIndex (vp.osc1Waveform,   juce::dontSendNotification);
        osc1LevelSlider[v].setValue (vp.osc1Level,                   juce::dontSendNotification);
        osc1OctaveBox  [v].setSelectedItemIndex (vp.osc1Octave + 2, juce::dontSendNotification);
        osc1PWMSlider  [v].setValue (vp.osc1PulseWidth,              juce::dontSendNotification);
        osc1FeedbackSlider[v].setValue (vp.osc1Feedback,             juce::dontSendNotification);
        driftSlider       [v].setValue (vp.driftAmount,              juce::dontSendNotification);
        fmDepthSlider     [v].setValue (vp.fmDepth,                  juce::dontSendNotification);
        fmRatioSlider     [v].setValue (vp.fmRatio,                  juce::dontSendNotification);
        crossModSlider    [v].setValue (vp.crossModDepth,            juce::dontSendNotification);

        osc2PosSlider  [v].setValue (vp.osc2Position,                juce::dontSendNotification);
        osc2LevelSlider[v].setValue (vp.osc2Level,                   juce::dontSendNotification);
        osc2OctaveBox  [v].setSelectedItemIndex (vp.osc2Octave + 2, juce::dontSendNotification);

        cutoffSlider      [v].setValue (vp.filterCutoff,           juce::dontSendNotification);
        resonanceSlider   [v].setValue (vp.filterResonance,        juce::dontSendNotification);
        filterEnvAmtSlider[v].setValue (vp.filterEnvAmount,        juce::dontSendNotification);
        filterDriveSlider [v].setValue (vp.filterDrive,            juce::dontSendNotification);
        filterModeBox     [v].setSelectedItemIndex (vp.filterMode,  juce::dontSendNotification);
        filterSlopeBtn    [v].setToggleState (vp.filterSlope == 1,  juce::dontSendNotification);
        filterSlopeBtn    [v].setButtonText  (vp.filterSlope ? "24" : "12");
        filterSlopeBtn    [v].setColour (juce::TextButton::buttonColourId,
                                         vp.filterSlope ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
        fAttackSlider [v].setValue (vp.filterEnvParams.attack,     juce::dontSendNotification);
        fDecaySlider  [v].setValue (vp.filterEnvParams.decay,      juce::dontSendNotification);
        fSustainSlider[v].setValue (vp.filterEnvParams.sustain,    juce::dontSendNotification);
        fReleaseSlider[v].setValue (vp.filterEnvParams.release,    juce::dontSendNotification);

        attackSlider [v].setValue (vp.adsrParams.attack,  juce::dontSendNotification);
        decaySlider  [v].setValue (vp.adsrParams.decay,   juce::dontSendNotification);
        sustainSlider[v].setValue (vp.adsrParams.sustain, juce::dontSendNotification);
        releaseSlider[v].setValue (vp.adsrParams.release, juce::dontSendNotification);

        lfoRateSlider [v].setValue (vp.lfoRate,  juce::dontSendNotification);
        lfoDepthSlider[v].setValue (vp.lfoDepth, juce::dontSendNotification);
        lfoTargetBox  [v].setSelectedItemIndex (vp.lfoTarget, juce::dontSendNotification);
        lfo2RateSlider [v].setValue (vp.lfo2Rate,  juce::dontSendNotification);
        lfo2DepthSlider[v].setValue (vp.lfo2Depth, juce::dontSendNotification);
        lfo2TargetBox  [v].setSelectedItemIndex (vp.lfo2Target, juce::dontSendNotification);

        modEnvAtkSlider  [v].setValue (vp.modEnv.attack,  juce::dontSendNotification);
        modEnvDecSlider  [v].setValue (vp.modEnv.decay,   juce::dontSendNotification);
        modEnvSusSlider  [v].setValue (vp.modEnv.sustain, juce::dontSendNotification);
        modEnvRelSlider  [v].setValue (vp.modEnv.release, juce::dontSendNotification);
        modEnvDepthSlider[v].setValue (vp.modEnv.depth,   juce::dontSendNotification);
        modEnvDestBox    [v].setSelectedItemIndex (vp.modEnv.dest,     juce::dontSendNotification);
        modEnvSyncBtn    [v].setToggleState (vp.modEnv.clockSync,      juce::dontSendNotification);
        modEnvSyncBtn    [v].setButtonText  (vp.modEnv.clockSync ? "SYNC" : "GATE");
        modEnvSyncBtn    [v].setColour (juce::TextButton::buttonColourId,
                                         vp.modEnv.clockSync ? juce::Colour(0xffe09040) : gateOnColour);
        modEnvDivBox     [v].setSelectedItemIndex (vp.modEnv.clockDiv, juce::dontSendNotification);
    }
    repaint();
}
