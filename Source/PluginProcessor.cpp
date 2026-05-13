#include "PluginProcessor.h"
#include "PluginEditor.h"

static const int scaleIntervals[][12] =
{
    { 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 },  // Major
    { 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },  // Natural Minor
    { 0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1 },  // Dorian
    { 0, 1, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },  // Phrygian
    { 0, 2, 4, 6, 7, 9, 11, -1, -1, -1, -1, -1 },  // Lydian
    { 0, 2, 4, 5, 7, 9, 10, -1, -1, -1, -1, -1 },  // Mixolydian
    { 0, 2, 4, 7,  9, -1, -1, -1, -1, -1, -1, -1 },// Pentatonic Major
    { 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 },// Pentatonic Minor
    { 0, 1, 2, 3,  4,  5,  6,  7,  8,  9, 10, 11 } // Chromatic
};
static const int scaleSizes[] = { 7, 7, 7, 7, 7, 7, 5, 5, 12 };

//==============================================================================
VoltageSeq2AudioProcessor::VoltageSeq2AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    adsrParams.attack  = 0.01f;
    adsrParams.decay   = 0.1f;
    adsrParams.sustain = 0.7f;
    adsrParams.release = 0.08f;
    adsr.setParameters (adsrParams);
    filterEnv.setParameters (filterEnvParams);

    buildWavetables();

    float defaultVoltages[16] = {
         0.0f,  1.0f,  2.5f,  1.5f,
        -1.0f,  0.5f,  2.0f,  3.5f,
         0.0f, -1.0f,  1.0f,  2.0f,
        -2.0f,  0.5f,  1.5f, -0.5f
    };
    for (int i = 0; i < numSteps; ++i)
    {
        stepVoltages[i] = defaultVoltages[i];
        stepGates[i]    = true;
        stepGlides[i]   = false;
    }
}

VoltageSeq2AudioProcessor::~VoltageSeq2AudioProcessor() {}

//==============================================================================
void VoltageSeq2AudioProcessor::buildWavetables()
{
    for (int i = 0; i < wavetableSize; ++i)
    {
        double phase = (double)i / wavetableSize;
        wavetables[0][i] = (float)std::sin (phase * juce::MathConstants<double>::twoPi);
        wavetables[1][i] = (phase < 0.5) ? (float)(phase * 4.0 - 1.0)
                                          : (float)(3.0 - phase * 4.0);
        wavetables[2][i] = (float)(phase * 2.0 - 1.0);
        wavetables[3][i] = (phase < 0.5) ? 1.0f : -1.0f;
    }
}

//==============================================================================
void VoltageSeq2AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    adsr.setSampleRate (sampleRate);
    adsr.setParameters (adsrParams);
    filterEnv.setSampleRate (sampleRate);
    filterEnv.setParameters (filterEnvParams);

    ic1eq = 0.0f;
    ic2eq = 0.0f;
    lfoPhase     = 0.0f;
    pulseWidth   = 0.5f;
    glideActive  = false;
    sampleCounter = 0.0;
    lastStep      = -1;
    currentStep   = 0;

    float freq   = voltageToQuantizedFreq (stepVoltages[0]);
    currentFreq1 = freq * (float)std::pow (2.0, (double)osc1Octave);
    currentFreq2 = freq * (float)std::pow (2.0, (double)osc2Octave);
    targetFreq1  = currentFreq1;
    targetFreq2  = currentFreq2;
    osc1PhaseInc = currentFreq1 / sampleRate;
    osc2PhaseInc = currentFreq2 / sampleRate;
}

void VoltageSeq2AudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VoltageSeq2AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}
#endif

