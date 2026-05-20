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
    const juce::Colour tieColour          { 0xffe07020 };   // amber-orange for tied gates
    const juce::Colour activeTieColour    { 0xffffc060 };   // bright amber when tied step is active
    const juce::Colour ratchetColour      { 0xff5566dd };   // blue-purple for ratcheted steps
    const juce::Colour activeRatchetColour{ 0xff99aaff };   // bright blue-purple when ratchet step active
    const juce::Colour playBtnOn          { 0xff2255aa };
    const juce::Colour playBtnOff         { 0xff161630 };
    const juce::Colour voiceAColour       { 0xff00aaff };
    const juce::Colour voiceBColour       { 0xffaa44ff };

    // ── Layout ────────────────────────────────────────────────────────────────
    constexpr int headerH  = 28;
    constexpr int gapH     = 22;    // breathing room between header and seq strip

    // Sequencer strips
    constexpr int seqX       =    5;
    constexpr int seqW       = 1490;   // 16 steps × 88 px + margins
    constexpr int seqH       =  120;
    constexpr int stepStride =   88;

    // Step controls (relative to strip Y origin)
    constexpr int stepSliderTop = 14;
    constexpr int stepSliderH   = 76;
    constexpr int gateRelY      = 93;
    constexpr int slideRelY     = 107;

    // Sub-strips (taller — room for label row + control row)
    constexpr int subH = 40;

    // Absolute Y positions
    constexpr int seqAY  = headerH + gapH;      //  50
    constexpr int subAY  = seqAY  + seqH;       // 170
    constexpr int seqBY  = subAY  + subH;       // 210
    constexpr int subBY  = seqBY  + seqH;       // 330
    constexpr int ctrlAY = subBY  + subH;       // 370
    constexpr int ctrlH  = 180;
    constexpr int ctrlBY = ctrlAY + ctrlH;      // 550
    constexpr int winH   = ctrlBY + ctrlH + 8; // 738

    // Label / knob row offsets inside a control panel
    constexpr int lOff1 = 18, lOff2 = 72, lOff3 = 122;
    constexpr int cOff1 = 30, cOff2 = 84, cOff3 = 134;
    // Knob size: 38 × 38 px
    constexpr int kSz = 38;

    // Control panel X positions (total span 5…1490 = 1485 px inside 1500 px window)
    constexpr int pSeqX  =    5, pSeqW  = 155;
    constexpr int pQntX  =  165, pQntW  = 175;
    constexpr int pO1X   =  345, pO1W   = 185;
    constexpr int pO2X   =  535, pO2W   = 175;
    constexpr int pFltX  =  715, pFltW  = 170;
    constexpr int pAEX   =  890, pAEW   = 200;
    constexpr int pLfo1X = 1095, pLfoW  =  95;  // all 4 LFO panels same width
    constexpr int pLfo2X = 1195;
    constexpr int pLfo3X = 1295;
    constexpr int pLfo4X = 1395;
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
// GateBtnListener — right-click shows ratchet count menu
//==============================================================================
void VoltageSeq2AudioProcessorEditor::GateBtnListener::mouseDown (const juce::MouseEvent& ev)
{
    if (!ev.mods.isPopupMenu()) return;

    ed.suppressNextGateClick = true;   // block the onClick cycle that may follow
    auto& vp = ed.audioProcessor.voice[vi];
    juce::PopupMenu menu;
    const char* labels[] = { "1\xc3\x97  (no ratchet)", "2\xc3\x97", "3\xc3\x97", "4\xc3\x97" };
    for (int r = 0; r < 4; ++r)
        menu.addItem (r + 1, labels[r], true, vp.stepRepeats[step] == r);

    menu.showMenuAsync (
        juce::PopupMenu::Options{}.withTargetComponent (&ed.gateBtn[vi][step]),
        [this](int result)
        {
            if (result >= 1 && result <= 4)
            {
                ed.audioProcessor.voice[vi].stepRepeats[step] = result - 1;
                ed.refreshGateBtn (vi, step);
            }
        });
}

