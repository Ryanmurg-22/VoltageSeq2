
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "BackplateData.h"
#include "VoltageSeqLookAndFeel.h"

//==============================================================================
// Oscilloscope — live pre-filter waveform with zero-crossing trigger
//==============================================================================
class OscScopeComponent : public juce::Component, public juce::Timer
{
public:
    OscScopeComponent (VoltageSeq2AudioProcessor& p, int voiceIndex)
        : proc (p), vi (voiceIndex) { startTimerHz (30); }
    ~OscScopeComponent() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    VoltageSeq2AudioProcessor& proc;
    int vi;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscScopeComponent)
};

//==============================================================================
// Wavetable display — mathematical OSC 2 morph render
//==============================================================================
class WavetableDisplayComponent : public juce::Component, public juce::Timer
{
public:
    WavetableDisplayComponent (VoltageSeq2AudioProcessor& p, int voiceIndex)
        : proc (p), vi (voiceIndex) { startTimerHz (30); }
    ~WavetableDisplayComponent() override { stopTimer(); }
    void paint (juce::Graphics& g) override;
    void timerCallback() override { repaint(); }
private:
    VoltageSeq2AudioProcessor& proc;
    int vi;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableDisplayComponent)
};

//==============================================================================
// Pattern slot — one clickable tile in the pattern bank page
//==============================================================================
class PatternSlotView : public juce::Component
{
public:
    PatternSlotView (VoltageSeq2AudioProcessor& p, int voiceIdx, int slotIdx)
        : proc (p), vi (voiceIdx), slot (slotIdx) {}

    // Called after a successful load (so editor can update active highlight + sync UI)
    std::function<void()> onLoaded;

    void setActive (bool a) { active = a; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& ps = proc.patternBank[vi][slot];

        // Background
        auto bgCol = ps.used ? juce::Colour (0xff141420) : juce::Colour (0xff0c0c14);
        if (active) bgCol = juce::Colour (0xff0e1e10);
        g.fillAll (bgCol);

        // Border
        auto border = active           ? juce::Colour (0xff33cc55)
                    : ps.used          ? juce::Colour (0xff2a3a88)
                                       : juce::Colour (0xff1a1a2a);
        g.setColour (border);
        g.drawRect (getLocalBounds(), active ? 2 : 1);

        // Slot number
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.setColour (active ? juce::Colour (0xff33cc55) : juce::Colour (0xff444466));
        g.drawText (juce::String (slot + 1), 5, 4, 22, 12, juce::Justification::centredLeft);

        if (!ps.used)
        {
            g.setFont (juce::Font (11.0f));
            g.setColour (juce::Colour (0xff252535));
            g.drawText ("EMPTY", getLocalBounds(), juce::Justification::centred);
            return;
        }

        // Root + scale name
        static const char* noteNames[]  = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        static const char* scaleNames[] = { "Maj","Min","Dor","Phr","Lyd","Mix","PMaj","PMin","Chr" };
        juce::String label = noteNames[juce::jlimit (0, 11, ps.rootNote)];
        label += " ";
        label += scaleNames[juce::jlimit (0, 8, ps.currentScale)];
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.setColour (juce::Colour (0xffe09040));
        g.drawText (label, 0, 18, getWidth(), 14, juce::Justification::centred);

        // Mini 16-step gate grid
        const int gridX = 5, gridY = 38;
        const int gridW = getWidth() - 10;
        const int gridH = 32;
        const int sw    = gridW / 16;
        for (int i = 0; i < 16; ++i)
        {
            bool inLen = (i < ps.sequenceLength);
            bool gate  = ps.stepGates[i];
            bool tied  = inLen && ps.stepTied[i];
            int  rep   = inLen ? ps.stepRepeats[i] : 0;
            int  puls  = inLen ? ps.stepPulses[i]  : 1;
            auto col   = !inLen ? juce::Colour (0xff0a0a12)
                       : !gate  ? juce::Colour (0xff1a1a2c)
                       : tied   ? juce::Colour (0xff704010)
                       : rep > 0? juce::Colour (0xff2233aa)
                                : juce::Colour (0xff2255cc);
            g.setColour (col);
            g.fillRect (gridX + i * sw, gridY, sw - 1, gridH);
            // Glide marker
            if (inLen && ps.stepGlides[i]) {
                g.setColour (juce::Colour (0xffe94560));
                g.fillRect (gridX + i * sw, gridY + gridH - 3, sw - 1, 3);
            }
            // Ratchet count label (2/3/4)
            if (inLen && gate && rep > 0) {
                g.setFont (juce::Font (7.0f, juce::Font::bold));
                g.setColour (juce::Colour (0xffaabbff));
                g.drawText (juce::String (rep + 1),
                            gridX + i * sw, gridY + 2, sw - 1, 10,
                            juce::Justification::centred);
            }
            // Pulse count bar at bottom of step cell (cyan, proportional width)
            if (inLen && puls > 1) {
                g.setColour (juce::Colour (0xff00d4aa).withAlpha (0.7f));
                g.fillRect (gridX + i * sw, gridY + gridH - 5, (sw - 1) * puls / 8, 2);
            }
        }

        // Length + div info at bottom
        static const char* divNames[] = { "1/4","1/8","1/16","1/8T","1/16T","1/8.","1/16." };
        juce::String info = "LEN:" + juce::String (ps.sequenceLength)
                          + "  " + divNames[juce::jlimit (0, 6, ps.clockDivision)];
        g.setFont (juce::Font (8.5f));
        g.setColour (juce::Colour (0xff445566));
        g.drawText (info, 0, gridY + gridH + 5, getWidth(), 12, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Save current pattern here");
            if (proc.patternBank[vi][slot].used)
                menu.addItem (2, "Clear this slot");
            menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetComponent (this),
                [this](int result)
                {
                    if (result == 1)
                    {
                        proc.savePattern (vi, slot);
                        if (onLoaded) onLoaded();
                        repaint();
                    }
                    else if (result == 2)
                    {
                        proc.clearPattern (vi, slot);
                        if (active) { active = false; }
                        repaint();
                    }
                });
        }
        else if (proc.patternBank[vi][slot].used)
        {
            proc.loadPattern (vi, slot);
            if (onLoaded) onLoaded();
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { repaint(); }

private:
    VoltageSeq2AudioProcessor& proc;
    int vi, slot;
    bool active = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternSlotView)
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
    void timerCallback() override;   // step highlight + state poll

private:
    VoltageSeq2AudioProcessor& audioProcessor;

