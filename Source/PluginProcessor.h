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

    // Clock division index (sequencer):
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
    int   osc1Octave     = 0;
    float osc1PulseWidth = 0.5f;

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

    // Sequencer length & display
    int  sequenceLength = 16;
    int  currentStep    = 0;     // plain int — read from UI thread for display only
    bool unipolar       = false;

    // Play order
    enum PlayOrder { Forward = 0, Backward, Converge, Random };
    int playOrder = Forward;

    // ── Complex envelopes ─────────────────────────────────────────────────────
    struct ComplexEnvParams
    {
        float attack   = 0.05f;
        float decay    = 0.3f;
        float sustain  = 0.6f;
        float release  = 0.2f;
        float depth    = 0.5f;
        int   dest     = 0;       // 0 = Amplitude, 1 = Filter Cutoff, 2 = Pitch
        bool  looping  = false;
        bool  clockSync = false;
        int   clockDiv  = 3;      // index into cenvDivBars[]
    };
    ComplexEnvParams cenv1, cenv2;

    // Clock divisions for complex envelopes (bars; 1/1 = 1 bar = 4 beats)
    static constexpr double cenvDivBars[8] = {
        8.0,    // 8/1
        4.0,    // 4/1
        2.0,    // 2/1
        1.0,    // 1/1
        0.5,    // 1/2
        0.25,   // 1/4
        0.125,  // 1/8
        0.0625  // 1/16
    };

    // ── Visualiser data (written on audio thread, read on UI thread) ──────────
    static const int wavetableSize = 2048;
    static const int numWavetables = 4;
    float wavetables[numWavetables][wavetableSize];

    static const int scopeSize = 512;
    float            oscScopeBuffer[scopeSize] {};
    int              scopeWritePos = 0;

private:
    double currentSampleRate = 44100.0;

    // OSC phases
    double osc1Phase    = 0.0;
    double osc1PhaseInc = 0.0;
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

    float pulseWidth = 0.5f;

    // Sequencer state
    int    lastPos       = -1;   // clock position (0..seqLen-1), not step index
    int    randomStep    = 0;    // persists between blocks in Random mode
    double sampleCounter = 0.0;

    // PPQ duration of one step for each clock-division index
    static constexpr double ppqDivTable[7] = {
        1.0,           // 0: 1/4
        0.5,           // 1: 1/8
        0.25,          // 2: 1/16 (default)
        1.0 / 3.0,     // 3: 1/8 triplet
        1.0 / 6.0,     // 4: 1/16 triplet
        0.75,          // 5: 1/8 dotted
        0.375          // 6: 1/16 dotted
    };

    // Complex envelope state machine
    struct CEnvState
    {
        enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
        float  level    = 0.0f;
        double clockPos = 0.0;   // position within clock-sync cycle (samples)
        bool   prevGate = false; // for gate edge-detection in gate-triggered mode
    };
    CEnvState cenv1State, cenv2State;

    void  buildWavetables();
    float generateOsc1Sample (double phaseInc);
    float generateOsc2Sample (double phaseInc);
    float applyFilter (float input, float effectiveCutoff);
    float voltageToQuantizedFreq (float voltage);
    int   quantizeNoteToScale (int midiNote);
    float processCEnv (const ComplexEnvParams& p, CEnvState& s, bool gateOn, double bpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessor)
};
