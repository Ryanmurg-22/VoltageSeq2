#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AboutImageData.h"

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
    const juce::Colour accentOnColour     { 0xffdd6600 };   // amber-orange for accented steps
    const juce::Colour accentBothColour   { 0xffff9933 };   // bright amber when slide+accent both on
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
    constexpr int seqH       =  134;   // taller lane
    constexpr int stepStride =   44;   // packed box columns (was 88)

    // Step controls (relative to strip Y origin)
    constexpr int stepSliderTop = 5;
    constexpr int stepSliderH   = 98;    // taller box columns
    constexpr int gateRelY      = 106;
    constexpr int slideRelY     = 120;

    // Sub-strip band removed (controls now live in the seq lane's RHS) — keep a
    // thin gap between lanes.
    constexpr int subH = 8;

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
    // Knob size: 42 × 42 px (bumped from 38 for approachability)
    constexpr int kSz = 42;

    // Control panel X positions — set-and-forget (SEQ/QUANTIZER) shrunk, the rest
    // shifted left ~100px to reclaim a band on the right for the modulation slot.
    // Bottom = the SYNTH. RANGE/ROOT/SCALE/CLOCK have moved UP to the pattern
    // (top) section, so the bottom-left holds only GLIDE (porta); the synth
    // panels shift left, opening a wide right band for envelopes + modulation.
    constexpr int pSeqX  =    5, pSeqW  =  78;   // GLIDE (PORTA only)
    constexpr int pQntX  =  100, pQntW  = 140;   // (unused: QUANT moved to top)
    constexpr int pO1X   =   86, pO1W   = 185;
    constexpr int pO2X   =  276, pO2W   = 175;
    constexpr int pFltX  =  452, pFltW  = 188;
    constexpr int pAEX   =  646, pAEW   = 200;
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
// GateBtnListener — right-click shows ratchet + pulse count menu
//==============================================================================
void VoltageSeq2AudioProcessorEditor::GateBtnListener::mouseDown (const juce::MouseEvent& ev)
{
    if (!ev.mods.isPopupMenu()) return;

    ed.suppressNextGateClick = true;
    auto& vp = ed.audioProcessor.voice[vi];
    juce::PopupMenu menu;

    // ── Ratchet ──────────────────────────────────────────────────────────────
    menu.addSectionHeader ("RATCHET");
    const char* rLabels[] = { "1x  (off)", "2x", "3x", "4x" };
    for (int r = 0; r < 4; ++r)
        menu.addItem (r + 1, rLabels[r], true, vp.stepRepeats[step] == r);

    // ── Pulse count ──────────────────────────────────────────────────────────
    menu.addSeparator();
    menu.addSectionHeader ("PULSE COUNT");
    for (int p = 1; p <= 8; ++p)
        menu.addItem (10 + p, juce::String (p) + (p == 1 ? " pulse  (default)" : " pulses"),
                      true, vp.stepPulses[step] == p);

    // ── Octave shift ─────────────────────────────────────────────────────────
    menu.addSeparator();
    menu.addSectionHeader ("OCTAVE SHIFT");
    const char* octLabels[] = { "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4" };
    for (int o = 0; o < 9; ++o)
        menu.addItem (100 + o, octLabels[o], true, vp.stepOctave[step] == (o - 4));

    // ── Probability ──────────────────────────────────────────────────────────
    menu.addSeparator();
    menu.addSectionHeader ("PROBABILITY");
    const int probVals[]      = { 100, 90, 75, 50, 25, 10, 0 };
    const char* probLabels[]  = { "100%", "90%", "75%", "50%", "25%", "10%", "0% (mute)" };
    for (int p = 0; p < 7; ++p)
        menu.addItem (200 + p, probLabels[p], true, (int)vp.stepProbability[step] == probVals[p]);

    menu.showMenuAsync (
        juce::PopupMenu::Options{}.withTargetComponent (&ed.gateBtn[vi][step]),
        [this](int result)
        {
            ed.suppressNextGateClick = false;  // always clear — menu consumed the click
            if (result >= 1 && result <= 4)
                ed.audioProcessor.voice[vi].stepRepeats[step] = result - 1;
            else if (result >= 11 && result <= 18)
                ed.audioProcessor.voice[vi].stepPulses[step] = result - 10;
            else if (result >= 100 && result <= 108)
                ed.audioProcessor.voice[vi].stepOctave[step] = (result - 100) - 4;
            else if (result >= 200 && result <= 206)
            {
                const int pv[] = { 100, 90, 75, 50, 25, 10, 0 };
                ed.audioProcessor.voice[vi].stepProbability[step] = (float)pv[result - 200];
            }
            if (result > 0)
                ed.refreshGateBtn (vi, step);
        });
}


//==============================================================================
// SlideBtnListener — Ctrl/right-click shows accent popup menu
//==============================================================================
void VoltageSeq2AudioProcessorEditor::SlideBtnListener::mouseDown (const juce::MouseEvent& ev)
{
    if (!ev.mods.isPopupMenu()) return;

    ed.suppressNextSlideClick = true;
    auto& vp = ed.audioProcessor.voice[vi];

    juce::PopupMenu menu;
    menu.addSectionHeader ("ACCENT");
    menu.addItem (1, "Accent", true, vp.stepAccents[step]);

    menu.showMenuAsync (
        juce::PopupMenu::Options{}.withTargetComponent (&ed.slideBtn[vi][step]),
        [this](int result)
        {
            ed.suppressNextSlideClick = false;  // always clear — menu consumed the click
            if (result == 1)
                ed.audioProcessor.voice[vi].stepAccents[step] =
                    !ed.audioProcessor.voice[vi].stepAccents[step];
            ed.refreshSlideBtn (vi, step);
        });
}

//==============================================================================
// refreshSlideBtn — sync one slide button's colour from processor state
//==============================================================================
void VoltageSeq2AudioProcessorEditor::refreshSlideBtn (int v, int i)
{
    const bool slide  = audioProcessor.voice[v].stepGlides [i];
    const bool accent = audioProcessor.voice[v].stepAccents[i];

    juce::Colour col;
    if      (slide && accent) col = accentBothColour;
    else if (slide)           col = slideOnColour;
    else if (accent)          col = accentOnColour;
    else                      col = gateOffColour;

    // Set both colour IDs so the button looks correct regardless of toggle state
    slideBtn[v][i].setColour (juce::TextButton::buttonColourId,   col);
    slideBtn[v][i].setColour (juce::TextButton::buttonOnColourId, col);
}

//==============================================================================
// refreshGateBtn — sync one gate button's text and colour from processor state
//==============================================================================
void VoltageSeq2AudioProcessorEditor::refreshGateBtn (int v, int i)
{
    const auto& vp  = audioProcessor.voice[v];
    const bool  g   = vp.stepGates      [i];
    const bool  t   = vp.stepTied       [i];
    const int   r   = vp.stepRepeats    [i];   // 0=1× … 3=4×
    const int   oct = vp.stepOctave     [i];
    const float prob= vp.stepProbability[i];

    // Display priority: tied > ratchet count > octave+prob combined
    juce::String txt;
    if (t)
        txt = "~";
    else if (r > 0)
        txt = juce::String (r + 1);   // "2", "3", "4"
    else
    {
        if (oct != 0)
            txt = (oct > 0 ? "+" : "") + juce::String (oct);
        if (prob < 99.f)
            txt += "%";
    }

    juce::Colour col;
    if      (!g)    col = gateOffColour;
    else if (t)     col = tieColour;
    else if (r > 0) col = ratchetColour;
    else if (prob < 99.f) col = juce::Colour (0xff2a5080);
    else            col = gateOnColour;

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
    // ── Apply custom LookAndFeel globally ─────────────────────────────────────
    // Must be set before any child components are created so they all inherit it.
    juce::LookAndFeel::setDefaultLookAndFeel (&voltageSeqLAF);

    // ── Load backplate SVG ────────────────────────────────────────────────────
    // fromUTF8 so the SVG's "·", "—", "→" etc. decode correctly (a plain
    // juce::String(const char*) was rendering them as Latin-1 mojibake).
    if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (kBackplateSVG)))
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

    // ── Floating section panels setup ────────────────────────────────────────
    for (int v = 0; v < 2; ++v)
    {
        // OSC panel
        oscPanel[v].title = juce::String::fromUTF8 (v == 0 ? "OSC  \xe2\x80\x94  VOICE A" : "OSC  \xe2\x80\x94  VOICE B");
        oscPanel[v].closeBtn.onClick = [this, v]() { closeOscPanel (v); };
        oscPanel[v].closeBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a1a00));
        oscPanel[v].closeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe09040));
        addChildComponent (oscPanel[v]);
        synthPageComponents.push_back (&oscPanel[v]);

        // ENV panel
        envPanel[v].title = juce::String::fromUTF8 (v == 0 ? "MOD ENV  \xe2\x80\x94  VOICE A" : "MOD ENV  \xe2\x80\x94  VOICE B");
        envPanel[v].closeBtn.onClick = [this, v]() { closeEnvPanel (v); };
        envPanel[v].closeBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a1a00));
        envPanel[v].closeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe09040));
        addChildComponent (envPanel[v]);
        synthPageComponents.push_back (&envPanel[v]);

        // LFO panel
        lfoPanel[v].title = juce::String::fromUTF8 (v == 0 ? "LFO  \xe2\x80\x94  VOICE A" : "LFO  \xe2\x80\x94  VOICE B");
        lfoPanel[v].closeBtn.onClick = [this, v]() { closeLfoPanel (v); };
        lfoPanel[v].closeBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a1a00));
        lfoPanel[v].closeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe09040));
        addChildComponent (lfoPanel[v]);
        synthPageComponents.push_back (&lfoPanel[v]);

        // OSC 1 / OSC 2 view radio (replaces the OSC popup toggle)
        {
            auto styleOscRadio = [](juce::TextButton& b, const juce::String& t)
            {
                b.setButtonText (t);
                b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff161630));
                b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2255aa));
                b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffa0a0b4));
                b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
            };
            styleOscRadio (oscView1Btn[v], "OSC 1");
            styleOscRadio (oscView2Btn[v], "OSC 2");
            oscView1Btn[v].onClick = [this, v]() { oscView[v] = 0; refreshOscView (v); };
            oscView2Btn[v].onClick = [this, v]() { oscView[v] = 1; refreshOscView (v); };
            addAndMakeVisible (oscView1Btn[v]);  synthPageComponents.push_back (&oscView1Btn[v]);
            addAndMakeVisible (oscView2Btn[v]);  synthPageComponents.push_back (&oscView2Btn[v]);
        }

        envPanelBtn[v].setButtonText (juce::String::fromUTF8 ("MOD \xe2\x96\xbc"));
        envPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
        envPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
        envPanelBtn[v].onClick = [this, v]() {
            envPanelOpen[v] ? closeEnvPanel(v) : openEnvPanel(v);
        };
        addAndMakeVisible (envPanelBtn[v]);
        synthPageComponents.push_back (&envPanelBtn[v]);

        lfoPanelBtn[v].setButtonText (juce::String::fromUTF8 ("LFO \xe2\x96\xbc"));
        lfoPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
        lfoPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
        lfoPanelBtn[v].onClick = [this, v]() {
            lfoPanelOpen[v] ? closeLfoPanel(v) : openLfoPanel(v);
        };
        addAndMakeVisible (lfoPanelBtn[v]);
        synthPageComponents.push_back (&lfoPanelBtn[v]);
    }

    //==========================================================================
    // SHARED / GLOBAL CONTROLS
    //==========================================================================

    // RAND mode + trigger buttons (one pair per voice)
    for (int v = 0; v < 2; ++v)
    {
        // ── Mode cycler: GATE → PITCH → BOTH ─────────────────────────────────
        randModeBtn[v].setButtonText ("GATE");
        randModeBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a40));
        randModeBtn[v].onClick = [this, v]()
        {
            randMode[v] = (randMode[v] + 1) % 3;
            static const char* labels[] = { "GATE", "PITCH", "BOTH" };
            randModeBtn[v].setButtonText (labels[randMode[v]]);
        };
        addAndMakeVisible (randModeBtn[v]);

        // ── Trigger: fires randomise with current mode ────────────────────────
        randomBtn[v].setButtonText ("RAND");
        randomBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a3040));
        randomBtn[v].onClick = [this, v]()
        {
            const bool doG = (randMode[v] == 0 || randMode[v] == 2);
            const bool doP = (randMode[v] == 1 || randMode[v] == 2);
            audioProcessor.generateRandomSequence (v, doG, doP);
            syncUIFromProcessor();
        };
        addAndMakeVisible (randomBtn[v]);
    }

    // Pattern transpose buttons (per voice) — configured here, added to page components below
    for (int v = 0; v < 2; ++v)
    {
        patTransposeUpBtn[v].setButtonText ("+OCT");
        patTransposeUpBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff203040));
        patTransposeUpBtn[v].onClick = [this, v]()
        {
            auto& vp = audioProcessor.voice[v];
            for (int i = 0; i < 16; ++i)
                vp.stepOctave[i] = juce::jlimit (-4, 4, vp.stepOctave[i] + 1);
            // Patch the stored slot directly — only the octave field changes,
            // so root key, pitches, and all other stored params are untouched.
            const int slot = activePatternSlot[v];
            if (slot >= 0 && slot < 16 && audioProcessor.patternBank[v][slot].used)
                for (int i = 0; i < 16; ++i)
                    audioProcessor.patternBank[v][slot].stepOctave[i] = vp.stepOctave[i];
            syncUIFromProcessor();
        };

        patTransposeDnBtn[v].setButtonText ("-OCT");
        patTransposeDnBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff203040));
        patTransposeDnBtn[v].onClick = [this, v]()
        {
            auto& vp = audioProcessor.voice[v];
            for (int i = 0; i < 16; ++i)
                vp.stepOctave[i] = juce::jlimit (-4, 4, vp.stepOctave[i] - 1);
            // Patch the stored slot directly — only the octave field changes,
            // so root key, pitches, and all other stored params are untouched.
            const int slot = activePatternSlot[v];
            if (slot >= 0 && slot < 16 && audioProcessor.patternBank[v][slot].used)
                for (int i = 0; i < 16; ++i)
                    audioProcessor.patternBank[v][slot].stepOctave[i] = vp.stepOctave[i];
            syncUIFromProcessor();
        };
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

    genPageBtn.setButtonText ("GENERATE");
    genPageBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    genPageBtn.onClick = [this]() { showPage (3); };
    addAndMakeVisible (genPageBtn);

    aboutPageBtn.setButtonText ("ABOUT");
    aboutPageBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    aboutPageBtn.onClick = [this]() { showPage (4); };
    addAndMakeVisible (aboutPageBtn);

    // ── Capture all synth-page components (everything added so far except nav btns)
    for (auto* c : getChildren())
        if (c != &synthPageBtn && c != &patternPageBtn
         && c != &fxPageBtn    && c != &genPageBtn && c != &aboutPageBtn)
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

        // Transpose buttons are already added as visible in constructor — register for pattern page
        addChildComponent (patTransposeUpBtn[vi]);
        patternPageComponents.push_back (&patTransposeUpBtn[vi]);
        addChildComponent (patTransposeDnBtn[vi]);
        patternPageComponents.push_back (&patTransposeDnBtn[vi]);
    }

    // ── FX page controls ─────────────────────────────────────────────────────
    setupFxControls();

    // ── Generate page controls ────────────────────────────────────────────────
    setupGenControls();

    // ── Pattern sequencer controls ────────────────────────────────────────────
    setupPatternSeqControls();

    // ── Macro controllers ─────────────────────────────────────────────────────
    setupMacros();

    // ── About page ────────────────────────────────────────────────────────────
    setupAboutPage();

    // setSize LAST — triggers resized() which calls layoutVoice()
    setSize (1500, winH);
    showPage (0);   // ensure FX / pattern / gen controls start hidden
    startTimerHz (30);
}

VoltageSeq2AudioProcessorEditor::~VoltageSeq2AudioProcessorEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);  // restore before voltageSeqLAF is destroyed
}

