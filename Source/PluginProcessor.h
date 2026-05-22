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

    struct UnisonSlot
    {
        double osc1Phase = 0.0, osc1PhaseInc = 0.0;
        double osc2Phase = 0.0, osc2PhaseInc = 0.0;
        float  currentFreq1 = 261.63f, targetFreq1 = 261.63f;
        float  currentFreq2 = 261.63f, targetFreq2 = 261.63f;
        bool   glideActive  = false;
        float  osc1FeedbackSample = 0.0f;
        // Independent drift per slot (so unison voices drift differently)
        float  osc1DriftPhase = 0.0f, osc1DriftRate = 0.13f, osc1DriftVal = 0.0f;
        float  osc2DriftPhase = 0.0f, osc2DriftRate = 0.19f, osc2DriftVal = 0.0f;
        // Pre-computed pan gains (constant-power)
        float  panL = 1.0f, panR = 1.0f;
        // Pre-computed detune frequency ratio (pow(2, detuneSemitones/12))
        float  detuneRatio = 1.0f;
        // Poly shift-register: this slot's assigned step voltage
        float  assignedVoltage = 0.0f;
    };

    // Mod envelope — ADSR envelope assignable to FM or Pitch, with optional clock sync
    struct ModEnvParams
    {
        float attack  = 0.01f;
        float decay   = 0.5f;
        float sustain = 0.0f;
        float release = 0.3f;
        float depth   = 0.0f;
        int   dest    = 0;      // 0=FM Depth  1=Pitch  2=Filter
        bool  clockSync = false;
        int   clockDiv  = 3;    // index into cenvDivBars[]
    };

    // All per-voice parameters — written by the UI thread.
    struct VoiceParams
    {
        // ── Sequencer ────────────────────────────────────────────────────────
        float stepVoltages[16] = {};
        bool  stepGates   [16] = {};
        bool  stepGlides  [16] = {};
        bool  stepTied    [16] = {};   // true = sustain gate from prev step, no envelope retrigger
        int   stepRepeats [16] = {};   // 0=1× 1=2× 2=3× 3=4× — ratchet count
        int   stepPulses  [16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };  // 1–8 clock pulses per step
        bool  envReset         = false;   // true = hard-reset envelopes on every retrigger
        bool  pulseLengthMode  = false;   // true = reset after fixed pulse count, not stage count
        int   pulseLength      = 16;      // target total pulses before reset (pulse mode only)
        float portamentoTime   = 0.0f;
        float swingAmount      = 0.5f;
        int   clockDivision    = 2;      // 1/16 default
        int   sequenceLength   = 16;
        bool  unipolar         = false;
        int   playOrder        = 0;      // Forward
        int   nudgeOffset      = 0;      // step start offset [0 .. sequenceLength-1]

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
        float driftAmount    = 0.0f;    // analogue VCO drift [0..1]

        // ── OSC 2 ────────────────────────────────────────────────────────────
        float osc2Position = 0.0f;
        float osc2Level    = 0.5f;
        int   osc2Octave   = 0;

        // ── FM ───────────────────────────────────────────────────────────────────
        float fmDepth = 0.0f;    // OSC2 → OSC1 FM depth [0..1]
        float fmRatio = 1.0f;    // FM ratio: 0.25 … 8.0 (continuous)
        float crossModDepth = 0.0f;   // cross-mod from other voice [0..1]

        // ── Filter ───────────────────────────────────────────────────────────
        float filterCutoff    = 2000.0f;
        float filterResonance = 0.0f;
        float filterEnvAmount = 0.5f;
        float filterDrive     = 0.0f;    // pre-filter saturation [0..1]
        int   filterMode      = 0;       // 0=LP  1=BP  2=HP
        int   filterSlope     = 0;       // 0=12dB  1=24dB
        juce::ADSR::Parameters filterEnvParams { 0.01f, 0.5f, 0.0f, 0.3f };

        // ── Amp envelope ─────────────────────────────────────────────────────
        juce::ADSR::Parameters adsrParams { 0.01f, 0.1f, 0.7f, 0.08f };

        // ── LFO 1 ────────────────────────────────────────────────────────────
        float lfoRate  = 2.0f;
        float lfoDepth = 0.0f;
        int   lfoTarget = 0;
        int   lfoWaveform  = 0;     // 0=Sine 1=Tri 2=Saw 3=Sqr
        bool  lfoSync      = false;
        int   lfoSyncDiv   = 5;     // index into cenvDivBars[]

        // ── LFO 2 ────────────────────────────────────────────────────────────
        float lfo2Rate  = 3.0f;
        float lfo2Depth = 0.0f;
        int   lfo2Target = 1;
        int   lfo2Waveform = 0;
        bool  lfo2Sync     = false;
        int   lfo2SyncDiv  = 5;

        // ── LFO 3 ────────────────────────────────────────────────────────────
        float lfo3Rate     = 1.5f;
        float lfo3Depth    = 0.0f;
        int   lfo3Target   = 3;     // Range
        int   lfo3Waveform = 1;     // Triangle
        bool  lfo3Sync     = false;
        int   lfo3SyncDiv  = 5;

        // ── LFO 4 ────────────────────────────────────────────────────────────
        float lfo4Rate     = 0.5f;
        float lfo4Depth    = 0.0f;
        int   lfo4Target   = 4;     // FM Depth
        int   lfo4Waveform = 0;     // Sine
        bool  lfo4Sync     = false;
        int   lfo4SyncDiv  = 5;

        // ── Mod Envelope ─────────────────────────────────────────────────────
        ModEnvParams modEnv;

        // ── MIDI Out ─────────────────────────────────────────────────────────
        bool midiOutEnabled = false;
        int  midiOutChannel = 1;     // 1–16

        // ── Unison / Poly mode ────────────────────────────────────────────────
        enum VoiceMode { Mono = 0, Unison = 1, Poly = 2 };
        VoiceMode voiceMode    = Mono;
        int       unisonCount  = 4;      // 2 or 4 slots when mode != Mono
        float     unisonSpread = 0.15f;  // detune half-width in semitones (0..0.5)
        float     unisonWidth  = 0.7f;   // stereo pan width (0..1)

        // ── Display / transport (written audio thread, read UI — harmless race)
        int currentStep = 0;
        std::atomic<bool> sequencerRunning { true };
        std::atomic<bool> resetOnNextBlock  { false };

        enum PlayOrder { Forward = 0, Backward, Converge, Random };
        enum Waveform  { Sine   = 0, Saw, Square, Triangle };
    };

    //==========================================================================
    // PATTERN BANK
    //==========================================================================

    // A pattern snapshot — sequencer & quantizer fields only, no synth voice settings
    struct PatternSlot
    {
        bool  used           = false;
        float stepVoltages[16] = {};
        bool  stepGates   [16] = {};
        bool  stepGlides  [16] = {};
        bool  stepTied    [16] = {};
        int   stepRepeats [16] = {};   // 0=1× 1=2× 2=3× 3=4×
        int   stepPulses  [16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };  // 1–8 clock pulses per step
        int   sequenceLength   = 16;
        int   clockDivision    = 2;
        float swingAmount      = 0.5f;
        float portamentoTime   = 0.0f;
        int   playOrder        = 0;
        bool  unipolar         = false;
        int   rootNote         = 0;
        int   currentScale     = 0;
        float rangeVCA         = 1.0f;
    };

    static const int numPatternSlots = 16;
    PatternSlot patternBank[2][numPatternSlots];   // [voice][slot]

    // Save current voice state → a slot; load slot → live voice; clear a slot
    void savePattern  (int voiceIdx, int slotIdx);
    void loadPattern  (int voiceIdx, int slotIdx);
    void clearPattern (int voiceIdx, int slotIdx);

    struct FxParams {
        // Per-voice bypass — when true, voice passes dry (no delay/reverb/chorus)
        bool  fxBypass      = false;

        // Delay
        bool  delayOn       = false;
        bool  delaySync     = true;
        int   delaySyncDiv  = 2;       // index into ppqDivTable (0=1/4 .. 6=1/16.)
        float delayTimeMs   = 375.0f;  // manual time when sync=false, 1..2000ms
        float delayFeedback = 0.40f;   // 0..0.95
        bool  delayPingPong = false;
        float delayMix      = 0.30f;

        // Reverb (Dattorro plate)
        bool  reverbOn      = false;
        float reverbSize    = 0.75f;   // decay 0..1
        float reverbDamping = 0.40f;   // HF damping 0..1
        float reverbPreDelay = 20.0f;  // ms, 0..100
        float reverbMix     = 0.25f;

        // Chorus
        bool  chorusOn      = false;
        float chorusRate    = 0.50f;   // Hz, 0.1..5
        float chorusDepth   = 0.50f;   // 0..1
        float chorusMix     = 0.50f;

        // Master
        float masterDrive   = 0.0f;   // 0..1
        float masterGain    = 1.0f;   // 0..2
    };
    FxParams fx[2];   // fx[0] = Voice A,  fx[1] = Voice B

    //==========================================================================
    // PUBLIC DATA
    //==========================================================================
    static const int numVoices = 2;
    static const int numSteps  = 16;

    VoiceParams voice[numVoices];   // voice[0]=A  voice[1]=B

    // APVTS — registers automatable parameters with the DAW host.
    // Declare after voice[] so createParameterLayout() can reference defaults.
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Push current VoiceParams values into APVTS (call after loadPattern / old-format restore).
    void syncAPVTSFromVoice (int vi);

    // Shared between both voices
    double            internalBPM = 120.0;
    std::atomic<bool> autoRun     { true };

    // Clock divisions for mod envelope (bars; 1 bar = 4 beats)
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
    double currentSampleRate  = 44100.0;
    bool   prevHostPlaying    = false;   // transport state last block — used to detect start/stop

    // Per-voice cross-mod sample storage
    float crossModSample[numVoices] = {};

    struct VoiceState
    {
        // ── Per-slot oscillator state (oscillators only; filter/env are shared)
        static constexpr int kMaxSlots = 4;
        UnisonSlot slots[kMaxSlots];
        int srFilled = 0;   // how many shift-register slots have been filled (POLY mode)

        // ── Shared filter state — two independent channels (L / R) ─────────────
        float  ic1eq  = 0.0f, ic2eq  = 0.0f;    // L  first SVF stage
        float  ic1eq2 = 0.0f, ic2eq2 = 0.0f;    // L  second SVF stage (24 dB)
        float  ic1eqR  = 0.0f, ic2eqR  = 0.0f;  // R  first SVF stage
        float  ic1eq2R = 0.0f, ic2eq2R = 0.0f;  // R  second SVF stage (24 dB)

        // ── Shared envelopes ─────────────────────────────────────────────────────
        juce::ADSR adsr, filterEnv;

        // ── LFO phases ────────────────────────────────────────────────────────────
        float  lfoPhase  = 0.0f, lfo2Phase  = 0.0f;
        float  lfo3Phase = 0.0f, lfo4Phase  = 0.0f;

        // ── Pulse width (shared, modulated by LFO PWM) ───────────────────────────
        float  pulseWidth = 0.5f;

        // ── Sequencer clock state ─────────────────────────────────────────────────
        int    lastPos = -1, randomStep = 0;
        double sampleCounter = 0.0;

        // ── Mod envelope ──────────────────────────────────────────────────────────
        juce::ADSR modEnvAdsr;
        double     modEnvClockPos = 0.0;
        bool       modEnvPrevGate = false;

        // ── Ratchet sub-step state ────────────────────────────────────────────────
        int    ratchetSubStep    = 0;
        double ratchetSubStepDur = 0.0;  // duration of one sub-step in samples
        double ratchetStepPos    = 0.0;  // samples elapsed since step start
        bool   ratchetNoteOff    = false;

        // ── MIDI Out step signals (set by processSingleVoiceSample, consumed in processBlock)
        bool  midiStepFired   = false;  // one-shot: new step just advanced
        bool  midiStepGate    = false;  // gate state of the new step
        bool  midiStepGlide   = false;  // new step has portamento active
        int   midiStepNote    = -1;     // quantized MIDI note for new step (-1 = none)
        int   midiOutNote     = -1;     // currently sounding MIDI note (-1 = silent)
        bool  midiRatchetOff  = false;  // ratchet 50%-point: emit note-off
        bool  midiRatchetOn   = false;  // ratchet sub-step advance: emit note-off + note-on
        // Pitch-bend portamento ramp
        bool  midiGlideActive = false;  // PB ramp in progress
        float midiGlideBend   = 0.0f;   // current PB offset in semitones (decays → 0)
        int   midiGlideTick   = 0;      // samples since last PB message
    };

    VoiceState vstate[numVoices];

    struct FxState {
        // ── Stereo Delay ─────────────────────────────────────────────────────────
        std::vector<float> dlyL, dlyR;
        int dlyWL = 0, dlyWR = 0;

        // ── Dattorro Plate Reverb ─────────────────────────────────────────────────
        // Input diffusers (4 all-pass chains)
        std::vector<float> iap[4]; int iapW[4]={}, iapLen[4]={};
        // Tank: 4 main delay lines + 4 all-pass delay lines
        std::vector<float> td[4];  int tdW[4]={},  tdLen[4]={};
        std::vector<float> tap[4]; int tapW[4]={}, tapLen[4]={};
        // Damping one-pole LP states
        float lpL = 0.f, lpR = 0.f;
        // Tank modulation phases
        double modPh1 = 0.0, modPh2 = 0.5;
        // Pre-delay line
        std::vector<float> preL, preR; int preWL = 0, preWR = 0;

        // ── Chorus ────────────────────────────────────────────────────────────────
        std::vector<float> chorusBuf;
        int   chorusW = 0;
        float chorusPh[3] = { 0.f, 1.f/3.f, 2.f/3.f };
    };
    FxState fxs[2];   // fxs[0] = Voice A DSP state,  fxs[1] = Voice B DSP state

    // Pre-allocated temp buffers for Voice B output when the host only
    // activates one stereo output bus (fallback-mix path).
    std::vector<float> voiceBTempL, voiceBTempR;

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
    void  prepareFx (double sampleRate);
    void  processFxBuffer (float* L, float* R, int numSamples,
                           FxState& state, const FxParams& params);

    // Process one sample of one voice; writes stereo output to outL and outR.
    void  processSingleVoiceSample (int vi, bool running,
                                    bool useHostSync, double samplePPQ,
                                    double effectiveBPM,
                                    const double* swingBounds,
                                    const double* swingPPQBounds,
                                    double totalSwingCycle,
                                    double totalSwingPPQ,
                                    const int*   stepOrder,
                                    float        glideCoeff,
                                    float        crossModIn,
                                    float&       outL,
                                    float&       outR);

    float generateOsc1Sample (UnisonSlot& slot, const VoiceParams& vp,
                              double phaseInc, float pulseWidth);
    float generateOsc2Sample (UnisonSlot& slot, const VoiceParams& vp,
                              double phaseInc);
    float applyFilter        (float& ic1, float& ic2, float& ic1_2, float& ic2_2,
                              const VoiceParams& vp,
                              float input, float effectiveCutoff);
    float voltageToQuantizedFreq (const VoiceParams& vp, float voltage,
                                  float rangeOverride = -1.0f);
    int   voltageToMidiNote      (const VoiceParams& vp, float voltage) const;
    int   quantizeNoteToScale    (int midiNote, int rootNote, int scale) const;
    float processModEnv          (const ModEnvParams& p, VoiceState& vs,
                                  bool gateOn, double bpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessor)
};
