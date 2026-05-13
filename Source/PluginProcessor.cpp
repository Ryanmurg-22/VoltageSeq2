#include "PluginProcessor.h"
#include "PluginEditor.h"

// Static member definitions required by the linker
constexpr double VoltageSeq2AudioProcessor::ppqDivTable[7];
constexpr double VoltageSeq2AudioProcessor::cenvDivBars[8];

static const int scaleIntervals[][12] =
{
    { 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 },
    { 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },
    { 0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1 },
    { 0, 1, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },
    { 0, 2, 4, 6, 7, 9, 11, -1, -1, -1, -1, -1 },
    { 0, 2, 4, 5, 7, 9, 10, -1, -1, -1, -1, -1 },
    { 0, 2, 4, 7,  9, -1, -1, -1, -1, -1, -1, -1 },
    { 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 },
    { 0, 1, 2, 3,  4,  5,  6,  7,  8,  9, 10, 11 }
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
    lfo2Phase    = 0.0f;
    pulseWidth   = osc1PulseWidth;
    glideActive  = false;
    sampleCounter = 0.0;
    lastPos      = -1;
    currentStep  = 0;
    randomStep   = 0;
    cenv1State   = {};
    cenv2State   = {};

    if (autoRun.load())
        sequencerRunning.store (true);

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

    // Act on a reset request from the UI thread
    if (resetOnNextBlock.exchange (false))
    {
        sampleCounter = 0.0;
        lastPos       = -1;
    }

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

    // PPQ per step from the clock-division table
    const double ppqStep      = ppqDivTable[juce::jlimit (0, 6, clockDivision)];
    const double samplesPerBeat = currentSampleRate * 60.0 / effectiveBPM;
    const double samplesPerStep = ppqStep * samplesPerBeat;
    const double ppqPerSample   = effectiveBPM / 60.0 / currentSampleRate;

    // Pre-compute glide coefficient once per block
    float glideCoeff = 0.0f;
    if (portamentoTime > 0.001f)
        glideCoeff = std::exp (-1.0f / ((float)portamentoTime * (float)currentSampleRate));

    auto* leftCh  = buffer.getWritePointer (0);
    auto* rightCh = buffer.getWritePointer (1);

    const bool running = sequencerRunning.load();

    // Build step-order mapping for this block (cheap, O(seqLen))
    int stepOrder[16];
    switch (playOrder)
    {
        case Backward:
            for (int i = 0; i < sequenceLength; ++i)
                stepOrder[i] = sequenceLength - 1 - i;
            break;
        case Converge:
        {
            int lo = 0, hi = sequenceLength - 1;
            for (int i = 0; i < sequenceLength; ++i)
                stepOrder[i] = (i % 2 == 0) ? lo++ : hi--;
            break;
        }
        case Forward:
        case Random:
        default:
            for (int i = 0; i < sequenceLength; ++i)
                stepOrder[i] = i;
            break;
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        //----------------------------------------------------------------------
        // SEQUENCER CLOCK  (only advances when running)
        //----------------------------------------------------------------------
        if (running)
        {
            int pos;
            if (useHostSync)
            {
                double samplePPQ = startPPQ + (double)sample * ppqPerSample;
                pos = (int)(samplePPQ / ppqStep) % sequenceLength;
                if (pos < 0) pos = 0;
            }
            else
            {
                pos = (int)(sampleCounter / samplesPerStep) % sequenceLength;
            }

            if (pos != lastPos)
            {
                lastPos = pos;

                int newStep;
                if (playOrder == Random)
                {
                    newStep = juce::Random::getSystemRandom().nextInt (sequenceLength);
                    randomStep = newStep;
                }
                else
                {
                    newStep = stepOrder[pos];
                }
                currentStep = newStep;

                if (stepGates[currentStep])
                {
                    float baseFreq = voltageToQuantizedFreq (stepVoltages[currentStep]);
                    targetFreq1 = baseFreq * (float)std::pow (2.0, (double)osc1Octave);
                    targetFreq2 = baseFreq * (float)std::pow (2.0, (double)osc2Octave);

                    bool doGlide = (portamentoTime > 0.001f && stepGlides[currentStep]);
                    glideActive  = doGlide;

                    if (!doGlide)
                    {
                        currentFreq1 = targetFreq1;
                        currentFreq2 = targetFreq2;
                        osc1PhaseInc = currentFreq1 / currentSampleRate;
                        osc2PhaseInc = currentFreq2 / currentSampleRate;
                        adsr.noteOff();      adsr.noteOn();
                        filterEnv.noteOff(); filterEnv.noteOn();
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
            if (sampleCounter >= samplesPerStep * sequenceLength)
                sampleCounter -= samplesPerStep * sequenceLength;
        }

        //----------------------------------------------------------------------
        // PORTAMENTO
        //----------------------------------------------------------------------
        if (glideActive)
        {
            currentFreq1 = currentFreq1 * glideCoeff + targetFreq1 * (1.0f - glideCoeff);
            currentFreq2 = currentFreq2 * glideCoeff + targetFreq2 * (1.0f - glideCoeff);
            osc1PhaseInc = currentFreq1 / currentSampleRate;
            osc2PhaseInc = currentFreq2 / currentSampleRate;
        }

        //----------------------------------------------------------------------
        // LFO 1 + LFO 2
        // Both contribute additively to each destination; pitch is multiplicative.
        //----------------------------------------------------------------------
        float lfo1Val = std::sin (lfoPhase  * juce::MathConstants<float>::twoPi) * lfoDepth;
        float lfo2Val = std::sin (lfo2Phase * juce::MathConstants<float>::twoPi) * lfo2Depth;

        lfoPhase  += (float)(lfoRate  / currentSampleRate);
        lfo2Phase += (float)(lfo2Rate / currentSampleRate);
        if (lfoPhase  >= 1.0f) lfoPhase  -= 1.0f;
        if (lfo2Phase >= 1.0f) lfo2Phase -= 1.0f;

        // Accumulate modulation per destination
        float pwmMod    = 0.0f;
        float cutoffMod = 0.0f;
        float pitchMod  = 1.0f;

        // LFO 1
        if (lfoTarget == 0) pwmMod    += lfo1Val * 0.4f;
        if (lfoTarget == 1) cutoffMod += lfo1Val * 4000.0f;
        if (lfoTarget == 2) pitchMod  *= std::pow (2.0f, lfo1Val / 12.0f);

        // LFO 2
        if (lfo2Target == 0) pwmMod    += lfo2Val * 0.4f;
        if (lfo2Target == 1) cutoffMod += lfo2Val * 4000.0f;
        if (lfo2Target == 2) pitchMod  *= std::pow (2.0f, lfo2Val / 12.0f);

        // Pulse width = user base + combined LFO contribution
        pulseWidth = juce::jlimit (0.05f, 0.95f, osc1PulseWidth + pwmMod);

        //----------------------------------------------------------------------
        // COMPLEX ENVELOPES
        // Gate signal: sustained-high while current step has a gate
        //----------------------------------------------------------------------
        const bool gateOn = running && stepGates[currentStep];
        const float cenv1Out = processCEnv (cenv1, cenv1State, gateOn, effectiveBPM);
        const float cenv2Out = processCEnv (cenv2, cenv2State, gateOn, effectiveBPM);

        // Accumulate complex env modulation per destination
        float cenvAmpMod    = 1.0f;
        float cenvCutoffMod = 1.0f;
        float cenvPitchMod  = 1.0f;

        auto applyCEnv = [&](const ComplexEnvParams& p, float envOut)
        {
            const float v = envOut * p.depth;
            if (p.dest == 0)  // Amplitude
                cenvAmpMod    *= (1.0f - p.depth + v);
            else if (p.dest == 1)  // Filter Cutoff
                cenvCutoffMod *= std::pow (2.0f, v * 4.0f);
            else if (p.dest == 2)  // Pitch
                cenvPitchMod  *= std::pow (2.0f, v * 2.0f);
        };
        applyCEnv (cenv1, cenv1Out);
        applyCEnv (cenv2, cenv2Out);

        // Combine pitch mods (LFO + complex env)
        pitchMod *= cenvPitchMod;

        //----------------------------------------------------------------------
        // FILTER ENVELOPE → effective cutoff
        //----------------------------------------------------------------------
        float fEnvSample  = filterEnv.getNextSample();
        float effectiveCut = filterCutoff
                             * std::pow (2.0f, filterEnvAmount * 4.0f * fEnvSample);
        effectiveCut = juce::jlimit (20.0f, 20000.0f, (effectiveCut + cutoffMod) * cenvCutoffMod);

        //----------------------------------------------------------------------
        // SYNTH VOICE
        //----------------------------------------------------------------------
        float osc1    = generateOsc1Sample (osc1PhaseInc * pitchMod) * osc1Level;
        float osc2    = generateOsc2Sample (osc2PhaseInc * pitchMod) * osc2Level;

        // Feed scope ring-buffer (pre-filter, pre-envelope — always shows waveform shape)
        oscScopeBuffer[scopeWritePos] = osc1 + osc2;
        scopeWritePos = (scopeWritePos + 1) % scopeSize;

        float filtered = applyFilter (osc1 + osc2, effectiveCut);
        float envelope = adsr.getNextSample();
        float output   = filtered * envelope * cenvAmpMod * 0.3f;

        leftCh[sample]  = output;
        rightCh[sample] = output;
    }
}

//==============================================================================
float VoltageSeq2AudioProcessor::applyFilter (float input, float effectiveCutoff)
{
    float cut = juce::jlimit (20.0f, (float)currentSampleRate * 0.45f, effectiveCutoff);
    float g  = std::tan (juce::MathConstants<float>::pi * cut / (float)currentSampleRate);
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
            // pulseWidth = osc1PulseWidth (base) + LFO contributions, clamped 0.05–0.95
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
// Complex envelope processor
// Clock-sync mode: cycles continuously at the selected bar division.
// Gate-triggered mode: standard ADSR state machine driven by the step gate.
//==============================================================================
float VoltageSeq2AudioProcessor::processCEnv (const ComplexEnvParams& p, CEnvState& s,
                                               bool gateOn, double bpm)
{
    if (p.clockSync)
    {
        // One cycle = cenvDivBars bars; 1 bar = 4 beats = 4 * (60/bpm) seconds
        const double cycleSeconds = cenvDivBars[juce::jlimit (0, 7, p.clockDiv)]
                                    * 4.0 * 60.0 / bpm;
        const double cycleSamples = cycleSeconds * currentSampleRate;

        // A, D, R are proportional to the time params (0–4 s range each);
        // sustain fills whatever remains.
        const float weightTotal = p.attack + p.decay + p.release + 0.3f;
        const double aSamp = (p.attack  / weightTotal) * cycleSamples;
        const double dSamp = (p.decay   / weightTotal) * cycleSamples;
        const double sSamp = (0.3f      / weightTotal) * cycleSamples;
        const double rSamp = (p.release / weightTotal) * cycleSamples;

        s.clockPos += 1.0;
        if (s.clockPos >= cycleSamples) s.clockPos -= cycleSamples;

        const double cp = s.clockPos;
        if (cp < aSamp)
        {
            s.level = (float)(cp / aSamp);
        }
        else if (cp < aSamp + dSamp)
        {
            float t = (float)((cp - aSamp) / dSamp);
            s.level = 1.0f - t * (1.0f - p.sustain);
        }
        else if (cp < aSamp + dSamp + sSamp)
        {
            s.level = p.sustain;
        }
        else
        {
            float t = (float)((cp - aSamp - dSamp - sSamp) / juce::jmax (1.0, rSamp));
            s.level = p.sustain * (1.0f - juce::jmin (1.0f, t));
        }
    }
    else
    {
        // Gate-triggered ADSR
        const float sr    = (float)currentSampleRate;
        const float aSamp = juce::jmax (1.0f, p.attack  * sr);
        const float dSamp = juce::jmax (1.0f, p.decay   * sr);
        const float rSamp = juce::jmax (1.0f, p.release * sr);

        const bool rise = gateOn  && !s.prevGate;
        const bool fall = !gateOn && s.prevGate;
        s.prevGate = gateOn;

        if (rise) s.stage = CEnvState::Attack;
        else if (fall && s.stage != CEnvState::Idle) s.stage = CEnvState::Release;

        switch (s.stage)
        {
            case CEnvState::Idle: break;
            case CEnvState::Attack:
                s.level += 1.0f / aSamp;
                if (s.level >= 1.0f) { s.level = 1.0f; s.stage = CEnvState::Decay; }
                break;
            case CEnvState::Decay:
                s.level -= (1.0f - p.sustain) / dSamp;
                if (s.level <= p.sustain) { s.level = p.sustain; s.stage = CEnvState::Sustain; }
                break;
            case CEnvState::Sustain:
                s.level = p.sustain;
                break;
            case CEnvState::Release:
                s.level -= p.sustain / rSamp;
                if (s.level <= 0.0f)
                {
                    s.level = 0.0f;
                    s.stage = p.looping ? CEnvState::Attack : CEnvState::Idle;
                }
                break;
        }
    }
    return s.level;
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