//==============================================================================
// setupVoice — wire controls for one voice
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupVoice (int v)
{
    auto& vp = audioProcessor.voice[v];

    // ── VELO mode toggle button ───────────────────────────────────────────────
    veloModeBtn[v].setButtonText ("VELO");
    veloModeBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
    veloModeBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
    veloModeBtn[v].onClick = [this, v]()
    {
        veloMode[v] = !veloMode[v];
        veloModeBtn[v].setColour (juce::TextButton::buttonColourId,
            veloMode[v] ? juce::Colour (0xff5a3000) : juce::Colour (0xff161630));
        veloModeBtn[v].setColour (juce::TextButton::textColourOffId,
            veloMode[v] ? juce::Colour (0xffff9900) : juce::Colour (0xffe0e0e0));
        for (int i = 0; i < 16; ++i)
        {
            stepKnob[v][i].setVisible (!veloMode[v]);
            veloKnob[v][i].setVisible ( veloMode[v]);
        }
    };
    addAndMakeVisible (veloModeBtn[v]);
    synthPageComponents.push_back (&veloModeBtn[v]);

    // Step sliders + gate + slide
    for (int i = 0; i < 16; ++i)
    {
        stepKnob[v][i].setSliderStyle (juce::Slider::LinearVertical);
        stepKnob[v][i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        stepKnob[v][i].getProperties().set ("boxStyle", true);   // readable box render
        stepKnob[v][i].setColour (juce::Slider::trackColourId,      knobColour);
        stepKnob[v][i].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252540));
        addAndMakeVisible (stepKnob[v][i]);
        // APVTS attachment: owns range (-5..+5) and bidirectional value sync.
        stepAttach[v][i] = std::make_unique<SliderAtt> (
            audioProcessor.apvts,
            "step" + juce::String (i) + "_" + juce::String (v),
            stepKnob[v][i]);

        // ── Velocity overlay slider (hidden until VELO mode active) ───────────
        veloKnob[v][i].setSliderStyle (juce::Slider::LinearVertical);
        veloKnob[v][i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        veloKnob[v][i].getProperties().set ("boxStyle", true);   // readable box render
        veloKnob[v][i].setRange (1.0, 127.0, 1.0);
        veloKnob[v][i].setValue (audioProcessor.voice[v].stepVelocity[i], juce::dontSendNotification);
        veloKnob[v][i].setColour (juce::Slider::trackColourId,      juce::Colour (0xff00aaff));  // cyan — clearly distinct from pitch amber
        veloKnob[v][i].setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252520));
        veloKnob[v][i].onValueChange = [this, v, i]()
        {
            audioProcessor.voice[v].stepVelocity[i] = (float)veloKnob[v][i].getValue();
        };
        addChildComponent (veloKnob[v][i]);   // starts hidden
        veloKnob[v][i].setVisible (false);    // JUCE components default visible=true; force hidden

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

        slideBtn[v][i].setButtonText (juce::String (i + 1));
        slideBtn[v][i].setClickingTogglesState (false);
        slideBtn[v][i].onClick = [this, v, i]()
        {
            if (suppressNextSlideClick) { suppressNextSlideClick = false; return; }
            audioProcessor.voice[v].stepGlides[i] = !audioProcessor.voice[v].stepGlides[i];
            refreshSlideBtn (v, i);
        };
        slideMouseListener[v][i] = std::make_unique<SlideBtnListener> (*this, v, i);
        slideBtn[v][i].addMouseListener (slideMouseListener[v][i].get(), false);
        refreshSlideBtn (v, i);
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
        auto& vp = audioProcessor.voice[v];

        // Full per-step clear back to a blank pattern.
        for (int i = 0; i < 16; ++i)
        {
            vp.stepVoltages[i]    = 0.0f;
            vp.stepGates[i]       = false;
            vp.stepGlides[i]      = false;
            vp.stepAccents[i]     = false;
            vp.stepTied[i]        = false;
            vp.stepRepeats[i]     = 0;       // 1× — no ratchet
            vp.stepPulses[i]      = 1;
            vp.stepOctave[i]      = 0;
            vp.stepVelocity[i]    = 100.0f;
            vp.stepProbability[i] = 100.0f;
        }
        vp.unipolar = true;                  // clean slate = UNI (positive-only) default

        // Push step pitches (APVTS-backed) to the params/host, then refresh the
        // non-APVTS UI (gate buttons, velocity overlays, bipolar toggle).
        audioProcessor.syncAPVTSFromVoice (v);
        syncUIFromProcessor();
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
    rangeSlider[v].setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    rangeSlider[v].setColour (juce::Slider::trackColourId,            knobColour);
    rangeSlider[v].setColour (juce::Slider::backgroundColourId,       juce::Colour (0xff252540));
    rangeSlider[v].setColour (juce::Slider::textBoxTextColourId,      textColour);
    rangeSlider[v].setColour (juce::Slider::textBoxBackgroundColourId,bgColour);
    rangeSlider[v].setColour (juce::Slider::textBoxOutlineColourId,   bgColour);
    addAndMakeVisible (rangeSlider[v]);
    rangeAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "range_" + juce::String (v), rangeSlider[v]);

    setupKnob (portaSlider[v], 0.0, 2.0, vp.portamentoTime);
    portaAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "porta_" + juce::String (v), portaSlider[v]);

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
    // osc1WaveBox stays permanently visible on front page — NOT in oscSectionComps

    setupKnob (osc1LevelSlider[v], 0.0, 1.0, vp.osc1Level);
    osc1LevelSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc1Level = (float)osc1LevelSlider[v].getValue(); };
    // osc1LevelSlider stays permanently visible on front page — NOT in oscSectionComps

    osc1OctaveBox[v].addItem ("-2",1); osc1OctaveBox[v].addItem ("-1",2); osc1OctaveBox[v].addItem ("0",3);
    osc1OctaveBox[v].addItem ("+1",4); osc1OctaveBox[v].addItem ("+2",5);
    osc1OctaveBox[v].setSelectedItemIndex (vp.osc1Octave + 2, juce::dontSendNotification);
    osc1OctaveBox[v].onChange = [this, v]() { audioProcessor.voice[v].osc1Octave = osc1OctaveBox[v].getSelectedItemIndex() - 2; };
    addAndMakeVisible (osc1OctaveBox[v]);
    // osc1OctaveBox stays permanently visible on front page — NOT in oscSectionComps

    setupKnob (osc1PWMSlider[v], 0.05, 0.95, vp.osc1PulseWidth);
    osc1PWMSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc1PulseWidth = (float)osc1PWMSlider[v].getValue(); };
    oscSectionComps[v].push_back (&osc1PWMSlider[v]);

    // OSC1 feedback
    setupKnob (osc1FeedbackSlider[v], 0.0, 1.0, vp.osc1Feedback);
    osc1FeedbackSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc1Feedback = (float)osc1FeedbackSlider[v].getValue(); };
    oscSectionComps[v].push_back (&osc1FeedbackSlider[v]);

    // Drift
    setupKnob (driftSlider[v], 0.0, 1.0, vp.driftAmount);
    driftSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].driftAmount = (float)driftSlider[v].getValue(); };
    oscSectionComps[v].push_back (&driftSlider[v]);

    // OSC 2
    setupKnob (osc2PosSlider[v], 0.0, 1.0, vp.osc2Position);
    osc2PosSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc2Position = (float)osc2PosSlider[v].getValue(); };
    oscSectionComps[v].push_back (&osc2PosSlider[v]);

    setupKnob (osc2LevelSlider[v], 0.0, 1.0, vp.osc2Level);
    osc2LevelSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].osc2Level = (float)osc2LevelSlider[v].getValue(); };
    oscSectionComps[v].push_back (&osc2LevelSlider[v]);

    osc2OctaveBox[v].addItem ("-2",1); osc2OctaveBox[v].addItem ("-1",2); osc2OctaveBox[v].addItem ("0",3);
    osc2OctaveBox[v].addItem ("+1",4); osc2OctaveBox[v].addItem ("+2",5);
    osc2OctaveBox[v].setSelectedItemIndex (vp.osc2Octave + 2, juce::dontSendNotification);
    osc2OctaveBox[v].onChange = [this, v]() { audioProcessor.voice[v].osc2Octave = osc2OctaveBox[v].getSelectedItemIndex() - 2; };
    addAndMakeVisible (osc2OctaveBox[v]);
    oscSectionComps[v].push_back (&osc2OctaveBox[v]);

    // FM
    setupKnob (fmDepthSlider[v], 0.0, 1.0, vp.fmDepth);
    fmDepthAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fmDepth_" + juce::String (v), fmDepthSlider[v]);
    oscSectionComps[v].push_back (&fmDepthSlider[v]);

    // FM Ratio — full-width LinearHorizontal, shows exact value
    fmRatioSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    fmRatioSlider[v].setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 20);
    fmRatioSlider[v].setNumDecimalPlacesToDisplay (2);
    fmRatioSlider[v].setColour (juce::Slider::trackColourId,             knobColour);
    fmRatioSlider[v].setColour (juce::Slider::backgroundColourId,        juce::Colour (0xff252540));
    fmRatioSlider[v].setColour (juce::Slider::textBoxTextColourId,       textColour);
    fmRatioSlider[v].setColour (juce::Slider::textBoxBackgroundColourId, bgColour);
    fmRatioSlider[v].setColour (juce::Slider::textBoxOutlineColourId,    bgColour);
    addAndMakeVisible (fmRatioSlider[v]);
    fmRatioAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fmRatio_" + juce::String (v), fmRatioSlider[v]);
    oscSectionComps[v].push_back (&fmRatioSlider[v]);

    // Cross-mod (not automatable — keep manual callback)
    setupKnob (crossModSlider[v], 0.0, 1.0, vp.crossModDepth);
    crossModSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].crossModDepth = (float)crossModSlider[v].getValue(); };
    oscSectionComps[v].push_back (&crossModSlider[v]);

    // Filter
    setupKnob (cutoffSlider[v], 20.0, 16000.0, vp.filterCutoff, 1000.0);
    cutoffAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "cutoff_" + juce::String (v), cutoffSlider[v]);

    setupKnob (resonanceSlider[v], 0.0, 1.0, vp.filterResonance);
    resonanceSlider[v].onValueChange = [this, v]() { audioProcessor.voice[v].filterResonance = (float)resonanceSlider[v].getValue(); };

    setupKnob (filterEnvAmtSlider[v], 0.0, 1.0, vp.filterEnvAmount);
    fEnvAmtAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fEnvAmt_" + juce::String (v), filterEnvAmtSlider[v]);

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

    // Env reset toggle
    envResetBtn[v].setButtonText (vp.envReset ? "RESET" : "LEGATO");
    envResetBtn[v].setClickingTogglesState (true);
    envResetBtn[v].setToggleState (vp.envReset, juce::dontSendNotification);
    envResetBtn[v].setColour (juce::TextButton::buttonColourId,   vp.envReset ? juce::Colour(0xff994422) : juce::Colour(0xff161630));
    envResetBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff994422));
    envResetBtn[v].onClick = [this, v]()
    {
        bool r = envResetBtn[v].getToggleState();
        audioProcessor.voice[v].envReset = r;
        envResetBtn[v].setButtonText (r ? "RESET" : "LEGATO");
        envResetBtn[v].setColour (juce::TextButton::buttonColourId, r ? juce::Colour(0xff994422) : juce::Colour(0xff161630));
    };
    addAndMakeVisible (envResetBtn[v]);
    synthPageComponents.push_back (&envResetBtn[v]);
    // envResetBtn stays permanently visible on front page — NOT in envSectionComps

    // Pulse mode toggle (STAGES / PULSES)
    pulseModeBtn[v].setButtonText (vp.pulseLengthMode ? "PULSES" : "STAGES");
    pulseModeBtn[v].setClickingTogglesState (true);
    pulseModeBtn[v].setToggleState (vp.pulseLengthMode, juce::dontSendNotification);
    pulseModeBtn[v].setColour (juce::TextButton::buttonColourId,   vp.pulseLengthMode ? juce::Colour(0xff00aa88) : juce::Colour(0xff161630));
    pulseModeBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff00aa88));
    pulseModeBtn[v].onClick = [this, v]()
    {
        bool m = pulseModeBtn[v].getToggleState();
        audioProcessor.voice[v].pulseLengthMode = m;
        pulseModeBtn[v].setButtonText (m ? "PULSES" : "STAGES");
        pulseModeBtn[v].setColour (juce::TextButton::buttonColourId, m ? juce::Colour(0xff00aa88) : juce::Colour(0xff161630));
        pulseLenBox[v].setVisible (m);
    };
    addAndMakeVisible (pulseModeBtn[v]);
    synthPageComponents.push_back (&pulseModeBtn[v]);

    // Pulse length selector (visible only in PULSES mode)
    static const int pulseLengthValues[] = { 4,6,8,12,16,24,32,48,64,96,128 };
    for (int i = 0; i < 11; ++i)
        pulseLenBox[v].addItem (juce::String (pulseLengthValues[i]), i + 1);
    // Select closest item to current pulseLength
    {
        int bestId = 4;
        for (int i = 0; i < 11; ++i)
            if (pulseLengthValues[i] <= vp.pulseLength) bestId = i + 1;
        pulseLenBox[v].setSelectedId (bestId, juce::dontSendNotification);
    }
    pulseLenBox[v].setColour (juce::ComboBox::backgroundColourId, juce::Colour(0xff0e1a14));
    pulseLenBox[v].setColour (juce::ComboBox::textColourId,       juce::Colour(0xff00d4aa));
    pulseLenBox[v].onChange = [this, v]()
    {
        static const int vals[] = { 4,6,8,12,16,24,32,48,64,96,128 };
        int idx = pulseLenBox[v].getSelectedItemIndex();
        if (idx >= 0 && idx < 11)
            audioProcessor.voice[v].pulseLength = vals[idx];
    };
    pulseLenBox[v].setVisible (vp.pulseLengthMode);
    addChildComponent (pulseLenBox[v]);
    synthPageComponents.push_back (&pulseLenBox[v]);

    // Amp Envelope
    // Amp Envelope — stays permanently visible on front page — NOT in envSectionComps
    setupKnob (attackSlider[v],  0.001, 2.0, vp.adsrParams.attack,  0.3);
    ampAAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "ampA_" + juce::String (v), attackSlider[v]);
    setupKnob (decaySlider[v],   0.001, 2.0, vp.adsrParams.decay,   0.3);
    ampDAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "ampD_" + juce::String (v), decaySlider[v]);
    setupKnob (sustainSlider[v], 0.0,   1.0, vp.adsrParams.sustain);
    ampSAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "ampS_" + juce::String (v), sustainSlider[v]);
    setupKnob (releaseSlider[v], 0.001, 3.0, vp.adsrParams.release, 0.3);
    ampRAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "ampR_" + juce::String (v), releaseSlider[v]);

    // Filter Envelope
    setupKnob (fAttackSlider[v],  0.001, 4.0, vp.filterEnvParams.attack,  0.3);
    fAAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fA_" + juce::String (v), fAttackSlider[v]);
    setupKnob (fDecaySlider[v],   0.001, 4.0, vp.filterEnvParams.decay,   0.3);
    fDAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fD_" + juce::String (v), fDecaySlider[v]);
    setupKnob (fSustainSlider[v], 0.0,   1.0, vp.filterEnvParams.sustain);
    fSAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fS_" + juce::String (v), fSustainSlider[v]);
    setupKnob (fReleaseSlider[v], 0.001, 4.0, vp.filterEnvParams.release, 0.3);
    fRAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "fR_" + juce::String (v), fReleaseSlider[v]);

    // Helper: build a standard LFO target combo
    auto addLFOTargetItems = [](juce::ComboBox& box)
    {
        box.addItem ("PWM",    1); box.addItem ("Cutoff", 2);
        box.addItem ("Pitch",  3); box.addItem ("Range",  4);
        box.addItem ("FM Dpt", 5);
        box.addItem ("PL Harm",  6);
        box.addItem ("PL Timb",  7);
        box.addItem ("PL Morph", 8);
        box.addItem ("Delay",    9);   // index 8 → LFO→Delay Time
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
    lfo1RateAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "lfo1Rate_" + juce::String (v), lfoRateSlider[v]);
    lfoSectionComps[v].push_back (&lfoRateSlider[v]);
    setupKnob (lfoDepthSlider[v], 0.0, 1.0, vp.lfoDepth);
    lfo1DepAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "lfo1Dep_" + juce::String (v), lfoDepthSlider[v]);
    lfoSectionComps[v].push_back (&lfoDepthSlider[v]);
    addWaveItems (lfoWaveBox[v]);
    lfoWaveBox[v].setSelectedItemIndex (vp.lfoWaveform, juce::dontSendNotification);
    lfoWaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfoWaveform = lfoWaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfoWaveBox[v]);
    lfoSectionComps[v].push_back (&lfoWaveBox[v]);
    addLFOTargetItems (lfoTargetBox[v]);
    lfoTargetBox[v].setSelectedItemIndex (vp.lfoTarget, juce::dontSendNotification);
    lfoTargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfoTarget = lfoTargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfoTargetBox[v]);
    lfoSectionComps[v].push_back (&lfoTargetBox[v]);
    setupLFOSync (lfoSyncBtn[v], lfoSyncDivBox[v], vp.lfoSync, vp.lfoSyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfoSync = s; });
    lfoSectionComps[v].push_back (&lfoSyncBtn[v]);
    lfoSectionComps[v].push_back (&lfoSyncDivBox[v]);
    lfoSyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfoSyncDiv = lfoSyncDivBox[v].getSelectedItemIndex(); };

    // LFO 2
    setupKnob (lfo2RateSlider[v], 0.1, 20.0, vp.lfo2Rate, 4.0);
    lfo2RateAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "lfo2Rate_" + juce::String (v), lfo2RateSlider[v]);
    lfoSectionComps[v].push_back (&lfo2RateSlider[v]);
    setupKnob (lfo2DepthSlider[v], 0.0, 1.0, vp.lfo2Depth);
    lfo2DepAttach[v] = std::make_unique<SliderAtt> (audioProcessor.apvts, "lfo2Dep_" + juce::String (v), lfo2DepthSlider[v]);
    lfoSectionComps[v].push_back (&lfo2DepthSlider[v]);
    addWaveItems (lfo2WaveBox[v]);
    lfo2WaveBox[v].setSelectedItemIndex (vp.lfo2Waveform, juce::dontSendNotification);
    lfo2WaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo2Waveform = lfo2WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo2WaveBox[v]);
    lfoSectionComps[v].push_back (&lfo2WaveBox[v]);
    addLFOTargetItems (lfo2TargetBox[v]);
    lfo2TargetBox[v].setSelectedItemIndex (vp.lfo2Target, juce::dontSendNotification);
    lfo2TargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo2Target = lfo2TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo2TargetBox[v]);
    lfoSectionComps[v].push_back (&lfo2TargetBox[v]);
    setupLFOSync (lfo2SyncBtn[v], lfo2SyncDivBox[v], vp.lfo2Sync, vp.lfo2SyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfo2Sync = s; });
    lfoSectionComps[v].push_back (&lfo2SyncBtn[v]);
    lfoSectionComps[v].push_back (&lfo2SyncDivBox[v]);
    lfo2SyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo2SyncDiv = lfo2SyncDivBox[v].getSelectedItemIndex(); };

    // LFO 3
    setupKnob (lfo3RateSlider[v], 0.1, 20.0, vp.lfo3Rate, 4.0);
    lfo3RateSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo3Rate  = (float)lfo3RateSlider[v].getValue(); };
    lfoSectionComps[v].push_back (&lfo3RateSlider[v]);
    setupKnob (lfo3DepthSlider[v], 0.0, 1.0, vp.lfo3Depth);
    lfo3DepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo3Depth = (float)lfo3DepthSlider[v].getValue(); };
    lfoSectionComps[v].push_back (&lfo3DepthSlider[v]);
    addWaveItems (lfo3WaveBox[v]);
    lfo3WaveBox[v].setSelectedItemIndex (vp.lfo3Waveform, juce::dontSendNotification);
    lfo3WaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo3Waveform = lfo3WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo3WaveBox[v]);
    lfoSectionComps[v].push_back (&lfo3WaveBox[v]);
    addLFOTargetItems (lfo3TargetBox[v]);
    lfo3TargetBox[v].setSelectedItemIndex (vp.lfo3Target, juce::dontSendNotification);
    lfo3TargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo3Target = lfo3TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo3TargetBox[v]);
    lfoSectionComps[v].push_back (&lfo3TargetBox[v]);
    setupLFOSync (lfo3SyncBtn[v], lfo3SyncDivBox[v], vp.lfo3Sync, vp.lfo3SyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfo3Sync = s; });
    lfoSectionComps[v].push_back (&lfo3SyncBtn[v]);
    lfoSectionComps[v].push_back (&lfo3SyncDivBox[v]);
    lfo3SyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo3SyncDiv = lfo3SyncDivBox[v].getSelectedItemIndex(); };

    // LFO 4
    setupKnob (lfo4RateSlider[v], 0.1, 20.0, vp.lfo4Rate, 4.0);
    lfo4RateSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo4Rate  = (float)lfo4RateSlider[v].getValue(); };
    lfoSectionComps[v].push_back (&lfo4RateSlider[v]);
    setupKnob (lfo4DepthSlider[v], 0.0, 1.0, vp.lfo4Depth);
    lfo4DepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].lfo4Depth = (float)lfo4DepthSlider[v].getValue(); };
    lfoSectionComps[v].push_back (&lfo4DepthSlider[v]);
    addWaveItems (lfo4WaveBox[v]);
    lfo4WaveBox[v].setSelectedItemIndex (vp.lfo4Waveform, juce::dontSendNotification);
    lfo4WaveBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo4Waveform = lfo4WaveBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo4WaveBox[v]);
    lfoSectionComps[v].push_back (&lfo4WaveBox[v]);
    addLFOTargetItems (lfo4TargetBox[v]);
    lfo4TargetBox[v].setSelectedItemIndex (vp.lfo4Target, juce::dontSendNotification);
    lfo4TargetBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo4Target = lfo4TargetBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (lfo4TargetBox[v]);
    lfoSectionComps[v].push_back (&lfo4TargetBox[v]);
    setupLFOSync (lfo4SyncBtn[v], lfo4SyncDivBox[v], vp.lfo4Sync, vp.lfo4SyncDiv,
                  [this,v](bool s){ audioProcessor.voice[v].lfo4Sync = s; });
    lfoSectionComps[v].push_back (&lfo4SyncBtn[v]);
    lfoSectionComps[v].push_back (&lfo4SyncDivBox[v]);
    lfo4SyncDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].lfo4SyncDiv = lfo4SyncDivBox[v].getSelectedItemIndex(); };

    // Mod Envelope
    setupKnob (modEnvAtkSlider  [v], 0.001, 4.0, vp.modEnv.attack,  0.3);
    modEnvAtkSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.attack  = (float)modEnvAtkSlider  [v].getValue(); };
    envSectionComps[v].push_back (&modEnvAtkSlider[v]);
    setupKnob (modEnvDecSlider  [v], 0.001, 4.0, vp.modEnv.decay,   0.3);
    modEnvDecSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.decay   = (float)modEnvDecSlider  [v].getValue(); };
    envSectionComps[v].push_back (&modEnvDecSlider[v]);
    setupKnob (modEnvSusSlider  [v], 0.0,   1.0, vp.modEnv.sustain);
    modEnvSusSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.sustain = (float)modEnvSusSlider  [v].getValue(); };
    envSectionComps[v].push_back (&modEnvSusSlider[v]);
    setupKnob (modEnvRelSlider  [v], 0.001, 4.0, vp.modEnv.release, 0.3);
    modEnvRelSlider  [v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.release = (float)modEnvRelSlider  [v].getValue(); };
    envSectionComps[v].push_back (&modEnvRelSlider[v]);
    setupKnob (modEnvDepthSlider[v], 0.0,   1.0, vp.modEnv.depth);
    modEnvDepthSlider[v].onValueChange = [this,v]() { audioProcessor.voice[v].modEnv.depth   = (float)modEnvDepthSlider[v].getValue(); };
    envSectionComps[v].push_back (&modEnvDepthSlider[v]);

    modEnvDestBox[v].addItem ("FM Depth", 1); modEnvDestBox[v].addItem ("Pitch", 2); modEnvDestBox[v].addItem ("Filter", 3);
    modEnvDestBox[v].addItem ("PL Harm",  4); modEnvDestBox[v].addItem ("PL Timb", 5); modEnvDestBox[v].addItem ("PL Morph", 6);
    modEnvDestBox[v].setSelectedItemIndex (vp.modEnv.dest, juce::dontSendNotification);
    modEnvDestBox[v].onChange = [this,v]() { audioProcessor.voice[v].modEnv.dest = modEnvDestBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (modEnvDestBox[v]);
    envSectionComps[v].push_back (&modEnvDestBox[v]);

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
    envSectionComps[v].push_back (&modEnvSyncBtn[v]);

    addCenvDivItems (modEnvDivBox[v]);
    modEnvDivBox[v].setSelectedItemIndex (vp.modEnv.clockDiv, juce::dontSendNotification);
    modEnvDivBox[v].onChange = [this,v]() { audioProcessor.voice[v].modEnv.clockDiv = modEnvDivBox[v].getSelectedItemIndex(); };
    addAndMakeVisible (modEnvDivBox[v]);
    envSectionComps[v].push_back (&modEnvDivBox[v]);

    addAndMakeVisible (*oscScope[v]);
    // oscScope stays permanently visible on front page — NOT in oscSectionComps
    addAndMakeVisible (*wavetableDisplay[v]);
    oscSectionComps[v].push_back (wavetableDisplay[v].get());  // wavetable display is OSC panel-only

    // ── MIDI Out ──────────────────────────────────────────────────────────────
    midiOutBtn[v].setButtonText (vp.midiOutEnabled ? "MIDI OUT" : "MIDI OUT");
    midiOutBtn[v].setClickingTogglesState (true);
    midiOutBtn[v].setToggleState (vp.midiOutEnabled, juce::dontSendNotification);
    midiOutBtn[v].setColour (juce::TextButton::buttonColourId,
                             vp.midiOutEnabled ? juce::Colour(0xff228844) : juce::Colour(0xff161630));
    midiOutBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff228844));
    midiOutBtn[v].onClick = [this, v]()
    {
        bool en = midiOutBtn[v].getToggleState();
        audioProcessor.voice[v].midiOutEnabled = en;
        midiOutBtn[v].setColour (juce::TextButton::buttonColourId,
                                 en ? juce::Colour(0xff228844) : juce::Colour(0xff161630));
        midiOutChBox[v].setEnabled (en);
    };
    addAndMakeVisible (midiOutBtn[v]);
    synthPageComponents.push_back (&midiOutBtn[v]);
    synthPageComponents.push_back (&randModeBtn[v]);

    // ── MIDI / VOICE config radio (one config group visible at a time) ────────
    {
        auto styleRadio = [](juce::TextButton& b, const juce::String& t)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff161630));
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2255aa));
            b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffa0a0b4));
            b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
        };
        styleRadio (midiViewBtn[v],  "MIDI");
        styleRadio (voiceViewBtn[v], "VOICE");
        midiViewBtn[v].onClick  = [this, v]() { cfgView[v] = 0; applyCfgView (v); };
        voiceViewBtn[v].onClick = [this, v]() { cfgView[v] = 1; applyCfgView (v); };
        addAndMakeVisible (midiViewBtn[v]);  synthPageComponents.push_back (&midiViewBtn[v]);
        addAndMakeVisible (voiceViewBtn[v]); synthPageComponents.push_back (&voiceViewBtn[v]);

        // TOOLS reveal toggle — collapses the secondary utility cluster
        // (RST/UNI · randomise · nudge · MIDI/VOICE config) behind one switch.
        toolsBtn[v].setButtonText (juce::String::fromUTF8 ("TOOLS"));
        toolsBtn[v].setClickingTogglesState (true);
        toolsBtn[v].setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff161630));
        toolsBtn[v].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5a3aa0));
        toolsBtn[v].setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffa0a0b4));
        toolsBtn[v].setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
        toolsBtn[v].onClick = [this, v]()
        {
            toolsView[v] = toolsBtn[v].getToggleState() ? 1 : 0;
            applyToolsView (v);
        };
        addAndMakeVisible (toolsBtn[v]); synthPageComponents.push_back (&toolsBtn[v]);

        // QUANT ↔ ORDER radio (shares one middle slot in the pattern section)
        auto styleMid = [](juce::TextButton& b, const juce::String& t)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff161630));
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2255aa));
            b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffa0a0b4));
            b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
        };
        styleMid (quantViewBtn[v], "QUANT");
        styleMid (orderViewBtn[v], "ORDER");
        quantViewBtn[v].onClick = [this, v]() { midView[v] = 0; applyMidView (v); };
        orderViewBtn[v].onClick = [this, v]() { midView[v] = 1; applyMidView (v); };
        addAndMakeVisible (quantViewBtn[v]); synthPageComponents.push_back (&quantViewBtn[v]);
        addAndMakeVisible (orderViewBtn[v]); synthPageComponents.push_back (&orderViewBtn[v]);
    }

    for (int ch = 1; ch <= 16; ++ch)
        midiOutChBox[v].addItem ("Ch " + juce::String (ch), ch);
    midiOutChBox[v].setSelectedId (vp.midiOutChannel, juce::dontSendNotification);
    midiOutChBox[v].setEnabled (vp.midiOutEnabled);
    midiOutChBox[v].onChange = [this, v]()
    {
        audioProcessor.voice[v].midiOutChannel = midiOutChBox[v].getSelectedId();
    };
    addAndMakeVisible (midiOutChBox[v]);
    synthPageComponents.push_back (&midiOutChBox[v]);

    // ── Voice mode (Unison / Poly) ────────────────────────────────────────────
    voiceModeBox[v].addItem ("MONO",   1);
    voiceModeBox[v].addItem ("UNISON", 2);
    voiceModeBox[v].addItem ("POLY",   3);
    voiceModeBox[v].setSelectedId (vp.voiceMode + 1, juce::dontSendNotification);
    voiceModeBox[v].onChange = [this, v]()
    {
        audioProcessor.voice[v].voiceMode =
            (VoltageSeq2AudioProcessor::VoiceParams::VoiceMode)(voiceModeBox[v].getSelectedId() - 1);
    };
    addAndMakeVisible (voiceModeBox[v]);
    synthPageComponents.push_back (&voiceModeBox[v]);

    uniCountBtn[v].setButtonText (vp.unisonCount == 2 ? "2V" : "4V");
    uniCountBtn[v].onClick = [this, v]()
    {
        int cur = audioProcessor.voice[v].unisonCount;
        audioProcessor.voice[v].unisonCount = (cur == 2) ? 4 : 2;
        uniCountBtn[v].setButtonText (audioProcessor.voice[v].unisonCount == 2 ? "2V" : "4V");
    };
    addAndMakeVisible (uniCountBtn[v]);
    synthPageComponents.push_back (&uniCountBtn[v]);

    // ── Chord mode button (Poly shift-register: re-fire full register each step) ──
    chordModeBtn[v].setButtonText ("CHD");
    chordModeBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
    chordModeBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
    chordModeBtn[v].onClick = [this, v]()
    {
        auto& vp = audioProcessor.voice[v];
        vp.shiftRegChordMode = !vp.shiftRegChordMode;
        chordModeBtn[v].setColour (juce::TextButton::buttonColourId,
            vp.shiftRegChordMode ? juce::Colour (0xff1a5533) : juce::Colour (0xff161630));
        chordModeBtn[v].setColour (juce::TextButton::textColourOffId,
            vp.shiftRegChordMode ? juce::Colour (0xff00ff88) : juce::Colour (0xffe0e0e0));
    };
    addAndMakeVisible (chordModeBtn[v]);
    synthPageComponents.push_back (&chordModeBtn[v]);

    uniSpreadSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    uniSpreadSlider[v].setRange (0.0, 0.5, 0.001);
    uniSpreadSlider[v].setValue (vp.unisonSpread, juce::dontSendNotification);
    uniSpreadSlider[v].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    uniSpreadSlider[v].onValueChange = [this, v]()
    {
        audioProcessor.voice[v].unisonSpread = (float)uniSpreadSlider[v].getValue();
    };
    addAndMakeVisible (uniSpreadSlider[v]);
    synthPageComponents.push_back (&uniSpreadSlider[v]);

    uniWidthSlider[v].setSliderStyle (juce::Slider::LinearHorizontal);
    uniWidthSlider[v].setRange (0.0, 1.0, 0.01);
    uniWidthSlider[v].setValue (vp.unisonWidth, juce::dontSendNotification);
    uniWidthSlider[v].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    uniWidthSlider[v].onValueChange = [this, v]()
    {
        audioProcessor.voice[v].unisonWidth = (float)uniWidthSlider[v].getValue();
    };
    addAndMakeVisible (uniWidthSlider[v]);
    synthPageComponents.push_back (&uniWidthSlider[v]);

    // ── PLAITS toggle ─────────────────────────────────────────────────────────
    plaitsBtn[v].setButtonText ("PLAITS");
    plaitsBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    plaitsBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
    plaitsBtn[v].onClick = [this, v]()
    {
        audioProcessor.voice[v].plaitsEnabled = !audioProcessor.voice[v].plaitsEnabled;
        refreshPlaitsMode (v);
    };
    addAndMakeVisible (plaitsBtn[v]);
    synthPageComponents.push_back (&plaitsBtn[v]);

    // ── Engine selector ───────────────────────────────────────────────────────
    for (int i = 0; i < 24; ++i)
        plaitsEngBox[v].addItem (juce::String(i + 1) + "  " +
                                 VoltageSeq2AudioProcessor::kPlaitsEngineNames[i], i + 1);
    plaitsEngBox[v].setSelectedId (audioProcessor.voice[v].plaitsEngine + 1,
                                   juce::dontSendNotification);
    plaitsEngBox[v].onChange = [this, v]()
    {
        audioProcessor.voice[v].plaitsEngine = plaitsEngBox[v].getSelectedId() - 1;
    };
    plaitsEngBox[v].setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0c0c1c));
    plaitsEngBox[v].setColour (juce::ComboBox::textColourId,       juce::Colour (0xffe0e0e0));
    addAndMakeVisible (plaitsEngBox[v]);
    synthPageComponents.push_back (&plaitsEngBox[v]);
    oscSectionComps[v].push_back  (&plaitsEngBox[v]);

    // ── Plaits parameter sliders ──────────────────────────────────────────────
    auto setupPlaitsSlider = [&](juce::Slider& sl, float val)
    {
        sl.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        sl.setRange (0.0, 1.0, 0.001);
        sl.setValue (val, juce::dontSendNotification);
        sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff00d4aa));
        sl.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1a3030));
        addAndMakeVisible (sl);
    };

    setupPlaitsSlider (plaitsHarmSlider[v], 0.5f);
    plaitsHarmAttach[v] = std::make_unique<SliderAtt> (
        audioProcessor.apvts, "plaitsHarm_" + juce::String(v), plaitsHarmSlider[v]);
    synthPageComponents.push_back (&plaitsHarmSlider[v]);
    oscSectionComps[v].push_back  (&plaitsHarmSlider[v]);

    setupPlaitsSlider (plaitsTimSlider[v], 0.5f);
    plaitsTimbAttach[v] = std::make_unique<SliderAtt> (
        audioProcessor.apvts, "plaitsTimb_" + juce::String(v), plaitsTimSlider[v]);
    synthPageComponents.push_back (&plaitsTimSlider[v]);
    oscSectionComps[v].push_back  (&plaitsTimSlider[v]);

    setupPlaitsSlider (plaitsMorphSlider[v], 0.5f);
    plaitsMorphAttach[v] = std::make_unique<SliderAtt> (
        audioProcessor.apvts, "plaitsMorph_" + juce::String(v), plaitsMorphSlider[v]);
    synthPageComponents.push_back (&plaitsMorphSlider[v]);
    oscSectionComps[v].push_back  (&plaitsMorphSlider[v]);

    setupPlaitsSlider (plaitsAuxSlider[v], audioProcessor.voice[v].plaitsAuxBlend);
    plaitsAuxSlider[v].onValueChange = [this, v]()
        { audioProcessor.voice[v].plaitsAuxBlend = (float)plaitsAuxSlider[v].getValue(); };
    synthPageComponents.push_back (&plaitsAuxSlider[v]);
    oscSectionComps[v].push_back  (&plaitsAuxSlider[v]);

    // ── TRIG mode button ──────────────────────────────────────────────────────
    // OFF (default) = free-running: Plaits outputs continuously, ADSR shapes amplitude.
    // ON            = triggered:    Plaits fires its internal LPG on each gate (strings/modal/drums).
    plaitsTrigBtn[v].setButtonText ("TRIG");
    plaitsTrigBtn[v].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    plaitsTrigBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
    plaitsTrigBtn[v].onClick = [this, v]()
    {
        audioProcessor.voice[v].plaitsTrigMode = !audioProcessor.voice[v].plaitsTrigMode;
        refreshPlaitsMode (v);
    };
    addAndMakeVisible (plaitsTrigBtn[v]);
    synthPageComponents.push_back (&plaitsTrigBtn[v]);
    oscSectionComps[v].push_back  (&plaitsTrigBtn[v]);

    // ── Plaits octave transpose ───────────────────────────────────────────────
    plaitsOctBox[v].addItem ("-2 oct", 1);
    plaitsOctBox[v].addItem ("-1 oct", 2);
    plaitsOctBox[v].addItem (" 0  oct", 3);
    plaitsOctBox[v].addItem ("+1 oct", 4);
    plaitsOctBox[v].addItem ("+2 oct", 5);
    plaitsOctBox[v].setSelectedId (audioProcessor.voice[v].plaitsOctave + 3,
                                   juce::dontSendNotification);
    plaitsOctBox[v].setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0c0c1c));
    plaitsOctBox[v].setColour (juce::ComboBox::textColourId,       juce::Colour (0xffcc88ff));
    plaitsOctBox[v].onChange = [this, v]()
    {
        audioProcessor.voice[v].plaitsOctave = plaitsOctBox[v].getSelectedId() - 3;
    };
    addAndMakeVisible (plaitsOctBox[v]);
    synthPageComponents.push_back (&plaitsOctBox[v]);
    oscSectionComps[v].push_back  (&plaitsOctBox[v]);

    refreshPlaitsMode (v);
}