    // ── LookAndFeel — must be declared before any child components ────────────
    VoltageSeqLookAndFeel voltageSeqLAF;

    // ── Per-voice controls  [vi][step] or [vi] ───────────────────────────────
    juce::Slider     stepKnob  [2][16];
    juce::Slider     veloKnob  [2][16];   // velocity overlay (same bounds, amber, 1-127)
    juce::TextButton veloModeBtn[2];      // VELO toggle — switches the voice into velocity view
    bool             veloMode  [2] = { false, false };
    juce::TextButton gateBtn   [2][16];
    juce::TextButton slideBtn  [2][16];

    juce::Slider     seqLengthSlider [2];
    juce::Slider     swingSlider     [2];
    juce::TextButton playFwdBtn  [2], playRevBtn  [2];
    juce::TextButton playConvBtn [2], playRndBtn  [2];
    juce::TextButton resetBtn    [2];
    juce::TextButton bipolarBtn  [2];
    juce::TextButton nudgeLeftBtn[2], nudgeRightBtn[2];
    juce::TextButton runStopBtn  [2];
    juce::TextButton envResetBtn    [2];
    juce::TextButton pulseModeBtn   [2];
    juce::ComboBox   pulseLenBox    [2];

    juce::Slider     portaSlider     [2];
    juce::Slider     rangeSlider     [2];
    juce::ComboBox   clockDivBox     [2];

    juce::ComboBox   rootBox   [2];
    juce::ComboBox   scaleBox  [2];