//==============================================================================
// refreshGateBtn — sync one gate button's text and colour from processor state
//==============================================================================
void VoltageSeq2AudioProcessorEditor::refreshGateBtn (int v, int i)
{
    const auto& vp = audioProcessor.voice[v];
    const bool  g  = vp.stepGates  [i];
    const bool  t  = vp.stepTied   [i];
    const int   r  = vp.stepRepeats[i];   // 0=1× … 3=4×

    // Display priority: tied > ratchet count > normal
    juce::String txt;
    if      (t)    txt = "~";
    else if (r > 0) txt = juce::String (r + 1);   // "2", "3", "4"
    else           txt = "";

    juce::Colour col;
    if      (!g)   col = gateOffColour;
    else if (t)    col = tieColour;
    else if (r > 0) col = ratchetColour;
    else           col = gateOnColour;

    gateBtn[v][i].setButtonText (txt);
    gateBtn[v][i].setColour (juce::TextButton::buttonColourId, col);
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

    // ── Page navigation buttons ───────────────────────────────────────────────
    synthPageBtn.setButtonText ("SYNTH");
    synthPageBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2255aa));
    synthPageBtn.onClick = [this]() { showPage (0); };
    addAndMakeVisible (synthPageBtn);

    patternPageBtn.setButtonText ("PATTERNS");
    patternPageBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    patternPageBtn.onClick = [this]() { showPage (1); };
    addAndMakeVisible (patternPageBtn);

    fxPageBtn.setButtonText ("FX");
    fxPageBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    fxPageBtn.onClick = [this]() { showPage (2); };
    addAndMakeVisible (fxPageBtn);

    // ── Capture all synth-page components (everything added so far except nav btns)
    for (auto* c : getChildren())
        if (c != &synthPageBtn && c != &patternPageBtn && c != &fxPageBtn)
            synthPageComponents.push_back (c);

    // ── Pattern bank tiles (added invisible by default) ───────────────────────
    for (int vi = 0; vi < 2; ++vi)
    {
        for (int s = 0; s < 16; ++s)
        {
            patternSlot[vi][s] = std::make_unique<PatternSlotView> (audioProcessor, vi, s);
            // Capture vi and s by value to avoid loop-variable bug
            patternSlot[vi][s]->onLoaded = [this, vi, s]()
            {
                activePatternSlot[vi] = s;
                for (int i = 0; i < 16; ++i)
                    patternSlot[vi][i]->setActive (i == s);
                syncUIFromProcessor();
            };
            addChildComponent (*patternSlot[vi][s]);   // invisible until pattern page shown
            patternPageComponents.push_back (patternSlot[vi][s].get());
        }
    }

    // ── Per-voice SAVE controls (pattern page) ────────────────────────────────
    for (int vi = 0; vi < 2; ++vi)
    {
        for (int s = 1; s <= 16; ++s)
            saveToBox[vi].addItem ("Slot " + juce::String (s), s);
        saveToBox[vi].setSelectedItemIndex (0, juce::dontSendNotification);
        saveToBox[vi].setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0e1020));
        saveToBox[vi].setColour (juce::ComboBox::textColourId,       juce::Colour (0xffe0e0e0));
        addChildComponent (saveToBox[vi]);
        patternPageComponents.push_back (&saveToBox[vi]);

        saveBtn[vi].setButtonText ("SAVE");
        saveBtn[vi].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a3a1a));
        saveBtn[vi].setColour (juce::TextButton::textColourOffId, juce::Colour (0xff44cc44));
        saveBtn[vi].onClick = [this, vi]()
        {
            const int slot = saveToBox[vi].getSelectedItemIndex();   // 0-based
            audioProcessor.savePattern (vi, slot);
            // Mark as active and refresh tiles
            activePatternSlot[vi] = slot;
            for (int i = 0; i < 16; ++i)
                patternSlot[vi][i]->setActive (i == slot);
            repaint();
        };
        addChildComponent (saveBtn[vi]);
        patternPageComponents.push_back (&saveBtn[vi]);
    }

    // ── FX page controls ─────────────────────────────────────────────────────
    setupFxControls();

    // setSize LAST — triggers resized() which calls layoutVoice()
    setSize (1500, winH);
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

        // Gate button: left-click cycles OFF→ON→TIED→OFF; right-click sets ratchet count
        {
            gateBtn[v][i].setClickingTogglesState (false);   // manual 3-state
            gateBtn[v][i].onClick = [this, v, i]()
            {
                if (suppressNextGateClick) { suppressNextGateClick = false; return; }
                auto& vp2 = audioProcessor.voice[v];
                if (!vp2.stepGates[i])
                {
                    vp2.stepGates[i] = true;   // OFF → ON
                    vp2.stepTied [i] = false;
                }
                else if (!vp2.stepTied[i])
                {
                    vp2.stepTied[i] = true;    // ON → TIED
                }
                else
                {
                    vp2.stepGates[i] = false;  // TIED → OFF
                    vp2.stepTied [i] = false;
                }
                refreshGateBtn (v, i);
            };
            // Right-click listener for ratchet selection
            gateMouseListener[v][i] = std::make_unique<GateBtnListener> (*this, v, i);
            gateBtn[v][i].addMouseListener (gateMouseListener[v][i].get(), false);
            addAndMakeVisible (gateBtn[v][i]);
            refreshGateBtn (v, i);   // set initial text + colour
        }

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

    // Nudge left / right  (◄ ► — shift sequence start by one step)
    {
        const juce::Colour nudgeCol { 0xff252545 };
        nudgeLeftBtn[v].setButtonText  ("<");
        nudgeRightBtn[v].setButtonText (">");
        nudgeLeftBtn[v].setColour  (juce::TextButton::buttonColourId, nudgeCol);
        nudgeRightBtn[v].setColour (juce::TextButton::buttonColourId, nudgeCol);

        nudgeLeftBtn[v].onClick = [this, v]()
        {
            auto& vp2 = audioProcessor.voice[v];
            const int seqLen = juce::jmax (1, vp2.sequenceLength);
            vp2.nudgeOffset = (vp2.nudgeOffset - 1 + seqLen) % seqLen;
        };
        nudgeRightBtn[v].onClick = [this, v]()
        {
            auto& vp2 = audioProcessor.voice[v];
            const int seqLen = juce::jmax (1, vp2.sequenceLength);
            vp2.nudgeOffset = (vp2.nudgeOffset + 1) % seqLen;
        };
        addAndMakeVisible (nudgeLeftBtn[v]);
        addAndMakeVisible (nudgeRightBtn[v]);
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

    // RANGE — full-width linear slider (most important sequencer parameter)
    rangeSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    rangeSlider[v].setRange (0.0, 1.0, 0.01);
    rangeSlider[v].setValue (vp.rangeVCA, juce::dontSendNotification);
    rangeSlider[v].setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 18);
    rangeSlider[v].setColour (juce::Slider::trackColourId,            knobColour);
    rangeSlider[v].setColour (juce::Slider::backgroundColourId,       juce::Colour (0xff252540));
    rangeSlider[v].setColour (juce::Slider::textBoxTextColourId,      textColour);
    rangeSlider[v].setColour (juce::Slider::textBoxBackgroundColourId,bgColour);
    rangeSlider[v].setColour (juce::Slider::textBoxOutlineColourId,   bgColour);
    rangeSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].rangeVCA = (float)rangeSlider[v].getValue(); };
    addAndMakeVisible (rangeSlider[v]);

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
    fmRatioSlider[v].setRange (0.0, 6.0, 0.25);
    fmRatioSlider[v].setValue (juce::jlimit (0.0f, 6.0f, vp.fmRatio), juce::dontSendNotification);
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

    // Helper: build a standard LFO target combo
    auto addLFOTargetItems = [](juce::ComboBox& box)
    {
        box.addItem ("PWM",    1); box.addItem ("Cutoff", 2);
        box.addItem ("Pitch",  3); box.addItem ("Range",  4);
        box.addItem ("FM Dpt", 5);
    };

    // Helper: build a waveform combo
    auto addWaveItems = [](juce::ComboBox& box)
    {
        box.addItem ("Sine", 1); box.addItem ("Tri",  2);
        box.addItem ("Saw",  3); box.addItem ("Sqr",  4);
    };

    // Helper: set up sync button + div box for one LFO
    auto setupLFOSync = [&](juce::TextButton& btn, juce::ComboBox& divBox,
                             bool syncState, int divIdx,
                             std::function<void(bool)> onToggle)
    {
        btn.setButtonText (syncState ? "SYNC" : "FREE");
        btn.setClickingTogglesState (true);
        btn.setToggleState (syncState, juce::dontSendNotification);
        btn.setColour (juce::TextButton::buttonColourId,   syncState ? knobColour : gateOffColour);
        btn.setColour (juce::TextButton::buttonOnColourId, knobColour);
        btn.onClick = [&btn, onToggle]()
        {
            bool s = btn.getToggleState();
            onToggle (s);
            btn.setButtonText (s ? "SYNC" : "FREE");
            btn.setColour (juce::TextButton::buttonColourId, s ? knobColour : gateOffColour);
        };
        addAndMakeVisible (btn);
        addCenvDivItems (divBox);
        divBox.setSelectedItemIndex (divIdx, juce::dontSendNotification);
        addAndMakeVisible (divBox);
    };

    // LFO 1
    setupKnob (lfoRateSlider[v], 0.1, 20.0, vp.lfoRate, 4.0);
    lfoRateSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfoRate  = (float)lfoRateSlider[v].getValue(); };
    setupKnob (lfoDepthSlider[v], 0.0, 1.0, vp.lfoDepth);
    lfoDepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfoDepth = (float)lfoDepthSlider[v].getValue(); };
    addWaveItems (lfoWaveBox[v]);
    lfoWaveBox[v].setSelectedItemIndex (vp.lfoWaveform, juce::dontSendNotification);
    lfoWaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfoWaveform = lfoWaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfoWaveBox[v]);
    addLFOTargetItems (lfoTargetBox[v]);
    lfoTargetBox[v].setSelectedItemIndex (vp.lfoTarget, juce::dontSendNotification);
    lfoTargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfoTarget = lfoTargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfoTargetBox[v]);
    setupLFOSync (lfoSyncBtn[v], lfoSyncDivBox[v], vp.lfoSync, vp.lfoSyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfoSync = s; });
    lfoSyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfoSyncDiv = lfoSyncDivBox[v].getSelectedItemIndex(); };

    // LFO 2
    setupKnob (lfo2RateSlider[v], 0.1, 20.0, vp.lfo2Rate, 4.0);
    lfo2RateSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo2Rate  = (float)lfo2RateSlider[v].getValue(); };
    setupKnob (lfo2DepthSlider[v], 0.0, 1.0, vp.lfo2Depth);
    lfo2DepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo2Depth = (float)lfo2DepthSlider[v].getValue(); };
    addWaveItems (lfo2WaveBox[v]);
    lfo2WaveBox[v].setSelectedItemIndex (vp.lfo2Waveform, juce::dontSendNotification);
    lfo2WaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo2Waveform = lfo2WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo2WaveBox[v]);
    addLFOTargetItems (lfo2TargetBox[v]);
    lfo2TargetBox[v].setSelectedItemIndex (vp.lfo2Target, juce::dontSendNotification);
    lfo2TargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo2Target = lfo2TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo2TargetBox[v]);
    setupLFOSync (lfo2SyncBtn[v], lfo2SyncDivBox[v], vp.lfo2Sync, vp.lfo2SyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfo2Sync = s; });
    lfo2SyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo2SyncDiv = lfo2SyncDivBox[v].getSelectedItemIndex(); };

    // LFO 3
    setupKnob (lfo3RateSlider[v], 0.1, 20.0, vp.lfo3Rate, 4.0);
    lfo3RateSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo3Rate  = (float)lfo3RateSlider[v].getValue(); };
    setupKnob (lfo3DepthSlider[v], 0.0, 1.0, vp.lfo3Depth);
    lfo3DepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo3Depth = (float)lfo3DepthSlider[v].getValue(); };
    addWaveItems (lfo3WaveBox[v]);
    lfo3WaveBox[v].setSelectedItemIndex (vp.lfo3Waveform, juce::dontSendNotification);
    lfo3WaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo3Waveform = lfo3WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo3WaveBox[v]);
    addLFOTargetItems (lfo3TargetBox[v]);
    lfo3TargetBox[v].setSelectedItemIndex (vp.lfo3Target, juce::dontSendNotification);
    lfo3TargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo3Target = lfo3TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo3TargetBox[v]);
    setupLFOSync (lfo3SyncBtn[v], lfo3SyncDivBox[v], vp.lfo3Sync, vp.lfo3SyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfo3Sync = s; });
    lfo3SyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo3SyncDiv = lfo3SyncDivBox[v].getSelectedItemIndex(); };

    // LFO 4
    setupKnob (lfo4RateSlider[v], 0.1, 20.0, vp.lfo4Rate, 4.0);
    lfo4RateSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo4Rate  = (float)lfo4RateSlider[v].getValue(); };
    setupKnob (lfo4DepthSlider[v], 0.0, 1.0, vp.lfo4Depth);
    lfo4DepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo4Depth = (float)lfo4DepthSlider[v].getValue(); };
    addWaveItems (lfo4WaveBox[v]);
    lfo4WaveBox[v].setSelectedItemIndex (vp.lfo4Waveform, juce::dontSendNotification);
    lfo4WaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo4Waveform = lfo4WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo4WaveBox[v]);
    addLFOTargetItems (lfo4TargetBox[v]);
    lfo4TargetBox[v].setSelectedItemIndex (vp.lfo4Target, juce::dontSendNotification);
    lfo4TargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo4Target = lfo4TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo4TargetBox[v]);
    setupLFOSync (lfo4SyncBtn[v], lfo4SyncDivBox[v], vp.lfo4Sync, vp.lfo4SyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfo4Sync = s; });
    lfo4SyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo4SyncDiv = lfo4SyncDivBox[v].getSelectedItemIndex(); };

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
// setupFxControls — wire up all FX page controls
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupFxControls()
{
    auto& p = audioProcessor.fx;

    auto addFx = [&](juce::Component& c) {
        addChildComponent (c);
        fxPageComponents.push_back (&c);
    };

    // ── Delay ─────────────────────────────────────────────────────────────
    delayOnBtn.setButtonText ("OFF");
    delayOnBtn.setClickingTogglesState (true);
    delayOnBtn.setToggleState (p.delayOn, juce::dontSendNotification);
    delayOnBtn.setColour (juce::TextButton::buttonColourId,   p.delayOn ? gateOnColour : gateOffColour);
    delayOnBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    delayOnBtn.onClick = [this]() {
        bool s = delayOnBtn.getToggleState();
        audioProcessor.fx.delayOn = s;
        delayOnBtn.setButtonText (s ? "ON" : "OFF");
        delayOnBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addFx (delayOnBtn);

    delaySyncBtn.setButtonText (p.delaySync ? "SYNC" : "FREE");
    delaySyncBtn.setClickingTogglesState (true);
    delaySyncBtn.setToggleState (p.delaySync, juce::dontSendNotification);
    delaySyncBtn.setColour (juce::TextButton::buttonColourId,   p.delaySync ? juce::Colour(0xffe09040) : gateOffColour);
    delaySyncBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xffe09040));
    delaySyncBtn.onClick = [this]() {
        bool s = delaySyncBtn.getToggleState();
        audioProcessor.fx.delaySync = s;
        delaySyncBtn.setButtonText (s ? "SYNC" : "FREE");
        delaySyncBtn.setColour (juce::TextButton::buttonColourId, s ? juce::Colour(0xffe09040) : gateOffColour);
    };
    addFx (delaySyncBtn);

    delaySyncDivBox.addItem ("1/4",   1);
    delaySyncDivBox.addItem ("1/8",   2);
    delaySyncDivBox.addItem ("1/16",  3);
    delaySyncDivBox.addItem ("1/8T",  4);
    delaySyncDivBox.addItem ("1/16T", 5);
    delaySyncDivBox.addItem ("1/8.",  6);
    delaySyncDivBox.addItem ("1/16.", 7);
    delaySyncDivBox.setSelectedItemIndex (p.delaySyncDiv, juce::dontSendNotification);
    delaySyncDivBox.onChange = [this]() { audioProcessor.fx.delaySyncDiv = delaySyncDivBox.getSelectedItemIndex(); };
    delaySyncDivBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour(0xff0e1020));
    delaySyncDivBox.setColour (juce::ComboBox::textColourId,       juce::Colour(0xffe0e0e0));
    addFx (delaySyncDivBox);

    setupKnob (delayTimeMsSlider, 1.0, 2000.0, p.delayTimeMs);
    delayTimeMsSlider.onValueChange = [this]() { audioProcessor.fx.delayTimeMs = (float)delayTimeMsSlider.getValue(); };
    addFx (delayTimeMsSlider);

    setupKnob (delayFeedbackSlider, 0.0, 0.95, p.delayFeedback);
    delayFeedbackSlider.onValueChange = [this]() { audioProcessor.fx.delayFeedback = (float)delayFeedbackSlider.getValue(); };
    addFx (delayFeedbackSlider);

    delayPingPongBtn.setButtonText ("PING");
    delayPingPongBtn.setClickingTogglesState (true);
    delayPingPongBtn.setToggleState (p.delayPingPong, juce::dontSendNotification);
    delayPingPongBtn.setColour (juce::TextButton::buttonColourId,   p.delayPingPong ? juce::Colour(0xff5566dd) : gateOffColour);
    delayPingPongBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff5566dd));
    delayPingPongBtn.onClick = [this]() {
        bool s = delayPingPongBtn.getToggleState();
        audioProcessor.fx.delayPingPong = s;
        delayPingPongBtn.setColour (juce::TextButton::buttonColourId, s ? juce::Colour(0xff5566dd) : gateOffColour);
    };
    addFx (delayPingPongBtn);

    setupKnob (delayMixSlider, 0.0, 1.0, p.delayMix);
    delayMixSlider.onValueChange = [this]() { audioProcessor.fx.delayMix = (float)delayMixSlider.getValue(); };
    addFx (delayMixSlider);

    // ── Reverb ─────────────────────────────────────────────────────────────
    reverbOnBtn.setButtonText ("OFF");
    reverbOnBtn.setClickingTogglesState (true);
    reverbOnBtn.setToggleState (p.reverbOn, juce::dontSendNotification);
    reverbOnBtn.setColour (juce::TextButton::buttonColourId,   p.reverbOn ? gateOnColour : gateOffColour);
    reverbOnBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    reverbOnBtn.onClick = [this]() {
        bool s = reverbOnBtn.getToggleState();
        audioProcessor.fx.reverbOn = s;
        reverbOnBtn.setButtonText (s ? "ON" : "OFF");
        reverbOnBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addFx (reverbOnBtn);

    setupKnob (reverbSizeSlider,     0.0, 1.0, p.reverbSize);
    reverbSizeSlider.onValueChange = [this]() { audioProcessor.fx.reverbSize = (float)reverbSizeSlider.getValue(); };
    addFx (reverbSizeSlider);

    setupKnob (reverbDampingSlider,  0.0, 1.0, p.reverbDamping);
    reverbDampingSlider.onValueChange = [this]() { audioProcessor.fx.reverbDamping = (float)reverbDampingSlider.getValue(); };
    addFx (reverbDampingSlider);

    setupKnob (reverbPreDelaySlider, 0.0, 100.0, p.reverbPreDelay);
    reverbPreDelaySlider.onValueChange = [this]() { audioProcessor.fx.reverbPreDelay = (float)reverbPreDelaySlider.getValue(); };
    addFx (reverbPreDelaySlider);

    setupKnob (reverbMixSlider, 0.0, 1.0, p.reverbMix);
    reverbMixSlider.onValueChange = [this]() { audioProcessor.fx.reverbMix = (float)reverbMixSlider.getValue(); };
    addFx (reverbMixSlider);

    // ── Chorus ─────────────────────────────────────────────────────────────
    chorusOnBtn.setButtonText ("OFF");
    chorusOnBtn.setClickingTogglesState (true);
    chorusOnBtn.setToggleState (p.chorusOn, juce::dontSendNotification);
    chorusOnBtn.setColour (juce::TextButton::buttonColourId,   p.chorusOn ? gateOnColour : gateOffColour);
    chorusOnBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    chorusOnBtn.onClick = [this]() {
        bool s = chorusOnBtn.getToggleState();
        audioProcessor.fx.chorusOn = s;
        chorusOnBtn.setButtonText (s ? "ON" : "OFF");
        chorusOnBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addFx (chorusOnBtn);

    setupKnob (chorusRateSlider,  0.1, 5.0, p.chorusRate);
    chorusRateSlider.onValueChange = [this]() { audioProcessor.fx.chorusRate = (float)chorusRateSlider.getValue(); };
    addFx (chorusRateSlider);

    setupKnob (chorusDepthSlider, 0.0, 1.0, p.chorusDepth);
    chorusDepthSlider.onValueChange = [this]() { audioProcessor.fx.chorusDepth = (float)chorusDepthSlider.getValue(); };
    addFx (chorusDepthSlider);

    setupKnob (chorusMixSlider, 0.0, 1.0, p.chorusMix);
    chorusMixSlider.onValueChange = [this]() { audioProcessor.fx.chorusMix = (float)chorusMixSlider.getValue(); };
    addFx (chorusMixSlider);

    // ── Master ─────────────────────────────────────────────────────────────
    setupKnob (masterDriveSlider, 0.0, 1.0, p.masterDrive);
    masterDriveSlider.onValueChange = [this]() { audioProcessor.fx.masterDrive = (float)masterDriveSlider.getValue(); };
    addFx (masterDriveSlider);

    setupKnob (masterGainSlider, 0.0, 2.0, p.masterGain);
    masterGainSlider.onValueChange = [this]() { audioProcessor.fx.masterGain = (float)masterGainSlider.getValue(); };
    addFx (masterGainSlider);
}

//==============================================================================
// layoutFxPage — size and position all FX page controls
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutFxPage()
{
    // Four panels across the page: DELAY | REVERB | CHORUS | MASTER
    constexpr int py = 60, dR2 = 60 + 150;

    // DELAY: x=10..440
    constexpr int dX=10;
    delayOnBtn        .setBounds (dX,        py+10,  80, 26);
    delaySyncBtn      .setBounds (dX+90,     py+10,  80, 26);
    delaySyncDivBox   .setBounds (dX+180,    py+10, 100, 26);
    delayPingPongBtn  .setBounds (dX+290,    py+10,  80, 26);
    delayTimeMsSlider .setBounds (dX+10,     dR2,    52, 52);
    delayFeedbackSlider.setBounds(dX+80,     dR2,    52, 52);
    delayMixSlider    .setBounds (dX+150,    dR2,    52, 52);

    // REVERB: x=460..890
    constexpr int rX=460;
    reverbOnBtn         .setBounds (rX,      py+10,  80, 26);
    reverbSizeSlider    .setBounds (rX+10,   dR2,    52, 52);
    reverbDampingSlider .setBounds (rX+80,   dR2,    52, 52);
    reverbPreDelaySlider.setBounds (rX+150,  dR2,    52, 52);
    reverbMixSlider     .setBounds (rX+220,  dR2,    52, 52);

    // CHORUS: x=960..1260
    constexpr int cX=960;
    chorusOnBtn       .setBounds (cX,      py+10,  80, 26);
    chorusRateSlider  .setBounds (cX+10,   dR2,    52, 52);
    chorusDepthSlider .setBounds (cX+80,   dR2,    52, 52);
    chorusMixSlider   .setBounds (cX+150,  dR2,    52, 52);

    // MASTER: x=1290..1490
    constexpr int mX=1290;
    masterDriveSlider .setBounds (mX+10,   dR2,    52, 52);
    masterGainSlider  .setBounds (mX+80,   dR2,    52, 52);
}

//==============================================================================
// showPage — switch between pages 0=synth 1=pattern 2=fx
//==============================================================================
void VoltageSeq2AudioProcessorEditor::showPage (int page)
{
    currentPage = page;
    for (auto* c : synthPageComponents)   c->setVisible (page == 0);
    for (auto* c : patternPageComponents) c->setVisible (page == 1);
    for (auto* c : fxPageComponents)      c->setVisible (page == 2);
    synthPageBtn  .setColour (juce::TextButton::buttonColourId,
                              page == 0 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
    patternPageBtn.setColour (juce::TextButton::buttonColourId,
                              page == 1 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
    fxPageBtn     .setColour (juce::TextButton::buttonColourId,
                              page == 2 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
    repaint();
}

//==============================================================================
// layoutPatternPage — size and position all 32 pattern slot tiles
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutPatternPage()
{
    constexpr int margin   = 8;
    constexpr int slotGap  = 4;
    const int     totalW   = getWidth() - margin * 2;
    const int     slotW    = (totalW - slotGap * 7) / 8; // 8 columns
    const int     slotH    = 125;
    const int     rowGap   = 6;

    // Voice A: two rows starting at y=72
    const int rowA0 = 72;
    const int rowA1 = rowA0 + slotH + rowGap;

    // Voice B: two rows starting below a divider
    const int rowB0 = rowA1 + slotH + 26;   // 26px for divider + label
    const int rowB1 = rowB0 + slotH + rowGap;

    // Label row Y positions (match paint() comments)
    // Voice A label at y=55, Voice B label at y=337
    const int labelYA = 54;
    const int labelYB = 336;

    for (int vi = 0; vi < 2; ++vi)
    {
        const int row0   = (vi == 0) ? rowA0  : rowB0;
        const int row1   = (vi == 0) ? rowA1  : rowB1;
        const int labelY = (vi == 0) ? labelYA : labelYB;

        // SAVE controls — right-aligned in the label row
        saveToBox[vi].setBounds (getWidth() - 202, labelY, 130, 18);
        saveBtn  [vi].setBounds (getWidth() -  68, labelY,  60, 18);

        for (int s = 0; s < 16; ++s)
        {
            const int col = s % 8;
            const int row = s / 8;
            const int x   = margin + col * (slotW + slotGap);
            const int y   = (row == 0) ? row0 : row1;
            patternSlot[vi][s]->setBounds (x, y, slotW, slotH);
        }
    }
}

//==============================================================================
// PAINT
//==============================================================================
void VoltageSeq2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    // ── Global header ─────────────────────────────────────────────────────────
    g.setColour (juce::Colour (0xff0a0a1a));
    g.fillRect (0, 0, getWidth(), headerH);

    // Branding
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.setColour (juce::Colour (0xffe09040));
    g.drawText ("VoltageSEQ", 8, 3, 130, 22, juce::Justification::centredLeft);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.setColour (dimColour.withAlpha (0.55f));
    g.drawText ("MURGATROYD INSTRUMENTS", 0, 0, getWidth(), headerH, juce::Justification::centred);

    // Preset label
    g.setFont (juce::Font (9.0f, juce::Font::bold));
    g.setColour (dimColour.withAlpha (0.6f));
    g.drawText ("PRESET", 1240, 4, 42, 20, juce::Justification::centredLeft);

    // ── Pattern page overlay ──────────────────────────────────────────────────
    if (currentPage == 1)
    {
        g.setColour (juce::Colour (0xff040410));
        g.fillRect (0, headerH, getWidth(), winH - headerH);

        // rowA0=72, slotH=125, rowGap=6 → rowA1=203, end-of-A=328, rowB0=354
        // Voice A label row: y=55
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.setColour (voiceAColour);
        g.drawText ("VOICE A — PATTERN BANK", 8, 55, 600, 14, juce::Justification::centredLeft);

        // Divider and Voice B label at y=332/336
        g.setColour (voiceAColour.withAlpha (0.2f));
        g.fillRect (0, 332, getWidth(), 1);
        g.setColour (voiceBColour.withAlpha (0.2f));
        g.fillRect (0, 334, getWidth(), 1);
        g.setColour (voiceBColour);
        g.drawText ("VOICE B — PATTERN BANK", 8, 337, 600, 14, juce::Justification::centredLeft);

        // Bottom hint
        g.setFont (juce::Font (8.5f));
        g.setColour (juce::Colour (0xff333355));
        g.drawText ("Left-click to load  ·  Right-click to clear  ·  Use SAVE button to store current pattern",
                    0, winH - 14, getWidth(), 12, juce::Justification::centred);
        return;
    }

    // ── FX page overlay ───────────────────────────────────────────────────────
    if (currentPage == 2)
    {
        g.setColour (juce::Colour (0xff040410));
        g.fillRect (0, headerH, getWidth(), winH - headerH);

        // Panel backgrounds
        auto drawFxPanel = [&](int px, int pw, const juce::String& title, juce::Colour accent)
        {
            g.setColour (juce::Colour (0xff0c0c18).withAlpha (0.92f));
            g.fillRoundedRectangle ((float)px, 50.f, (float)pw, 630.f, 6.f);
            g.setColour (accent.withAlpha (0.6f));
            g.fillRect (px, 50, pw, 3);
            g.setFont (juce::Font (11.f, juce::Font::bold));
            g.setColour (accent);
            g.drawText (title, px, 56, pw, 16, juce::Justification::centred);
        };
        drawFxPanel (10,  430, "DELAY",  juce::Colour (0xff00d4aa));
        drawFxPanel (460, 430, "REVERB", juce::Colour (0xffaa44ff));
        drawFxPanel (960, 300, "CHORUS", juce::Colour (0xffe09040));
        drawFxPanel (1290,200, "MASTER", juce::Colour (0xffe94560));

        // Knob labels
        g.setFont (juce::Font (9.f));
        g.setColour (textColour);
        // Delay labels
        g.drawText ("TIME",     10+10,  220, 52, 12, juce::Justification::centred);
        g.drawText ("FEEDBK",   10+80,  220, 52, 12, juce::Justification::centred);
        g.drawText ("MIX",      10+150, 220, 52, 12, juce::Justification::centred);
        // Reverb labels
        g.drawText ("SIZE",    460+10,  220, 52, 12, juce::Justification::centred);
        g.drawText ("DAMP",    460+80,  220, 52, 12, juce::Justification::centred);
        g.drawText ("PRE-DLY", 460+150, 220, 52, 12, juce::Justification::centred);
        g.drawText ("MIX",     460+220, 220, 52, 12, juce::Justification::centred);
        // Chorus labels
        g.drawText ("RATE",    960+10,  220, 52, 12, juce::Justification::centred);
        g.drawText ("DEPTH",   960+80,  220, 52, 12, juce::Justification::centred);
        g.drawText ("MIX",     960+150, 220, 52, 12, juce::Justification::centred);
        // Master labels
        g.drawText ("DRIVE",  1290+10,  220, 52, 12, juce::Justification::centred);
        g.drawText ("GAIN",   1290+80,  220, 52, 12, juce::Justification::centred);
        return;
    }

    // Backplate drawn once for the full content area
    if (backplate != nullptr)
        backplate->drawWithin (g,
            juce::Rectangle<float> (0.0f, (float)headerH, (float)getWidth(), (float)(winH - headerH)),
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
        g.drawText ("GATE",  seqX + 16 * stepStride + 3, sY + gateRelY,  50, 13, juce::Justification::centredLeft);
        g.setColour (slideOnColour);
        g.drawText ("SLIDE", seqX + 16 * stepStride + 3, sY + slideRelY, 50, 13, juce::Justification::centredLeft);

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
        g.drawText ("NUDGE", 384, sbY + 3, 46, 10, juce::Justification::centred);
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
        drawPanel (pLfo1X, pLfoW, "LFO 1");
        drawPanel (pLfo2X, pLfoW, "LFO 2");
        drawPanel (pLfo3X, pLfoW, "LFO 3");
        drawPanel (pLfo4X, pLfoW, "LFO 4");

        // LFO panels — each has WAVE / RATE+DEPTH / TARGET / SYNC labels
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.setColour (dimColour);
        for (int lfoIdx = 0; lfoIdx < 4; ++lfoIdx)
        {
            const int lx = (lfoIdx == 0) ? pLfo1X : (lfoIdx == 1) ? pLfo2X : (lfoIdx == 2) ? pLfo3X : pLfo4X;
            g.setFont (juce::Font (8.0f));
            g.setColour (textColour);
            g.drawText ("WAVE",   lx, cY+14,  pLfoW, 11, juce::Justification::centred);
            g.drawText ("RATE",   lx,    cY+50,  kSz+4, 11, juce::Justification::centred);
            g.drawText ("DEPTH",  lx+42, cY+50,  kSz+4, 11, juce::Justification::centred);
            g.drawText ("TARGET", lx, cY+102, pLfoW, 11, juce::Justification::centred);
            g.drawText ("FREE",   lx,    cY+140, 38,    11, juce::Justification::centred);
            g.drawText ("DIV",    lx+42, cY+140, 40,    11, juce::Justification::centred);
        }

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
        g.drawText ("A",  pFltX+2,   lY3, kSz, 12, juce::Justification::centred);
        g.drawText ("D",  pFltX+42,  lY3, kSz, 12, juce::Justification::centred);
        g.drawText ("S",  pFltX+82,  lY3, kSz, 12, juce::Justification::centred);
        g.drawText ("R",  pFltX+122, lY3, kSz, 12, juce::Justification::centred);
        // AMP ENV
        g.setColour (dimColour); g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.drawText ("AMP",   pAEX, cY+14, pAEW, 10, juce::Justification::centred);
        g.setColour (textColour); g.setFont (juce::Font (9.0f));
        g.drawText ("A", pAEX+2,    lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("D", pAEX+50,  lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("S", pAEX+98,  lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("R", pAEX+146, lY1, kSz, 12, juce::Justification::centred);
        // MOD ENV (in AMP panel rows 2+3)
        g.setColour (dimColour); g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.drawText ("MOD ENV", pAEX, cY+lOff2-4, pAEW, 10, juce::Justification::centred);
        g.setColour (textColour); g.setFont (juce::Font (9.0f));
        g.drawText ("A", pAEX+2,    lY2, kSz, 12, juce::Justification::centred);
        g.drawText ("D", pAEX+50,   lY2, kSz, 12, juce::Justification::centred);
        g.drawText ("S", pAEX+98,   lY2, kSz, 12, juce::Justification::centred);
        g.drawText ("R", pAEX+146,  lY2, kSz, 12, juce::Justification::centred);
        g.drawText ("DEPTH", pAEX+2,   lY3, kSz,  12, juce::Justification::centred);
        g.drawText ("DEST",  pAEX+50,  lY3, 100,  12, juce::Justification::centred);
        g.drawText ("TRIG",  pAEX+156, lY3, 42,   12, juce::Justification::centred);

        // Accent stripe on control panel left edge
        g.setColour (accent.withAlpha (0.4f));
        g.fillRect (pSeqX, cY, 2, ctrlH);
    }

    // Divider between Voice A controls and Voice B controls
    g.setColour (voiceAColour.withAlpha (0.2f));
    g.fillRect (0, ctrlBY - 1, getWidth(), 2);
    g.setColour (voiceBColour.withAlpha (0.2f));
    g.fillRect (0, ctrlBY + 1, getWidth(), 1);
}

//==============================================================================
// RESIZED
//==============================================================================
void VoltageSeq2AudioProcessorEditor::resized()
{
    // Global header (always present)
    synthPageBtn .setBounds (220,  3,  65, 22);
    patternPageBtn.setBounds(290,  3,  80, 22);
    fxPageBtn    .setBounds (375,  3,  55, 22);
    autoBtn      .setBounds (530,  3,  55, 22);
    savePresetBtn.setBounds (1305, 3,  85, 22);
    loadPresetBtn.setBounds (1396, 3,  85, 22);

    layoutVoice (0, seqAY, ctrlAY);
    layoutVoice (1, seqBY, ctrlBY);
    layoutPatternPage();
    layoutFxPage();
}

//==============================================================================
// layoutVoice — position all per-voice controls
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutVoice (int v, int seqTopY, int ctrlTopY)
{
    const int sbY  = seqTopY + seqH;           // sub-strip Y
    const int sbCY = sbY + 18;                // control row — leaves room for label at top
    const int cy1  = ctrlTopY + cOff1;
    const int cy2  = ctrlTopY + cOff2;
    const int cy3  = ctrlTopY + cOff3;

    // ── Step controls ─────────────────────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        const int bx = seqX + i * stepStride;
        stepKnob[v][i].setBounds (bx + 4,  seqTopY + stepSliderTop, stepStride - 8,  stepSliderH);
        gateBtn [v][i].setBounds (bx + 8,  seqTopY + gateRelY,      stepStride - 16, 13);
        slideBtn[v][i].setBounds (bx + 8,  seqTopY + slideRelY,     stepStride - 16, 12);
    }

    // ── Sub-strip ─────────────────────────────────────────────────────────────
    seqLengthSlider[v].setBounds (12,  sbCY, 82, 18);
    playFwdBtn [v]    .setBounds (112, sbCY, 36, 18);
    playRevBtn [v]    .setBounds (151, sbCY, 36, 18);
    playConvBtn[v]    .setBounds (190, sbCY, 42, 18);
    playRndBtn [v]    .setBounds (235, sbCY, 36, 18);
    resetBtn   [v]    .setBounds (280, sbCY, 46, 18);
    bipolarBtn   [v]  .setBounds (330, sbCY,  46, 18);
    nudgeLeftBtn [v]  .setBounds (384, sbCY,  22, 18);
    nudgeRightBtn[v]  .setBounds (408, sbCY,  22, 18);
    swingSlider  [v]  .setBounds (490, sbCY, 110, 18);
    runStopBtn [v]    .setBounds (608, sbCY, 50, 18);

    if (v == 0)
        autoBtn.setBounds (665, sbCY, 50, 18);   // shared — only show in Voice A sub-strip

    // ── SEQ panel ─────────────────────────────────────────────────────────────
    rangeSlider[v]  .setBounds (pSeqX + 5,                     cy1 - 2, pSeqW - 10, 22);
    clockDivBox[v]  .setBounds (pSeqX + 8,                     cy2,     pSeqW - 16, 20);
    portaSlider[v]  .setBounds (pSeqX + (pSeqW - kSz) / 2,    cy3,     kSz, kSz);

    // ── Quantizer ─────────────────────────────────────────────────────────────
    rootBox [v].setBounds (pQntX + 8, cy1, pQntW - 16, 22);
    scaleBox[v].setBounds (pQntX + 8, cy2, pQntW - 16, 22);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    osc1WaveBox        [v].setBounds (pO1X + 8,   ctrlTopY + 26, pO1W - 16, 20);
    osc1LevelSlider    [v].setBounds (pO1X + 6,   ctrlTopY + 66, kSz, kSz);
    osc1OctaveBox      [v].setBounds (pO1X + 48,  ctrlTopY + 72, 90,  22);
    osc1FeedbackSlider [v].setBounds (pO1X + 142, ctrlTopY + 66, kSz, kSz);
    osc1PWMSlider      [v].setBounds (pO1X + 4,   ctrlTopY + 122, kSz, kSz);
    driftSlider        [v].setBounds (pO1X + 46,  ctrlTopY + 122, kSz, kSz);
    oscScope           [v]->setBounds(pO1X + 88,  ctrlTopY + 122, pO1W - 93, 52);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    osc2PosSlider   [v].setBounds (pO2X + 5,   ctrlTopY + 26,  kSz, kSz);
    osc2LevelSlider [v].setBounds (pO2X + 60,  ctrlTopY + 26,  kSz, kSz);
    fmDepthSlider   [v].setBounds (pO2X + 115, ctrlTopY + 26,  kSz, kSz);
    fmRatioSlider   [v].setBounds (pO2X + 5,   ctrlTopY + 80,  pO2W - 10, 22);
    osc2OctaveBox   [v].setBounds (pO2X + 5,   ctrlTopY + 118, 70,  22);
    crossModSlider  [v].setBounds (pO2X + 95,  ctrlTopY + 114, kSz, kSz);
    wavetableDisplay[v]->setBounds(pO2X + 5,   ctrlTopY + 152, pO2W - 10, 16);

    // ── Filter ────────────────────────────────────────────────────────────────
    cutoffSlider      [v].setBounds (pFltX + 5,  cy1, kSz, kSz);
    resonanceSlider   [v].setBounds (pFltX + 49, cy1, kSz, kSz);
    filterDriveSlider [v].setBounds (pFltX + 93, cy1, kSz, kSz);
    filterModeBox     [v].setBounds (pFltX + 5,  cy2, 70,  20);
    filterSlopeBtn    [v].setBounds (pFltX + 80, cy2, 32,  20);
    filterEnvAmtSlider[v].setBounds (pFltX + 118,cy2, kSz, kSz);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    const int aeStride = 48;
    attackSlider [v].setBounds (pAEX + 2,            cy1, kSz, kSz);
    decaySlider  [v].setBounds (pAEX + 2 + aeStride, cy1, kSz, kSz);
    sustainSlider[v].setBounds (pAEX + 2 + aeStride*2, cy1, kSz, kSz);
    releaseSlider[v].setBounds (pAEX + 2 + aeStride*3, cy1, kSz, kSz);

    // ── Filter Envelope (in Filter panel cy3) ─────────────────────────────────
    const int feStride = 40;
    fAttackSlider [v].setBounds (pFltX + 2,               cy3, kSz, kSz);
    fDecaySlider  [v].setBounds (pFltX + 2 + feStride,    cy3, kSz, kSz);
    fSustainSlider[v].setBounds (pFltX + 2 + feStride*2,  cy3, kSz, kSz);
    fReleaseSlider[v].setBounds (pFltX + 2 + feStride*3,  cy3, kSz, kSz);

    // ── 4 LFO panels ─────────────────────────────────────────────────────────
    // Each panel is pLfoW=85px wide. Layout within each:
    //   ctrlTopY+26 : wave combo
    //   ctrlTopY+62 : rate knob | depth knob
    //   ctrlTopY+116: target combo
    //   ctrlTopY+150: sync btn | div combo
    const int lfoXArr[4] = { pLfo1X, pLfo2X, pLfo3X, pLfo4X };
    juce::Slider*    rateSliders[4]  = { &lfoRateSlider[v],  &lfo2RateSlider[v],  &lfo3RateSlider[v],  &lfo4RateSlider[v]  };
    juce::Slider*    depSliders[4]   = { &lfoDepthSlider[v], &lfo2DepthSlider[v], &lfo3DepthSlider[v], &lfo4DepthSlider[v] };
    juce::ComboBox*  waveBoxes[4]    = { &lfoWaveBox[v],     &lfo2WaveBox[v],     &lfo3WaveBox[v],     &lfo4WaveBox[v]     };
    juce::ComboBox*  tgtBoxes[4]     = { &lfoTargetBox[v],   &lfo2TargetBox[v],   &lfo3TargetBox[v],   &lfo4TargetBox[v]   };
    juce::TextButton* syncBtns[4]    = { &lfoSyncBtn[v],     &lfo2SyncBtn[v],     &lfo3SyncBtn[v],     &lfo4SyncBtn[v]     };
    juce::ComboBox*  divBoxes[4]     = { &lfoSyncDivBox[v],  &lfo2SyncDivBox[v],  &lfo3SyncDivBox[v],  &lfo4SyncDivBox[v]  };

    for (int li = 0; li < 4; ++li)
    {
        const int lx = lfoXArr[li];
        waveBoxes [li]->setBounds (lx + 3,  ctrlTopY + 26,  pLfoW - 6,  18);
        rateSliders[li]->setBounds (lx + 3,  ctrlTopY + 62,  kSz, kSz);
        depSliders [li]->setBounds (lx + 51, ctrlTopY + 62,  kSz, kSz);
        tgtBoxes  [li]->setBounds (lx + 3,  ctrlTopY + 116, pLfoW - 6,  18);
        syncBtns  [li]->setBounds (lx + 3,  ctrlTopY + 152, 42, 18);
        divBoxes  [li]->setBounds (lx + 49, ctrlTopY + 152, 43, 18);
    }

    // ── Mod Envelope (in AMP ENV panel cy2 + cy3) ─────────────────────────────
    modEnvAtkSlider  [v].setBounds (pAEX + 2,            cy2, kSz, kSz);
    modEnvDecSlider  [v].setBounds (pAEX + 2 + 48,       cy2, kSz, kSz);
    modEnvSusSlider  [v].setBounds (pAEX + 2 + 96,       cy2, kSz, kSz);
    modEnvRelSlider  [v].setBounds (pAEX + 2 + 144,      cy2, kSz, kSz);
    modEnvDepthSlider[v].setBounds (pAEX + 2,            cy3, kSz, kSz);
    modEnvDestBox    [v].setBounds (pAEX + 50,           cy3 + 6, 100, 20);
    modEnvSyncBtn    [v].setBounds (pAEX + 156,          cy3 + 6,  42, 20);
    modEnvDivBox     [v].setBounds (pAEX + 156,          cy3 + 27, 42, 20);
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
            const bool gOn      = vp.stepGates  [i];
            const bool tied     = vp.stepTied   [i];
            const int  r        = vp.stepRepeats[i];

            if (isActive)
            {
                // Flash the active step with a bright highlight colour
                juce::Colour col;
                if      (!gOn)   col = activeGateOffColour;
                else if (tied)   col = activeTieColour;
                else if (r > 0)  col = activeRatchetColour;
                else             col = activeGateOnColour;

                juce::String txt;
                if      (tied)   txt = "~";
                else if (r > 0)  txt = juce::String (r + 1);
                else             txt = "";

                gateBtn[v][i].setButtonText (txt);
                gateBtn[v][i].setColour (juce::TextButton::buttonColourId, col);
            }
            else
            {
                refreshGateBtn (v, i);
            }

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


    const bool isAuto = audioProcessor.autoRun.load();
    autoBtn.setToggleState (isAuto, juce::dontSendNotification);
    autoBtn.setColour (juce::TextButton::buttonColourId, isAuto ? gateOnColour : gateOffColour);

    for (int v = 0; v < 2; ++v)
    {
        auto& vp = audioProcessor.voice[v];

        for (int i = 0; i < 16; ++i)
        {
            stepKnob[v][i].setValue (vp.stepVoltages[i], juce::dontSendNotification);
            refreshGateBtn (v, i);
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

        lfoRateSlider  [v].setValue (vp.lfoRate,  juce::dontSendNotification);
        lfoDepthSlider [v].setValue (vp.lfoDepth, juce::dontSendNotification);
        lfoWaveBox     [v].setSelectedItemIndex (vp.lfoWaveform,  juce::dontSendNotification);
        lfoTargetBox   [v].setSelectedItemIndex (vp.lfoTarget,    juce::dontSendNotification);
        lfoSyncBtn     [v].setToggleState (vp.lfoSync, juce::dontSendNotification);
        lfoSyncBtn     [v].setButtonText  (vp.lfoSync ? "SYNC" : "FREE");
        lfoSyncBtn     [v].setColour (juce::TextButton::buttonColourId, vp.lfoSync ? knobColour : gateOffColour);
        lfoSyncDivBox  [v].setSelectedItemIndex (vp.lfoSyncDiv,   juce::dontSendNotification);

        lfo2RateSlider [v].setValue (vp.lfo2Rate,  juce::dontSendNotification);
        lfo2DepthSlider[v].setValue (vp.lfo2Depth, juce::dontSendNotification);
        lfo2WaveBox    [v].setSelectedItemIndex (vp.lfo2Waveform, juce::dontSendNotification);
        lfo2TargetBox  [v].setSelectedItemIndex (vp.lfo2Target,   juce::dontSendNotification);
        lfo2SyncBtn    [v].setToggleState (vp.lfo2Sync, juce::dontSendNotification);
        lfo2SyncBtn    [v].setButtonText  (vp.lfo2Sync ? "SYNC" : "FREE");
        lfo2SyncBtn    [v].setColour (juce::TextButton::buttonColourId, vp.lfo2Sync ? knobColour : gateOffColour);
        lfo2SyncDivBox [v].setSelectedItemIndex (vp.lfo2SyncDiv,  juce::dontSendNotification);

        lfo3RateSlider [v].setValue (vp.lfo3Rate,  juce::dontSendNotification);
        lfo3DepthSlider[v].setValue (vp.lfo3Depth, juce::dontSendNotification);
        lfo3WaveBox    [v].setSelectedItemIndex (vp.lfo3Waveform, juce::dontSendNotification);
        lfo3TargetBox  [v].setSelectedItemIndex (vp.lfo3Target,   juce::dontSendNotification);
        lfo3SyncBtn    [v].setToggleState (vp.lfo3Sync, juce::dontSendNotification);
        lfo3SyncBtn    [v].setButtonText  (vp.lfo3Sync ? "SYNC" : "FREE");
        lfo3SyncBtn    [v].setColour (juce::TextButton::buttonColourId, vp.lfo3Sync ? knobColour : gateOffColour);
        lfo3SyncDivBox [v].setSelectedItemIndex (vp.lfo3SyncDiv,  juce::dontSendNotification);

        lfo4RateSlider [v].setValue (vp.lfo4Rate,  juce::dontSendNotification);
        lfo4DepthSlider[v].setValue (vp.lfo4Depth, juce::dontSendNotification);
        lfo4WaveBox    [v].setSelectedItemIndex (vp.lfo4Waveform, juce::dontSendNotification);
        lfo4TargetBox  [v].setSelectedItemIndex (vp.lfo4Target,   juce::dontSendNotification);
        lfo4SyncBtn    [v].setToggleState (vp.lfo4Sync, juce::dontSendNotification);
        lfo4SyncBtn    [v].setButtonText  (vp.lfo4Sync ? "SYNC" : "FREE");
        lfo4SyncBtn    [v].setColour (juce::TextButton::buttonColourId, vp.lfo4Sync ? knobColour : gateOffColour);
        lfo4SyncDivBox [v].setSelectedItemIndex (vp.lfo4SyncDiv,  juce::dontSendNotification);

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

    const auto& p = audioProcessor.fx;
    delayOnBtn.setToggleState (p.delayOn, juce::dontSendNotification);
    delayOnBtn.setButtonText (p.delayOn ? "ON" : "OFF");
    delayOnBtn.setColour (juce::TextButton::buttonColourId, p.delayOn ? gateOnColour : gateOffColour);
    delaySyncBtn.setToggleState (p.delaySync, juce::dontSendNotification);
    delaySyncBtn.setButtonText (p.delaySync ? "SYNC" : "FREE");
    delaySyncBtn.setColour (juce::TextButton::buttonColourId, p.delaySync ? juce::Colour(0xffe09040) : gateOffColour);
    delaySyncDivBox.setSelectedItemIndex (p.delaySyncDiv, juce::dontSendNotification);
    delayTimeMsSlider.setValue (p.delayTimeMs, juce::dontSendNotification);
    delayFeedbackSlider.setValue (p.delayFeedback, juce::dontSendNotification);
    delayPingPongBtn.setToggleState (p.delayPingPong, juce::dontSendNotification);
    delayPingPongBtn.setColour (juce::TextButton::buttonColourId, p.delayPingPong ? juce::Colour(0xff5566dd) : gateOffColour);
    delayMixSlider.setValue (p.delayMix, juce::dontSendNotification);
    reverbOnBtn.setToggleState (p.reverbOn, juce::dontSendNotification);
    reverbOnBtn.setButtonText (p.reverbOn ? "ON" : "OFF");
    reverbOnBtn.setColour (juce::TextButton::buttonColourId, p.reverbOn ? gateOnColour : gateOffColour);
    reverbSizeSlider.setValue (p.reverbSize, juce::dontSendNotification);
    reverbDampingSlider.setValue (p.reverbDamping, juce::dontSendNotification);
    reverbPreDelaySlider.setValue (p.reverbPreDelay, juce::dontSendNotification);
    reverbMixSlider.setValue (p.reverbMix, juce::dontSendNotification);
    chorusOnBtn.setToggleState (p.chorusOn, juce::dontSendNotification);
    chorusOnBtn.setButtonText (p.chorusOn ? "ON" : "OFF");
    chorusOnBtn.setColour (juce::TextButton::buttonColourId, p.chorusOn ? gateOnColour : gateOffColour);
    chorusRateSlider.setValue (p.chorusRate, juce::dontSendNotification);
    chorusDepthSlider.setValue (p.chorusDepth, juce::dontSendNotification);
    chorusMixSlider.setValue (p.chorusMix, juce::dontSendNotification);
    masterDriveSlider.setValue (p.masterDrive, juce::dontSendNotification);
    masterGainSlider.setValue (p.masterGain, juce::dontSendNotification);

    repaint();
}