void VoltageSeq2AudioProcessorEditor::refreshPlaitsMode (int v)
{
    const bool on   = audioProcessor.voice[v].plaitsEnabled;
    const bool trig = audioProcessor.voice[v].plaitsTrigMode;

    // PLAITS toggle button colour
    plaitsBtn[v].setColour (juce::TextButton::buttonColourId,
        on ? juce::Colour (0xff2a0055) : juce::Colour (0xff161630));
    plaitsBtn[v].setColour (juce::TextButton::textColourOffId,
        on ? juce::Colour (0xffcc88ff) : juce::Colour (0xffe0e0e0));

    // TRIG button colour
    plaitsTrigBtn[v].setColour (juce::TextButton::buttonColourId,
        trig ? juce::Colour (0xff442200) : juce::Colour (0xff161630));
    plaitsTrigBtn[v].setColour (juce::TextButton::textColourOffId,
        trig ? juce::Colour (0xffff8844) : juce::Colour (0xffe0e0e0));

    // All OSC / Plaits control visibility now flows through the inline view toggle.
    refreshOscView (v);

    macroOverlay.toFront (false);   // visible-knob set changed — refresh rings
    macroOverlay.repaint();
}

//==============================================================================
// setupGenControls — wire up Page 4 GENERATE controls (single shared generator)
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupGenControls()
{
    auto addGen = [&](juce::Component& c) {
        addChildComponent (c);
        c.setVisible (false);
        genPageComponents.push_back (&c);
    };

    // Helper to refresh target button colours
    auto refreshTargetBtns = [this]()
    {
        euclidVoiceABtn.setColour (juce::TextButton::buttonColourId,
            euclidTargetVoice == 0 ? juce::Colour (0xff1a4a88) : juce::Colour (0xff161630));
        euclidVoiceBBtn.setColour (juce::TextButton::buttonColourId,
            euclidTargetVoice == 1 ? juce::Colour (0xff5a2488) : juce::Colour (0xff161630));
    };

    // ── Target voice selectors ────────────────────────────────────────────────
    euclidVoiceABtn.setButtonText ("VOICE A");
    euclidVoiceABtn.onClick = [this, refreshTargetBtns]()
    {
        euclidTargetVoice = 0;
        refreshTargetBtns();
        repaint();
    };
    addGen (euclidVoiceABtn);

    euclidVoiceBBtn.setButtonText ("VOICE B");
    euclidVoiceBBtn.onClick = [this, refreshTargetBtns]()
    {
        euclidTargetVoice = 1;
        refreshTargetBtns();
        repaint();
    };
    addGen (euclidVoiceBBtn);
    refreshTargetBtns();

    // ── N: Steps (2–16) ──────────────────────────────────────────────────────
    euclidStepsSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    euclidStepsSlider.setRange (2.0, 16.0, 1.0);
    euclidStepsSlider.setValue (8.0, juce::dontSendNotification);
    euclidStepsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 32, 22);
    euclidStepsSlider.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xffe0e0e0));
    euclidStepsSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
    euclidStepsSlider.onValueChange = [this]()
    {
        const int n = (int)euclidStepsSlider.getValue();
        euclidHitsSlider.setRange (0.0, (double)(n * euclidR), 1.0);
        euclidHitsSlider.setValue (
            juce::jlimit (0.0, (double)(n * euclidR), euclidHitsSlider.getValue()),
            juce::dontSendNotification);
        repaint();
    };
    addGen (euclidStepsSlider);

    // ── K: Hits (0..N×R) ─────────────────────────────────────────────────────
    euclidHitsSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    euclidHitsSlider.setRange (0.0, 8.0, 1.0);
    euclidHitsSlider.setValue (4.0, juce::dontSendNotification);
    euclidHitsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 32, 22);
    euclidHitsSlider.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xffe0e0e0));
    euclidHitsSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x00000000));
    euclidHitsSlider.onValueChange = [this]() { repaint(); };
    addGen (euclidHitsSlider);

    // ── R: Max Ratchets (cycles 1→2→3→4→1) ──────────────────────────────────
    euclidRatchetBtn.setButtonText ("1");
    euclidRatchetBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a40));
    euclidRatchetBtn.onClick = [this]()
    {
        euclidR = (euclidR % 4) + 1;
        euclidRatchetBtn.setButtonText (juce::String (euclidR));
        const int n = (int)euclidStepsSlider.getValue();
        euclidHitsSlider.setRange (0.0, (double)(n * euclidR), 1.0);
        euclidHitsSlider.setValue (
            juce::jlimit (0.0, (double)(n * euclidR), euclidHitsSlider.getValue()),
            juce::dontSendNotification);
        repaint();
    };
    addGen (euclidRatchetBtn);

    // ── APPLY ─────────────────────────────────────────────────────────────────
    euclidApplyBtn.setButtonText ("APPLY");
    euclidApplyBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff163016));
    euclidApplyBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff44ee44));
    euclidApplyBtn.onClick = [this]()
    {
        const int n = (int)euclidStepsSlider.getValue();
        const int k = (int)euclidHitsSlider.getValue();
        audioProcessor.applyEuclidean (euclidTargetVoice, n, k, euclidR);
        syncUIFromProcessor();
    };
    addGen (euclidApplyBtn);

    // ────────────────────────────────────────────────────────────────────────
    // TURING MACHINE controls
    // ────────────────────────────────────────────────────────────────────────

    // Helper to refresh TM voice button colours
    auto refreshTMVoiceBtns = [this]()
    {
        const int tv = audioProcessor.tmTargetVoice.load();
        tmVoiceABtn.setColour (juce::TextButton::buttonColourId,
            tv == 0 ? juce::Colour (0xff1a4a88) : juce::Colour (0xff161630));
        tmVoiceBBtn.setColour (juce::TextButton::buttonColourId,
            tv == 1 ? juce::Colour (0xff5a2488) : juce::Colour (0xff161630));
    };

    // TM voice A selector
    tmVoiceABtn.setButtonText ("VOICE A");
    tmVoiceABtn.onClick = [this, refreshTMVoiceBtns]()
    {
        audioProcessor.tmTargetVoice.store (0);
        refreshTMVoiceBtns();
        syncTMControlsFromVoice();
        repaint();
    };
    addGen (tmVoiceABtn);

    // TM voice B selector
    tmVoiceBBtn.setButtonText ("VOICE B");
    tmVoiceBBtn.onClick = [this, refreshTMVoiceBtns]()
    {
        audioProcessor.tmTargetVoice.store (1);
        refreshTMVoiceBtns();
        syncTMControlsFromVoice();
        repaint();
    };
    addGen (tmVoiceBBtn);
    refreshTMVoiceBtns();

    // LOCK knob — large rotary, the visual centrepiece
    tmLockKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tmLockKnob.setRange (0.0, 1.0, 0.001);
    tmLockKnob.setValue (0.75, juce::dontSendNotification);
    tmLockKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    tmLockKnob.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff00d4aa));
    tmLockKnob.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1a3030));
    tmLockKnob.onValueChange = [this]()
    {
        audioProcessor.turingMachine.lockAmount = (float)tmLockKnob.getValue();
    };
    addGen (tmLockKnob);

    // LENGTH stepper
    tmLengthUpBtn.setButtonText ("+");
    tmLengthUpBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a40));
    tmLengthUpBtn.onClick = [this]()
    {
        audioProcessor.turingMachine.length = juce::jlimit (2, 16, audioProcessor.turingMachine.length + 1);
        repaint();
    };
    addGen (tmLengthUpBtn);

    tmLengthDnBtn.setButtonText ("-");
    tmLengthDnBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a40));
    tmLengthDnBtn.onClick = [this]()
    {
        audioProcessor.turingMachine.length = juce::jlimit (2, 16, audioProcessor.turingMachine.length - 1);
        repaint();
    };
    addGen (tmLengthDnBtn);

    // RESET
    tmResetBtn.setButtonText ("RESET");
    tmResetBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1a1a));
    tmResetBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc4444));
    tmResetBtn.onClick = [this]()
    {
        audioProcessor.resetTuringMachine();
        syncTMControlsFromVoice();
        repaint();
    };
    addGen (tmResetBtn);

    // MODE: PITCH / GATE+PITCH
    tmPitchModeBtn.setButtonText ("PITCH");
    tmPitchModeBtn.onClick = [this]()
    {
        audioProcessor.turingMachine.affectGates = false;
        tmPitchModeBtn    .setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2255aa));
        tmGatePitchModeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
    };
    addGen (tmPitchModeBtn);

    tmGatePitchModeBtn.setButtonText ("GATE+PITCH");
    tmGatePitchModeBtn.onClick = [this]()
    {
        audioProcessor.turingMachine.affectGates = true;
        tmPitchModeBtn    .setColour (juce::TextButton::buttonColourId, juce::Colour (0xff161630));
        tmGatePitchModeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2255aa));
    };
    addGen (tmGatePitchModeBtn);

    // WRITE toggle
    tmWriteBtn.setButtonText ("WRITE");
    tmWriteBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff161630));
    tmWriteBtn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffe0e0e0));
    tmWriteBtn.onClick = [this]()
    {
        auto& tm = audioProcessor.turingMachine;
        tm.writeEnabled = !tm.writeEnabled;
        tmWriteBtn.setColour (juce::TextButton::buttonColourId,
            tm.writeEnabled ? juce::Colour (0xff006644) : juce::Colour (0xff161630));
        tmWriteBtn.setColour (juce::TextButton::textColourOffId,
            tm.writeEnabled ? juce::Colour (0xff00ffaa) : juce::Colour (0xffe0e0e0));
    };
    addGen (tmWriteBtn);

    // CAPTURE
    tmCaptureBtn.setButtonText ("CAPTURE");
    tmCaptureBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff163016));
    tmCaptureBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff44ee44));
    tmCaptureBtn.onClick = [this]()
    {
        audioProcessor.captureTuringMachine();
        // captureTuringMachine() sets writeEnabled=false — reflect that in the WRITE button
        tmWriteBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
        tmWriteBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
        syncUIFromProcessor();
    };
    addGen (tmCaptureBtn);

    // Bit register cells (16 clickable toggles)
    for (int i = 0; i < 16; ++i)
    {
        tmBitBtn[i].setButtonText ("");
        tmBitBtn[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0e0e1c));
        tmBitBtn[i].onClick = [this, i]()
        {
            auto& tm = audioProcessor.turingMachine;
            tm.bits[i] = !tm.bits[i];
            const juce::Colour vc = (audioProcessor.tmTargetVoice.load() == 0)
                                    ? juce::Colour (0xff00aaff)
                                    : juce::Colour (0xffaa44ff);
            tmBitBtn[i].setColour (juce::TextButton::buttonColourId,
                tm.bits[i] ? vc.withAlpha (0.7f) : juce::Colour (0xff0e0e1c));
            repaint();
        };
        addGen (tmBitBtn[i]);
    }

    // Sync initial state
    syncTMControlsFromVoice();
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::syncTMControlsFromVoice()
{
    const auto& tm = audioProcessor.turingMachine;
    const int   tv = audioProcessor.tmTargetVoice.load();
    const juce::Colour vc = (tv == 0)
        ? juce::Colour (0xff00aaff)
        : juce::Colour (0xffaa44ff);

    // Voice selector buttons
    tmVoiceABtn.setColour (juce::TextButton::buttonColourId,
        tv == 0 ? juce::Colour (0xff1a4a88) : juce::Colour (0xff161630));
    tmVoiceBBtn.setColour (juce::TextButton::buttonColourId,
        tv == 1 ? juce::Colour (0xff5a2488) : juce::Colour (0xff161630));

    tmLockKnob.setValue (tm.lockAmount, juce::dontSendNotification);

    // Mode buttons
    tmPitchModeBtn    .setColour (juce::TextButton::buttonColourId,
        !tm.affectGates ? juce::Colour (0xff2255aa) : juce::Colour (0xff161630));
    tmGatePitchModeBtn.setColour (juce::TextButton::buttonColourId,
         tm.affectGates ? juce::Colour (0xff2255aa) : juce::Colour (0xff161630));

    // WRITE button
    tmWriteBtn.setColour (juce::TextButton::buttonColourId,
        tm.writeEnabled ? juce::Colour (0xff006644) : juce::Colour (0xff161630));
    tmWriteBtn.setColour (juce::TextButton::textColourOffId,
        tm.writeEnabled ? juce::Colour (0xff00ffaa) : juce::Colour (0xffe0e0e0));

    // Bit cells
    for (int i = 0; i < 16; ++i)
        tmBitBtn[i].setColour (juce::TextButton::buttonColourId,
            tm.bits[i] ? vc.withAlpha (0.7f) : juce::Colour (0xff0e0e1c));
}