//==============================================================================
void VoltageSeq2AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    adsr.setParameters (adsrParams);
    filterEnv.setParameters (filterEnvParams);

    //--------------------------------------------------------------------------
    // HOST SYNC
    //--------------------------------------------------------------------------
    double effectiveBPM = internalBPM;
    double startPPQ     = -1.0;
    bool   useHostSync  = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                effectiveBPM = *bpm;

            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                { startPPQ = *ppq; useHostSync = (startPPQ >= 0.0); }
        }
    }

    double ppqPerSample   = effectiveBPM / 60.0 / currentSampleRate;
    double samplesPerBeat = currentSampleRate * 60.0 / effectiveBPM;
    double samplesPerStep = samplesPerBeat / 4.0;   // 1/16-note

    //--------------------------------------------------------------------------
    // Pre-compute glide coefficient once per block (std::exp is expensive)
    //--------------------------------------------------------------------------
    float glideCoeff = 0.0f;
    if (portamentoTime > 0.001f)
        glideCoeff = std::exp (-1.0f / ((float)portamentoTime * (float)currentSampleRate));

    auto* leftCh  = buffer.getWritePointer (0);
    auto* rightCh = buffer.getWritePointer (1);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        //----------------------------------------------------------------------
        // SEQUENCER CLOCK
        //----------------------------------------------------------------------
        int newStep;
        if (useHostSync)
        {
            double samplePPQ = startPPQ + (double)sample * ppqPerSample;
            newStep = (int)(samplePPQ / 0.25) % numSteps;
            if (newStep < 0) newStep = 0;
        }
        else
        {
            newStep = (int)(sampleCounter / samplesPerStep) % numSteps;
        }

        if (newStep != lastStep)
        {
            lastStep    = newStep;
            currentStep = newStep;

            if (stepGates[currentStep])
            {
                float baseFreq = voltageToQuantizedFreq (stepVoltages[currentStep]);
                targetFreq1 = baseFreq * (float)std::pow (2.0, (double)osc1Octave);
                targetFreq2 = baseFreq * (float)std::pow (2.0, (double)osc2Octave);

                bool doGlide = (portamentoTime > 0.001f && stepGlides[currentStep]);

                if (doGlide)
                {
                    // 303-style legato slide:
                    //  - pitch glides smoothly from currentFreq to targetFreq
                    //  - ADSR is NOT retriggered (envelope continues)
                    glideActive = true;
                }
                else
                {
                    // Hard step: snap to new pitch and retrigger both envelopes
                    glideActive  = false;
                    currentFreq1 = targetFreq1;
                    currentFreq2 = targetFreq2;
                    osc1PhaseInc = currentFreq1 / currentSampleRate;
                    osc2PhaseInc = currentFreq2 / currentSampleRate;
                    adsr.noteOff();
                    adsr.noteOn();
                    filterEnv.noteOff();
                    filterEnv.noteOn();
                }
            }
            else
            {
                glideActive = false;
                adsr.noteOff();
                filterEnv.noteOff();
            }
        }

        sampleCounter += 1.0;
        if (sampleCounter >= samplesPerStep * numSteps)
            sampleCounter -= samplesPerStep * numSteps;

        //----------------------------------------------------------------------
        // PORTAMENTO — exponential frequency smoothing towards target
        //----------------------------------------------------------------------
        if (glideActive)
        {
            currentFreq1 = currentFreq1 * glideCoeff + targetFreq1 * (1.0f - glideCoeff);
            currentFreq2 = currentFreq2 * glideCoeff + targetFreq2 * (1.0f - glideCoeff);
            osc1PhaseInc = currentFreq1 / currentSampleRate;
            osc2PhaseInc = currentFreq2 / currentSampleRate;
        }

        //----------------------------------------------------------------------
        // LFO
        //----------------------------------------------------------------------
        float lfoRaw   = std::sin (lfoPhase * juce::MathConstants<float>::twoPi);
        float lfoValue = lfoRaw * lfoDepth;
        lfoPhase += (float)(lfoRate / currentSampleRate);
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        pulseWidth = 0.5f;
        float pitchMod  = 1.0f;
        float cutoffMod = 0.0f;

        switch (lfoTarget)
        {
            case 0:  pulseWidth = juce::jlimit (0.05f, 0.95f, 0.5f + lfoValue * 0.4f); break;
            case 1:  cutoffMod  = lfoValue * 4000.0f;                                   break;
            case 2:  pitchMod   = std::pow (2.0f, lfoValue / 12.0f);                    break;
        }

        //----------------------------------------------------------------------
        // FILTER ENVELOPE → effective cutoff
        //----------------------------------------------------------------------
        float filterEnvSample = filterEnv.getNextSample();
        float effectiveCutoff = filterCutoff
                                * std::pow (2.0f, filterEnvAmount * 4.0f * filterEnvSample);
        effectiveCutoff = juce::jlimit (20.0f, 20000.0f, effectiveCutoff + cutoffMod);

        //----------------------------------------------------------------------
        // SYNTH VOICE
        //----------------------------------------------------------------------
        float osc1    = generateOsc1Sample (osc1PhaseInc * pitchMod) * osc1Level;
        float osc2    = generateOsc2Sample (osc2PhaseInc * pitchMod) * osc2Level;
        float filtered = applyFilter (osc1 + osc2, effectiveCutoff);
        float envelope = adsr.getNextSample();
        float output   = filtered * envelope * 0.3f;

        leftCh[sample]  = output;
        rightCh[sample] = output;
    }
}

