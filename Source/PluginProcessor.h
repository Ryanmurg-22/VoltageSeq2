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
    // TYPES
    //==========================================================================

    // Complex envelope parameters — top-level nested type so ComplexEnvDisplay
    // in the editor can reference it without knowing about VoiceParams.
    struct ComplexEnvParams
    {
        float attack   = 0.05f;
        float decay    = 0.3f;
        float sustain  = 0.6f;
        float release  = 0.2f;
        float depth    = 0.5f;
        int   dest     = 0;       // 0=Amplitude  1=Filter  2=Pitch
        bool  looping  = false;
        bool  clockSync = false;
        int   clockDiv  = 3;      // index into cenvDivBars[]
    };

    // All per-voice parameters — written by the UI thread.
    struct VoiceParams
    {
        // ── Sequencer ────────────────────────────────────────────────────────
        float stepVoltages[16] = {};
        bool  stepGates   [16] = {};
        bool  stepGlides  [16] = {};
        float portamentoTime   = 0.0f;
        float swingAmount      = 0.5f;
        int   clockDivision    = 2;      // 1/16 default
        int   sequenceLength   = 16;
        bool  unipolar         = false;
        int   playOrder        = 0;      // Forward

        // ── Pitch / quantiser ────────────────────────────────────────────────
        float rangeVCA    = 1.0f;
        int   rootNote    = 0;
        int   currentScale = 0;

        // ── OSC 1 ────────────────────────────────────────────────────────────
        int   osc1Waveform   = 1;        // Saw
        float osc1Level      = 0.7f;
        int   osc1Octave     = 0;
        float osc1PulseWidth = 0.5f;
        float osc1Feedback   = 0.0f;    // self-FM feedback [0..1]

        // ── OSC 2 ────────────────────────────────────────────────────────────
        float osc2Position = 0.0f;
        float osc2Level    = 0.5f;
        int   osc2Octave   = 0;

        // ── FM ───────────────────────────────────────────────────────────────────
        float fmDepth = 0.0f;    // OSC2 → OSC1 FM depth [0..1]
        int   fmRatio = 1;       // FM ratio index: 0=0.5x 1=1x 2=2x … 7=7x

        // ── Filter ───────────────────────────────────────────────────────────
        float filterCutoff    = 2000.0f;
        float filterResonance = 0.0f;
        float filterEnvAmount = 0.5f;
        juce::ADSR::Parameters filterEnvParams { 0.01f, 0.5f, 0.0f, 0.3f };

        // ── Amp envelope ─────────────────────────────────────────────────────
        juce::ADSR::Parameters adsrParams { 0.01f, 0.1f, 0.7f, 0.08f };

        // ── LFO 1 ────────────────────────────────────────────────────────────
        float lfoRate  = 2.0f;
        float lfoDepth = 0.0f;
        int   lfoTarget = 0;

        // ── LFO 2 ────────────────────────────────────────────────────────────
        float lfo2Rate  = 3.0f;
        float lfo2Depth = 0.0f;
        int   lfo2Target = 1;

        // ── Complex envelopes ────────────────────────────────────────────────
        ComplexEnvParams cenv1, cenv2;

        // ── Display / transport (written audio thread, read UI — harmless race)
        int currentStep = 0;
        std::atomic<bool> sequencerRunning { true };
        std::atomic<bool> resetOnNextBlock  { false };

        enum PlayOrder { Forward = 0, Backward, Converge, Random };
        enum Waveform  { Sine   = 0, Saw, Square, Triangle };
    };

    //==========================================================================
    // PUBLIC DATA
    //==========================================================================
    static const int numVoices = 2;
    static const int numSteps  = 16;

    VoiceParams voice[numVoices];   // voice[0]=A  voice[1]=B

    // Shared between both voices
    double            internalBPM = 120.0;
    std::atomic<bool> autoRun     { true };

    // Clock divisions for complex envelopes (bars; 1 bar = 4 beats)
    static constexpr double cenvDivBars[8] = {
        8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625
    };

    // Wavetables — shared (OSC 2)
    static const int wavetableSize = 2048;
    static const int numWavetables = 4;
    float wavetables[numWavetables][wavetableSize];

    // Per-voice oscilloscope ring buffers (written audio, read UI)
    static const int scopeSize = 512;
    float oscScopeBuffer[numVoices][scopeSize] {};
    int   scopeWritePos [numVoices]            {};

private:
    //==========================================================================
    // PRIVATE AUDIO-THREAD STATE
    //==========================================================================
    double currentSampleRate = 44100.0;

    struct CEnvState
    {
        enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
        float  level    = 0.0f;
        double clockPos = 0.0;
        bool   prevGate = false;
    };

    struct VoiceState
    {
        double osc1Phase = 0.0, osc1PhaseInc = 0.0;
        double osc2Phase = 0.0, osc2PhaseInc = 0.0;
        float  currentFreq1 = 261.63f, targetFreq1 = 261.63f;
        float  currentFreq2 = 261.63f, targetFreq2 = 261.63f;
        bool   glideActive  = false;
        float  ic1eq = 0.0f, ic2eq = 0.0f;
        juce::ADSR adsr, filterEnv;
        float  lfoPhase  = 0.0f, lfo2Phase  = 0.0f;
        float  osc1FeedbackSample = 0.0f;
        float  pulseWidth = 0.5f;
        int    lastPos = -1, randomStep = 0;
        double sampleCounter = 0.0;
        CEnvState cenv1State, cenv2State;
    };

    VoiceState vstate[numVoices];

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

    //==========================================================================
    // PRIVATE METHODS
    //==========================================================================
    void  buildWavetables();

    // Process one sample of one voice; returns the output sample.
    float processSingleVoiceSample (int vi, bool running,
                                    bool useHostSync, double samplePPQ,
                                    double effectiveBPM,
                                    const double* swingBounds,
                                    const double* swingPPQBounds,
                                    double totalSwingCycle,
                                    double totalSwingPPQ,
                                    const int*   stepOrder,
                                    float        glideCoeff);

    float generateOsc1Sample (VoiceState& vs, const VoiceParams& vp, double phaseInc);
    float generateOsc2Sample (VoiceState& vs, const VoiceParams& vp, double phaseInc);
    float applyFilter        (VoiceState& vs, const VoiceParams& vp,
                              float input, float effectiveCutoff);
    float voltageToQuantizedFreq (const VoiceParams& vp, float voltage,
                                  float rangeOverride = -1.0f);
    int   quantizeNoteToScale    (int midiNote, int rootNote, int scale);
    float processCEnv            (const ComplexEnvParams& p, CEnvState& s,
                                  bool gateOn, double bpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessor)
};