//==============================================================================
// layoutGenPage — position Page 4 controls (single panel)
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutGenPage()
{
    constexpr int panelX  = 10;
    constexpr int panelY  = headerH + 10;
    constexpr int ctrlX   = panelX + 24;
    constexpr int sliderX = ctrlX + 120;
    constexpr int sliderW = 340;
    constexpr int sliderH = 26;

    // Target voice buttons (side by side)
    euclidVoiceABtn.setBounds (sliderX,      panelY + 42, 100, 26);
    euclidVoiceBBtn.setBounds (sliderX + 108, panelY + 42, 100, 26);

    // Sliders
    euclidStepsSlider.setBounds (sliderX, panelY + 90,  sliderW, sliderH);
    euclidHitsSlider .setBounds (sliderX, panelY + 140, sliderW, sliderH);

    // Ratchet cycler
    euclidRatchetBtn .setBounds (sliderX, panelY + 190, 70, sliderH);

    // APPLY — prominent, below controls
    euclidApplyBtn   .setBounds (sliderX, panelY + 240, 150, 50);

    // ── Turing Machine layout ────────────────────────────────────────────────
    // TM section starts below euclidean (y ≈ panelY + 310)
    constexpr int tmY       = panelY + 310;   // top of TM section
    constexpr int knobSize  = 120;
    constexpr int knobX     = panelX + 24;    // left-align with euclidean labels

    // Large LOCK knob
    tmLockKnob.setBounds (knobX, tmY + 30, knobSize, knobSize);

    // TM voice selectors (above controls, same row as section title)
    constexpr int tmCtrlX = knobX + knobSize + 18;
    tmVoiceABtn      .setBounds (tmCtrlX,       tmY + 4,   90, 22);
    tmVoiceBBtn      .setBounds (tmCtrlX + 96,  tmY + 4,   90, 22);

    // Controls to the right of the knob
    tmLengthDnBtn    .setBounds (tmCtrlX,       tmY + 34, 28, 26);
    tmLengthUpBtn    .setBounds (tmCtrlX + 32,  tmY + 34, 28, 26);
    tmResetBtn       .setBounds (tmCtrlX + 68,  tmY + 34, 70, 26);
    tmPitchModeBtn   .setBounds (tmCtrlX,       tmY + 68, 70, 26);
    tmGatePitchModeBtn.setBounds(tmCtrlX + 78,  tmY + 68, 90, 26);
    tmWriteBtn       .setBounds (tmCtrlX,       tmY + 102, 90, 30);
    tmCaptureBtn     .setBounds (tmCtrlX + 98,  tmY + 102, 90, 30);

    // Bit register cells: 16 cells across the right portion, 3 rows below
    constexpr int tmDisplayX = 530;
    constexpr int tmDisplayW = 1480 - tmDisplayX;       // 950 px
    const     int tmCellW    = tmDisplayW / 16;          // ~59 px
    constexpr int tmCellH    = 52;
    constexpr int tmBitRowY  = tmY + 28;

    for (int i = 0; i < 16; ++i)
        tmBitBtn[i].setBounds (tmDisplayX + i * tmCellW, tmBitRowY,
                               tmCellW - 3, tmCellH);
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
    // ── Helper: add to fxPageComponents (initially hidden) ───────────────────
    auto addFx = [&](juce::Component& c) {
        addChildComponent (c);
        c.setVisible (false);
        fxPageComponents.push_back (&c);
    };

    // ── Voice A / B tab buttons ───────────────────────────────────────────────
    auto setupVoiceTab = [&](juce::TextButton& btn, int vi, const char* label)
    {
        btn.setButtonText (label);
        btn.setClickingTogglesState (false);
        btn.setColour (juce::TextButton::buttonColourId,
                       vi == 0 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
        btn.onClick = [this, vi]()
        {
            fxVoiceTab = vi;
            fxVoiceABtn.setColour (juce::TextButton::buttonColourId,
                                   vi == 0 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
            fxVoiceBBtn.setColour (juce::TextButton::buttonColourId,
                                   vi == 1 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
            syncFxPageFromVoice();
        };
        addFx (btn);
    };
    setupVoiceTab (fxVoiceABtn, 0, "VOICE A");
    setupVoiceTab (fxVoiceBBtn, 1, "VOICE B");

    // ── Bypass button ─────────────────────────────────────────────────────────
    fxBypassBtn.setButtonText ("BYPASS");
    fxBypassBtn.setClickingTogglesState (true);
    fxBypassBtn.setToggleState (false, juce::dontSendNotification);
    fxBypassBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour(0xff161630));
    fxBypassBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xffaa3322));
    fxBypassBtn.onClick = [this]()
    {
        bool b = fxBypassBtn.getToggleState();
        audioProcessor.fx[fxVoiceTab].fxBypass = b;
        fxBypassBtn.setColour (juce::TextButton::buttonColourId,
                               b ? juce::Colour(0xffaa3322) : juce::Colour(0xff161630));
    };
    addFx (fxBypassBtn);

    // ── Delay ─────────────────────────────────────────────────────────────────
    // Controls read/write audioProcessor.fx[fxVoiceTab] at the time of interaction.
    delayOnBtn.setButtonText ("OFF");
    delayOnBtn.setClickingTogglesState (true);
    delayOnBtn.setColour (juce::TextButton::buttonColourId,   gateOffColour);
    delayOnBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    delayOnBtn.onClick = [this]() {
        bool s = delayOnBtn.getToggleState();
        audioProcessor.fx[fxVoiceTab].delayOn = s;
        delayOnBtn.setButtonText (s ? "ON" : "OFF");
        delayOnBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addFx (delayOnBtn);

    delaySyncBtn.setButtonText ("SYNC");
    delaySyncBtn.setClickingTogglesState (true);
    delaySyncBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour(0xffe09040));
    delaySyncBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xffe09040));
    delaySyncBtn.onClick = [this]() {
        bool s = delaySyncBtn.getToggleState();
        audioProcessor.fx[fxVoiceTab].delaySync = s;
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
    delaySyncDivBox.onChange = [this]() { audioProcessor.fx[fxVoiceTab].delaySyncDiv = delaySyncDivBox.getSelectedItemIndex(); };
    delaySyncDivBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour(0xff0e1020));
    delaySyncDivBox.setColour (juce::ComboBox::textColourId,       juce::Colour(0xffe0e0e0));
    addFx (delaySyncDivBox);

    setupKnob (delayTimeMsSlider, 1.0, 2000.0, 375.0);
    delayTimeMsSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delayTimeMs = (float)delayTimeMsSlider.getValue(); };
    addFx (delayTimeMsSlider);

    setupKnob (delayFeedbackSlider, 0.0, 0.95, 0.40);
    delayFeedbackSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delayFeedback = (float)delayFeedbackSlider.getValue(); };
    addFx (delayFeedbackSlider);

    delayPingPongBtn.setButtonText ("PING");
    delayPingPongBtn.setClickingTogglesState (true);
    delayPingPongBtn.setColour (juce::TextButton::buttonColourId,   gateOffColour);
    delayPingPongBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour(0xff5566dd));
    delayPingPongBtn.onClick = [this]() {
        bool s = delayPingPongBtn.getToggleState();
        audioProcessor.fx[fxVoiceTab].delayPingPong = s;
        delayPingPongBtn.setColour (juce::TextButton::buttonColourId, s ? juce::Colour(0xff5566dd) : gateOffColour);
    };
    addFx (delayPingPongBtn);

    setupKnob (delayMixSlider, 0.0, 1.0, 0.30);
    delayMixSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delayMix = (float)delayMixSlider.getValue(); };
    addFx (delayMixSlider);

    // Tape character knobs
    setupKnob (delayWowSlider, 0.0, 1.0, 0.0);
    delayWowSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delayWow = (float)delayWowSlider.getValue(); };
    addFx (delayWowSlider);

    setupKnob (delayFlutterSlider, 0.0, 1.0, 0.0);
    delayFlutterSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delayFlutter = (float)delayFlutterSlider.getValue(); };
    addFx (delayFlutterSlider);

    setupKnob (delaySatSlider, 0.0, 1.0, 0.0);
    delaySatSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delaySat = (float)delaySatSlider.getValue(); };
    addFx (delaySatSlider);

    // Bernoulli gate probability knob — amber accent to signal it's special
    setupKnob (delayProbSlider, 0.0, 1.0, 1.0);
    delayProbSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe09040));
    delayProbSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].delayProb = (float)delayProbSlider.getValue(); };
    addFx (delayProbSlider);

    // ── Reverb ────────────────────────────────────────────────────────────────
    reverbOnBtn.setButtonText ("OFF");
    reverbOnBtn.setClickingTogglesState (true);
    reverbOnBtn.setColour (juce::TextButton::buttonColourId,   gateOffColour);
    reverbOnBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    reverbOnBtn.onClick = [this]() {
        bool s = reverbOnBtn.getToggleState();
        audioProcessor.fx[fxVoiceTab].reverbOn = s;
        reverbOnBtn.setButtonText (s ? "ON" : "OFF");
        reverbOnBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addFx (reverbOnBtn);

    setupKnob (reverbSizeSlider,     0.0, 1.0, 0.75);
    reverbSizeSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].reverbSize = (float)reverbSizeSlider.getValue(); };
    addFx (reverbSizeSlider);

    setupKnob (reverbDampingSlider,  0.0, 1.0, 0.40);
    reverbDampingSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].reverbDamping = (float)reverbDampingSlider.getValue(); };
    addFx (reverbDampingSlider);

    setupKnob (reverbPreDelaySlider, 0.0, 100.0, 20.0);
    reverbPreDelaySlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].reverbPreDelay = (float)reverbPreDelaySlider.getValue(); };
    addFx (reverbPreDelaySlider);

    setupKnob (reverbMixSlider, 0.0, 1.0, 0.25);
    reverbMixSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].reverbMix = (float)reverbMixSlider.getValue(); };
    addFx (reverbMixSlider);

    // ── Chorus ────────────────────────────────────────────────────────────────
    chorusOnBtn.setButtonText ("OFF");
    chorusOnBtn.setClickingTogglesState (true);
    chorusOnBtn.setColour (juce::TextButton::buttonColourId,   gateOffColour);
    chorusOnBtn.setColour (juce::TextButton::buttonOnColourId, gateOnColour);
    chorusOnBtn.onClick = [this]() {
        bool s = chorusOnBtn.getToggleState();
        audioProcessor.fx[fxVoiceTab].chorusOn = s;
        chorusOnBtn.setButtonText (s ? "ON" : "OFF");
        chorusOnBtn.setColour (juce::TextButton::buttonColourId, s ? gateOnColour : gateOffColour);
    };
    addFx (chorusOnBtn);

    setupKnob (chorusRateSlider,  0.1, 5.0, 0.50);
    chorusRateSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].chorusRate = (float)chorusRateSlider.getValue(); };
    addFx (chorusRateSlider);

    setupKnob (chorusDepthSlider, 0.0, 1.0, 0.50);
    chorusDepthSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].chorusDepth = (float)chorusDepthSlider.getValue(); };
    addFx (chorusDepthSlider);

    setupKnob (chorusMixSlider, 0.0, 1.0, 0.50);
    chorusMixSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].chorusMix = (float)chorusMixSlider.getValue(); };
    addFx (chorusMixSlider);

    // ── Master ────────────────────────────────────────────────────────────────
    setupKnob (masterDriveSlider, 0.0, 1.0, 0.0);
    masterDriveSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].masterDrive = (float)masterDriveSlider.getValue(); };
    addFx (masterDriveSlider);

    setupKnob (masterGainSlider, 0.0, 2.0, 1.0);
    masterGainSlider.onValueChange = [this]() { audioProcessor.fx[fxVoiceTab].masterGain = (float)masterGainSlider.getValue(); };
    addFx (masterGainSlider);

    // Sync controls to Voice A defaults at startup.
    syncFxPageFromVoice();
}