    // ── Plaits (per voice) ──────────────────────────────────────────────────────
    juce::TextButton  plaitsBtn    [2];    // PLAITS ON/OFF toggle
    juce::ComboBox    plaitsEngBox [2];    // engine selector 0-23
    juce::Slider      plaitsHarmSlider[2]; // HARMONICS
    juce::Slider      plaitsTimSlider [2]; // TIMBRE
    juce::Slider      plaitsMorphSlider[2];// MORPH
    juce::Slider      plaitsAuxSlider  [2];// AUX BLEND
    juce::TextButton  plaitsTrigBtn    [2];// TRIG mode (internal LPG vs free-running)
    juce::ComboBox    plaitsOctBox     [2];// octave transpose -2..+2
    void              refreshPlaitsMode (int v); // show/hide plaits vs osc controls

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    juce::ComboBox osc1WaveBox   [2];
    juce::Slider   osc1LevelSlider [2];
    juce::ComboBox osc1OctaveBox [2];
    juce::Slider   osc1PWMSlider [2];
    juce::Slider   osc1FeedbackSlider [2];
    juce::Slider   driftSlider [2];

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   osc2PosSlider   [2];
    juce::Slider   osc2LevelSlider [2];
    juce::ComboBox osc2OctaveBox   [2];

    // ── FM ─────────────────────────────────────────────────────────────────────
    juce::Slider   fmDepthSlider  [2];
    juce::Slider   fmRatioSlider  [2];
    juce::Slider   crossModSlider [2];

    // ── Filter ────────────────────────────────────────────────────────────────
    juce::Slider     cutoffSlider       [2];
    juce::Slider     resonanceSlider    [2];
    juce::Slider     filterEnvAmtSlider [2];
    juce::Slider     filterDriveSlider  [2];
    juce::ComboBox   filterModeBox      [2];
    juce::TextButton filterSlopeBtn     [2];

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    juce::Slider   attackSlider  [2];
    juce::Slider   decaySlider   [2];
    juce::Slider   sustainSlider [2];
    juce::Slider   releaseSlider [2];

    // ── Filter Envelope ───────────────────────────────────────────────────────
    juce::Slider   fAttackSlider  [2];
    juce::Slider   fDecaySlider   [2];
    juce::Slider   fSustainSlider [2];
    juce::Slider   fReleaseSlider [2];

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfoRateSlider  [2];
    juce::Slider   lfoDepthSlider [2];
    juce::ComboBox lfoTargetBox   [2];
    juce::ComboBox lfoWaveBox     [2];
    juce::TextButton lfoSyncBtn   [2];
    juce::ComboBox lfoSyncDivBox  [2];

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo2RateSlider  [2];
    juce::Slider   lfo2DepthSlider [2];
    juce::ComboBox lfo2TargetBox   [2];
    juce::ComboBox lfo2WaveBox     [2];
    juce::TextButton lfo2SyncBtn   [2];
    juce::ComboBox lfo2SyncDivBox  [2];

    // ── LFO 3 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo3RateSlider  [2];
    juce::Slider   lfo3DepthSlider [2];
    juce::ComboBox lfo3TargetBox   [2];
    juce::ComboBox lfo3WaveBox     [2];
    juce::TextButton lfo3SyncBtn   [2];
    juce::ComboBox lfo3SyncDivBox  [2];

    // ── LFO 4 ─────────────────────────────────────────────────────────────────
    juce::Slider   lfo4RateSlider  [2];
    juce::Slider   lfo4DepthSlider [2];
    juce::ComboBox lfo4TargetBox   [2];
    juce::ComboBox lfo4WaveBox     [2];
    juce::TextButton lfo4SyncBtn   [2];
    juce::ComboBox lfo4SyncDivBox  [2];

    // ── Mod Envelope (per voice) ───────────────────────────────────────────────
    juce::Slider     modEnvAtkSlider  [2];
    juce::Slider     modEnvDecSlider  [2];
    juce::Slider     modEnvSusSlider  [2];
    juce::Slider     modEnvRelSlider  [2];
    juce::Slider     modEnvDepthSlider[2];
    juce::ComboBox   modEnvDestBox    [2];
    juce::TextButton modEnvSyncBtn    [2];
    juce::ComboBox   modEnvDivBox     [2];

    // ── Per-voice scope / WT displays ─────────────────────────────────────────
    std::unique_ptr<OscScopeComponent>         oscScope       [2];
    std::unique_ptr<WavetableDisplayComponent> wavetableDisplay [2];

    // ── Shared controls (single instance, sits in Voice A panel) ──────────────
    juce::TextButton randomBtn[2];          // trigger: fires RAND
    juce::TextButton randModeBtn[2];        // cycles: GATE → PITCH → BOTH
    int              randMode[2] = { 0, 0 }; // 0=GATE, 1=PITCH, 2=BOTH
    juce::TextButton patTransposeUpBtn[2], patTransposeDnBtn[2];
    juce::TextButton savePresetBtn, loadPresetBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── Page navigation ───────────────────────────────────────────────────────
    juce::TextButton synthPageBtn, patternPageBtn;
    int currentPage = 0;  // 0=synth 1=pattern 2=fx

