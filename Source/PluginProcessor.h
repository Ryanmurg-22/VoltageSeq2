#pragma once
#include <JuceHeader.h>
#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"

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
        float stepVoltages   [16] = {};
        float stepVelocity   [16] = { 100,100,100,100,100,100,100,100,
                                      100,100,100,100,100,100,100,100 }; // 1–127, default 100
        bool  stepGates      [16] = {};
        bool  stepGlides     [16] = {};
        bool  stepAccents    [16] = {};   // true = accent boost (VCA +3dB, filter env +, resonance +)
        bool  stepTied       [16] = {};   // true = sustain gate from prev step, no envelope retrigger
        int   stepRepeats    [16] = {};   // 0=1× 1=2× 2=3× 3=4× — ratchet count
        int   stepPulses     [16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };  // 1–8 clock pulses per step
        int   stepOctave     [16] = {};                                      // −4..+4, default 0
        float stepProbability[16] = { 100.f,100.f,100.f,100.f,100.f,100.f,100.f,100.f,
                                      100.f,100.f,100.f,100.f,100.f,100.f,100.f,100.f };
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

        // ── Plaits ───────────────────────────────────────────────────────────────
        bool  plaitsEnabled   = false;
        int   plaitsEngine    = 8;     // default: VirtualAnalog (engine index 8)
        float plaitsHarmonics = 0.5f;
        float plaitsTimbre    = 0.5f;
        float plaitsMorph     = 0.5f;
        float plaitsAuxBlend  = 0.0f;  // 0=main output, 1=aux output
        bool  plaitsTrigMode  = false; // false=free-running (ADSR shapes amp), true=internal LPG triggered
        int   plaitsOctave    = 0;     // -2..+2 octave transpose

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
        VoiceMode voiceMode       = Mono;
        int       unisonCount     = 4;      // 2 or 4 slots when mode != Mono
        float     unisonSpread    = 0.15f;  // detune half-width in semitones (0..0.5)
        float     unisonWidth     = 0.7f;   // stereo pan width (0..1)
        bool      shiftRegChordMode = false; // Poly MIDI: re-fire full register as chord each step

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
        bool  used              = false;
        float stepVoltages  [16] = {};
        float stepVelocity  [16] = { 100,100,100,100,100,100,100,100,
                                     100,100,100,100,100,100,100,100 };
        bool  stepGates     [16] = {};
        bool  stepGlides    [16] = {};
        bool  stepAccents   [16] = {};
        bool  stepTied      [16] = {};
        int   stepRepeats   [16] = {};   // 0=1× 1=2× 2=3× 3=4×
        int   stepPulses    [16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
        int   stepOctave    [16] = {};
        float stepProbability[16] = { 100.f,100.f,100.f,100.f,100.f,100.f,100.f,100.f,
                                      100.f,100.f,100.f,100.f,100.f,100.f,100.f,100.f };
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

    void resetTuringMachine   ();                 // randomises bits[]
    void captureTuringMachine ();                 // snapshots TM output → target voice

    struct PatternSeqState
    {
        int  mode          = 0;    // 0=OFF  1=SEQ (auto-advance)  2=MIDI (note-triggered)
        bool immediate     = false; // false=queued (wait for full cycle), true=next step
        int  list     [16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};  // bank slot indices (0-based)
        int  loopCount[16] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
        int  listLength    = 0;    // number of active playlist entries
        // Runtime (not serialised)
        int  currentEntry  = 0;
        int  currentRepeat = 0;
        int  pendingSlot   = -1;   // MIDI mode: next slot to load, -1=none
    };
    PatternSeqState patSeq[2];
    std::atomic<bool> patternChangedForUI[2] { false, false };

    struct TuringMachineState
    {
        bool  writeEnabled = false;   // WRITE mode — overwrite voice steps in real-time
        float lockAmount   = 0.75f;   // 0=fully random  1=locked loop
        int   length       = 16;      // active register length (2-16)
        bool  affectGates  = false;   // false=pitch only  true=gate+pitch
        bool  bits[16]     = {};      // the shift register (initialised to random in ctor)

        // Display mirror — written by audio thread, read by 30-Hz timer (message thread).
        // Holds the PREDICTED output for each of the next 16 steps (locked simulation).
        float previewVoltages[16] = {};
        bool  previewGates   [16] = {};
        std::atomic<bool> displayDirty { false };

        // Copy/move deleted because of the atomic — use default-construct only
        TuringMachineState() = default;
        TuringMachineState (const TuringMachineState&)            = delete;
        TuringMachineState& operator= (const TuringMachineState&) = delete;
    };
    TuringMachineState   turingMachine;           // single shared TM
    std::atomic<int>     tmTargetVoice { 0 };    // which voice TM writes to (0=A, 1=B)

    struct PlaitsState {
        plaits::Voice         voice;
        plaits::Patch         patch    = {};
        plaits::Modulations   mods     = {};
        stmlib::BufferAllocator allocator;
        char                  memory[32768];
        static constexpr int  kBufSize = 24;
        plaits::Voice::Frame  frames[24];
        int                   frameIdx       = 24;   // start exhausted → triggers first render
        bool                  triggerPending = false;
        bool                  initialized    = false;
        float                 baseNote       = 60.0f; // quantised target note (set on step advance)
        float                 smoothedNote   = 60.0f; // portamento-smoothed version of baseNote
        float                 stepVoltage    = 0.0f;  // raw step voltage (stored so range-LFO can recompute)
        int                   stepOctShift   = 0;     // stepOctave + plaitsOctave for current step
        // Non-copyable due to allocator
        PlaitsState() = default;
        PlaitsState(const PlaitsState&) = delete;
        PlaitsState& operator=(const PlaitsState&) = delete;
    };
    PlaitsState plaitsState[2];

    // Engine name table (24 entries, indexed by engine number)
    static const char* const kPlaitsEngineNames[24];

    // audio-thread-safe (no APVTS sync).
    // resetPos=false: keep sampleCounter running (use when called from SEQ mode
    // so the cycle-0 trigger that just fired doesn't repeat on the next block).
    void loadPatternAudio (int vi, int slot, bool resetPos = true);

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

    // Generate a random sequence for voice vi.
    // doGates: randomise gate pattern / length.  doPitch: randomise step voltages.
    void generateRandomSequence (int vi, bool doGates = true, bool doPitch = true);

    // Apply a Euclidean (Bjorklund) rhythm to voice vi.
    // steps: sequence length (2-16), hits: total events (1..steps*maxRatchets),
    // maxRatchets: 1=standard binary, 2-4=ratchet density per step.
    void applyEuclidean (int vi, int steps, int hits, int maxRatchets);

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

        // ── Accent ───────────────────────────────────────────────────────────────
        bool   accentActive      = false;  // true for duration of an accented step

        // ── Ratchet sub-step state ────────────────────────────────────────────────
        int    ratchetSubStep    = 0;
        double ratchetSubStepDur = 0.0;  // duration of one sub-step in samples
        double ratchetStepPos    = 0.0;  // samples elapsed since step start
        bool   ratchetNoteOff    = false;

        // ── MIDI Out step signals (set by processSingleVoiceSample, consumed in processBlock)
        bool  midiStepFired   = false;  // one-shot: new step just advanced
        bool  midiStepGate     = false;  // gate state of the new step
        bool  midiStepGlide    = false;  // new step has portamento active
        bool  midiStepTied     = false;  // new step is a tie → legato, no retrigger
        int   midiStepNote     = -1;     // quantized MIDI note for new step (-1 = none)
        juce::uint8 midiStepVel = 100;  // MIDI velocity for the new step (1–127)
        int   midiOutNote     = -1;     // currently sounding MIDI note (MONO/UNISON)
        bool  midiRatchetOff  = false;  // ratchet 50%-point: emit note-off
        bool  midiRatchetOn   = false;  // ratchet sub-step advance: emit note-off + note-on
        // Pitch-bend portamento ramp
        bool  midiGlideActive = false;  // PB ramp in progress
        float midiGlideBend   = 0.0f;   // current PB offset in semitones (decays → 0)
        int   midiGlideTick   = 0;      // samples since last PB message
        // POLY mode shift-register note tracking
        int   midiPolyNotes[kMaxSlots] = { -1, -1, -1, -1 }; // per-slot held notes
        bool  midiPolyEvict    = false; // oldest slot note needs note-off
        int   midiPolyEvictNote = -1;   // the note being evicted
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
                              float input, float effectiveCutoff,
                              float resBoost = 0.0f);
    float voltageToQuantizedFreq (const VoiceParams& vp, float voltage,
                                  float rangeOverride = -1.0f);
    int   voltageToMidiNote      (const VoiceParams& vp, float voltage,
                                  float rangeOverride = -1.0f) const;
    int   quantizeNoteToScale    (int midiNote, int rootNote, int scale) const;
    float processModEnv          (const ModEnvParams& p, VoiceState& vs,
                                  bool gateOn, double bpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessor)
};