//==============================================================================
// layoutFxPage — size and position all FX page controls
//==============================================================================
void VoltageSeq2AudioProcessorEditor::layoutFxPage()
{
    // Voice tab row — sits just below the nav bar (nav bar ends at y=25)
    constexpr int tabY = 30, tabH = 24;
    fxVoiceABtn .setBounds (10,  tabY, 100, tabH);
    fxVoiceBBtn .setBounds (120, tabY, 100, tabH);
    fxBypassBtn .setBounds (240, tabY, 100, tabH);

    // Four panels across the page: DELAY | REVERB | CHORUS | MASTER
    // Controls start at py=90, leaving ~30px gap below the tab row
    constexpr int py = 90, dR2 = 90 + 150;

    // DELAY — two rows, self-contained in x=10..440
    constexpr int dX  = 10;
    constexpr int dR3 = dR2 + 76;    // second knob row (tape char + gate)
    delayOnBtn         .setBounds (dX,      py+10,  80, 26);
    delaySyncBtn       .setBounds (dX+90,   py+10,  80, 26);
    delaySyncDivBox    .setBounds (dX+180,  py+10, 100, 26);
    delayPingPongBtn   .setBounds (dX+290,  py+10,  80, 26);
    // Row 1: core controls
    delayTimeMsSlider  .setBounds (dX+10,   dR2,    52, 52);
    delayFeedbackSlider.setBounds (dX+80,   dR2,    52, 52);
    delayMixSlider     .setBounds (dX+150,  dR2,    52, 52);
    // Row 2: tape character + Bernoulli gate
    delayWowSlider     .setBounds (dX+10,   dR3,    52, 52);
    delayFlutterSlider .setBounds (dX+80,   dR3,    52, 52);
    delaySatSlider     .setBounds (dX+150,  dR3,    52, 52);
    delayProbSlider    .setBounds (dX+240,  dR3,    52, 52);

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
// syncFxPageFromVoice — refresh all FX controls from audioProcessor.fx[fxVoiceTab]
//==============================================================================
void VoltageSeq2AudioProcessorEditor::syncFxPageFromVoice()
{
    const auto& p = audioProcessor.fx[fxVoiceTab];

    // Voice tab highlight
    fxVoiceABtn.setColour (juce::TextButton::buttonColourId,
                           fxVoiceTab == 0 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
    fxVoiceBBtn.setColour (juce::TextButton::buttonColourId,
                           fxVoiceTab == 1 ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));

    // Bypass
    fxBypassBtn.setToggleState (p.fxBypass, juce::dontSendNotification);
    fxBypassBtn.setColour (juce::TextButton::buttonColourId,
                           p.fxBypass ? juce::Colour(0xffaa3322) : juce::Colour(0xff161630));

    // Delay
    delayOnBtn.setToggleState (p.delayOn, juce::dontSendNotification);
    delayOnBtn.setButtonText (p.delayOn ? "ON" : "OFF");
    delayOnBtn.setColour (juce::TextButton::buttonColourId, p.delayOn ? gateOnColour : gateOffColour);
    delaySyncBtn.setToggleState (p.delaySync, juce::dontSendNotification);
    delaySyncBtn.setButtonText (p.delaySync ? "SYNC" : "FREE");
    delaySyncBtn.setColour (juce::TextButton::buttonColourId,
                            p.delaySync ? juce::Colour(0xffe09040) : gateOffColour);
    delaySyncDivBox.setSelectedItemIndex (p.delaySyncDiv, juce::dontSendNotification);
    delayTimeMsSlider.setValue (p.delayTimeMs,   juce::dontSendNotification);
    delayFeedbackSlider.setValue (p.delayFeedback, juce::dontSendNotification);
    delayPingPongBtn.setToggleState (p.delayPingPong, juce::dontSendNotification);
    delayPingPongBtn.setColour (juce::TextButton::buttonColourId,
                                p.delayPingPong ? juce::Colour(0xff5566dd) : gateOffColour);
    delayMixSlider.setValue    (p.delayMix,     juce::dontSendNotification);
    delayWowSlider.setValue    (p.delayWow,     juce::dontSendNotification);
    delayFlutterSlider.setValue(p.delayFlutter, juce::dontSendNotification);
    delaySatSlider.setValue    (p.delaySat,     juce::dontSendNotification);
    delayProbSlider.setValue   (p.delayProb,    juce::dontSendNotification);

    // Reverb
    reverbOnBtn.setToggleState (p.reverbOn, juce::dontSendNotification);
    reverbOnBtn.setButtonText (p.reverbOn ? "ON" : "OFF");
    reverbOnBtn.setColour (juce::TextButton::buttonColourId, p.reverbOn ? gateOnColour : gateOffColour);
    reverbSizeSlider.setValue    (p.reverbSize,     juce::dontSendNotification);
    reverbDampingSlider.setValue (p.reverbDamping,  juce::dontSendNotification);
    reverbPreDelaySlider.setValue(p.reverbPreDelay, juce::dontSendNotification);
    reverbMixSlider.setValue     (p.reverbMix,      juce::dontSendNotification);

    // Chorus
    chorusOnBtn.setToggleState (p.chorusOn, juce::dontSendNotification);
    chorusOnBtn.setButtonText (p.chorusOn ? "ON" : "OFF");
    chorusOnBtn.setColour (juce::TextButton::buttonColourId, p.chorusOn ? gateOnColour : gateOffColour);
    chorusRateSlider.setValue  (p.chorusRate,  juce::dontSendNotification);
    chorusDepthSlider.setValue (p.chorusDepth, juce::dontSendNotification);
    chorusMixSlider.setValue   (p.chorusMix,   juce::dontSendNotification);

    // Master
    masterDriveSlider.setValue (p.masterDrive, juce::dontSendNotification);
    masterGainSlider.setValue  (p.masterGain,  juce::dontSendNotification);
}

//==============================================================================
// showPage — switch between pages 0=synth 1=pattern 2=fx
//==============================================================================
void VoltageSeq2AudioProcessorEditor::showPage (int page)
{
    currentPage = page;
    if (page != 0 && macroLearnActive >= 0) exitMacroLearn();   // cancel learn on page change
    for (auto* c : synthPageComponents)   c->setVisible (page == 0);
    for (auto* c : patternPageComponents) c->setVisible (page == 1);
    for (auto* c : fxPageComponents)      c->setVisible (page == 2);
    for (auto* c : genPageComponents)     c->setVisible (page == 3);
    for (auto* c : aboutPageComponents)   c->setVisible (page == 4);

    // The bulk-show above makes every synthPageComponent visible on page 0.
    // Re-apply per-voice Plaits vs OSC visibility so controls don't overlap.
    if (page == 0)
    {
        for (int v = 0; v < 2; ++v)
        {
            refreshPlaitsMode (v);

            // veloKnobs were swept into synthPageComponents by the constructor's
            // catch-all loop, so the bulk-show above would have forced them visible.
            // Re-apply the actual velo/pitch mode visibility here.
            for (int i = 0; i < 16; ++i)
            {
                stepKnob[v][i].setVisible (!veloMode[v]);
                veloKnob[v][i].setVisible ( veloMode[v]);
            }

            // OSC/ENV/LFO sections are hidden by default — only visible inside
            // their floating panels. The bulk-show above would have forced them
            // visible, so re-apply the collapsed state here.
            applySectionVisibility (v);
            applyMidView (v);   // QUANT (root/scale/clock) vs ORDER (play order)
            applyToolsView (v); // TOOLS reveal + (when on) MIDI/VOICE config group
            refreshOscView (v); // inline OSC1/OSC2/Plaits visibility (after section hide)

            // The bulk-show also forces the panel overlays themselves visible.
            // Restore each panel to its actual open/closed state.
            oscPanel[v].setVisible (oscPanelOpen[v]);
            envPanel[v].setVisible (envPanelOpen[v]);
            lfoPanel[v].setVisible (lfoPanelOpen[v]);
        }
    }

    // After bulk-showing pattern page components, correct seq/bank visibility per voice
    // (showPage shows everything in patternPageComponents, then refreshPatPageView
    //  hides whichever set — tiles OR seq controls — isn't active for each voice)
    if (page == 1)
    {
        refreshPatPageView (0);
        refreshPatPageView (1);
    }

    const auto activeCol  = juce::Colour (0xff2255aa);
    const auto inactiveCol = juce::Colour (0xff161630);
    synthPageBtn  .setColour (juce::TextButton::buttonColourId, page == 0 ? activeCol : inactiveCol);
    patternPageBtn.setColour (juce::TextButton::buttonColourId, page == 1 ? activeCol : inactiveCol);
    fxPageBtn     .setColour (juce::TextButton::buttonColourId, page == 2 ? activeCol : inactiveCol);
    genPageBtn    .setColour (juce::TextButton::buttonColourId, page == 3 ? activeCol : inactiveCol);
    aboutPageBtn  .setColour (juce::TextButton::buttonColourId, page == 4 ? activeCol : inactiveCol);
    macroOverlay.toFront (false);   // keep overlay topmost across page switches
    macroOverlay.repaint();
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

        // Pattern transpose buttons
        patTransposeUpBtn[vi].setBounds (getWidth() - 340, labelY, 60, 18);
        patTransposeDnBtn[vi].setBounds (getWidth() - 275, labelY, 60, 18);

        for (int s = 0; s < 16; ++s)
        {
            const int col = s % 8;
            const int row = s / 8;
            const int x   = margin + col * (slotW + slotGap);
            const int y   = (row == 0) ? row0 : row1;
            patternSlot[vi][s]->setBounds (x, y, slotW, slotH);
        }
    }
    layoutPatternSeqControls();
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

    // ── About page overlay ────────────────────────────────────────────────────
    if (currentPage == 4)
    {
        g.setColour (juce::Colour (0xff060610));
        g.fillRect (0, headerH, getWidth(), getHeight() - headerH);

        auto ib = aboutImageBounds;
        if (aboutImage.isValid())
        {
            g.drawImageWithin (aboutImage, ib.getX(), ib.getY(), ib.getWidth(), ib.getHeight(),
                               juce::RectanglePlacement::centred, false);
        }
        else
        {
            g.setColour (juce::Colour (0xff111120));
            g.fillRect (ib);
            g.setColour (juce::Colour (0xff3a3a4a));
            g.setFont (juce::Font (13.0f));
            g.drawText ("portrait image\n(pending)", ib, juce::Justification::centred);
        }
        // Thin frame around the image
        g.setColour (juce::Colour (0xff00d4aa).withAlpha (0.35f));
        g.drawRect (ib, 1);
        return;   // skip the synth-page backplate/labels drawn further below
    }

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

        // ── Sequencer view panels (one per voice) ─────────────────────────────
        for (int v = 0; v < 2; ++v)
        {
            if (patPageView[v] == 1)
            {
                const int py = (v == 0) ? 72 : 354;
                g.setColour (juce::Colour (0xff080818));
                g.fillRect (8, py, getWidth() - 16, 250);

                const int m = audioProcessor.patSeq[v].mode;
                const int activeLen = audioProcessor.patSeq[v].listLength;

                if (m == 2) // MIDI mode — draw note map
                {
                    g.setFont (juce::Font (9.f, juce::Font::bold));
                    g.setColour (dimColour);
                    g.drawText ("MIDI TRIGGER MAP  (notes C2 – D#3 trigger pattern slots 1–16)",
                                8 + 300, py + 4, 800, 14, juce::Justification::centredLeft);

                    static const char* noteNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                    const int mapX = 8 + 300, mapY = py + 22;
                    const int cellW = 80, cellH = 28, cellGap = 3;
                    for (int i = 0; i < 16; ++i)
                    {
                        const int col = i % 8, row = i / 8;
                        const int cx = mapX + col * (cellW + cellGap);
                        const int cy = mapY + row * (cellH + cellGap);
                        g.setColour (juce::Colour (0xff141428));
                        g.fillRoundedRectangle ((float)cx, (float)cy, (float)cellW, (float)cellH, 3.f);
                        const int midiNote = 36 + i;
                        const juce::String noteName = juce::String(noteNames[midiNote % 12])
                                                    + juce::String(midiNote / 12 - 1);
                        g.setFont (juce::Font (8.f));
                        g.setColour (dimColour);
                        g.drawText (noteName + juce::String::fromUTF8 (" \xe2\x86\x92 Slot ") + juce::String(i + 1),
                                    cx + 4, cy, cellW - 8, cellH, juce::Justification::centredLeft);
                    }
                }
                else if (m == 1 && activeLen > 0) // SEQ mode — draw position indicator
                {
                    const int curEntry = audioProcessor.patSeq[v].currentEntry;
                    const int entryW = 85, entryGap = 4, margin2 = 8;
                    for (int i = 0; i < activeLen; ++i)
                    {
                        const int row = i / 8, col = i % 8;
                        const int ex = margin2 + col * (entryW + entryGap);
                        const int ey = py + 32 + row * 50;
                        if (i == curEntry)
                        {
                            g.setColour (juce::Colour (0xff00d4aa).withAlpha (0.3f));
                            g.fillRoundedRectangle ((float)ex - 2, (float)ey - 2,
                                                   (float)entryW + 4, 48.f, 3.f);
                        }
                    }
                }

                // List length label at right edge
                const juce::String lenStr = "LIST: " + juce::String(juce::jmax(1, activeLen)) + " steps";
                g.setFont (juce::Font (9.f));
                g.setColour (dimColour);
                g.drawText (lenStr, getWidth() - 200, py + 4, 180, 14, juce::Justification::centredRight);
            }
        }

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
        drawFxPanel (10,  320, "DELAY",  juce::Colour (0xff00d4aa));   // two-row layout
        drawFxPanel (460, 430, "REVERB", juce::Colour (0xffaa44ff));
        drawFxPanel (960, 300, "CHORUS", juce::Colour (0xffe09040));
        drawFxPanel (1290,200, "MASTER", juce::Colour (0xffe94560));

        // Knob labels — drawn above the rotary knobs (knobs at dR2=240; labels sit just above)
        constexpr int lblY = 224;
        g.setFont (juce::Font (9.f));
        g.setColour (textColour);
        // Tape Delay labels — row 1 (core)
        g.drawText ("TIME",    10+10,  lblY,      52, 12, juce::Justification::centred);
        g.drawText ("FEEDBK",  10+80,  lblY,      52, 12, juce::Justification::centred);
        g.drawText ("MIX",     10+150, lblY,      52, 12, juce::Justification::centred);
        // Row 2 labels (tape char + gate) sit below row 2 knobs (lblY + 76)
        constexpr int lblY2 = lblY + 76;
        g.drawText ("WOW",     10+10,  lblY2,     52, 12, juce::Justification::centred);
        g.drawText ("FLUTTER", 10+80,  lblY2,     52, 12, juce::Justification::centred);
        g.drawText ("SAT",     10+150, lblY2,     52, 12, juce::Justification::centred);
        // Bernoulli gate — amber label
        g.setColour (juce::Colour (0xffe09040));
        g.drawText ("PROB",    10+240, lblY2,     52, 12, juce::Justification::centred);
        g.setColour (textColour);
        // Reverb labels
        g.drawText ("SIZE",    460+10,  lblY, 52, 12, juce::Justification::centred);
        g.drawText ("DAMP",    460+80,  lblY, 52, 12, juce::Justification::centred);
        g.drawText ("PRE-DLY", 460+150, lblY, 52, 12, juce::Justification::centred);
        g.drawText ("MIX",     460+220, lblY, 52, 12, juce::Justification::centred);
        // Chorus labels
        g.drawText ("RATE",    960+10,  lblY, 52, 12, juce::Justification::centred);
        g.drawText ("DEPTH",   960+80,  lblY, 52, 12, juce::Justification::centred);
        g.drawText ("MIX",     960+150, lblY, 52, 12, juce::Justification::centred);
        // Master labels
        g.drawText ("DRIVE",  1290+10,  lblY, 52, 12, juce::Justification::centred);
        g.drawText ("GAIN",   1290+80,  lblY, 52, 12, juce::Justification::centred);
        return;
    }

    // ── Generate page ─────────────────────────────────────────────────────────
    if (currentPage == 3)
    {
        g.setColour (juce::Colour (0xff040410));
        g.fillRect (0, headerH, getWidth(), winH - headerH);

        // Bjorklund inline helper for preview
        auto bjorklundPreview = [](int k, int n) -> std::vector<int>
        {
            if (n <= 0 || k <= 0) return std::vector<int> (std::max (n, 0), 0);
            k = std::min (k, n);
            if (k == n) return std::vector<int> (n, 1);
            const int q = n / k, rem = n % k;
            std::vector<int> res; res.reserve (n);
            for (int i = 0; i < k; ++i) {
                res.push_back (1);
                for (int j = 0; j < q - 1 + (i < rem ? 1 : 0); ++j) res.push_back (0);
            }
            return res;
        };

        // Accent colour tracks the selected target voice
        const juce::Colour accent = (euclidTargetVoice == 0)
                                    ? juce::Colour (0xff00aaff)
                                    : juce::Colour (0xffaa44ff);
        const juce::String targetLabel = (euclidTargetVoice == 0) ? "VOICE  A" : "VOICE  B";

        // ── Single panel ──────────────────────────────────────────────────────
        constexpr int panelX = 10;
        constexpr int panelY = headerH + 10;
        constexpr int panelW = 1480;
        const     int panelH = winH - panelY - 10;

        g.setColour (juce::Colour (0xff0c0c18));
        g.fillRoundedRectangle ((float)panelX, (float)panelY, (float)panelW, (float)panelH, 6.f);

        // Accent top bar
        g.setColour (accent.withAlpha (0.7f));
        g.fillRect (panelX, panelY, panelW, 3);

        // Title
        g.setFont (juce::Font (11.f, juce::Font::bold));
        g.setColour (juce::Colour (0xffe0e0e0));
        g.drawText ("EUCLIDEAN RHYTHM GENERATOR", panelX + 16, panelY + 8, 400, 16,
                    juce::Justification::centredLeft);

        // Control labels (left column, x=panelX+24)
        constexpr int lblX  = panelX + 24;
        constexpr int lblW  = 115;
        g.setFont (juce::Font (9.f, juce::Font::bold));
        g.setColour (dimColour);
        g.drawText ("TARGET",       lblX, panelY + 48,  lblW, 14, juce::Justification::centredLeft);
        g.drawText ("STEPS",        lblX, panelY + 96,  lblW, 14, juce::Justification::centredLeft);
        g.drawText ("HITS",         lblX, panelY + 146, lblW, 14, juce::Justification::centredLeft);
        g.drawText ("MAX RATCHETS", lblX, panelY + 196, lblW, 14, juce::Justification::centredLeft);

        // ── Preview grid ──────────────────────────────────────────────────────
        const int n = (int)euclidStepsSlider.getValue();
        const int k = (int)euclidHitsSlider.getValue();
        const int r = euclidR;

        const auto slots = bjorklundPreview (k, n * r);

        constexpr int gridX   = 560;
        const     int gridY   = panelY + 36;
        const     int gridW   = panelW - gridX - 20;
        const     int gridH   = panelH - 60;
        const     int cellW   = gridW / 16;
        constexpr int cellGap = 3;
        // Compute cell height so all r rows fill the grid height cleanly
        const int cellH = std::min ((gridH - (r - 1) * cellGap) / r, 160);

        // Step number headers
        g.setFont (juce::Font (9.f, juce::Font::bold));
        for (int i = 0; i < 16; ++i)
        {
            const bool inRange = (i < n);
            g.setColour (inRange ? dimColour : dimColour.withAlpha (0.22f));
            g.drawText (juce::String (i + 1),
                        gridX + i * cellW, gridY - 16, cellW - cellGap, 13,
                        juce::Justification::centred);
        }

        // Grid cells + ratchet badges
        for (int i = 0; i < 16; ++i)
        {
            const int  cx      = gridX + i * cellW;
            const bool inRange = (i < n);
            int hitCount = 0;

            for (int row = 0; row < r; ++row)
            {
                const int  cy  = gridY + row * (cellH + cellGap);
                const bool hit = inRange && (slots[i * r + row] != 0);
                if (hit) ++hitCount;

                juce::Colour cellCol;
                if (!inRange)  cellCol = juce::Colour (0xff080812);
                else if (hit)  cellCol = accent.withAlpha (0.85f);
                else           cellCol = juce::Colour (0xff14142a);

                g.setColour (cellCol);
                g.fillRoundedRectangle ((float)cx, (float)cy,
                                        (float)(cellW - cellGap), (float)cellH, 3.f);

                // Ratchet row number (right edge of cell, subtle)
                if (r > 1 && inRange)
                {
                    g.setFont (juce::Font (7.f));
                    g.setColour ((hit ? accent : dimColour).withAlpha (0.4f));
                    g.drawText (juce::String (row + 1),
                                cx + cellW - cellGap - 12, cy + 2, 10, cellH - 4,
                                juce::Justification::centredRight);
                }
            }

            // Hit badge below each column
            if (inRange)
            {
                const int badgeY = gridY + r * (cellH + cellGap) + 5;
                g.setFont (juce::Font (10.f, juce::Font::bold));
                if (hitCount > 1)
                {
                    g.setColour (accent);
                    g.drawText (juce::String (L"×") + juce::String (hitCount),
                                cx, badgeY, cellW - cellGap, 14, juce::Justification::centred);
                }
                else if (hitCount == 1)
                {
                    g.setColour (accent.withAlpha (0.6f));
                    g.drawText (juce::String (L"•"),
                                cx, badgeY, cellW - cellGap, 14, juce::Justification::centred);
                }
            }
        }

        // Info strip below grid
        const int infoY = gridY + r * (cellH + cellGap) + 24;
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.setColour (dimColour.withAlpha (0.55f));
        const int density = (n * r > 0) ? (int)juce::roundToInt (100.0 * k / (n * r)) : 0;
        g.drawText ("STEPS " + juce::String (n)
                  + "   ·   HITS " + juce::String (k)
                  + "   ·   MAX RATCHETS " + juce::String (r)
                  + "   ·   DENSITY " + juce::String (density) + "%"
                  + "   ·   TARGET → " + targetLabel,
                  gridX, infoY, gridW, 12, juce::Justification::centredLeft);

        // ── TURING MACHINE section ─────────────────────────────────────────────
        {
            constexpr int tmY       = panelY + 310;
            constexpr int tmSectionX = panelX;
            const     int tmSectionW = panelW;

            // Section separator line
            g.setColour (juce::Colour (0xff1a1a30));
            g.fillRect (tmSectionX, tmY, tmSectionW, 1);

            // Section title
            g.setFont (juce::Font (11.f, juce::Font::bold));
            g.setColour (accent.withAlpha (0.7f));
            g.drawText ("TURING MACHINE", tmSectionX + 16, tmY + 8, 300, 14,
                        juce::Justification::centredLeft);

            // ── LOCK knob area (left of panel) ──────────────────────────────
            constexpr int knobX    = panelX + 24;
            constexpr int knobSize = 120;
            constexpr int knobCX   = knobX + knobSize / 2;

            // Knob labels
            g.setFont (juce::Font (9.f, juce::Font::bold));
            g.setColour (dimColour);
            g.drawText ("LOCK",   knobX,  tmY + 30 + knobSize + 4, knobSize, 13,
                        juce::Justification::centred);
            g.setColour (dimColour.withAlpha (0.55f));
            g.setFont (juce::Font (8.f));
            g.drawText ("RANDOM", knobX - 8,  tmY + 30 + knobSize - 12, 52, 11,
                        juce::Justification::centredLeft);
            g.drawText ("LOCKED", knobX + knobSize - 44, tmY + 30 + knobSize - 12, 52, 11,
                        juce::Justification::centredRight);

            // LOCK value %
            const int   tmVi = audioProcessor.tmTargetVoice.load();
            const auto& tm   = audioProcessor.turingMachine;
            g.setFont (juce::Font (10.f, juce::Font::bold));
            g.setColour (accent);
            g.drawText (juce::String ((int)std::round (tm.lockAmount * 100.f)) + "%",
                        knobX, tmY + 30 + knobSize + 18, knobSize, 13,
                        juce::Justification::centred);

            // Controls area labels
            constexpr int tmCtrlX = knobX + knobSize + 18;
            g.setFont (juce::Font (9.f, juce::Font::bold));
            g.setColour (dimColour);
            g.drawText ("LENGTH",  tmCtrlX,      tmY + 22, 80, 10, juce::Justification::centredLeft);
            g.drawText ("MODE",    tmCtrlX,      tmY + 56, 80, 10, juce::Justification::centredLeft);

            // LENGTH value (between - and + buttons)
            g.setFont (juce::Font (12.f, juce::Font::bold));
            g.setColour (textColour);
            g.drawText (juce::String (tm.length),
                        tmCtrlX + 28, tmY + 34, 36, 26, juce::Justification::centred);

            // ── Right side: bit register + previews ─────────────────────────
            constexpr int tmDisplayX = 530;
            constexpr int tmDisplayW = 1480 - tmDisplayX;
            const     int tmCellW    = tmDisplayW / 16;
            constexpr int tmCellH    = 52;
            constexpr int tmBitRowY  = tmY + 28;

            // Step number header
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            for (int i = 0; i < 16; ++i)
            {
                const bool inLen = (i < tm.length);
                g.setColour (inLen ? dimColour : dimColour.withAlpha (0.22f));
                g.drawText (juce::String (i + 1),
                            tmDisplayX + i * tmCellW, tmBitRowY - 16, tmCellW - 3, 13,
                            juce::Justification::centred);
            }

            // Overlay dimming on cells outside LENGTH (bit buttons paint themselves)
            for (int i = 0; i < 16; ++i)
            {
                if (i >= tm.length)
                {
                    g.setColour (juce::Colour (0x88000000));
                    g.fillRect (tmDisplayX + i * tmCellW, tmBitRowY,
                                tmCellW - 3, tmCellH);
                }
            }

            // Active-step cursor ring on current step
            {
                const int cs = audioProcessor.voice[tmVi].currentStep;
                if (cs >= 0 && cs < 16)
                {
                    g.setColour (accent.withAlpha (0.55f));
                    g.drawRoundedRectangle (
                        (float)(tmDisplayX + cs * tmCellW) - 1.f,
                        (float)tmBitRowY - 1.f,
                        (float)(tmCellW - 3) + 2.f,
                        (float)tmCellH + 2.f,
                        3.f, 2.f);
                }
            }

            // ── Pitch preview bars ──────────────────────────────────────────
            constexpr int pitchBarY = tmBitRowY + tmCellH + 12;
            constexpr int pitchBarH = 54;

            g.setFont (juce::Font (8.f));
            g.setColour (dimColour.withAlpha (0.5f));
            g.drawText ("PITCH", tmDisplayX - 58, pitchBarY, 54, pitchBarH,
                        juce::Justification::centredRight);

            g.setColour (juce::Colour (0xff0a0a18));
            g.fillRect (tmDisplayX, pitchBarY, tmDisplayW, pitchBarH);
            g.setColour (juce::Colour (0xff1a1a30));
            g.drawRect (tmDisplayX, pitchBarY, tmDisplayW, pitchBarH, 1);

            // 0 V reference line
            const float zeroYpitch = (float)(pitchBarY + pitchBarH / 2);
            g.setColour (juce::Colour (0xff333366));
            g.drawLine ((float)tmDisplayX, zeroYpitch,
                        (float)(tmDisplayX + tmDisplayW), zeroYpitch, 1.f);

            for (int i = 0; i < 16; ++i)
            {
                const bool inLen = (i < tm.length);
                const float v = tm.previewVoltages[i];          // -5..+5
                const float norm = (v + 5.0f) / 10.0f;          // 0..1
                const float barH = std::abs (norm - 0.5f) * (float)pitchBarH;
                const float barY = (norm >= 0.5f)
                    ? zeroYpitch - barH
                    : zeroYpitch;
                const int   bx   = tmDisplayX + i * tmCellW;
                const int   bw   = tmCellW - 3;
                g.setColour (inLen
                    ? accent.withAlpha (0.65f)
                    : dimColour.withAlpha (0.15f));
                g.fillRect ((float)bx, barY, (float)bw, barH);
            }

            // ── Gate preview row ────────────────────────────────────────────
            constexpr int gateBarY = pitchBarY + pitchBarH + 8;
            constexpr int gateBarH = 22;

            g.setFont (juce::Font (8.f));
            g.setColour (dimColour.withAlpha (0.5f));
            g.drawText ("GATE", tmDisplayX - 58, gateBarY, 54, gateBarH,
                        juce::Justification::centredRight);

            for (int i = 0; i < 16; ++i)
            {
                const bool inLen = (i < tm.length);
                const bool gOn   = tm.previewGates[i];
                const int  bx    = tmDisplayX + i * tmCellW;
                const int  bw    = tmCellW - 3;

                g.setColour (inLen && gOn
                    ? gateOnColour.withAlpha (0.75f)
                    : juce::Colour (0xff0e0e1c));
                g.fillRoundedRectangle ((float)bx, (float)gateBarY,
                                        (float)bw,  (float)gateBarH, 3.f);
                if (inLen)
                {
                    g.setColour (inLen && gOn
                        ? gateOnColour.withAlpha (0.3f)
                        : juce::Colour (0xff1a1a28));
                    g.drawRoundedRectangle ((float)bx, (float)gateBarY,
                                            (float)bw, (float)gateBarH, 3.f, 1.f);
                }
            }

            // Mode label
            constexpr int modeLabelY = gateBarY + gateBarH + 6;
            g.setFont (juce::Font (8.5f));
            g.setColour (dimColour.withAlpha (0.45f));
            g.drawText (
                juce::String (tm.writeEnabled ? "■ WRITE ACTIVE  ·  " : "○ WRITE OFF  ·  ")
                + "LENGTH " + juce::String (tm.length)
                + "  ·  LOCK " + juce::String ((int)std::round (tm.lockAmount * 100.f)) + "%"
                + "  ·  MODE: " + (tm.affectGates ? "GATE+PITCH" : "PITCH ONLY")
                + "  ·  TARGET → " + targetLabel,
                tmDisplayX, modeLabelY, tmDisplayW, 12,
                juce::Justification::centredLeft);
        }

        return;
    }

    // Backplate drawn once for the full content area.
    // stretchToFit fills the rect exactly (2600x1300 SVG → 1500x710 screen, ratio ~2:1 vs 2.11:1,
    // so distortion is minimal and the design was built for this mapping).
    if (backplate != nullptr)
    {
        const juce::Rectangle<float> fullArea (0.0f, 0.0f,
                                               (float)getWidth(), (float)winH);
        // Base pass — covers whole plugin background (seq strips, header, etc.)
        backplate->drawWithin (g, fullArea, juce::RectanglePlacement::stretchToFit, 0.82f);
        // NOTE: the branding logo boost pass runs AFTER the ctrl strip fills
        // (see end of paint) so the logo renders on top of the background panels.
    }

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
        // Ensure colour fills to the very right edge (no black gap beyond seqW)
        g.fillRect (seqX + seqW, sY, getWidth() - (seqX + seqW), seqH);

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

        // RHS control-block group labels (match layoutVoice's rX / rows)
        {
            const int perfX = seqX + 16 * stepStride + 96;
            const int midX  = perfX + 295;
            const int toolX = midX + 200;
            g.setColour (dimColour);
            g.setFont (juce::Font (8.0f, juce::Font::bold));
            // Performance sliders
            g.drawText ("RANGE",  perfX, sY + 5,  120, 10, juce::Justification::centredLeft);
            g.drawText ("LENGTH", perfX, sY + 50, 180, 10, juce::Justification::centredLeft);
            g.drawText ("SWING",  perfX, sY + 92, 180, 10, juce::Justification::centredLeft);
            // Middle slot — QUANT row labels (ORDER view buttons self-label)
            if (midView[v] == 0)
            {
                g.drawText ("ROOT",  midX, sY + 31, 36, 12, juce::Justification::centredRight);
                g.drawText ("SCALE", midX, sY + 59, 36, 12, juce::Justification::centredRight);
                g.drawText ("CLOCK", midX, sY + 87, 36, 12, juce::Justification::centredRight);
            }
            // TOOLS column — nudge hint (only when revealed)
            if (toolsView[v] != 0)
                g.drawText ("NUDGE", toolX + 104, sY + 16, 58, 10, juce::Justification::centredLeft);
        }

        // ── Pulse counter (top-right of sequencer strip) ───────────────────
        {
            const auto& vp2 = audioProcessor.voice[v];
            int totalPulses = 0;
            for (int i = 0; i < vp2.sequenceLength; ++i)
                totalPulses += vp2.stepPulses[i];

            const int pCtrlX = seqX + 16 * stepStride + 8;
            const int pCtrlW = 74;

            // Label
            g.setFont (juce::Font (7.5f, juce::Font::bold));
            g.setColour (juce::Colour (0xff00d4aa).withAlpha (0.7f));
            g.drawText ("TOTAL", pCtrlX, sY + stepSliderTop, pCtrlW, 11, juce::Justification::centred);

            // Count value — cyan if in range, amber warning if over pulse limit
            const bool overLimit = vp2.pulseLengthMode && totalPulses > vp2.pulseLength;
            g.setColour (overLimit ? juce::Colour(0xffe09040) : juce::Colour(0xff00d4aa));
            g.setFont (juce::Font (16.0f, juce::Font::bold));
            g.drawText (juce::String (totalPulses), pCtrlX, sY + stepSliderTop + 10, pCtrlW, 20, juce::Justification::centred);

            // In pulse mode: show "/N" target below
            if (vp2.pulseLengthMode)
            {
                g.setFont (juce::Font (8.0f));
                g.setColour (juce::Colour (0xff00aa88));
                g.drawText ("/ " + juce::String (vp2.pulseLength), pCtrlX, sY + stepSliderTop + 28, pCtrlW, 10, juce::Justification::centred);
            }
        }

        // Pulse pips (step numbers now live on the slide buttons)
        g.setFont (juce::Font (8.0f));
        for (int i = 0; i < 16; ++i)
        {
            const int pulses = audioProcessor.voice[v].stepPulses[i];
            const bool multiPulse = (pulses > 1);

            // Pulse pips — small squares just above the slide button
            if (multiPulse)
            {
                const int pipW = 4, pipH = 3, pipGap = 2;
                const int totalPipW = pulses * pipW + (pulses - 1) * pipGap;
                int px = seqX + i * stepStride + (stepStride - totalPipW) / 2;
                const int py = sY + seqH - 14;
                g.setColour (juce::Colour (0xff00d4aa).withAlpha (0.75f));
                for (int p = 0; p < pulses; ++p)
                {
                    g.fillRect (px, py, pipW, pipH);
                    px += pipW + pipGap;
                }
            }
        }

        // ── Sub-strip removed — its controls now live in the RHS block of the
        // step row (see layoutVoice). The freed band gets reclaimed by the
        // control-row relayout in the next checkpoint.
        juce::ignoreUnused (sbY);

        // ── Control panels row ────────────────────────────────────────────────
        auto drawPanel = [&](int px, int pw, const juce::String& title)
        {
            g.setColour (sectionColour.withAlpha (0.85f));
            g.fillRoundedRectangle ((float)px, (float)cY, (float)pw, (float)ctrlH, 4.0f);
            g.setColour (dimColour);
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.drawText (title, px, cY + 3, pw, 12, juce::Justification::centred);
        };
        drawPanel (pSeqX,  pSeqW,  "GLIDE");   // PORTA only (pattern controls moved up)
        drawPanel (pO1X,   pO1W + pO2W, "");   // OSC section (radio shows active view)
        drawPanel (pFltX,  pFltW,  "FILTER");
        drawPanel (pAEX,   pAEW,   "AMP ENV");
        // LFO 1-4 panels removed — extend ctrl strip background to right edge
        // so the freed/branding zone matches the section colour (not base black).
        g.setColour (sectionColour.withAlpha (0.85f));
        g.fillRect (pAEX + pAEW, cY, getWidth() - (pAEX + pAEW), ctrlH);

        // Control panel labels
        const int lY1 = cY + lOff1, lY2 = cY + lOff2, lY3 = cY + lOff3;
        g.setColour (textColour);
        g.setFont (juce::Font (9.0f));
        // GLIDE — PORTA (the only set-once synth control on the bottom-left)
        g.drawText ("PORTA",    pSeqX,  cY + 48, pSeqW, 12, juce::Justification::centred);
        // ── OSC section labels (per active view) ──────────────────────────────
        {
            const int oscX = pO1X, oscW = pO1W + pO2W;
            const bool plaits = audioProcessor.voice[v].plaitsEnabled;
            auto kl = [&](const char* t, int x, int y, int w = 38)
            { g.drawText (t, x, y, w, 10, juce::Justification::centred); };
            if (plaits)
            {
                g.setColour (dimColour); g.setFont (juce::Font (7.5f, juce::Font::bold));
                const int sp = oscW / 5;
                kl ("HARM",  oscX + sp*0 + 6, cY + 110, 44);
                kl ("TIMBRE",oscX + sp*1 + 6, cY + 110, 44);
                kl ("MORPH", oscX + sp*2 + 6, cY + 110, 44);
                kl ("AUX",   oscX + sp*3 + 6, cY + 110, 44);
            }
            else if (oscView[v] == 0)
            {
                g.setColour (textColour); g.setFont (juce::Font (8.0f));
                g.drawText ("WAVE", oscX + 4, cY + 18, 170, 10, juce::Justification::centredLeft);
                kl ("LEVEL", oscX + 6,   cY + 108);
                kl ("PWM",   oscX + 130, cY + 108);
                kl ("FB",    oscX + 174, cY + 108);
                kl ("DRIFT", oscX + 218, cY + 108);
                g.setColour (dimColour); g.setFont (juce::Font (7.5f, juce::Font::bold));
                g.drawText ("SCOPE", oscX + 268, cY + 18, 88, 10, juce::Justification::centred);
            }
            else
            {
                g.setColour (textColour); g.setFont (juce::Font (8.0f));
                kl ("POS",  oscX + 6,   cY + 72);
                kl ("LVL",  oscX + 50,  cY + 72);
                kl ("FM",   oscX + 94,  cY + 72);
                kl ("XMOD", oscX + 138, cY + 72);
                g.setColour (dimColour); g.setFont (juce::Font (7.5f, juce::Font::bold));
                g.drawText ("OCT",   oscX + 6,  cY + 112, 70,  10, juce::Justification::centredLeft);
                g.drawText ("RATIO", oscX + 86, cY + 112, 200, 10, juce::Justification::centredLeft);
            }
        }
        // Filter — hero Cutoff label spans the bigger knob
        g.drawText ("CUTOFF",  pFltX+6,   lY1, 52,  12, juce::Justification::centred);
        g.drawText ("RES",     pFltX+66,  lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("DRIVE",   pFltX+112, lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("MODE",    pFltX+6,   lY2, 64,  12, juce::Justification::centred);
        g.drawText ("SLOPE",   pFltX+74,  lY2, 30,  12, juce::Justification::centred);
        g.drawText ("ENV",     pFltX+112, lY2, kSz, 12, juce::Justification::centred);
        g.drawText ("A",  pFltX+2,   lY3, kSz, 12, juce::Justification::centred);
        g.drawText ("D",  pFltX+48,  lY3, kSz, 12, juce::Justification::centred);
        g.drawText ("S",  pFltX+94,  lY3, kSz, 12, juce::Justification::centred);
        g.drawText ("R",  pFltX+140, lY3, kSz, 12, juce::Justification::centred);
        // AMP ENV labels (Amp ADSR stays permanently on front page)
        g.setColour (dimColour); g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.drawText ("AMP",   pAEX, cY+14, pAEW, 10, juce::Justification::centred);
        g.setColour (textColour); g.setFont (juce::Font (9.0f));
        g.drawText ("A", pAEX+2,    lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("D", pAEX+50,   lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("S", pAEX+98,   lY1, kSz, 12, juce::Justification::centred);
        g.drawText ("R", pAEX+146,  lY1, kSz, 12, juce::Justification::centred);
        // MOD ENV lives in the MOD floating panel — no front-page labels needed

        // Accent stripe on control panel left edge
        g.setColour (accent.withAlpha (0.4f));
        g.fillRect (pSeqX, cY, 2, ctrlH);
    }

    // Divider between Voice A controls and Voice B controls
    g.setColour (voiceAColour.withAlpha (0.2f));
    g.fillRect (0, ctrlBY - 1, getWidth(), 2);
    g.setColour (voiceBColour.withAlpha (0.2f));
    g.fillRect (0, ctrlBY + 1, getWidth(), 1);

    // ── Branding logo boost pass ─────────────────────────────────────────────
    // Drawn AFTER all ctrl strip background fills so the logo renders on top,
    // not dimmed underneath the sectionColour panels. Restricted to the TOP
    // (Voice A) row only — the bottom row is now the MACROS panel.
    if (backplate != nullptr)
    {
        const juce::Rectangle<float> fullArea (0.0f, 0.0f,
                                               (float)getWidth(), (float)winH);
        g.saveState();
        g.reduceClipRegion (1090, ctrlAY, getWidth() - 1090, ctrlH);
        backplate->drawWithin (g, fullArea, juce::RectanglePlacement::stretchToFit, 0.88f);
        g.restoreState();
    }

    // ── MACROS panel (bottom-right, replaces the lower branding block) ────────
    if (currentPage == 0)
    {
        const int mzX = 1090, mzW = getWidth() - 1090;
        const juce::Colour macTeal (0xff00d4aa);
        g.setColour (juce::Colour (0xff0c0c18));               // opaque — hide branding text
        g.fillRect (mzX, ctrlBY, mzW, ctrlH);
        g.setColour (macTeal.withAlpha (0.35f));
        g.fillRect (mzX, ctrlBY, 2, ctrlH);                    // accent stripe
        g.setColour (macTeal);
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.drawText ("MACROS", mzX + 8, ctrlBY + 4, mzW - 16, 12,
                    juce::Justification::centredLeft);
        g.setColour (macTeal.withAlpha (0.25f));
        g.fillRect (mzX + 8, ctrlBY + 18, mzW - 16, 1);
    }
}

//==============================================================================
// RESIZED
//==============================================================================
void VoltageSeq2AudioProcessorEditor::resized()
{
    // Global header (always present)
    synthPageBtn  .setBounds (220, 3,  65, 22);
    patternPageBtn.setBounds (290, 3,  80, 22);
    fxPageBtn     .setBounds (375, 3,  55, 22);
    genPageBtn    .setBounds (435, 3,  80, 22);
    aboutPageBtn  .setBounds (520, 3,  62, 22);
    savePresetBtn .setBounds (1305, 3, 85, 22);
    loadPresetBtn .setBounds (1396, 3, 85, 22);

    layoutVoice (0, seqAY, ctrlAY);
    layoutVoice (1, seqBY, ctrlBY);
    layoutPatternPage();
    layoutFxPage();
    layoutGenPage();
    layoutAboutPage();

    // ── Macro controllers — bottom-right branding zone (Voice B control row) ──
    {
        constexpr int mx0   = 1100;          // left edge of macro zone
        constexpr int colW  = 195;           // per-macro column width
        constexpr int kSzM  = 58;            // wheel diameter (slightly < TM lock wheel)
        const int     topY  = ctrlBY + 24;   // below the "LFO ▼" button row
        for (int m = 0; m < kNumMacros; ++m)
        {
            const int cx = mx0 + m * colW;
            macroNameLabel  [m].setBounds (cx, topY,                       colW - 8, 14);
            macroKnob       [m].setBounds (cx + (colW - kSzM) / 2 - 4, topY + 14, kSzM, kSzM);
            macroAssignBtn  [m].setBounds (cx + (colW - 76) / 2 - 4, topY + 74, 76, 15);
            macroAssignLabel[m].setBounds (cx + 4, topY + 94,             colW - 12, 58);
        }
    }

    macroOverlay.setBounds (getLocalBounds());
    macroOverlay.toFront (false);
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
        // Packed box columns — slim with a hair of gap (PDF Example 2 style).
        const int boxW = stepStride - 4;
        const int boxX = bx + 2;
        stepKnob[v][i].setBounds (boxX, seqTopY + stepSliderTop, boxW, stepSliderH);
        veloKnob[v][i].setBounds (boxX, seqTopY + stepSliderTop, boxW, stepSliderH);
        gateBtn [v][i].setBounds (bx + 2,  seqTopY + gateRelY,      stepStride - 4, 13);
        slideBtn[v][i].setBounds (bx + 2,  seqTopY + slideRelY,     stepStride - 4, 12);
    }

    // ── Pulse counter / mode controls (right of sequencer steps) ─────────────
    const int pCtrlX = seqX + 16 * stepStride + 8;   // just right of packed steps
    const int pCtrlW = 74;
    pulseModeBtn[v].setBounds (pCtrlX, seqTopY + stepSliderTop + 34, pCtrlW, 15);
    pulseLenBox [v].setBounds (pCtrlX, seqTopY + stepSliderTop + 53, pCtrlW, 16);

    // ══ PATTERN-SECTION control block (right of the step lanes) ════════════════
    // Three zones: PERFORMANCE sliders · QUANT↔ORDER radio slot · TOOLS column.
    // Everything saved-per-pattern lives up here with the lanes.
    const int bh = 16;

    // ── Zone 1 — PERFORMANCE (Range / Length / Swing + transport / Velo) ───────
    const int perfX = seqX + 16 * stepStride + 96;   // ~800
    rangeSlider    [v].setBounds (perfX,       seqTopY + 20, 215, 22);   // hero slider
    seqLengthSlider[v].setBounds (perfX,       seqTopY + 62, 180, bh);
    swingSlider    [v].setBounds (perfX,       seqTopY + 104, 180, bh);
    runStopBtn     [v].setBounds (perfX + 225, seqTopY + 62, 60, bh);
    veloModeBtn    [v].setBounds (perfX + 225, seqTopY + 104, 60, bh);

    // ── Zone 2 — MIDDLE radio slot: QUANT (Root/Scale/Clock) ↔ ORDER ───────────
    const int midX = perfX + 295;   // ~1095
    quantViewBtn[v].setBounds (midX,      seqTopY + 6, 58, bh);
    orderViewBtn[v].setBounds (midX + 62, seqTopY + 6, 58, bh);
    // QUANT view — Root / Scale / Clock stacked (labels painted to the left)
    rootBox    [v].setBounds (midX + 40, seqTopY + 30, 145, 20);
    scaleBox   [v].setBounds (midX + 40, seqTopY + 58, 145, 20);
    clockDivBox[v].setBounds (midX + 40, seqTopY + 86, 145, 20);
    // ORDER view — play order 2×2 grid (shares the slot)
    playFwdBtn [v].setBounds (midX,      seqTopY + 32, 88, 20);
    playRevBtn [v].setBounds (midX + 92, seqTopY + 32, 88, 20);
    playConvBtn[v].setBounds (midX,      seqTopY + 56, 88, 20);
    playRndBtn [v].setBounds (midX + 92, seqTopY + 56, 88, 20);

    // ── Zone 3 — TOOLS toggle + revealed utility cluster (set-and-forget) ──────
    const int toolX = midX + 200;   // ~1295
    toolsBtn[v].setBounds (toolX, seqTopY + 6, 56, bh);
    // Row 1 — reset / uni / nudge
    resetBtn     [v].setBounds (toolX,       seqTopY + 30, 46, bh);
    bipolarBtn   [v].setBounds (toolX + 50,  seqTopY + 30, 46, bh);
    nudgeLeftBtn [v].setBounds (toolX + 104, seqTopY + 30, 28, bh);
    nudgeRightBtn[v].setBounds (toolX + 134, seqTopY + 30, 28, bh);
    // Row 2 — randomise targets
    randModeBtn  [v].setBounds (toolX,       seqTopY + 52, 56, bh);
    randomBtn    [v].setBounds (toolX + 60,  seqTopY + 52, 56, bh);
    // Row 3 — MIDI / VOICE config radio
    midiViewBtn [v].setBounds (toolX,       seqTopY + 74, 48, bh);
    voiceViewBtn[v].setBounds (toolX + 50,  seqTopY + 74, 48, bh);
    // Row 4 — config group (MIDI or VOICE shares the slot)
    midiOutBtn  [v].setBounds (toolX,       seqTopY + 96, 80, bh);
    midiOutChBox[v].setBounds (toolX + 84,  seqTopY + 96, 66, bh);
    voiceModeBox [v].setBounds (toolX,      seqTopY + 96, 78, bh);
    uniCountBtn  [v].setBounds (toolX + 80, seqTopY + 96, 34, bh);
    chordModeBtn [v].setBounds (toolX + 116,seqTopY + 96, 42, bh);
    // Row 5 — unison spread / width (VOICE view only)
    uniSpreadSlider[v].setBounds (toolX,      seqTopY + 116, 90, bh);
    uniWidthSlider [v].setBounds (toolX + 94, seqTopY + 116, 90, bh);

    applyMidView   (v);   // QUANT vs ORDER slot
    applyToolsView (v);   // enforce TOOLS reveal + config-group visibility

    // ── GLIDE panel — PORTA only (Range/Root/Scale/Clock now live up top) ──────
    portaSlider[v].setBounds (pSeqX + (pSeqW - kSz) / 2, ctrlTopY + 64, kSz, kSz);

    // Panel toggle buttons (MOD/LFO still use popups for now; OSC is inline)
    {
        constexpr int btnH = 16, btnW = 52;
        envPanelBtn[v].setBounds (pAEX + pAEW - btnW - 4,         ctrlTopY + 4, btnW, btnH);
        lfoPanelBtn[v].setBounds (pLfo4X + pLfoW - btnW - 4,      ctrlTopY + 4, btnW, btnH);
    }

    // ── OSC section (inline; OSC1 / OSC2 / PLAITS overlap, toggled by view) ────
    const int oscX = pO1X;
    const int oscW = pO1W + pO2W;            // ~360 px full footprint
    // Header — view radio + Plaits toggle
    oscView1Btn[v].setBounds (oscX + 0,  ctrlTopY + 4, 52, 18);
    oscView2Btn[v].setBounds (oscX + 56, ctrlTopY + 4, 52, 18);
    plaitsBtn  [v].setBounds (oscX + oscW - 62, ctrlTopY + 2, 58, 20);

    // OSC 1 group
    osc1WaveBox       [v].setBounds (oscX + 4,   ctrlTopY + 32, 170, 22);
    osc1LevelSlider   [v].setBounds (oscX + 6,   ctrlTopY + 68, kSz, kSz);
    osc1OctaveBox     [v].setBounds (oscX + 50,  ctrlTopY + 72, 70,  22);
    osc1PWMSlider     [v].setBounds (oscX + 130, ctrlTopY + 68, kSz, kSz);
    osc1FeedbackSlider[v].setBounds (oscX + 174, ctrlTopY + 68, kSz, kSz);
    driftSlider       [v].setBounds (oscX + 218, ctrlTopY + 68, kSz, kSz);
    oscScope          [v]->setBounds(oscX + 268, ctrlTopY + 32, 88, 82);

    // OSC 2 group (overlaps OSC 1)
    osc2PosSlider   [v].setBounds (oscX + 6,   ctrlTopY + 32, kSz, kSz);
    osc2LevelSlider [v].setBounds (oscX + 50,  ctrlTopY + 32, kSz, kSz);
    fmDepthSlider   [v].setBounds (oscX + 94,  ctrlTopY + 32, kSz, kSz);
    crossModSlider  [v].setBounds (oscX + 138, ctrlTopY + 32, kSz, kSz);
    osc2OctaveBox   [v].setBounds (oscX + 6,   ctrlTopY + 86, 70,  22);
    fmRatioSlider   [v].setBounds (oscX + 86,  ctrlTopY + 86, 200, 22);
    wavetableDisplay[v]->setBounds(oscX + 6,   ctrlTopY + 116, 300, 18);

    // PLAITS group (overlaps OSC 1/2)
    plaitsEngBox[v].setBounds (oscX + 4, ctrlTopY + 32, oscW - 72, 24);
    {
        const int pkY = ctrlTopY + 64, pkSz = 44, pkSp = oscW / 5;
        plaitsHarmSlider [v].setBounds (oscX + pkSp * 0 + 6, pkY, pkSz, pkSz);
        plaitsTimSlider  [v].setBounds (oscX + pkSp * 1 + 6, pkY, pkSz, pkSz);
        plaitsMorphSlider[v].setBounds (oscX + pkSp * 2 + 6, pkY, pkSz, pkSz);
        plaitsAuxSlider  [v].setBounds (oscX + pkSp * 3 + 6, pkY, pkSz, pkSz);
    }
    plaitsTrigBtn[v].setBounds (oscX + 6,  ctrlTopY + 114, 84, 22);
    plaitsOctBox [v].setBounds (oscX + 96, ctrlTopY + 114, 90, 22);

    // ── Filter — hero Cutoff (52px) anchors the section ─────────────────────────
    const int heroSz = 52;
    cutoffSlider      [v].setBounds (pFltX + 6,   cy1,     heroSz, heroSz);
    resonanceSlider   [v].setBounds (pFltX + 66,  cy1 + 5, kSz, kSz);
    filterDriveSlider [v].setBounds (pFltX + 112, cy1 + 5, kSz, kSz);
    filterModeBox     [v].setBounds (pFltX + 6,   cy2, 64, 20);
    filterSlopeBtn    [v].setBounds (pFltX + 74,  cy2, 30, 20);
    filterEnvAmtSlider[v].setBounds (pFltX + 112, cy2, kSz, kSz);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    envResetBtn[v].setBounds (pAEX + pAEW - 44, ctrlTopY + 2, 42, 14);
    const int aeStride = 48;
    attackSlider [v].setBounds (pAEX + 2,            cy1, kSz, kSz);
    decaySlider  [v].setBounds (pAEX + 2 + aeStride, cy1, kSz, kSz);
    sustainSlider[v].setBounds (pAEX + 2 + aeStride*2, cy1, kSz, kSz);
    releaseSlider[v].setBounds (pAEX + 2 + aeStride*3, cy1, kSz, kSz);

    // ── Filter Envelope (in Filter panel cy3) ─────────────────────────────────
    const int feStride = 46;
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
// Pattern Sequencer Controls
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupPatternSeqControls()
{
    for (int v = 0; v < 2; ++v)
    {
        // ── Bank / Sequencer sub-tabs ─────────────────────────────────────────
        patBankTabBtn[v].setButtonText ("BANK");
        patBankTabBtn[v].onClick = [this, v]()
        {
            patPageView[v] = 0;
            refreshPatPageView (v);
        };
        addChildComponent (patBankTabBtn[v]);
        patternPageComponents.push_back (&patBankTabBtn[v]);

        patSeqTabBtn[v].setButtonText ("SEQUENCER");
        patSeqTabBtn[v].onClick = [this, v]()
        {
            patPageView[v] = 1;
            refreshPatPageView (v);
        };
        addChildComponent (patSeqTabBtn[v]);
        patternPageComponents.push_back (&patSeqTabBtn[v]);

        // ── Mode buttons ──────────────────────────────────────────────────────
        patSeqOffBtn[v].setButtonText ("OFF");
        patSeqOffBtn[v].onClick = [this, v]() {
            audioProcessor.patSeq[v].mode = 0;
            refreshPatSeqMode (v);
        };
        addChildComponent (patSeqOffBtn[v]);
        patternPageComponents.push_back (&patSeqOffBtn[v]);

        patSeqAutoBtn[v].setButtonText ("SEQ");
        patSeqAutoBtn[v].onClick = [this, v]() {
            audioProcessor.patSeq[v].mode = 1;
            audioProcessor.patSeq[v].currentEntry  = 0;
            audioProcessor.patSeq[v].currentRepeat = 0;
            refreshPatSeqMode (v);
        };
        addChildComponent (patSeqAutoBtn[v]);
        patternPageComponents.push_back (&patSeqAutoBtn[v]);

        patSeqMidiBtn[v].setButtonText ("MIDI");
        patSeqMidiBtn[v].onClick = [this, v]() {
            audioProcessor.patSeq[v].mode = 2;
            refreshPatSeqMode (v);
        };
        addChildComponent (patSeqMidiBtn[v]);
        patternPageComponents.push_back (&patSeqMidiBtn[v]);

        // ── Load timing toggle ────────────────────────────────────────────────
        patSeqImmBtn[v].setButtonText (audioProcessor.patSeq[v].immediate ? "IMMEDIATE" : "QUEUED");
        patSeqImmBtn[v].onClick = [this, v]()
        {
            auto& ps = audioProcessor.patSeq[v];
            ps.immediate = !ps.immediate;
            patSeqImmBtn[v].setButtonText (ps.immediate ? "IMMEDIATE" : "QUEUED");
        };
        addChildComponent (patSeqImmBtn[v]);
        patternPageComponents.push_back (&patSeqImmBtn[v]);

        // ── List length +/- buttons ───────────────────────────────────────────
        patSeqLenUpBtn[v].setButtonText ("+");
        patSeqLenUpBtn[v].onClick = [this, v]()
        {
            auto& ps = audioProcessor.patSeq[v];
            if (ps.listLength < 16) { ps.listLength++; refreshPatPageView (v); }
        };
        addChildComponent (patSeqLenUpBtn[v]);
        patternPageComponents.push_back (&patSeqLenUpBtn[v]);

        patSeqLenDnBtn[v].setButtonText ("-");
        patSeqLenDnBtn[v].onClick = [this, v]()
        {
            auto& ps = audioProcessor.patSeq[v];
            if (ps.listLength > 1) { ps.listLength--; refreshPatPageView (v); }
        };
        addChildComponent (patSeqLenDnBtn[v]);
        patternPageComponents.push_back (&patSeqLenDnBtn[v]);

        // ── Playlist entries ──────────────────────────────────────────────────
        for (int i = 0; i < 16; ++i)
        {
            // Slot picker
            for (int s = 1; s <= 16; ++s)
                patSeqSlotBox[v][i].addItem ("Slot " + juce::String(s), s);
            // Sync from processor state — ensures correct display after state restore
            // and also guarantees list[i] matches what the box shows from the start.
            patSeqSlotBox[v][i].setSelectedId (audioProcessor.patSeq[v].list[i] + 1,
                                               juce::dontSendNotification);
            patSeqSlotBox[v][i].setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0e1020));
            patSeqSlotBox[v][i].setColour (juce::ComboBox::textColourId, juce::Colour (0xffe0e0e0));
            patSeqSlotBox[v][i].onChange = [this, v, i]()
            {
                audioProcessor.patSeq[v].list[i] = patSeqSlotBox[v][i].getSelectedId() - 1;
            };
            addChildComponent (patSeqSlotBox[v][i]);
            patternPageComponents.push_back (&patSeqSlotBox[v][i]);

            // Loop count button (cycles 1→2→3→4→8→1) — sync from stored state
            patSeqLoopBtn[v][i].setButtonText ("x" + juce::String (audioProcessor.patSeq[v].loopCount[i]));
            patSeqLoopBtn[v][i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff141428));
            patSeqLoopBtn[v][i].onClick = [this, v, i]()
            {
                static const int seq[] = { 1, 2, 3, 4, 8 };
                auto& lc = audioProcessor.patSeq[v].loopCount[i];
                int idx = 0;
                for (int j = 0; j < 5; ++j) if (seq[j] == lc) { idx = j; break; }
                lc = seq[(idx + 1) % 5];
                patSeqLoopBtn[v][i].setButtonText ("x" + juce::String(lc));
            };
            addChildComponent (patSeqLoopBtn[v][i]);
            patternPageComponents.push_back (&patSeqLoopBtn[v][i]);
        }
    }
    refreshPatSeqMode (0);
    refreshPatSeqMode (1);
}