    // ── Pattern bank page ─────────────────────────────────────────────────────
    std::unique_ptr<PatternSlotView> patternSlot[2][16];
    int activePatternSlot[2] = { -1, -1 };
    juce::ComboBox   saveToBox[2];   // slot picker 1-16
    juce::TextButton saveBtn  [2];   // "SAVE" — saves current voice to chosen slot

    // Component lists for bulk show/hide
    std::vector<juce::Component*> synthPageComponents;
    std::vector<juce::Component*> patternPageComponents;

    // ── FX page controls ─────────────────────────────────────────────────────
    // Tape Delay
    juce::TextButton delayOnBtn;
    juce::TextButton delayPingPongBtn;
    juce::TextButton delaySyncBtn;
    juce::ComboBox   delaySyncDivBox;
    juce::Slider     delayTimeMsSlider;
    juce::Slider     delayFeedbackSlider;
    juce::Slider     delayMixSlider;
    // Tape character + Bernoulli gate
    juce::Slider     delayWowSlider;
    juce::Slider     delayFlutterSlider;
    juce::Slider     delaySatSlider;
    juce::Slider     delayProbSlider;
    // Reverb
    juce::TextButton reverbOnBtn;
    juce::Slider     reverbSizeSlider;
    juce::Slider     reverbDampingSlider;
    juce::Slider     reverbPreDelaySlider;
    juce::Slider     reverbMixSlider;
    // Chorus
    juce::TextButton chorusOnBtn;
    juce::Slider     chorusRateSlider;
    juce::Slider     chorusDepthSlider;
    juce::Slider     chorusMixSlider;
    // Master
    juce::Slider     masterDriveSlider;
    juce::Slider     masterGainSlider;

    // ── Page 3 — FX ───────────────────────────────────────────────────────────
    juce::TextButton fxPageBtn;
    std::vector<juce::Component*> fxPageComponents;
    void layoutFxPage();
    void setupFxControls();

    // ── Page 5 — ABOUT ────────────────────────────────────────────────────────
    juce::TextButton              aboutPageBtn;
    std::vector<juce::Component*> aboutPageComponents;
    juce::Image                   aboutImage;
    juce::Rectangle<int>          aboutImageBounds;
    juce::Label                   aboutTitle;
    juce::TextEditor              aboutBody;        // read-only writeup
    juce::TextEditor              aboutThanks;      // credits list
    juce::HyperlinkButton         aboutVideoLink;
    juce::HyperlinkButton         aboutEmailLink;
    void setupAboutPage();
    void layoutAboutPage();

    // ── Page 4 — GENERATE ─────────────────────────────────────────────────────
    juce::TextButton              genPageBtn;
    std::vector<juce::Component*> genPageComponents;

    // Single shared Euclidean generator — assignable to Voice A or B
    juce::TextButton euclidVoiceABtn;     // target selector
    juce::TextButton euclidVoiceBBtn;
    juce::Slider     euclidStepsSlider;   // N: sequence length (2-16)
    juce::Slider     euclidHitsSlider;    // K: total hits (0..N×R)
    juce::TextButton euclidRatchetBtn;    // R cycler: 1 → 2 → 3 → 4 → 1
    juce::TextButton euclidApplyBtn;      // writes to processor + syncs page 1
    int              euclidR            = 1;   // current R state
    int              euclidTargetVoice  = 0;   // 0 = Voice A,  1 = Voice B

    // ── Turing Machine (page 4) ───────────────────────────────────────────────
    juce::TextButton tmVoiceABtn, tmVoiceBBtn;  // TM-specific A/B selectors
    juce::Slider     tmLockKnob;
    juce::TextButton tmLengthUpBtn, tmLengthDnBtn;
    juce::TextButton tmResetBtn;
    juce::TextButton tmPitchModeBtn, tmGatePitchModeBtn;
    juce::TextButton tmWriteBtn;
    juce::TextButton tmCaptureBtn;
    juce::TextButton tmBitBtn[16];          // clickable shift-register cells
    void             syncTMControlsFromVoice ();   // syncs UI to TM state

