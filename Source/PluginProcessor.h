#pragma once
#include <JuceHeader.h>

class VoltageSeq2AudioProcessor : public juce::AudioProcessor
{
public:
    VoltageSeq2AudioProcessor();
    ~VoltageSeq2AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // PUBLIC PARAMETERS
    //==========================================================================

    static const int numSteps = 16;
    float  stepVoltages[16];
    bool   stepGates[16];
    bool   stepGlides[16];
    double internalBPM    = 120.0;
    float  portamentoTime = 0.0f;

    // Clock division index:
    //  0=1/4, 1=1/8, 2=1/16 (default), 3=1/8T, 4=1/16T, 5=1/8dot, 6=1/16dot
    int clockDivision = 2;

    // Transport (atomic — written from UI thread, read on audio thread)
    std::atomic<bool> sequencerRunning { true };
    std::atomic<bool> autoRun          { true };
    std::atomic<bool> resetOnNextBlock { false };

    float rangeVCA     = 1.0f;
    int   rootNote     = 0;
    int   currentScale = 0;

    enum Waveform { Sine = 0, Saw, Square, Triangle };
    int   osc1Waveform   = Saw;
    float osc1Level      = 0.7f;
    int   osc1Octave     = 0;         // -2 … +2
    float osc1PulseWidth = 0.5f;      // base pulse-width for square wave (0.05–0.95)

    float osc2Position = 0.0f;
    float osc2Level    = 0.5f;
    int   osc2Octave   = 0;

    float filterCutoff    = 2000.0f;
    float filterResonance = 0.0f;
    float filterEnvAmount = 0.5f;
    juce::ADSR::Parameters filterEnvParams { 0.01f, 0.5f, 0.0f, 0.3f };

    juce::ADSR::Parameters adsrParams;

    // LFO 1
    float lfoRate   = 2.0f;
    float lfoDepth  = 0.0f;
    int   lfoTarget = 0;     // 0=PWM, 1=Cutoff, 2=Pitch

    // LFO 2
    float lfo2Rate   = 3.0f;
    float lfo2Depth  = 0.0f;
    int   lfo2Target = 1;

    // Sequencer length (1–16 active steps); currentStep exposed for UI highlight
    int sequenceLength = 16;    // how many steps are active
    int currentStep    = 0;     // plain int — display-only read from UI thread, harmless race

    // Bipolar (default) vs unipolar mode — affects UI slider range only
    bool unipolar = false;

    // ── Visualiser data (written on audio thread, read on UI thread) ──────────
    // Wavetable arrays exposed so the WT display can render mathematically
    static const int wavetableSize = 2048;
    static const int numWavetables = 4;
    float wavetables[numWavetables][wavetableSize];

    // Ring buffer filled with pre-filter OSC mix for the oscilloscope display
    static const int scopeSize = 512;
    float            oscScopeBuffer[scopeSize] {};
    int              scopeWritePos = 0;   // plain int — visual-only, race is harmless

private:
    double currentSampleRate = 44100.0;

    // OSC 1
    double osc1Phase    = 0.0;
    double osc1PhaseInc = 0.0;

    // OSC 2 wavetable (arrays are now public for visualiser access)
    double osc2Phase    = 0.0;
    double osc2PhaseInc = 0.0;

    // Portamento
    float currentFreq1 = 261.63f;
    float currentFreq2 = 261.63f;
    float targetFreq1  = 261.63f;
    float targetFreq2  = 261.63f;
    bool  glideActive  = false;

    // SVF integrator states
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    juce::ADSR adsr;
    juce::ADSR filterEnv;

    // LFO phases
    float lfoPhase  = 0.0f;
    float lfo2Phase = 0.0f;

    // Current effective pulse width (set per-sample from base + LFO modulation)
    float pulseWidth = 0.5f;

    // Sequencer state
    int    lastStep      = -1;
    double sampleCounter = 0.0;

    // PPQ duration of one step for each clock-division index
    static constexpr double ppqDivTable[7] = {
        1.0,           // 0: 1/4
        0.5,           // 1: 1/8
        0.25,          // 2: 1/16  (default)
        1.0 / 3.0,     // 3: 1/8 triplet
        1.0 / 6.0,     // 4: 1/16 triplet
        0.75,          // 5: 1/8 dotted
        0.375          // 6: 1/16 dotted
    };

    void  buildWavetables();
    float generateOsc1Sample (double phaseInc);
    float generateOsc2Sample (double phaseInc);
    float applyFilter (float input, float effectiveCutoff);
    float voltageToQuantizedFreq (float voltage);
    int   quantizeNoteToScale (int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessor)
};