//==============================================================================
// Topology-Preserving Transform State Variable Filter (Simper / Cytomic)
float VoltageSeq2AudioProcessor::applyFilter (float input, float effectiveCutoff)
{
    float cutClamped = juce::jlimit (20.0f, (float)currentSampleRate * 0.45f, effectiveCutoff);
    float g  = std::tan (juce::MathConstants<float>::pi * cutClamped / (float)currentSampleRate);
    float k  = juce::jlimit (0.01f, 2.0f, 2.0f - 1.99f * filterResonance);
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;
    float v3 = input - ic2eq;
    float v1 = a1 * ic1eq + a2 * v3;
    float v2 = ic2eq + a2 * ic1eq + a3 * v3;
    ic1eq = 2.0f * v1 - ic1eq;
    ic2eq = 2.0f * v2 - ic2eq;
    return v2;
}

//==============================================================================
float VoltageSeq2AudioProcessor::voltageToQuantizedFreq (float voltage)
{
    float scaledVoltage = voltage * rangeVCA;
    float rawMidi = juce::jlimit (0.0f, 127.0f, 60.0f + scaledVoltage * 5.0f);
    int quantizedNote = quantizeNoteToScale ((int)std::round (rawMidi));
    return 440.0f * std::pow (2.0f, (quantizedNote - 69.0f) / 12.0f);
}

//==============================================================================
int VoltageSeq2AudioProcessor::quantizeNoteToScale (int midiNote)
{
    int bestNote   = midiNote;
    int bestDist   = 127;
    int octaveBase = (midiNote / 12) * 12;

    for (int octOffset = -12; octOffset <= 12; octOffset += 12)
    {
        int base = octaveBase + octOffset;
        for (int i = 0; i < scaleSizes[currentScale]; ++i)
        {
            int interval = scaleIntervals[currentScale][i];
            if (interval < 0) break;
            int note = base + rootNote + interval;
            int dist = std::abs (note - midiNote);
            if (dist < bestDist) { bestDist = dist; bestNote = note; }
        }
    }
    return juce::jlimit (0, 127, bestNote);
}

//==============================================================================
float VoltageSeq2AudioProcessor::generateOsc1Sample (double phaseInc)
{
    float output = 0.0f;
    switch (osc1Waveform)
    {
        case Sine:
            output = (float)std::sin (osc1Phase * juce::MathConstants<double>::twoPi);
            break;
        case Saw:
            output = (float)(osc1Phase * 2.0 - 1.0);
            break;
        case Square:
            output = (osc1Phase < pulseWidth) ? 1.0f : -1.0f;
            break;
        case Triangle:
            output = (osc1Phase < 0.5) ? (float)(osc1Phase * 4.0 - 1.0)
                                        : (float)(3.0 - osc1Phase * 4.0);
            break;
    }
    osc1Phase += phaseInc;
    if (osc1Phase >= 1.0) osc1Phase -= 1.0;
    return output;
}

//==============================================================================
float VoltageSeq2AudioProcessor::generateOsc2Sample (double phaseInc)
{
    float tablePos = osc2Position * (numWavetables - 1);
    int   tableA   = (int)tablePos;
    int   tableB   = juce::jmin (tableA + 1, numWavetables - 1);
    float blend    = tablePos - tableA;

    float readPos = (float)(osc2Phase * wavetableSize);
    int   indexA  = (int)readPos % wavetableSize;
    int   indexB  = (indexA + 1) % wavetableSize;
    float frac    = readPos - (int)readPos;

    float sA = wavetables[tableA][indexA] + frac * (wavetables[tableA][indexB] - wavetables[tableA][indexA]);
    float sB = wavetables[tableB][indexA] + frac * (wavetables[tableB][indexB] - wavetables[tableB][indexA]);

    osc2Phase += phaseInc;
    if (osc2Phase >= 1.0) osc2Phase -= 1.0;
    return sA + blend * (sB - sA);
}

//==============================================================================
bool VoltageSeq2AudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* VoltageSeq2AudioProcessor::createEditor()
{
    return new VoltageSeq2AudioProcessorEditor (*this);
}

const juce::String VoltageSeq2AudioProcessor::getName() const { return JucePlugin_Name; }
bool VoltageSeq2AudioProcessor::acceptsMidi() const  { return false; }
bool VoltageSeq2AudioProcessor::producesMidi() const { return false; }
bool VoltageSeq2AudioProcessor::isMidiEffect() const { return false; }
double VoltageSeq2AudioProcessor::getTailLengthSeconds() const { return 0.0; }
int VoltageSeq2AudioProcessor::getNumPrograms()    { return 1; }
int VoltageSeq2AudioProcessor::getCurrentProgram() { return 0; }
void VoltageSeq2AudioProcessor::setCurrentProgram (int) {}
const juce::String VoltageSeq2AudioProcessor::getProgramName (int) { return {}; }
void VoltageSeq2AudioProcessor::changeProgramName (int, const juce::String&) {}
void VoltageSeq2AudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void VoltageSeq2AudioProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoltageSeq2AudioProcessor();
}