    void setupGenControls();
    void layoutGenPage();

    // ── FX page voice selector ─────────────────────────────────────────────
    int              fxVoiceTab = 0;    // 0 = Voice A FX,  1 = Voice B FX
    juce::TextButton fxVoiceABtn, fxVoiceBBtn;   // tab selectors
    juce::TextButton fxBypassBtn;                // per-voice bypass

    // Refresh all FX page controls from audioProcessor.fx[fxVoiceTab].
    void syncFxPageFromVoice();

    // ── MIDI Out (per voice, on Synth page) ────────────────────────────────
    juce::TextButton midiOutBtn    [2];   // enable toggle
    juce::ComboBox   midiOutChBox  [2];   // MIDI channel 1–16

    // ── MIDI/VOICE config radio (toggles which config sub-group is visible) ──
    juce::TextButton midiViewBtn   [2];   // "MIDI" radio
    juce::TextButton voiceViewBtn  [2];   // "VOICE" radio
    int              cfgView       [2] = { 1, 1 };   // 0 = MIDI, 1 = VOICE (default)
    void applyCfgView (int v);

    // TOOLS is the 3rd option of the QUANT/ORDER/TOOLS radio (see midView below).
    juce::TextButton toolsBtn      [2];   // "TOOLS" radio

    // ── Middle radio slot: QUANT (Root/Scale/Clock) ↔ ORDER (play order) ────
    juce::TextButton quantViewBtn  [2];   // "QUANT" radio
    juce::TextButton orderViewBtn  [2];   // "ORDER" radio
    int              midView       [2] = { 0, 0 };   // 0 = QUANT, 1 = ORDER, 2 = TOOLS
    void applyMidView (int v);

    // Cached TOTAL-pulses per voice; timer repaints the readout when it changes.
    int              lastTotalPulses[2] = { -1, -1 };

    // ── Modulation slot: inline [ LFO | MOD ENV ] (replaces both popups) ────
    int              modSlotView   [2] = { 0, 0 };   // 0 = LFO, 1 = MOD ENV
    int              lfoSel        [2] = { 0, 0 };   // which LFO (0..3) is shown
    juce::TextButton lfoSelBtn     [2][4];           // "1".."4" LFO selector
    void refreshModSlot (int v);

    // ── Unison / Poly mode (per voice, on Synth page) ──────────────────────
    juce::ComboBox   voiceModeBox   [2];   // MONO / UNISON / POLY
    juce::TextButton uniCountBtn    [2];   // "2V" / "4V" toggle
    juce::TextButton chordModeBtn   [2];   // shift-reg chord mode toggle
    juce::Slider     uniSpreadSlider[2];   // detune spread
    juce::Slider     uniWidthSlider [2];   // stereo width

    // ── Backplate SVG ─────────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> backplate;

    // ── Brushed-metal Eurorack faceplate (generated once, blitted in paint) ───
    juce::Image backplateMetal;
    void buildBackplateMetal (int w, int h);
    void drawPanelScrew (juce::Graphics& g, float cx, float cy, float r);

