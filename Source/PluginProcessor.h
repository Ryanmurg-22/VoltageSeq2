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
    // PUBLIC PARAMETERS  (read/written directly by the editor)
    //==========================================================================

    static const int numSteps = 16;
    float  stepVoltages[16];
    bool   stepGates[16];
    bool   stepGlides[16];   // per-step slide enable
    double internalBPM = 120.0;

    // Global portamento time (0 = instant, >0 = slide time in seconds)
    float portamentoTime = 0.0f;

    float rangeVCA     = 1.0f;
    int   rootNote     = 0;
    int   currentScale = 0;

    enum Waveform { Sine = 0, Saw, Square, Triangle };
    int   osc1Waveform = Saw;
    float osc1Level    = 0.7f;
    int   osc1Octave   = 0;      // -2 … +2

    float osc2Position = 0.0f;
    float osc2Level    = 0.5f;
    int   osc2Octave   = 0;      // -2 … +2

    float filterCutoff    = 2000.0f;
    float filterResonance = 0.0f;

    float filterEnvAmount = 0.5f;
    juce::ADSR::Parameters filterEnvParams { 0.01f, 0.5f, 0.0f, 0.3f };

    juce::ADSR::Parameters adsrParams;

    float lfoRate   = 2.0f;
    float lfoDepth  = 0.0f;
    int   lfoTarget = 0;    // 0 = PWM, 1 = Cutoff, 2 = Pitch

private:
    double currentSampleRate = 44100.0;

    // OSC 1
    double osc1Phase    = 0.0;
    double osc1PhaseInc = 0.0;

    // OSC 2 wavetable
    static const int wavetableSize = 2048;
    static const int numWavetables = 4;
    float  wavetables[4][2048];
    double osc2Phase    = 0.0;
    double osc2PhaseInc = 0.0;

    // Portamento — actual playing frequencies, smoothed towards target
    float currentFreq1 = 261.63f;
    float currentFreq2 = 261.63f;
    float targetFreq1  = 261.63f;
    float targetFreq2  = 261.63f;
    bool  glideActive  = false;

    // State Variable Filter integrator states
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    // ADSR engines
    juce::ADSR adsr;
    juce::ADSR filterEnv;

    // LFO
    float lfoPhase   = 0.0f;
    float pulseWidth = 0.5f;

    // Sequencer state
    int    currentStep   = 0;
    int    lastStep      = -1;
    double sampleCounter = 0.0;

    void  buildWavetables();
    float generateOsc1Sample (double phaseInc);
    float generateOsc2Sample (double phaseInc);
    float applyFilter (float input, float effectiveCutoff);
    float voltageToQuantizedFreq (float voltage);
    int   quantizeNoteToScale (int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeq2AudioProcessor)
};
