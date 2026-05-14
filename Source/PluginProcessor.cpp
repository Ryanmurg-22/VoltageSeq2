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
    const double ppqStep        = ppqDivTable[juce::jlimit (0, 6, clockDivision)];
    const double samplesPerBeat = currentSampleRate * 60.0 / effectiveBPM;
    const double samplesPerStep = ppqStep * samplesPerBeat;
    const double ppqPerSample   = effectiveBPM / 60.0 / currentSampleRate;

    // ── Swing / shuffle ───────────────────────────────────────────────────────
    // Each step pair (even/odd) shares a combined duration of 2 × nominal step.
    // swingAmount 0.5 = straight; 0.75 = heavy swing (3:1 ratio).
    // swingBounds[i] = cumulative sample count to start of step i (internal clock).
    // swingPPQBounds[i] = same in PPQ (host sync).
    double swingBounds[17];
    double swingPPQBounds[17];
    swingBounds   [0] = 0.0;
    swingPPQBounds[0] = 0.0;
    for (int i = 0; i < sequenceLength; ++i)
    {
        const double factor = (i % 2 == 0)
            ? (2.0 * (double)swingAmount)
            : (2.0 * (1.0 - (double)swingAmount));
        swingBounds   [i + 1] = swingBounds   [i] + samplesPerStep * factor;
        swingPPQBounds[i + 1] = swingPPQBounds[i] + ppqStep        * factor;
    }
    const double totalSwingCycle = swingBounds   [sequenceLength];
    const double totalSwingPPQ   = swingPPQBounds[sequenceLength];

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
                // Wrap absolute PPQ into one swing cycle, then binary-search boundaries
                double samplePPQ  = startPPQ + (double)sample * ppqPerSample;
                double wrappedPPQ = std::fmod (samplePPQ, totalSwingPPQ);
                if (wrappedPPQ < 0.0) wrappedPPQ += totalSwingPPQ;
                pos = sequenceLength - 1;
                for (int i = 0; i < sequenceLength; ++i)
                    if (wrappedPPQ < swingPPQBounds[i + 1]) { pos = i; break; }
            }
            else
            {
                double wrappedCtr = std::fmod (sampleCounter, totalSwingCycle);
                pos = sequenceLength - 1;
                for (int i = 0; i < sequenceLength; ++i)
                    if (wrappedCtr < swingBounds[i + 1]) { pos = i; break; }
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
            if (sampleCounter >= totalSwingCycle)
                sampleCounter -= totalSwingCycle;
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
        float cenvRangeMod  = 0.0f;   // additive delta applied to rangeVCA pre-quantizer

        auto applyCEnv = [&](const ComplexEnvParams& p, float envOut)
        {
            const float v = envOut * p.depth;
            if (p.dest == 0)       // Amplitude
                cenvAmpMod    *= (1.0f - p.depth + v);
            else if (p.dest == 1)  // Filter Cutoff
                cenvCutoffMod *= std::pow (2.0f, v * 4.0f);
            else if (p.dest == 2)  // Pitch — pre-quantizer via range scaling
                cenvRangeMod  += v; // accumulates 0..depth per envelope
        };
        applyCEnv (cenv1, cenv1Out);
        applyCEnv (cenv2, cenv2Out);

        // Pre-quantizer pitch: re-run voltageToQuantizedFreq with a wider/narrower range.
        // The quantizer then snaps the result to the active scale, so every pitch value
        // the envelope produces is already in-key.
        if (cenvRangeMod > 0.001f && running)
        {
            // Treat depth as additive to rangeVCA (same units, same knob range 0–1)
            const float effRange = juce::jlimit (0.0f, 2.0f, rangeVCA + cenvRangeMod);
            const float baseFreq = voltageToQuantizedFreq (stepVoltages[currentStep], effRange);
            const float f1 = baseFreq * (float)std::pow (2.0, (double)osc1Octave);
            const float f2 = baseFreq * (float)std::pow (2.0, (double)osc2Octave);

            if (glideActive)
            {
                // Let the glide system slide toward the envelope-modulated target
                targetFreq1 = f1;
                targetFreq2 = f2;
            }
            else
            {
                osc1PhaseInc = (double)f1 / currentSampleRate;
                osc2PhaseInc = (double)f2 / currentSampleRate;
            }
        }
        // pitchMod (from LFOs) is still applied in OSC generation for vibrato etc.

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
float VoltageSeq2AudioProcessor::voltageToQuantizedFreq (float voltage, float rangeOverride)
{
    float range = (rangeOverride >= 0.0f) ? rangeOverride : rangeVCA;
    float scaledVoltage = voltage * range;
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
void VoltageSeq2AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("VoltageSeq2State");

    // ── Sequencer steps ───────────────────────────────────────────────────────
    for (int i = 0; i < numSteps; ++i)
    {
        xml.setAttribute ("v"  + juce::String (i), (double)stepVoltages[i]);
        xml.setAttribute ("g"  + juce::String (i), stepGates [i]);
        xml.setAttribute ("sl" + juce::String (i), stepGlides[i]);
    }

    // ── Global sequencer params ───────────────────────────────────────────────
    xml.setAttribute ("bpm",      internalBPM);
    xml.setAttribute ("porta",    (double)portamentoTime);
    xml.setAttribute ("clkDiv",   clockDivision);
    xml.setAttribute ("swing",    (double)swingAmount);
    xml.setAttribute ("seqLen",   sequenceLength);
    xml.setAttribute ("unipolar", unipolar);
    xml.setAttribute ("playOrder",playOrder);
    xml.setAttribute ("range",    (double)rangeVCA);
    xml.setAttribute ("root",     rootNote);
    xml.setAttribute ("scale",    currentScale);

    // ── OSC 1 ─────────────────────────────────────────────────────────────────
    xml.setAttribute ("osc1Wave", osc1Waveform);
    xml.setAttribute ("osc1Lvl",  (double)osc1Level);
    xml.setAttribute ("osc1Oct",  osc1Octave);
    xml.setAttribute ("osc1PW",   (double)osc1PulseWidth);

    // ── OSC 2 ─────────────────────────────────────────────────────────────────
    xml.setAttribute ("osc2Pos",  (double)osc2Position);
    xml.setAttribute ("osc2Lvl",  (double)osc2Level);
    xml.setAttribute ("osc2Oct",  osc2Octave);

    // ── Filter ────────────────────────────────────────────────────────────────
    xml.setAttribute ("cut",    (double)filterCutoff);
    xml.setAttribute ("res",    (double)filterResonance);
    xml.setAttribute ("fEnvAmt",(double)filterEnvAmount);
    xml.setAttribute ("fAtk",   (double)filterEnvParams.attack);
    xml.setAttribute ("fDec",   (double)filterEnvParams.decay);
    xml.setAttribute ("fSus",   (double)filterEnvParams.sustain);
    xml.setAttribute ("fRel",   (double)filterEnvParams.release);

    // ── Amp Envelope ──────────────────────────────────────────────────────────
    xml.setAttribute ("aAtk",   (double)adsrParams.attack);
    xml.setAttribute ("aDec",   (double)adsrParams.decay);
    xml.setAttribute ("aSus",   (double)adsrParams.sustain);
    xml.setAttribute ("aRel",   (double)adsrParams.release);

    // ── LFO 1 ─────────────────────────────────────────────────────────────────
    xml.setAttribute ("lfo1Rate", (double)lfoRate);
    xml.setAttribute ("lfo1Dep",  (double)lfoDepth);
    xml.setAttribute ("lfo1Tgt",  lfoTarget);

    // ── LFO 2 ─────────────────────────────────────────────────────────────────
    xml.setAttribute ("lfo2Rate", (double)lfo2Rate);
    xml.setAttribute ("lfo2Dep",  (double)lfo2Depth);
    xml.setAttribute ("lfo2Tgt",  lfo2Target);

    // ── Complex Envelope 1 ────────────────────────────────────────────────────
    xml.setAttribute ("c1Atk",  (double)cenv1.attack);
    xml.setAttribute ("c1Dec",  (double)cenv1.decay);
    xml.setAttribute ("c1Sus",  (double)cenv1.sustain);
    xml.setAttribute ("c1Rel",  (double)cenv1.release);
    xml.setAttribute ("c1Dep",  (double)cenv1.depth);
    xml.setAttribute ("c1Dst",  cenv1.dest);
    xml.setAttribute ("c1Loop", cenv1.looping);
    xml.setAttribute ("c1Sync", cenv1.clockSync);
    xml.setAttribute ("c1Div",  cenv1.clockDiv);

    // ── Complex Envelope 2 ────────────────────────────────────────────────────
    xml.setAttribute ("c2Atk",  (double)cenv2.attack);
    xml.setAttribute ("c2Dec",  (double)cenv2.decay);
    xml.setAttribute ("c2Sus",  (double)cenv2.sustain);
    xml.setAttribute ("c2Rel",  (double)cenv2.release);
    xml.setAttribute ("c2Dep",  (double)cenv2.depth);
    xml.setAttribute ("c2Dst",  cenv2.dest);
    xml.setAttribute ("c2Loop", cenv2.looping);
    xml.setAttribute ("c2Sync", cenv2.clockSync);
    xml.setAttribute ("c2Div",  cenv2.clockDiv);

    copyXmlToBinary (xml, destData);
}

void VoltageSeq2AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (!xml || xml->getTagName() != "VoltageSeq2State") return;

    auto getF = [&](const char* key, float def)
        { return (float)xml->getDoubleAttribute (key, (double)def); };
    auto getI = [&](const char* key, int def)
        { return xml->getIntAttribute (key, def); };
    auto getB = [&](const char* key, bool def)
        { return xml->getBoolAttribute (key, def); };

    // ── Sequencer steps ───────────────────────────────────────────────────────
    for (int i = 0; i < numSteps; ++i)
    {
        stepVoltages[i] = getF (("v"  + juce::String (i)).toRawUTF8(), stepVoltages[i]);
        stepGates   [i] = getB (("g"  + juce::String (i)).toRawUTF8(), stepGates   [i]);
        stepGlides  [i] = getB (("sl" + juce::String (i)).toRawUTF8(), stepGlides  [i]);
    }

    internalBPM    = xml->getDoubleAttribute ("bpm",  internalBPM);
    portamentoTime = getF ("porta",   portamentoTime);
    clockDivision  = getI ("clkDiv",  clockDivision);
    swingAmount    = getF ("swing",   swingAmount);
    sequenceLength = getI ("seqLen",  sequenceLength);
    unipolar       = getB ("unipolar",unipolar);
    playOrder      = getI ("playOrder",playOrder);
    rangeVCA       = getF ("range",   rangeVCA);
    rootNote       = getI ("root",    rootNote);
    currentScale   = getI ("scale",   currentScale);

    osc1Waveform   = getI ("osc1Wave",osc1Waveform);
    osc1Level      = getF ("osc1Lvl", osc1Level);
    osc1Octave     = getI ("osc1Oct", osc1Octave);
    osc1PulseWidth = getF ("osc1PW",  osc1PulseWidth);

    osc2Position   = getF ("osc2Pos", osc2Position);
    osc2Level      = getF ("osc2Lvl", osc2Level);
    osc2Octave     = getI ("osc2Oct", osc2Octave);

    filterCutoff      = getF ("cut",    filterCutoff);
    filterResonance   = getF ("res",    filterResonance);
    filterEnvAmount   = getF ("fEnvAmt",filterEnvAmount);
    filterEnvParams.attack  = getF ("fAtk", filterEnvParams.attack);
    filterEnvParams.decay   = getF ("fDec", filterEnvParams.decay);
    filterEnvParams.sustain = getF ("fSus", filterEnvParams.sustain);
    filterEnvParams.release = getF ("fRel", filterEnvParams.release);

    adsrParams.attack  = getF ("aAtk", adsrParams.attack);
    adsrParams.decay   = getF ("aDec", adsrParams.decay);
    adsrParams.sustain = getF ("aSus", adsrParams.sustain);
    adsrParams.release = getF ("aRel", adsrParams.release);

    lfoRate   = getF ("lfo1Rate",lfoRate);
    lfoDepth  = getF ("lfo1Dep", lfoDepth);
    lfoTarget = getI ("lfo1Tgt", lfoTarget);

    lfo2Rate   = getF ("lfo2Rate",lfo2Rate);
    lfo2Depth  = getF ("lfo2Dep", lfo2Depth);
    lfo2Target = getI ("lfo2Tgt", lfo2Target);

    cenv1.attack    = getF ("c1Atk",  cenv1.attack);
    cenv1.decay     = getF ("c1Dec",  cenv1.decay);
    cenv1.sustain   = getF ("c1Sus",  cenv1.sustain);
    cenv1.release   = getF ("c1Rel",  cenv1.release);
    cenv1.depth     = getF ("c1Dep",  cenv1.depth);
    cenv1.dest      = getI ("c1Dst",  cenv1.dest);
    cenv1.looping   = getB ("c1Loop", cenv1.looping);
    cenv1.clockSync = getB ("c1Sync", cenv1.clockSync);
    cenv1.clockDiv  = getI ("c1Div",  cenv1.clockDiv);

    cenv2.attack    = getF ("c2Atk",  cenv2.attack);
    cenv2.decay     = getF ("c2Dec",  cenv2.decay);
    cenv2.sustain   = getF ("c2Sus",  cenv2.sustain);
    cenv2.release   = getF ("c2Rel",  cenv2.release);
    cenv2.depth     = getF ("c2Dep",  cenv2.depth);
    cenv2.dest      = getI ("c2Dst",  cenv2.dest);
    cenv2.looping   = getB ("c2Loop", cenv2.looping);
    cenv2.clockSync = getB ("c2Sync", cenv2.clockSync);
    cenv2.clockDiv  = getI ("c2Div",  cenv2.clockDiv);

    // Apply to live ADSR objects immediately
    adsr.setParameters (adsrParams);
    filterEnv.setParameters (filterEnvParams);

    // Request sequencer reset so new length / swing takes effect cleanly
    resetOnNextBlock.store (true);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoltageSeq2AudioProcessor();
}
