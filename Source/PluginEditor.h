
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "BackplateData.h"

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

    // ── Per-voice controls  [vi][step] or [vi] ───────────────────────────────
    juce::Slider     stepKnob   [2][16];
    juce::TextButton gateBtn    [2][16];
    juce::TextButton slideBtn   [2][16];

    juce::Slider     seqLengthSlider [2];
    juce::Slider     swingSlider     [2];
    juce::TextButton playFwdBtn  [2], playRevBtn  [2];
    juce::TextButton playConvBtn [2], playRndBtn  [2];
    juce::TextButton resetBtn    [2];
    juce::TextButton bipolarBtn  [2];
    juce::TextButton nudgeLeftBtn[2], nudgeRightBtn[2];
    juce::TextButton runStopBtn  [2];

    juce::Slider     portaSlider     [2];
    juce::Slider     rangeSlider     [2];
    juce::ComboBox   clockDivBox     [2];

    juce::ComboBox   rootBox   [2];
    juce::ComboBox   scaleBox  [2];

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
    juce::TextButton autoBtn;
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
    // Delay
    juce::TextButton delayOnBtn;
    juce::TextButton delayPingPongBtn;
    juce::TextButton delaySyncBtn;
    juce::ComboBox   delaySyncDivBox;
    juce::Slider     delayTimeMsSlider;
    juce::Slider     delayFeedbackSlider;
    juce::Slider     delayMixSlider;
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

    // ── Page 3 navigation ────────────────────────────────────────────────────
    juce::TextButton fxPageBtn;
    std::vector<juce::Component*> fxPageComponents;
    void layoutFxPage();
    void setupFxControls();

    // ── Backplate SVG ─────────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> backplate;

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
    bool suppressNextGateClick = false;  // set by GateBtnListener to block onClick cycling

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