void VoltageSeq2AudioProcessorEditor::refreshPatSeqMode (int vi)
{
    const int m = audioProcessor.patSeq[vi].mode;
    const auto activeCol   = juce::Colour (0xff2255aa);
    const auto inactiveCol = juce::Colour (0xff161630);
    patSeqOffBtn [vi].setColour (juce::TextButton::buttonColourId, m == 0 ? activeCol : inactiveCol);
    patSeqAutoBtn[vi].setColour (juce::TextButton::buttonColourId, m == 1 ? activeCol : inactiveCol);
    patSeqMidiBtn[vi].setColour (juce::TextButton::buttonColourId, m == 2 ? activeCol : inactiveCol);
    repaint();
}

void VoltageSeq2AudioProcessorEditor::refreshPatPageView (int vi)
{
    const bool showBank = (patPageView[vi] == 0);
    const auto activeCol   = juce::Colour (0xff2255aa);
    const auto inactiveCol = juce::Colour (0xff161630);
    patBankTabBtn[vi].setColour (juce::TextButton::buttonColourId,  showBank ? activeCol : inactiveCol);
    patSeqTabBtn [vi].setColour (juce::TextButton::buttonColourId, !showBank ? activeCol : inactiveCol);

    // Bank tiles visibility
    for (int s = 0; s < 16; ++s)
        patternSlot[vi][s]->setVisible (showBank);

    // Sequencer controls visibility
    const bool showSeq = !showBank;
    patSeqOffBtn  [vi].setVisible (showSeq);
    patSeqAutoBtn [vi].setVisible (showSeq);
    patSeqMidiBtn [vi].setVisible (showSeq);
    patSeqImmBtn  [vi].setVisible (showSeq);
    patSeqLenUpBtn[vi].setVisible (showSeq);
    patSeqLenDnBtn[vi].setVisible (showSeq);

    const int activeLen = juce::jmax (1, audioProcessor.patSeq[vi].listLength);
    for (int i = 0; i < 16; ++i)
    {
        const bool entryVisible = showSeq && (i < activeLen);
        patSeqSlotBox[vi][i].setVisible (entryVisible);
        patSeqLoopBtn[vi][i].setVisible (entryVisible);
    }
    repaint();
}

void VoltageSeq2AudioProcessorEditor::layoutPatternSeqControls()
{
    // Y positions per voice (same zones as the tile rows)
    const int panelY[2]  = { 72, 354 };
    const int labelY[2]  = { 54, 336 };
    constexpr int margin = 8;

    for (int v = 0; v < 2; ++v)
    {
        const int ly = labelY[v];
        const int py = panelY[v];

        // Tab buttons in the label row
        patBankTabBtn[v].setBounds (margin,      ly, 55, 18);
        patSeqTabBtn [v].setBounds (margin + 62, ly, 90, 18);

        // Mode + timing controls  (top of sequencer panel)
        patSeqOffBtn  [v].setBounds (margin,       py,      44, 22);
        patSeqAutoBtn [v].setBounds (margin + 50,  py,      44, 22);
        patSeqMidiBtn [v].setBounds (margin + 100, py,      44, 22);
        patSeqImmBtn  [v].setBounds (margin + 180, py,     110, 22);

        // List length +/- buttons at far right
        patSeqLenUpBtn[v].setBounds (getWidth() - 60, py,  26, 22);
        patSeqLenDnBtn[v].setBounds (getWidth() - 30, py,  26, 22);

        // Playlist entries — two rows of 8, each entry 85px wide
        const int entryW = 85, entryGap = 4;
        const int row1Y  = py + 32;
        const int row2Y  = py + 32 + 50;

        for (int i = 0; i < 16; ++i)
        {
            const int row = i / 8;
            const int col = i % 8;
            const int ex  = margin + col * (entryW + entryGap);
            const int ey  = (row == 0) ? row1Y : row2Y;
            patSeqSlotBox[v][i].setBounds (ex,      ey,      entryW, 22);
            patSeqLoopBtn[v][i].setBounds (ex,      ey + 24, entryW, 20);
        }
    }
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

    for (int vi = 0; vi < 2; ++vi)
    {
        if (audioProcessor.patternChangedForUI[vi].exchange (false))
        {
            syncUIFromProcessor();
            repaint();
        }
    }

    if (audioProcessor.turingMachine.displayDirty.exchange (false))
    {
        // Update bit cell colours to reflect shifted register state
        const auto& tm = audioProcessor.turingMachine;
        const int   tv = audioProcessor.tmTargetVoice.load();
        const juce::Colour vc = (tv == 0)
            ? juce::Colour (0xff00aaff)
            : juce::Colour (0xffaa44ff);
        for (int i = 0; i < 16; ++i)
            tmBitBtn[i].setColour (juce::TextButton::buttonColourId,
                tm.bits[i] ? vc.withAlpha (0.7f) : juce::Colour (0xff0e0e1c));
        repaint();
    }

    // ── Live macro dot: repaint ring regions when a macro value changes ───────
    if (currentPage == 0)
    {
        bool moved = false;
        for (int m = 0; m < kNumMacros; ++m)
        {
            const float v = audioProcessor.macros[m].value.load();
            if (std::abs (v - lastMacroValue[m]) > 0.0005f) { lastMacroValue[m] = v; moved = true; }
        }
        if (moved)
            for (const auto& ri : buildRings())
            {
                const int rr = (int) ri.radius + 6;
                macroOverlay.repaint (juce::Rectangle<int> (
                    (int) ri.centre.x - rr, (int) ri.centre.y - rr, rr * 2, rr * 2));
            }
    }
}

//==============================================================================
// Macro controllers
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupMacros()
{
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto& k = macroKnob[m];
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff00d4aa));
        k.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1a3030));
        k.onRightClick = [this, m]() { showMacroMenu (m); };
        macroAttach[m] = std::make_unique<SliderAtt> (
            audioProcessor.apvts, "macro" + juce::String (m), k);
        addAndMakeVisible (k);
        synthPageComponents.push_back (&k);

        auto& nl = macroNameLabel[m];
        nl.setText ("MACRO " + juce::String (m + 1), juce::dontSendNotification);
        nl.setJustificationType (juce::Justification::centred);
        nl.setColour (juce::Label::textColourId, juce::Colour (0xff00d4aa));
        nl.setFont (juce::Font (12.0f, juce::Font::bold));
        addAndMakeVisible (nl);
        synthPageComponents.push_back (&nl);

        auto& al = macroAssignLabel[m];
        al.setJustificationType (juce::Justification::topLeft);
        al.setColour (juce::Label::textColourId, juce::Colour (0xffb0c4c0));
        al.setFont (juce::Font (10.5f));
        al.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (al);
        synthPageComponents.push_back (&al);

        auto& ab = macroAssignBtn[m];
        ab.setButtonText ("ASSIGN");
        ab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff14242a));
        ab.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff00d4aa));
        ab.onClick = [this, m]() {
            (macroLearnActive == m) ? exitMacroLearn() : enterMacroLearn (m);
        };
        addAndMakeVisible (ab);
        synthPageComponents.push_back (&ab);

        // Sit behind the floating LFO panels so opening an LFO panel covers them
        // (same as the branding zone they replace).
        nl.toBack();
        al.toBack();
        k.toBack();
    }

    // Full-window transparent overlay for learn-mode highlights + click capture.
    macroOverlay.ed = this;
    addAndMakeVisible (macroOverlay);
    macroOverlay.setBounds (getLocalBounds());
    macroOverlay.toFront (false);

    refreshMacroLabels();
}