    // ── Gate button right-click listener (ratchet selection) ──────────────────
    struct GateBtnListener : public juce::MouseListener
    {
        GateBtnListener (VoltageSeq2AudioProcessorEditor& e, int v, int s)
            : ed (e), vi (v), step (s) {}
        void mouseDown (const juce::MouseEvent& ev) override;
        VoltageSeq2AudioProcessorEditor& ed;
        int vi, step;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateBtnListener)
    };
    std::unique_ptr<GateBtnListener> gateMouseListener[2][16];
    bool suppressNextGateClick = false;

    struct SlideBtnListener : public juce::MouseListener
    {
        SlideBtnListener (VoltageSeq2AudioProcessorEditor& e, int v, int s)
            : ed (e), vi (v), step (s) {}
        void mouseDown (const juce::MouseEvent& ev) override;
        VoltageSeq2AudioProcessorEditor& ed;
        int vi, step;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlideBtnListener)
    };
    std::unique_ptr<SlideBtnListener> slideMouseListener[2][16];
    bool suppressNextSlideClick = false;

    void refreshSlideBtn (int v, int i);

    // ── APVTS slider attachments ─────────────────────────────────────────────
    // These must be declared BEFORE the controls they attach to are destroyed,
    // so put them here (members are destroyed in reverse declaration order).
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> plaitsHarmAttach [2];
    std::unique_ptr<SliderAtt> plaitsTimbAttach [2];
    std::unique_ptr<SliderAtt> plaitsMorphAttach[2];
    std::unique_ptr<SliderAtt> stepAttach     [2][16];
    std::unique_ptr<SliderAtt> cutoffAttach   [2];
    std::unique_ptr<SliderAtt> ampAAttach     [2], ampDAttach    [2];
    std::unique_ptr<SliderAtt> ampSAttach     [2], ampRAttach    [2];
    std::unique_ptr<SliderAtt> fAAttach       [2], fDAttach      [2];
    std::unique_ptr<SliderAtt> fSAttach       [2], fRAttach      [2];
    std::unique_ptr<SliderAtt> fEnvAmtAttach  [2];
    std::unique_ptr<SliderAtt> lfo1RateAttach [2], lfo1DepAttach [2];
    std::unique_ptr<SliderAtt> lfo2RateAttach [2], lfo2DepAttach [2];
    std::unique_ptr<SliderAtt> fmRatioAttach  [2], fmDepthAttach [2];
    std::unique_ptr<SliderAtt> portaAttach    [2], rangeAttach   [2];

    // ── Macro controllers (Synth page, bottom-right) ─────────────────────────
    // A rotary that opens its assignment menu on right-click instead of dragging.
    struct MacroKnob : public juce::Slider
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
            juce::Slider::mouseDown (e);
        }
    };
    static constexpr int kNumMacros = 2;
    MacroKnob   macroKnob       [kNumMacros];
    juce::Label macroNameLabel  [kNumMacros];   // "MACRO 1"
    juce::Label macroAssignLabel[kNumMacros];   // multiline assignment list
    std::unique_ptr<SliderAtt> macroAttach[kNumMacros];
    juce::TextButton macroAssignBtn[kNumMacros];   // "ASSIGN" — enters learn mode
    juce::TextButton macrosBtn;                     // reveal toggle (hidden by default)
    bool             macrosShown = false;
    void applyMacrosVisible();
    void setupMacros();
    void showMacroMenu (int m);
    void refreshMacroLabels();

    // ── Click-to-assign ("learn") mode + depth-ring overlay ──────────────────
    int macroLearnActive = -1;        // macro index currently learning, -1 = none

    // One assignable on-screen control → (macro target, voice).
    struct Assignable { juce::Slider* slider; int target; int voice; };
    std::vector<Assignable> buildAssignables();

    void enterMacroLearn (int m);
    void exitMacroLearn();
    void updateMacroAssignBtns();
    // Map a clicked assignable to a new macro assignment (scope: 2=Both if alt).
    void assignFromClick (int sliderTarget, int voice, bool both);
    // Bounds of an assignable knob in overlay/editor coords, empty if not visible.
    juce::Rectangle<int> assignableScreenBounds (juce::Slider* s);

    // One depth ring to draw around an assigned, currently-visible knob.
    struct RingInfo {
        int   macro, assignIdx;
        juce::Point<float> centre;
        float radius, depth;
        juce::Colour colour;
    };
    std::vector<RingInfo> buildRings();
    int dragRingMacro = -1, dragRingAssign = -1;   // active depth-ring drag
    float dragRingStartDepth = 0.0f;
    float lastMacroValue[kNumMacros] = { -1.0f, -1.0f };   // for live-dot repaint

    // Transparent top-level overlay: highlights assignable knobs in learn mode,
    // captures the assign click, and draws + drags the depth rings. It always
    // intercepts; hitTest() returns false (click-through) except in learn mode
    // or when the cursor is on a ring band, so normal knob use is untouched.
    struct MacroOverlay : public juce::Component
    {
        VoltageSeq2AudioProcessorEditor* ed = nullptr;
        MacroOverlay() { setInterceptsMouseClicks (true, false); }
        bool hitTest (int x, int y) override;
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp   (const juce::MouseEvent& e) override;
    };
    MacroOverlay macroOverlay;

    // ── Pattern sequencer controls (page 2) ──────────────────────────────────
    int              patPageView   [2] = { 0, 0 };   // 0=BANK  1=SEQUENCER
    juce::TextButton patBankTabBtn [2];
    juce::TextButton patSeqTabBtn  [2];

    juce::TextButton patSeqOffBtn  [2];
    juce::TextButton patSeqAutoBtn [2];
    juce::TextButton patSeqMidiBtn [2];
    juce::TextButton patSeqImmBtn  [2];   // shows "QUEUED" or "IMMEDIATE"

    juce::ComboBox   patSeqSlotBox [2][16];
    juce::TextButton patSeqLoopBtn [2][16];

    juce::TextButton patSeqLenUpBtn[2];
    juce::TextButton patSeqLenDnBtn[2];

    void setupPatternSeqControls();
    void layoutPatternSeqControls();
    void refreshPatSeqMode (int vi);
    void refreshPatPageView (int vi);

    // ── Floating section panels ──────────────────────────────────────────────────
    struct SectionPanel : public juce::Component
    {
        juce::String        title;
        juce::TextButton    closeBtn;
        std::function<void()> onClose;
        std::function<void(juce::Graphics&)> paintLabels;   // optional — draws param labels inside panel

        SectionPanel()
        {
            closeBtn.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));   // × close glyph
            addAndMakeVisible (closeBtn);
        }

        void paint (juce::Graphics& g) override
        {
            // Background
            g.setColour (juce::Colour (0xf4111122));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
            // Amber border
            g.setColour (juce::Colour (0xffe09040).withAlpha (0.7f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.5f);
            // Title bar tint
            g.setColour (juce::Colour (0x33e09040));
            g.fillRoundedRectangle (1.0f, 1.0f, (float)getWidth()-2.0f, 24.0f, 7.0f);
            g.fillRect (1, 12, getWidth()-2, 12);
            // Title text
            g.setColour (juce::Colour (0xffe09040));
            g.setFont (juce::Font ("Helvetica Neue", 11.5f, juce::Font::bold));
            g.drawText (title, 10, 0, getWidth()-28, 24, juce::Justification::centredLeft);
            // Optional parameter labels painted by owner
            if (paintLabels) paintLabels (g);
        }

        void resized() override
        {
            closeBtn.setBounds (getWidth()-22, 4, 17, 17);
        }

        // Drag to reposition
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.y < 24)
                dragger.startDraggingComponent (this, e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            dragger.dragComponent (this, e, nullptr);
        }

    private:
        juce::ComponentDragger dragger;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPanel)
    };

    // ── Floating section panel instances ─────────────────────────────────────
    SectionPanel  oscPanel[2], envPanel[2], lfoPanel[2];
    bool          oscPanelOpen[2] = { false, false };
    bool          envPanelOpen[2] = { false, false };
    bool          lfoPanelOpen[2] = { false, false };

    juce::TextButton oscPanelBtn[2], envPanelBtn[2], lfoPanelBtn[2];

    // ── Inline OSC section (replaces the OSC popup) ──────────────────────────
    juce::TextButton oscView1Btn[2];   // "OSC 1" radio
    juce::TextButton oscView2Btn[2];   // "OSC 2" radio
    int              oscView[2] = { 0, 0 };   // 0 = OSC1, 1 = OSC2 (native); Plaits overrides
    void refreshOscView (int v);       // show OSC1 / OSC2 / Plaits group inline

    void openOscPanel  (int v);
    void closeOscPanel (int v);
    void openEnvPanel  (int v);
    void closeEnvPanel (int v);
    void openLfoPanel  (int v);
    void closeLfoPanel (int v);

    // Per-section component lists — controls hidden by default, shown only inside panel
    std::vector<juce::Component*> oscSectionComps[2];
    std::vector<juce::Component*> envSectionComps[2];
    std::vector<juce::Component*> lfoSectionComps[2];
    void applySectionVisibility (int v);   // hide/show section controls based on panel open state

    // ── Helpers ───────────────────────────────────────────────────────────────
    void setupVoice (int v);                          // wire up all controls for one voice
    void layoutVoice (int v, int seqTopY, int ctrlTopY); // position all controls for one voice
    void layoutPatternPage();                         // position pattern bank tiles
    void setupKnob (juce::Slider& s, double min, double max, double val,
                    double skewMidpoint = 0.0);
    void refreshGateBtn (int v, int i);   // sync one gate button's text + colour
    void syncUIFromProcessor();        // refresh all widget values after preset load
    void showPage (int page);          // switch between pages 0=synth 1=pattern 2=fx

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessorEditor)
};