//------------------------------------------------------------------------------
std::vector<VoltageSeq2AudioProcessorEditor::Assignable>
VoltageSeq2AudioProcessorEditor::buildAssignables()
{
    using AP = VoltageSeq2AudioProcessor;
    std::vector<Assignable> out;
    for (int v = 0; v < 2; ++v)
    {
        out.push_back ({ &cutoffSlider     [v], AP::MT_Cutoff,    v });
        out.push_back ({ &resonanceSlider  [v], AP::MT_Resonance, v });
        out.push_back ({ &filterDriveSlider[v], AP::MT_Drive,     v });
        out.push_back ({ &rangeSlider      [v], AP::MT_Range,     v });
        out.push_back ({ &fmDepthSlider    [v], AP::MT_FM,        v });
        out.push_back ({ &osc1PWMSlider    [v], AP::MT_PWM,       v });
        out.push_back ({ &plaitsHarmSlider [v], AP::MT_Harm,      v });
        out.push_back ({ &plaitsTimSlider  [v], AP::MT_Timbre,    v });
        out.push_back ({ &plaitsMorphSlider[v], AP::MT_Morph,     v });
        // Amp + Filter ADSR (always-visible front-page knobs)
        out.push_back ({ &attackSlider     [v], AP::MT_AmpA,      v });
        out.push_back ({ &decaySlider      [v], AP::MT_AmpD,      v });
        out.push_back ({ &sustainSlider    [v], AP::MT_AmpS,      v });
        out.push_back ({ &releaseSlider    [v], AP::MT_AmpR,      v });
        out.push_back ({ &fAttackSlider    [v], AP::MT_FltA,      v });
        out.push_back ({ &fDecaySlider     [v], AP::MT_FltD,      v });
        out.push_back ({ &fSustainSlider   [v], AP::MT_FltS,      v });
        out.push_back ({ &fReleaseSlider   [v], AP::MT_FltR,      v });
    }
    return out;
}

juce::Rectangle<int>
VoltageSeq2AudioProcessorEditor::assignableScreenBounds (juce::Slider* s)
{
    if (s == nullptr || ! s->isShowing()) return {};
    return getLocalArea (s, s->getLocalBounds());
}

void VoltageSeq2AudioProcessorEditor::enterMacroLearn (int m)
{
    macroLearnActive = m;
    macroOverlay.toFront (false);
    updateMacroAssignBtns();
    macroOverlay.repaint();
}

void VoltageSeq2AudioProcessorEditor::exitMacroLearn()
{
    macroLearnActive = -1;
    updateMacroAssignBtns();
    macroOverlay.repaint();
}

void VoltageSeq2AudioProcessorEditor::updateMacroAssignBtns()
{
    for (int m = 0; m < kNumMacros; ++m)
    {
        const bool on = (macroLearnActive == m);
        macroAssignBtn[m].setButtonText (on ? "CLICK A KNOB" : "ASSIGN");
        macroAssignBtn[m].setColour (juce::TextButton::buttonColourId,
            on ? juce::Colour (0xff00d4aa) : juce::Colour (0xff14242a));
        macroAssignBtn[m].setColour (juce::TextButton::textColourOffId,
            on ? juce::Colour (0xff071518) : juce::Colour (0xff00d4aa));
    }
}

void VoltageSeq2AudioProcessorEditor::assignFromClick (int sliderTarget, int voice, bool both)
{
    if (macroLearnActive < 0) return;
    auto& mac = audioProcessor.macros[macroLearnActive];
    int cnt = mac.count.load();
    if (cnt < VoltageSeq2AudioProcessor::kMaxMacroAssign)
    {
        mac.assign[cnt].target = sliderTarget;
        mac.assign[cnt].scope  = both ? VoltageSeq2AudioProcessor::MS_Both : voice;
        mac.assign[cnt].depth  = 1.0f;
        mac.count.store (cnt + 1);
    }
    refreshMacroLabels();
    exitMacroLearn();
    repaint();
}

//==============================================================================
// Depth rings
//==============================================================================
std::vector<VoltageSeq2AudioProcessorEditor::RingInfo>
VoltageSeq2AudioProcessorEditor::buildRings()
{
    using AP = VoltageSeq2AudioProcessor;
    static const juce::Colour mc[kNumMacros] = {
        juce::Colour (0xff00d4aa), juce::Colour (0xffe09040) };
    std::vector<RingInfo> out;
    auto assignables = buildAssignables();
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto& mac = audioProcessor.macros[m];
        const int n = mac.count.load();
        for (int a = 0; a < n; ++a)
        {
            const auto& as = mac.assign[a];
            for (const auto& ax : assignables)
            {
                if (ax.target != as.target) continue;
                if (! (as.scope == AP::MS_Both || as.scope == ax.voice)) continue;
                auto b = assignableScreenBounds (ax.slider);
                if (b.isEmpty()) continue;
                const float half = b.getWidth() * 0.5f;
                out.push_back ({ m, a, b.getCentre().toFloat(),
                                 half + 3.0f + (float) m * 5.0f, as.depth, mc[m] });
            }
        }
    }
    return out;
}

//==============================================================================
// MacroOverlay — learn highlights + click capture + depth-ring draw/drag
//==============================================================================
bool VoltageSeq2AudioProcessorEditor::MacroOverlay::hitTest (int x, int y)
{
    if (ed == nullptr) return false;
    if (ed->macroLearnActive >= 0) return true;        // learning: capture all
    if (ed->currentPage != 0)      return false;       // rings live on synth page only
    const juce::Point<float> p ((float) x, (float) y);
    for (const auto& ri : ed->buildRings())            // solid only on ring bands
        if (std::abs (ri.centre.getDistanceFrom (p) - ri.radius) <= 5.0f)
            return true;
    return false;
}

void VoltageSeq2AudioProcessorEditor::MacroOverlay::paint (juce::Graphics& g)
{
    if (ed == nullptr || ed->currentPage != 0) return;

    // Learn mode: dim + highlight every visible assignable knob.
    if (ed->macroLearnActive >= 0)
    {
        g.fillAll (juce::Colour (0x33000000));
        const juce::Colour teal (0xff00d4aa);
        for (const auto& a : ed->buildAssignables())
        {
            auto b = ed->assignableScreenBounds (a.slider);
            if (b.isEmpty()) continue;
            g.setColour (teal.withAlpha (0.18f));
            g.fillRoundedRectangle (b.toFloat().expanded (3.0f), 6.0f);
            g.setColour (teal);
            g.drawRoundedRectangle (b.toFloat().expanded (3.0f), 6.0f, 2.0f);
        }
    }

    // Depth rings around assigned knobs (always shown on the synth page).
    for (const auto& ri : ed->buildRings())
    {
        const float cx = ri.centre.x, cy = ri.centre.y, r = ri.radius;
        g.setColour (ri.colour.withAlpha (0.22f));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 2.0f);   // faint track
        const float a0    = -juce::MathConstants<float>::halfPi;    // start position
        const float sweep = ri.depth * juce::MathConstants<float>::pi * 0.8f; // ±144°
        juce::Path arc;
        arc.addCentredArc (cx, cy, r, r, 0.0f, a0, a0 + sweep, true);
        g.setColour (ri.colour);
        g.strokePath (arc, juce::PathStrokeType (2.5f));

        // ── Live modulation dot ───────────────────────────────────────────────
        // Travels along the arc to the current point: macroValue × depth.
        const float mv  = ed->audioProcessor.macros[ri.macro].value.load();
        const float ang = a0 + mv * sweep;            // fraction mv toward depth end
        const float dx  = cx + r * std::sin (ang);    // addCentredArc angle convention
        const float dy  = cy - r * std::cos (ang);
        g.setColour (juce::Colour (0xffff3b30));       // bright red live indicator
        g.fillEllipse (dx - 3.0f, dy - 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (dx - 3.0f, dy - 3.0f, 6.0f, 6.0f, 1.0f);
    }
}

void VoltageSeq2AudioProcessorEditor::MacroOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (ed == nullptr) return;

    // ── Learn mode: assign the clicked knob ───────────────────────────────────
    if (ed->macroLearnActive >= 0)
    {
        const bool both = e.mods.isAltDown();
        for (const auto& a : ed->buildAssignables())
        {
            auto b = ed->assignableScreenBounds (a.slider);
            if (! b.isEmpty() && b.expanded (3).contains (e.getPosition()))
            {
                ed->assignFromClick (a.target, a.voice, both);
                return;
            }
        }
        ed->exitMacroLearn();   // clicked empty space — cancel
        return;
    }

    // ── Otherwise: begin a depth-ring drag (hitTest only let us here on a band)─
    const juce::Point<float> p = e.position;
    for (const auto& ri : ed->buildRings())
    {
        if (std::abs (ri.centre.getDistanceFrom (p) - ri.radius) <= 5.0f)
        {
            ed->dragRingMacro      = ri.macro;
            ed->dragRingAssign     = ri.assignIdx;
            ed->dragRingStartDepth = ri.depth;
            return;
        }
    }
}

void VoltageSeq2AudioProcessorEditor::MacroOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (ed == nullptr || ed->dragRingMacro < 0) return;
    auto& mac = ed->audioProcessor.macros[ed->dragRingMacro];
    if (ed->dragRingAssign >= mac.count.load()) return;
    // Drag up = increase depth. Full sweep (-1..+1) over ~200 px.
    const float d = juce::jlimit (-1.0f, 1.0f,
        ed->dragRingStartDepth + (float) (-e.getDistanceFromDragStartY()) / 200.0f);
    mac.assign[ed->dragRingAssign].depth = d;
    ed->refreshMacroLabels();
    repaint();
}

void VoltageSeq2AudioProcessorEditor::MacroOverlay::mouseUp (const juce::MouseEvent&)
{
    if (ed == nullptr) return;
    ed->dragRingMacro = ed->dragRingAssign = -1;
}

//==============================================================================
// ABOUT page
//==============================================================================
void VoltageSeq2AudioProcessorEditor::setupAboutPage()
{
    // Embedded portrait (placeholder until the real PNG bytes are baked in).
    if (AboutImage::dataSize > 0)
        aboutImage = juce::ImageFileFormat::loadFrom (AboutImage::data, (size_t) AboutImage::dataSize);

    const juce::Colour teal (0xff00d4aa);
    const juce::Colour body (0xffd6dde0);

    aboutTitle.setText (juce::String::fromUTF8 ("ABOUT  \xc2\xb7  MURGATROYD INSTRUMENTS"),
                        juce::dontSendNotification);
    aboutTitle.setFont (juce::Font ("Helvetica Neue", 22.0f, juce::Font::bold));
    aboutTitle.setColour (juce::Label::textColourId, teal);
    addChildComponent (aboutTitle);
    aboutPageComponents.push_back (&aboutTitle);

    auto styleReadOnly = [] (juce::TextEditor& te, juce::Colour col, float sz, bool bold)
    {
        te.setMultiLine (true);
        te.setReadOnly (true);
        te.setCaretVisible (false);
        te.setScrollbarsShown (true);
        te.setPopupMenuEnabled (false);
        te.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x00000000));
        te.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0x00000000));
        te.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0x00000000));
        te.setColour (juce::TextEditor::textColourId, col);
        te.setColour (juce::TextEditor::shadowColourId, juce::Colour (0x00000000));
        te.setFont (juce::Font ("Helvetica Neue", sz, bold ? juce::Font::bold : juce::Font::plain));
    };

    aboutBody.setText (juce::String::fromUTF8 (
        "Hey, I'm Ryan \xe2\x80\x94 owner of Murgatroyd Instruments. Our little company was "
        "founded in 2026 with the vision of bringing unique tools and processes to music "
        "makers around the globe.\n\n"
        "Borrowing heavily from modular ideas and values, we believe the creative process "
        "should be intuitive, rewarding and dynamic. We also believe in making tools that "
        "support music theory and compositional logic \xe2\x80\x94 offering new ways to think "
        "about, create, improvise and perform melodies.\n\n"
        "We'd love to hear from you, and have you tag, share and support our products. "
        "If you haven't checked out the VIDEO MANUAL, it's linked below. Please email me "
        "with any feedback and ideas."), juce::dontSendNotification);
    styleReadOnly (aboutBody, body, 15.0f, false);
    addChildComponent (aboutBody);
    aboutPageComponents.push_back (&aboutBody);

    aboutVideoLink.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6  Watch the Video Manual"));
    aboutVideoLink.setURL (juce::URL ("https://youtu.be/5uqDP0mt4Jw"));
    aboutVideoLink.setColour (juce::HyperlinkButton::textColourId, teal);
    aboutVideoLink.setFont (juce::Font ("Helvetica Neue", 15.0f, juce::Font::bold), false,
                            juce::Justification::centredLeft);
    addChildComponent (aboutVideoLink);
    aboutPageComponents.push_back (&aboutVideoLink);

    aboutEmailLink.setButtonText ("ryan@soulcandi.co.za");
    aboutEmailLink.setURL (juce::URL ("mailto:ryan@soulcandi.co.za"));
    aboutEmailLink.setColour (juce::HyperlinkButton::textColourId, teal);
    aboutEmailLink.setFont (juce::Font ("Helvetica Neue", 15.0f, juce::Font::bold), false,
                            juce::Justification::centredLeft);
    addChildComponent (aboutEmailLink);
    aboutPageComponents.push_back (&aboutEmailLink);

    aboutThanks.setText (juce::String::fromUTF8 (
        "SPECIAL THANKS \xe2\x80\x94 for the inspiration, assistance, insight and help with the "
        "overall design and workflow:\n\n"
        "Phoebe Pemberton  \xc2\xb7  Dash Glitch  \xc2\xb7  Omri Cohen  \xc2\xb7  Mark Valsecchi  \xc2\xb7  "
        "James Carter  \xc2\xb7  Andile Maphmulo  \xc2\xb7  Khumo Ranamane  \xc2\xb7  Ben Mabena  \xc2\xb7  "
        "Matt Flax  \xc2\xb7  Kosta Karatamoglou  \xc2\xb7  Blanka Mazimela  \xc2\xb7  and the rest of the "
        "beta-test team.\n\n"
        "Plugin development assisted by Claude (Anthropic)."), juce::dontSendNotification);
    styleReadOnly (aboutThanks, juce::Colour (0xff9aa6ac), 12.5f, false);
    addChildComponent (aboutThanks);
    aboutPageComponents.push_back (&aboutThanks);
}

void VoltageSeq2AudioProcessorEditor::layoutAboutPage()
{
    constexpr int headerH = 32;
    const int top = headerH + 18;
    const int W   = getWidth();
    const int H   = getHeight();

    // Image occupies the left ~46%, text the right.
    const int imgW = 660;
    const int imgX = 24;
    const int imgY = top;
    const int imgH = H - top - 70;          // leave room for credits strip
    // (the image itself is drawn in paint(); these bounds drive that draw)
    aboutImageBounds = juce::Rectangle<int> (imgX, imgY, imgW, imgH);

    const int tx = imgX + imgW + 36;
    const int tw = W - tx - 28;

    aboutTitle .setBounds (tx, top, tw, 30);
    aboutBody  .setBounds (tx, top + 44, tw, 210);
    aboutVideoLink.setBounds (tx, top + 264, tw, 22);
    aboutEmailLink.setBounds (tx, top + 290, tw, 22);
    aboutThanks.setBounds (tx, top + 326, tw, H - (top + 326) - 24);
}

void VoltageSeq2AudioProcessorEditor::refreshMacroLabels()
{
    static const char* const scopeTag[3] = { "A", "B", "A+B" };
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto& mac = audioProcessor.macros[m];
        const int n = mac.count.load();
        juce::String txt;
        for (int a = 0; a < n; ++a)
        {
            const auto& as = mac.assign[a];
            const int pct = juce::roundToInt (as.depth * 100.0f);
            txt << VoltageSeq2AudioProcessor::kMacroTargetNames[as.target]
                << juce::String::fromUTF8 (" \xc2\xb7 ") << scopeTag[juce::jlimit (0, 2, as.scope)]
                << "  " << (pct >= 0 ? "+" : "") << pct << "%\n";
        }
        if (txt.isEmpty()) txt = "(right-click to assign)";
        macroAssignLabel[m].setText (txt, juce::dontSendNotification);
    }
}

void VoltageSeq2AudioProcessorEditor::showMacroMenu (int m)
{
    using AP = VoltageSeq2AudioProcessor;
    auto& mac = audioProcessor.macros[m];

    juce::PopupMenu menu;

    // ── Add destination → scope submenu → parameter list ──────────────────────
    static const char* const scopeName[3] = { "Voice A", "Voice B", "Both" };
    juce::PopupMenu addMenu;
    for (int scope = 0; scope < 3; ++scope)
    {
        juce::PopupMenu params;
        for (int t = 0; t < AP::MT_Count; ++t)
            params.addItem (1000 + scope * 100 + t, AP::kMacroTargetNames[t]);
        addMenu.addSubMenu (scopeName[scope], params);
    }
    menu.addSubMenu ("Add destination", addMenu);

    // ── Existing assignments → edit depth / remove ────────────────────────────
    static const char* const scopeTag[3] = { "A", "B", "A+B" };
    const int n = mac.count.load();
    if (n > 0)
    {
        menu.addSeparator();
        static const int   depthPct[8]  = { 100, 75, 50, 25, -25, -50, -75, -100 };
        for (int a = 0; a < n; ++a)
        {
            const auto& as = mac.assign[a];
            const int pct = juce::roundToInt (as.depth * 100.0f);
            juce::String title;
            title << AP::kMacroTargetNames[as.target] << " (" << scopeTag[juce::jlimit(0,2,as.scope)]
                  << ")  " << (pct >= 0 ? "+" : "") << pct << "%";

            juce::PopupMenu item;
            juce::PopupMenu depthMenu;
            for (int d = 0; d < 8; ++d)
                depthMenu.addItem (2000 + a * 100 + d,
                                   juce::String (depthPct[d] > 0 ? "+" : "") + juce::String (depthPct[d]) + "%",
                                   true, pct == depthPct[d]);
            item.addSubMenu ("Depth", depthMenu);
            item.addItem (3000 + a, "Remove");
            menu.addSubMenu (title, item);
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (&macroKnob[m]),
        [this, m] (int r)
        {
            if (r == 0) return;
            auto& mac2 = audioProcessor.macros[m];
            if (r >= 1000 && r < 2000)               // add
            {
                const int scope  = (r - 1000) / 100;
                const int target = (r - 1000) % 100;
                int cnt = mac2.count.load();
                if (cnt < VoltageSeq2AudioProcessor::kMaxMacroAssign)
                {
                    mac2.assign[cnt].target = target;
                    mac2.assign[cnt].scope  = scope;
                    mac2.assign[cnt].depth  = 1.0f;
                    mac2.count.store (cnt + 1);      // publish after fields written
                }
            }
            else if (r >= 2000 && r < 3000)          // set depth
            {
                static const int depthPct[8] = { 100, 75, 50, 25, -25, -50, -75, -100 };
                const int a = (r - 2000) / 100;
                const int d = (r - 2000) % 100;
                if (a < mac2.count.load() && d >= 0 && d < 8)
                    mac2.assign[a].depth = depthPct[d] / 100.0f;
            }
            else if (r >= 3000 && r < 4000)          // remove
            {
                const int a   = r - 3000;
                const int cnt = mac2.count.load();
                if (a >= 0 && a < cnt)
                {
                    for (int i = a; i < cnt - 1; ++i) mac2.assign[i] = mac2.assign[i + 1];
                    mac2.count.store (cnt - 1);
                }
            }
            refreshMacroLabels();
            repaint();
        });
}

//==============================================================================
// syncUIFromProcessor
//==============================================================================
void VoltageSeq2AudioProcessorEditor::syncUIFromProcessor()
{


    refreshMacroLabels();   // macro assignments may have changed (preset load)

    for (int v = 0; v < 2; ++v)
    {
        auto& vp = audioProcessor.voice[v];

        for (int i = 0; i < 16; ++i)
        {
            // stepKnob values are owned by APVTS attachments — do not call setValue/setRange here.
            refreshGateBtn (v, i);
            refreshSlideBtn (v, i);
            // Sync velocity overlay from processor state (e.g. after pattern load)
            veloKnob[v][i].setValue (audioProcessor.voice[v].stepVelocity[i],
                                     juce::dontSendNotification);
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
        // Note: step knob ranges are fixed at -5..+5 by the APVTS attachment.
        // Portamento and Range are also APVTS-attached — no manual setValue needed.
        clockDivBox[v].setSelectedItemIndex (vp.clockDivision, juce::dontSendNotification);
        rootBox [v].setSelectedItemIndex (vp.rootNote,     juce::dontSendNotification);
        scaleBox[v].setSelectedItemIndex (vp.currentScale, juce::dontSendNotification);

        osc1WaveBox    [v].setSelectedItemIndex (vp.osc1Waveform,   juce::dontSendNotification);
        osc1LevelSlider[v].setValue (vp.osc1Level,                   juce::dontSendNotification);
        osc1OctaveBox  [v].setSelectedItemIndex (vp.osc1Octave + 2, juce::dontSendNotification);
        osc1PWMSlider  [v].setValue (vp.osc1PulseWidth,              juce::dontSendNotification);
        osc1FeedbackSlider[v].setValue (vp.osc1Feedback,             juce::dontSendNotification);
        driftSlider       [v].setValue (vp.driftAmount,              juce::dontSendNotification);
        // fmDepthSlider, fmRatioSlider — APVTS-attached, auto-synced.
        crossModSlider    [v].setValue (vp.crossModDepth,            juce::dontSendNotification);

        osc2PosSlider  [v].setValue (vp.osc2Position,                juce::dontSendNotification);
        osc2LevelSlider[v].setValue (vp.osc2Level,                   juce::dontSendNotification);
        osc2OctaveBox  [v].setSelectedItemIndex (vp.osc2Octave + 2, juce::dontSendNotification);

        // cutoffSlider, filterEnvAmtSlider — APVTS-attached, auto-synced.
        resonanceSlider   [v].setValue (vp.filterResonance,        juce::dontSendNotification);
        filterDriveSlider [v].setValue (vp.filterDrive,            juce::dontSendNotification);
        filterModeBox     [v].setSelectedItemIndex (vp.filterMode,  juce::dontSendNotification);
        filterSlopeBtn    [v].setToggleState (vp.filterSlope == 1,  juce::dontSendNotification);
        filterSlopeBtn    [v].setButtonText  (vp.filterSlope ? "24" : "12");
        filterSlopeBtn    [v].setColour (juce::TextButton::buttonColourId,
                                         vp.filterSlope ? juce::Colour(0xff2255aa) : juce::Colour(0xff161630));
        // Filter ADSR — APVTS-attached, auto-synced via attachment.

        // Amp ADSR — APVTS-attached, auto-synced via attachment.
        // lfoRate/lfoDepth — APVTS-attached, auto-synced via attachment.
        lfoWaveBox     [v].setSelectedItemIndex (vp.lfoWaveform,  juce::dontSendNotification);
        lfoTargetBox   [v].setSelectedItemIndex (vp.lfoTarget,    juce::dontSendNotification);
        lfoSyncBtn     [v].setToggleState (vp.lfoSync, juce::dontSendNotification);
        lfoSyncBtn     [v].setButtonText  (vp.lfoSync ? "SYNC" : "FREE");
        lfoSyncBtn     [v].setColour (juce::TextButton::buttonColourId, vp.lfoSync ? knobColour : gateOffColour);
        lfoSyncDivBox  [v].setSelectedItemIndex (vp.lfoSyncDiv,   juce::dontSendNotification);

        // lfo2Rate/lfo2Depth — APVTS-attached, auto-synced via attachment.
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

        // MIDI Out
        midiOutBtn[v].setToggleState (vp.midiOutEnabled, juce::dontSendNotification);
        midiOutBtn[v].setColour (juce::TextButton::buttonColourId,
                                  vp.midiOutEnabled ? juce::Colour(0xff228844) : juce::Colour(0xff161630));
        midiOutChBox[v].setSelectedId (vp.midiOutChannel, juce::dontSendNotification);
        midiOutChBox[v].setEnabled    (vp.midiOutEnabled);

        // Unison / Poly
        voiceModeBox   [v].setSelectedId ((int)vp.voiceMode + 1, juce::dontSendNotification);
        uniCountBtn    [v].setButtonText (vp.unisonCount == 2 ? "2V" : "4V");
        uniSpreadSlider[v].setValue (vp.unisonSpread, juce::dontSendNotification);
        uniWidthSlider [v].setValue (vp.unisonWidth,  juce::dontSendNotification);
        chordModeBtn   [v].setColour (juce::TextButton::buttonColourId,
            vp.shiftRegChordMode ? juce::Colour (0xff1a5533) : juce::Colour (0xff161630));
        chordModeBtn   [v].setColour (juce::TextButton::textColourOffId,
            vp.shiftRegChordMode ? juce::Colour (0xff00ff88) : juce::Colour (0xffe0e0e0));
    }

    for (int v = 0; v < 2; ++v)
    {
        plaitsBtn[v].setButtonText (audioProcessor.voice[v].plaitsEnabled ? "PLAITS ●" : "PLAITS");
        plaitsEngBox[v].setSelectedId (audioProcessor.voice[v].plaitsEngine + 1, juce::dontSendNotification);
        // plaitsHarm/Timb/Morph are now APVTS-attached — no manual setValue needed
        plaitsAuxSlider [v].setValue (audioProcessor.voice[v].plaitsAuxBlend,  juce::dontSendNotification);
        plaitsOctBox    [v].setSelectedId (audioProcessor.voice[v].plaitsOctave + 3, juce::dontSendNotification);
        // Only fix up Plaits vs OSC visibility when the synth page is actually showing —
        // calling setVisible() here on any other page would bleed synth controls onto it.
        if (currentPage == 0)
            refreshPlaitsMode (v);
    }

    // FX page — refresh whichever voice tab is currently selected
    syncFxPageFromVoice();

    repaint();
}

//==============================================================================
// Floating panel — open / close
//==============================================================================

// ── OSC ───────────────────────────────────────────────────────────────────────

void VoltageSeq2AudioProcessorEditor::openOscPanel (int v)
{
    // OSC 1 basic controls (wave/level/oct/scope) stay on the front page — not re-parented here.
    // Panel shows OSC 1 advanced (PWM, feedback, drift) + full OSC 2/FM + Plaits.
    constexpr int pw = 500, ph = 200;
    constexpr int kP = 46;
    const int px = 30;
    const int py = v == 0 ? 40 : 52;

    oscPanel[v].setBounds (px, py, pw, ph);
    oscPanel[v].setVisible (true);
    oscPanel[v].toFront (false);

    auto& p = oscPanel[v];

    // ── OSC 1 advanced controls ───────────────────────────────────────────────
    p.addAndMakeVisible (osc1FeedbackSlider[v]);   osc1FeedbackSlider[v].setBounds (8,   60, kP, kP);
    p.addAndMakeVisible (osc1PWMSlider[v]);         osc1PWMSlider[v].setBounds      (62,  60, kP, kP);
    p.addAndMakeVisible (driftSlider[v]);           driftSlider[v].setBounds        (116, 60, kP, kP);

    // ── OSC 2 / FM ────────────────────────────────────────────────────────────
    p.addAndMakeVisible (osc2PosSlider[v]);         osc2PosSlider[v].setBounds      (180, 60, kP, kP);
    p.addAndMakeVisible (osc2LevelSlider[v]);       osc2LevelSlider[v].setBounds    (234, 60, kP, kP);
    p.addAndMakeVisible (fmDepthSlider[v]);         fmDepthSlider[v].setBounds      (288, 60, kP, kP);
    p.addAndMakeVisible (crossModSlider[v]);        crossModSlider[v].setBounds     (342, 60, kP, kP);
    p.addAndMakeVisible (osc2OctaveBox[v]);         osc2OctaveBox[v].setBounds      (180,130, 80, 20);
    p.addAndMakeVisible (fmRatioSlider[v]);         fmRatioSlider[v].setBounds      (264,124,228, 28);
    p.addAndMakeVisible (*wavetableDisplay[v]);     wavetableDisplay[v]->setBounds  (180,163,312, 18);

    // ── Plaits (re-parent even if hidden — refreshPlaitsMode manages visibility)
    p.addAndMakeVisible (plaitsEngBox[v]);          plaitsEngBox[v].setBounds       (8,  50, 484, 22);
    p.addAndMakeVisible (plaitsHarmSlider[v]);      plaitsHarmSlider[v].setBounds   (8,  88, kP, kP);
    p.addAndMakeVisible (plaitsTimSlider[v]);       plaitsTimSlider[v].setBounds    (66, 88, kP, kP);
    p.addAndMakeVisible (plaitsMorphSlider[v]);     plaitsMorphSlider[v].setBounds  (124,88, kP, kP);
    p.addAndMakeVisible (plaitsAuxSlider[v]);       plaitsAuxSlider[v].setBounds    (182,88, kP, kP);
    p.addAndMakeVisible (plaitsTrigBtn[v]);         plaitsTrigBtn[v].setBounds      (8, 154, 90, 22);
    p.addAndMakeVisible (plaitsOctBox[v]);          plaitsOctBox[v].setBounds       (104,154, 90, 22);

    oscPanelOpen[v] = true;
    macroOverlay.toFront (false);   // keep learn/ring overlay above the panel
    macroOverlay.repaint();

    // ── Parameter labels drawn inside the panel ───────────────────────────────
    oscPanel[v].paintLabels = [](juce::Graphics& g)
    {
        const auto dim  = juce::Colour (0xff888899);
        const auto txt  = juce::Colour (0xffcccccc);
        // Fitted draws shrink text to its box so it can't overlap on Windows fonts.
        auto lblL = [&g](const char* t, int x, int y, int w, int h)
        { g.drawFittedText (t, x, y, w, h, juce::Justification::centredLeft, 1); };
        auto lblC = [&g](const char* t, int x, int y, int w, int h)
        { g.drawFittedText (t, x, y, w, h, juce::Justification::centred, 1); };

        g.setFont (juce::Font ("Helvetica Neue", 8.5f, juce::Font::bold));

        // Sub-section headers
        g.setColour (dim);
        lblL ("OSC 1 +", 8, 28, 160, 12);
        g.setColour (juce::Colour (0xff555566));
        g.fillRect (167, 28, 1, 160);   // divider line
        g.setColour (dim);
        lblL ("OSC 2",  180, 28, 312, 12);

        // OSC 1 advanced knob labels
        g.setColour (txt);
        g.setFont (juce::Font ("Helvetica Neue", 8.0f, juce::Font::plain));
        lblC ("FDBK",  8,   112, 46, 10);
        lblC ("PWM",   62,  112, 46, 10);
        lblC ("DRIFT", 116, 112, 46, 10);

        // OSC 2 knob labels
        lblC ("POS",   180, 112, 46, 10);
        lblC ("LVL",   234, 112, 46, 10);
        lblC ("FM",    288, 112, 46, 10);
        lblC ("XMOD",  342, 112, 46, 10);
        lblC ("OCT",   180, 153, 80, 10);
        lblL ("RATIO", 264, 153, 80, 10);
        lblC ("WT",    180, 182, 312, 10);

        // Plaits labels (always drawn — refreshPlaitsMode hides/shows controls)
        g.setFont (juce::Font ("Helvetica Neue", 8.5f, juce::Font::bold));
        g.setColour (dim);
        lblC ("ENGINE", 8, 28, 484, 12);
        g.setColour (txt);
        g.setFont (juce::Font ("Helvetica Neue", 8.0f, juce::Font::plain));
        lblC ("HARM",   8,   140, 46, 10);
        lblC ("TIMBRE", 66,  140, 46, 10);
        lblC ("MORPH",  124, 140, 46, 10);
        lblC ("AUX",    182, 140, 46, 10);
    };

    // Update button appearance
    oscPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a1a00));
    oscPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe09040));

    // Re-apply plaits/osc visibility within the panel
    refreshPlaitsMode (v);
}

void VoltageSeq2AudioProcessorEditor::closeOscPanel (int v)
{
    // Re-parent panel-only controls back to editor WITHOUT making visible.
    // osc1WaveBox/osc1LevelSlider/osc1OctaveBox/oscScope stay in editor always — not touched here.
    addChildComponent (osc1FeedbackSlider[v]);
    addChildComponent (osc1PWMSlider[v]);
    addChildComponent (driftSlider[v]);
    addChildComponent (osc2PosSlider[v]);
    addChildComponent (osc2LevelSlider[v]);
    addChildComponent (fmDepthSlider[v]);
    addChildComponent (osc2OctaveBox[v]);
    addChildComponent (fmRatioSlider[v]);
    addChildComponent (crossModSlider[v]);
    addChildComponent (*wavetableDisplay[v]);
    addChildComponent (plaitsEngBox[v]);
    addChildComponent (plaitsHarmSlider[v]);
    addChildComponent (plaitsTimSlider[v]);
    addChildComponent (plaitsMorphSlider[v]);
    addChildComponent (plaitsAuxSlider[v]);
    addChildComponent (plaitsTrigBtn[v]);
    addChildComponent (plaitsOctBox[v]);

    oscPanel[v].paintLabels = nullptr;   // clear label painter
    oscPanel[v].setVisible (false);
    oscPanelOpen[v] = false;
    oscPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
    oscPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));

    // Ensure panel-only OSC controls are hidden; front-page controls stay visible
    applySectionVisibility (v);
    // Re-apply Plaits state so front-page OSC1 controls show/hide correctly
    refreshPlaitsMode (v);
    macroOverlay.toFront (false);   // re-assert overlay topmost; refresh rings
    macroOverlay.repaint();
}

// ── ENV ───────────────────────────────────────────────────────────────────────

void VoltageSeq2AudioProcessorEditor::openEnvPanel (int v)
{
    // Amp ADSR stays on front page — this panel shows Mod ENV only.
    constexpr int pw = 300, ph = 185;
    constexpr int kP = 46;
    const int px = 600;
    const int py = v == 0 ? 40 : 52;

    envPanel[v].setBounds (px, py, pw, ph);
    envPanel[v].setVisible (true);
    envPanel[v].toFront (false);

    auto& p = envPanel[v];

    // Mod Env only
    p.addAndMakeVisible (modEnvAtkSlider[v]);    modEnvAtkSlider[v].setBounds   (8,   60, kP, kP);
    p.addAndMakeVisible (modEnvDecSlider[v]);    modEnvDecSlider[v].setBounds   (62,  60, kP, kP);
    p.addAndMakeVisible (modEnvSusSlider[v]);    modEnvSusSlider[v].setBounds   (116, 60, kP, kP);
    p.addAndMakeVisible (modEnvRelSlider[v]);    modEnvRelSlider[v].setBounds   (170, 60, kP, kP);
    p.addAndMakeVisible (modEnvDepthSlider[v]);  modEnvDepthSlider[v].setBounds (8,   120, kP, kP);
    p.addAndMakeVisible (modEnvDestBox[v]);      modEnvDestBox[v].setBounds     (62,  124, 130, 20);
    p.addAndMakeVisible (modEnvSyncBtn[v]);      modEnvSyncBtn[v].setBounds     (198, 124, 48, 20);
    p.addAndMakeVisible (modEnvDivBox[v]);       modEnvDivBox[v].setBounds      (250, 124, 42, 20);

    envPanelOpen[v] = true;

    // ── Parameter labels drawn inside the panel ───────────────────────────────
    envPanel[v].paintLabels = [](juce::Graphics& g)
    {
        const auto txt = juce::Colour (0xffcccccc);
        g.setColour (txt);
        g.setFont (juce::Font ("Helvetica Neue", 8.0f, juce::Font::plain));
        auto lbl = [&g](const char* t, int x, int y, int w, int h)
        { g.drawFittedText (t, x, y, w, h, juce::Justification::centred, 1); };
        // ADSR knob labels (row 1)
        lbl ("ATK",   8,   112, 46, 10);
        lbl ("DEC",   62,  112, 46, 10);
        lbl ("SUS",   116, 112, 46, 10);
        lbl ("REL",   170, 112, 46, 10);
        // Row 2 labels
        lbl ("DEPTH", 8,   167, 46, 10);
        lbl ("DEST",  62,  145, 130, 10);
        lbl ("SYNC",  198, 145, 48,  10);
        lbl ("DIV",   250, 145, 42,  10);
    };

    envPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a1a00));
    envPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe09040));
}

void VoltageSeq2AudioProcessorEditor::closeEnvPanel (int v)
{
    // Amp ADSR + envResetBtn stay on front page — only re-parent Mod ENV controls.
    addChildComponent (modEnvAtkSlider[v]);
    addChildComponent (modEnvDecSlider[v]);
    addChildComponent (modEnvSusSlider[v]);
    addChildComponent (modEnvRelSlider[v]);
    addChildComponent (modEnvDepthSlider[v]);
    addChildComponent (modEnvDestBox[v]);
    addChildComponent (modEnvSyncBtn[v]);
    addChildComponent (modEnvDivBox[v]);

    envPanel[v].paintLabels = nullptr;
    envPanel[v].setVisible (false);
    envPanelOpen[v] = false;
    envPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
    envPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
    applySectionVisibility (v);
}

// ── LFO ───────────────────────────────────────────────────────────────────────

void VoltageSeq2AudioProcessorEditor::openLfoPanel (int v)
{
    constexpr int pw = 430, ph = 210;
    constexpr int kP = 44;
    constexpr int colW = 104;
    const int px = 950;
    const int py = v == 0 ? 40 : 52;

    lfoPanel[v].setBounds (px, py, pw, ph);
    lfoPanel[v].setVisible (true);
    lfoPanel[v].toFront (false);

    auto& p = lfoPanel[v];

    // Helper: layout one LFO column
    auto layoutLfo = [&](int col,
                         juce::ComboBox& wave, juce::Slider& rate, juce::Slider& depth,
                         juce::ComboBox& target, juce::TextButton& sync, juce::ComboBox& div)
    {
        const int cx = 5 + col * colW;
        p.addAndMakeVisible (wave);   wave.setBounds   (cx,    44, 94, 18);
        p.addAndMakeVisible (rate);   rate.setBounds   (cx,    76, kP, kP);
        p.addAndMakeVisible (depth);  depth.setBounds  (cx+50, 76, kP, kP);
        p.addAndMakeVisible (target); target.setBounds (cx,   130, 94, 18);
        p.addAndMakeVisible (sync);   sync.setBounds   (cx,   152, 44, 18);
        p.addAndMakeVisible (div);    div.setBounds    (cx+48,152, 46, 18);
    };

    layoutLfo (0, lfoWaveBox[v],  lfoRateSlider[v],  lfoDepthSlider[v],  lfoTargetBox[v],  lfoSyncBtn[v],  lfoSyncDivBox[v]);
    layoutLfo (1, lfo2WaveBox[v], lfo2RateSlider[v], lfo2DepthSlider[v], lfo2TargetBox[v], lfo2SyncBtn[v], lfo2SyncDivBox[v]);
    layoutLfo (2, lfo3WaveBox[v], lfo3RateSlider[v], lfo3DepthSlider[v], lfo3TargetBox[v], lfo3SyncBtn[v], lfo3SyncDivBox[v]);
    layoutLfo (3, lfo4WaveBox[v], lfo4RateSlider[v], lfo4DepthSlider[v], lfo4TargetBox[v], lfo4SyncBtn[v], lfo4SyncDivBox[v]);

    lfoPanelOpen[v] = true;

    // ── Parameter labels drawn inside the panel ───────────────────────────────
    lfoPanel[v].paintLabels = [colW](juce::Graphics& g)
    {
        const auto dim = juce::Colour (0xff888899);
        const auto txt = juce::Colour (0xffcccccc);
        const char* headers[] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4" };

        // drawFittedText shrinks to fit its box, so labels never collide
        // horizontally regardless of the platform font's metrics.
        auto lbl = [&g](const char* t, int x, int y, int w, int h)
        { g.drawFittedText (t, x, y, w, h, juce::Justification::centred, 1); };

        for (int col = 0; col < 4; ++col)
        {
            const int cx = 5 + col * colW;
            // Each label sits in a free gap between controls (no vertical overlap):
            //   24-44 header+WAVE | 44-62 combo | 62-76 RATE/DEPTH | 76-120 knobs
            //   120-130 TARGET | 130-148 combo | 152-170 sync/div | 172+ SYNC/DIV
            g.setColour (dim);
            g.setFont (juce::Font ("Helvetica Neue", 8.5f, juce::Font::bold));
            lbl (headers[col], cx, 24, 94, 9);
            g.setColour (txt);
            g.setFont (juce::Font ("Helvetica Neue", 8.0f, juce::Font::plain));
            lbl ("WAVE",   cx,    34,  94, 9);
            lbl ("RATE",   cx,    64,  44, 10);
            lbl ("DEPTH",  cx+50, 64,  44, 10);
            lbl ("TARGET", cx,    120, 94, 9);
            lbl ("SYNC",   cx,    172, 44, 10);
            lbl ("DIV",    cx+48, 172, 46, 10);
        }
    };

    lfoPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff2a1a00));
    lfoPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe09040));
}

void VoltageSeq2AudioProcessorEditor::closeLfoPanel (int v)
{
    addChildComponent (lfoWaveBox[v]);   addChildComponent (lfoRateSlider[v]);   addChildComponent (lfoDepthSlider[v]);
    addChildComponent (lfoTargetBox[v]); addChildComponent (lfoSyncBtn[v]);      addChildComponent (lfoSyncDivBox[v]);
    addChildComponent (lfo2WaveBox[v]);  addChildComponent (lfo2RateSlider[v]);  addChildComponent (lfo2DepthSlider[v]);
    addChildComponent (lfo2TargetBox[v]);addChildComponent (lfo2SyncBtn[v]);     addChildComponent (lfo2SyncDivBox[v]);
    addChildComponent (lfo3WaveBox[v]);  addChildComponent (lfo3RateSlider[v]);  addChildComponent (lfo3DepthSlider[v]);
    addChildComponent (lfo3TargetBox[v]);addChildComponent (lfo3SyncBtn[v]);     addChildComponent (lfo3SyncDivBox[v]);
    addChildComponent (lfo4WaveBox[v]);  addChildComponent (lfo4RateSlider[v]);  addChildComponent (lfo4DepthSlider[v]);
    addChildComponent (lfo4TargetBox[v]);addChildComponent (lfo4SyncBtn[v]);     addChildComponent (lfo4SyncDivBox[v]);

    lfoPanel[v].paintLabels = nullptr;
    lfoPanel[v].setVisible (false);
    lfoPanelOpen[v] = false;
    lfoPanelBtn[v].setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
    lfoPanelBtn[v].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
    applySectionVisibility (v);
}

//==============================================================================
void VoltageSeq2AudioProcessorEditor::applyCfgView (int v)
{
    const bool midi = (cfgView[v] == 0);
    midiViewBtn [v].setToggleState ( midi, juce::dontSendNotification);
    voiceViewBtn[v].setToggleState (!midi, juce::dontSendNotification);
    // MIDI group
    midiOutBtn  [v].setVisible (midi);
    midiOutChBox[v].setVisible (midi);
    // VOICE group
    voiceModeBox   [v].setVisible (!midi);
    uniCountBtn    [v].setVisible (!midi);
    chordModeBtn   [v].setVisible (!midi);
    uniSpreadSlider[v].setVisible (!midi);
    uniWidthSlider [v].setVisible (!midi);
}

void VoltageSeq2AudioProcessorEditor::applyToolsView (int v)
{
    const bool on = (toolsView[v] != 0);
    toolsBtn[v].setToggleState (on, juce::dontSendNotification);

    // Row C — reset/uni · randomise · nudge
    resetBtn     [v].setVisible (on);
    bipolarBtn   [v].setVisible (on);
    randModeBtn  [v].setVisible (on);
    randomBtn    [v].setVisible (on);
    nudgeLeftBtn [v].setVisible (on);
    nudgeRightBtn[v].setVisible (on);

    // Row D — MIDI/VOICE config radio + the active sub-group
    midiViewBtn [v].setVisible (on);
    voiceViewBtn[v].setVisible (on);
    if (on)
    {
        applyCfgView (v);   // shows the active sub-group, hides the other
    }
    else
    {
        midiOutBtn  [v].setVisible (false);
        midiOutChBox[v].setVisible (false);
        voiceModeBox   [v].setVisible (false);
        uniCountBtn    [v].setVisible (false);
        chordModeBtn   [v].setVisible (false);
        uniSpreadSlider[v].setVisible (false);
        uniWidthSlider [v].setVisible (false);
    }

    repaint();
}

void VoltageSeq2AudioProcessorEditor::applyMidView (int v)
{
    const bool quant = (midView[v] == 0);
    quantViewBtn[v].setToggleState ( quant, juce::dontSendNotification);
    orderViewBtn[v].setToggleState (!quant, juce::dontSendNotification);
    // QUANT view — Root / Scale / Clock
    rootBox    [v].setVisible (quant);
    scaleBox   [v].setVisible (quant);
    clockDivBox[v].setVisible (quant);
    // ORDER view — play-order buttons
    playFwdBtn [v].setVisible (!quant);
    playRevBtn [v].setVisible (!quant);
    playConvBtn[v].setVisible (!quant);
    playRndBtn [v].setVisible (!quant);
    repaint();
}

void VoltageSeq2AudioProcessorEditor::refreshOscView (int v)
{
    const bool plaits = audioProcessor.voice[v].plaitsEnabled;
    const bool o1 = (!plaits && oscView[v] == 0);
    const bool o2 = (!plaits && oscView[v] == 1);

    oscView1Btn[v].setToggleState (oscView[v] == 0, juce::dontSendNotification);
    oscView2Btn[v].setToggleState (oscView[v] == 1, juce::dontSendNotification);
    oscView1Btn[v].setEnabled (!plaits);   // greyed when Plaits active
    oscView2Btn[v].setEnabled (!plaits);

    // OSC 1 native
    osc1WaveBox[v].setVisible (o1);  osc1LevelSlider[v].setVisible (o1);
    osc1OctaveBox[v].setVisible (o1); osc1PWMSlider[v].setVisible (o1);
    osc1FeedbackSlider[v].setVisible (o1); driftSlider[v].setVisible (o1);
    oscScope[v]->setVisible (o1);
    // OSC 2 native
    osc2PosSlider[v].setVisible (o2); osc2LevelSlider[v].setVisible (o2);
    fmDepthSlider[v].setVisible (o2); crossModSlider[v].setVisible (o2);
    osc2OctaveBox[v].setVisible (o2); fmRatioSlider[v].setVisible (o2);
    wavetableDisplay[v]->setVisible (o2);
    // PLAITS engine
    plaitsEngBox[v].setVisible (plaits); plaitsHarmSlider[v].setVisible (plaits);
    plaitsTimSlider[v].setVisible (plaits); plaitsMorphSlider[v].setVisible (plaits);
    plaitsAuxSlider[v].setVisible (plaits); plaitsTrigBtn[v].setVisible (plaits);
    plaitsOctBox[v].setVisible (plaits);

    repaint();
}

void VoltageSeq2AudioProcessorEditor::applySectionVisibility (int v)
{
    // Section controls are hidden by default — only visible when their panel is open.
    // Called after any bulk-show (showPage) or panel close to enforce the collapsed state.
    for (auto* c : oscSectionComps[v]) c->setVisible (oscPanelOpen[v]);
    for (auto* c : envSectionComps[v]) c->setVisible (envPanelOpen[v]);
    for (auto* c : lfoSectionComps[v]) c->setVisible (lfoPanelOpen[v]);
}
