#include "PluginProcessor.h"
#include "PluginEditor.h"

// Static constexpr member definitions required by some linkers
constexpr double VoltageSeq2AudioProcessor::ppqDivTable[7];
constexpr double VoltageSeq2AudioProcessor::cenvDivBars[8];

//==============================================================================
// Scale tables (chromatic intervals from root; -1 = unused slot)
//==============================================================================
static const int scaleIntervals[][12] =
{
    { 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 },   // 0 Major
    { 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },   // 1 Natural Minor
    { 0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1 },   // 2 Dorian
    { 0, 1, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },   // 3 Phrygian
    { 0, 2, 4, 6, 7, 9, 11, -1, -1, -1, -1, -1 },   // 4 Lydian
    { 0, 2, 4, 5, 7, 9, 10, -1, -1, -1, -1, -1 },   // 5 Mixolydian
    { 0, 2, 4, 7,  9, -1, -1, -1, -1, -1, -1, -1 }, // 6 Pentatonic Major
    { 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 }, // 7 Pentatonic Minor
    { 0, 1, 2, 3,  4,  5,  6,  7,  8,  9, 10, 11 }  // 8 Chromatic
};
static const int scaleSizes[] = { 7, 7, 7, 7, 7, 7, 5, 5, 12 };

//==============================================================================
VoltageSeq2AudioProcessor::VoltageSeq2AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    buildWavetables();

    // ── Voice A defaults (current) ─────────────────────────────────────────────
    static const float defaultVoltages[16] = {
         0.0f,  1.0f,  2.5f,  1.5f,
        -1.0f,  0.5f,  2.0f,  3.5f,
         0.0f, -1.0f,  1.0f,  2.0f,
        -2.0f,  0.5f,  1.5f, -0.5f
    };
    for (int i = 0; i < numSteps; ++i)
    {
        voice[0].stepVoltages[i] = defaultVoltages[i];
        voice[0].stepGates[i]    = true;
        voice[0].stepGlides[i]   = false;
    }

    // ── Voice B defaults (polyrhythmic counterpoint) ───────────────────────────
    // Different clock division and sequence length for immediate polyrhythm
    static const float defaultVoltagesB[16] = {
         0.0f, -1.5f,  1.0f,  3.0f,
         2.0f,  0.0f, -0.5f,  2.5f,
         1.0f,  0.5f, -1.0f,  0.0f,
         2.0f, -2.0f,  1.5f,  0.5f
    };
    for (int i = 0; i < numSteps; ++i)
    {
        voice[1].stepVoltages[i] = defaultVoltagesB[i];
        voice[1].stepGates[i]    = true;
        voice[1].stepGlides[i]   = false;
    }

    voice[1].clockDivision  = 1;     // 1/8  (creates polyrhythm with Voice A's 1/16)
    voice[1].sequenceLength = 12;    // 12-step pattern (3:4 polyrhythm with 16-step A)
    voice[1].osc1Waveform   = 0;     // Sine (contrasts with Voice A's Saw)
    voice[1].filterCutoff   = 1200.0f;
    voice[1].lfo2Target     = 1;     // LFO2 → filter for Voice B
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

    for (int vi = 0; vi < numVoices; ++vi)
    {
        VoiceState& vs = vstate[vi];
        VoiceParams& vp = voice[vi];

        vs.adsr.setSampleRate (sampleRate);
        vs.adsr.setParameters (vp.adsrParams);
        vs.filterEnv.setSampleRate (sampleRate);
        vs.filterEnv.setParameters (vp.filterEnvParams);

        vs.ic1eq        = 0.0f;
        vs.ic2eq        = 0.0f;
        vs.ic1eq2       = 0.0f;
        vs.ic2eq2       = 0.0f;
        vs.lfoPhase     = 0.0f;
        vs.lfo2Phase    = 0.0f;
        vs.lfo3Phase    = 0.0f;
        vs.lfo4Phase    = 0.0f;
        vs.osc1FeedbackSample = 0.0f;
        vs.osc1DriftRate = 0.08f + juce::Random::getSystemRandom().nextFloat() * 0.18f;
        vs.osc2DriftRate = 0.08f + juce::Random::getSystemRandom().nextFloat() * 0.18f;
        vs.osc1DriftPhase = juce::Random::getSystemRandom().nextFloat();
        vs.osc2DriftPhase = juce::Random::getSystemRandom().nextFloat();
        vs.pulseWidth   = vp.osc1PulseWidth;
        vs.glideActive  = false;
        vs.sampleCounter = 0.0;
        vs.lastPos      = -1;
        vp.currentStep  = 0;
        vs.randomStep   = 0;
        vs.modEnvAdsr.setSampleRate (sampleRate);
        juce::ADSR::Parameters mep { vp.modEnv.attack, vp.modEnv.decay,
                                     vp.modEnv.sustain, vp.modEnv.release };
        vs.modEnvAdsr.setParameters (mep);
        vs.modEnvClockPos = 0.0;
        vs.modEnvPrevGate = false;

        if (autoRun.load())
            vp.sequencerRunning.store (true);

        float freq       = voltageToQuantizedFreq (vp, vp.stepVoltages[0]);
        vs.currentFreq1  = freq * (float)std::pow (2.0, (double)vp.osc1Octave);
        vs.currentFreq2  = freq * (float)std::pow (2.0, (double)vp.osc2Octave);
        vs.targetFreq1   = vs.currentFreq1;
        vs.targetFreq2   = vs.currentFreq2;
        vs.osc1PhaseInc  = vs.currentFreq1 / sampleRate;
        vs.osc2PhaseInc  = vs.currentFreq2 / sampleRate;
    }
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

    //--------------------------------------------------------------------------
    // HOST SYNC — common to both voices (same timeline)
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

    const double ppqPerSample = effectiveBPM / 60.0 / currentSampleRate;

    //--------------------------------------------------------------------------
    // PER-VOICE PRE-COMPUTATION  (swing, step order, glide coeff)
    // Using stack-local arrays so no allocation inside the hot path.
    //--------------------------------------------------------------------------
    double swingBounds   [numVoices][17] = {};
    double swingPPQBounds[numVoices][17] = {};
    double totalSwingCycle[numVoices]    = {};
    double totalSwingPPQ  [numVoices]    = {};
    int    stepOrder      [numVoices][16] = {};
    float  glideCoeff     [numVoices]    = {};

    for (int vi = 0; vi < numVoices; ++vi)
    {
        VoiceParams& vp = voice[vi];

        // Handle reset request from UI thread
        if (vp.resetOnNextBlock.exchange (false))
        {
            vstate[vi].sampleCounter = 0.0;
            vstate[vi].lastPos       = -1;
        }

        // Sync ADSR parameters written by UI thread
        vstate[vi].adsr.setParameters      (vp.adsrParams);
        vstate[vi].filterEnv.setParameters  (vp.filterEnvParams);
        juce::ADSR::Parameters mep2 { voice[vi].modEnv.attack, voice[vi].modEnv.decay,
                                      voice[vi].modEnv.sustain, voice[vi].modEnv.release };
        vstate[vi].modEnvAdsr.setParameters (mep2);

        // Swing boundaries
        const double ppqStep        = ppqDivTable[juce::jlimit (0, 6, vp.clockDivision)];
        const double samplesPerBeat = currentSampleRate * 60.0 / effectiveBPM;
        const double samplesPerStep = ppqStep * samplesPerBeat;

        swingBounds   [vi][0] = 0.0;
        swingPPQBounds[vi][0] = 0.0;
        for (int i = 0; i < vp.sequenceLength; ++i)
        {
            const double factor = (i % 2 == 0)
                ? (2.0 * (double)vp.swingAmount)
                : (2.0 * (1.0 - (double)vp.swingAmount));
            swingBounds   [vi][i + 1] = swingBounds   [vi][i] + samplesPerStep * factor;
            swingPPQBounds[vi][i + 1] = swingPPQBounds[vi][i] + ppqStep        * factor;
        }
        totalSwingCycle[vi] = swingBounds   [vi][vp.sequenceLength];
        totalSwingPPQ  [vi] = swingPPQBounds[vi][vp.sequenceLength];

        // Step order
        switch (vp.playOrder)
        {
            case VoiceParams::Backward:
                for (int i = 0; i < vp.sequenceLength; ++i)
                    stepOrder[vi][i] = vp.sequenceLength - 1 - i;
                break;
            case VoiceParams::Converge:
            {
                int lo = 0, hi = vp.sequenceLength - 1;
                for (int i = 0; i < vp.sequenceLength; ++i)
                    stepOrder[vi][i] = (i % 2 == 0) ? lo++ : hi--;
                break;
            }
            default: // Forward / Random
                for (int i = 0; i < vp.sequenceLength; ++i)
                    stepOrder[vi][i] = i;
                break;
        }

        // Glide coefficient
        if (vp.portamentoTime > 0.001f)
            glideCoeff[vi] = std::exp (-1.0f / (vp.portamentoTime * (float)currentSampleRate));
        else
            glideCoeff[vi] = 0.0f;
    }

    //--------------------------------------------------------------------------
    // SAMPLE LOOP
    //--------------------------------------------------------------------------
    auto* leftCh  = buffer.getWritePointer (0);
    auto* rightCh = buffer.getWritePointer (1);

    const int numSamples = buffer.getNumSamples();
    for (int s = 0; s < numSamples; ++s)
    {
        const double samplePPQ = startPPQ + (double)s * ppqPerSample;

        float outA = processSingleVoiceSample (0,
            voice[0].sequencerRunning.load(), useHostSync, samplePPQ, effectiveBPM,
            swingBounds[0], swingPPQBounds[0],
            totalSwingCycle[0], totalSwingPPQ[0],
            stepOrder[0], glideCoeff[0],
            crossModSample[1]);   // voice B modulates voice A

        float outB = processSingleVoiceSample (1,
            voice[1].sequencerRunning.load(), useHostSync, samplePPQ, effectiveBPM,
            swingBounds[1], swingPPQBounds[1],
            totalSwingCycle[1], totalSwingPPQ[1],
            stepOrder[1], glideCoeff[1],
            crossModSample[0]);   // voice A modulates voice B

        const float mixed   = (outA + outB) * 0.5f;
        leftCh[s]  = mixed;
        rightCh[s] = mixed;
    }
}

//==============================================================================
// SINGLE-VOICE SAMPLE PROCESSOR
// Contains the full per-sample voice chain for voice[vi].
//==============================================================================
float VoltageSeq2AudioProcessor::processSingleVoiceSample (
    int vi, bool running,
    bool useHostSync, double samplePPQ,
    double effectiveBPM,
    const double* swingBounds,
    const double* swingPPQBounds,
    double totalSwingCycle,
    double totalSwingPPQ,
    const int* stepOrder,
    float glideCoeff,
    float crossModIn)
{
    VoiceState& vs = vstate[vi];
    VoiceParams& vp = voice[vi];

    //--------------------------------------------------------------------------
    // SEQUENCER CLOCK
    //--------------------------------------------------------------------------
    if (running)
    {
        int pos;
        if (useHostSync)
        {
            double wrappedPPQ = std::fmod (samplePPQ, totalSwingPPQ);
            if (wrappedPPQ < 0.0) wrappedPPQ += totalSwingPPQ;
            pos = vp.sequenceLength - 1;
            for (int i = 0; i < vp.sequenceLength; ++i)
                if (wrappedPPQ < swingPPQBounds[i + 1]) { pos = i; break; }
        }
        else
        {
            double wrappedCtr = std::fmod (vs.sampleCounter, totalSwingCycle);
            pos = vp.sequenceLength - 1;
            for (int i = 0; i < vp.sequenceLength; ++i)
                if (wrappedCtr < swingBounds[i + 1]) { pos = i; break; }
        }

        if (pos != vs.lastPos)
        {
            vs.lastPos = pos;

            int newStep;
            if (vp.playOrder == VoiceParams::Random)
            {
                newStep = juce::Random::getSystemRandom().nextInt (vp.sequenceLength);
                vs.randomStep = newStep;
            }
            else
            {
                newStep = stepOrder[pos];
            }
            vp.currentStep = newStep;

            if (vp.stepGates[vp.currentStep])
            {
                float baseFreq = voltageToQuantizedFreq (vp, vp.stepVoltages[vp.currentStep]);
                vs.targetFreq1 = baseFreq * (float)std::pow (2.0, (double)vp.osc1Octave);
                vs.targetFreq2 = baseFreq * (float)std::pow (2.0, (double)vp.osc2Octave);

                const bool doGlide = (vp.portamentoTime > 0.001f && vp.stepGlides[vp.currentStep]);
                vs.glideActive = doGlide;

                if (!doGlide)
                {
                    vs.currentFreq1  = vs.targetFreq1;
                    vs.currentFreq2  = vs.targetFreq2;
                    vs.osc1PhaseInc  = vs.currentFreq1 / currentSampleRate;
                    vs.osc2PhaseInc  = vs.currentFreq2 / currentSampleRate;
                    vs.adsr.noteOff();      vs.adsr.noteOn();
                    vs.filterEnv.noteOff(); vs.filterEnv.noteOn();
                    // Mod envelope: reset to zero then retrigger so attack
                    // always starts from silence — even on consecutive gates.
                    if (!vp.modEnv.clockSync)
                    {
                        vs.modEnvAdsr.reset();
                        vs.modEnvAdsr.noteOn();
                    }
                }
            }
            else
            {
                vs.glideActive = false;
                vs.adsr.noteOff();
                vs.filterEnv.noteOff();
                if (!vp.modEnv.clockSync)
                    vs.modEnvAdsr.noteOff();
            }
        }

        vs.sampleCounter += 1.0;
        if (vs.sampleCounter >= totalSwingCycle)
            vs.sampleCounter -= totalSwingCycle;
    }

    //--------------------------------------------------------------------------
    // PORTAMENTO (glide)
    //--------------------------------------------------------------------------
    if (vs.glideActive)
    {
        vs.currentFreq1  = vs.currentFreq1 * glideCoeff + vs.targetFreq1 * (1.0f - glideCoeff);
        vs.currentFreq2  = vs.currentFreq2 * glideCoeff + vs.targetFreq2 * (1.0f - glideCoeff);
        vs.osc1PhaseInc  = vs.currentFreq1 / currentSampleRate;
        vs.osc2PhaseInc  = vs.currentFreq2 / currentSampleRate;
    }

    //--------------------------------------------------------------------------
    // LFOs
    //--------------------------------------------------------------------------
    // LFO waveform helper
    auto lfoWave = [](float phase, int wave) -> float
    {
        switch (wave)
        {
            case 1: return (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f); // tri
            case 2: return phase * 2.0f - 1.0f;                                               // saw
            case 3: return (phase < 0.5f) ? 1.0f : -1.0f;                                    // sqr
            default: return std::sin (phase * juce::MathConstants<float>::twoPi);             // sine
        }
    };

    // LFO sync rate helper (returns Hz from BPM + division index)
    auto syncRate = [&](int divIdx) -> float
    {
        return (float)(effectiveBPM / 60.0 / (cenvDivBars[juce::jlimit (0, 7, divIdx)] * 4.0));
    };

    // Advance LFO phases (synced or free)
    auto advanceLFO = [&](float& phase, float freeRate, bool synced, int divIdx)
    {
        float rate = synced ? syncRate (divIdx) : freeRate;
        phase += rate / (float)currentSampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
    };

    advanceLFO (vs.lfoPhase,  vp.lfoRate,  vp.lfoSync,  vp.lfoSyncDiv);
    advanceLFO (vs.lfo2Phase, vp.lfo2Rate, vp.lfo2Sync, vp.lfo2SyncDiv);
    advanceLFO (vs.lfo3Phase, vp.lfo3Rate, vp.lfo3Sync, vp.lfo3SyncDiv);
    advanceLFO (vs.lfo4Phase, vp.lfo4Rate, vp.lfo4Sync, vp.lfo4SyncDiv);

    float lfo1Val = lfoWave (vs.lfoPhase,  vp.lfoWaveform)  * vp.lfoDepth;
    float lfo2Val = lfoWave (vs.lfo2Phase, vp.lfo2Waveform) * vp.lfo2Depth;
    float lfo3Val = lfoWave (vs.lfo3Phase, vp.lfo3Waveform) * vp.lfo3Depth;
    float lfo4Val = lfoWave (vs.lfo4Phase, vp.lfo4Waveform) * vp.lfo4Depth;

    float pwmMod      = 0.0f;
    float cutoffMod   = 0.0f;
    float pitchMod    = 1.0f;
    float lfoRangeMod = 0.0f;
    float lfoFMMod    = 0.0f;

    auto accLFO = [&](float val, int target)
    {
        if (target == 0) pwmMod      += val * 0.4f;
        if (target == 1) cutoffMod   += val * 4000.0f;
        if (target == 2) pitchMod    *= std::pow (2.0f, val / 12.0f);
        if (target == 3) lfoRangeMod += val;
        if (target == 4) lfoFMMod    += val;
    };
    accLFO (lfo1Val, vp.lfoTarget);
    accLFO (lfo2Val, vp.lfo2Target);
    accLFO (lfo3Val, vp.lfo3Target);
    accLFO (lfo4Val, vp.lfo4Target);

    vs.pulseWidth = juce::jlimit (0.05f, 0.95f, vp.osc1PulseWidth + pwmMod);

    //--------------------------------------------------------------------------
    // MOD ENVELOPE
    //--------------------------------------------------------------------------
    const bool gateOn   = running && vp.stepGates[vp.currentStep];
    const float modEnvOut = processModEnv (vp.modEnv, vs, gateOn, effectiveBPM)
                            * vp.modEnv.depth;

    float cenvAmpMod    = 1.0f;
    float cenvCutoffMod = 1.0f;
    float cenvRangeMod  = 0.0f;
    float cenvFMDepthMod = 0.0f;

    if (vp.modEnv.dest == 0) cenvFMDepthMod = modEnvOut;
    if (vp.modEnv.dest == 1) cenvRangeMod   = modEnvOut;
    if (vp.modEnv.dest == 2) cenvCutoffMod  = std::pow (2.0f, modEnvOut * 4.0f);

    const float effectiveFMDepth = juce::jlimit (0.0f, 1.0f, vp.fmDepth + cenvFMDepthMod + lfoFMMod);

    const float totalRangeMod = cenvRangeMod + lfoRangeMod;
    if (std::abs (totalRangeMod) > 0.001f && running)
    {
        const float effRange = juce::jlimit (0.0f, 2.0f, vp.rangeVCA + totalRangeMod);
        const float baseFreq = voltageToQuantizedFreq (vp, vp.stepVoltages[vp.currentStep], effRange);
        const float f1 = baseFreq * (float)std::pow (2.0, (double)vp.osc1Octave);
        const float f2 = baseFreq * (float)std::pow (2.0, (double)vp.osc2Octave);

        if (vs.glideActive)
        {
            vs.targetFreq1 = f1;
            vs.targetFreq2 = f2;
        }
        else
        {
            vs.osc1PhaseInc = (double)f1 / currentSampleRate;
            vs.osc2PhaseInc = (double)f2 / currentSampleRate;
        }
    }

    //--------------------------------------------------------------------------
    // FILTER ENVELOPE → effective cutoff
    //--------------------------------------------------------------------------
    float fEnvSample   = vs.filterEnv.getNextSample();
    float effectiveCut = vp.filterCutoff
                         * std::pow (2.0f, vp.filterEnvAmount * 4.0f * fEnvSample);
    effectiveCut = juce::jlimit (20.0f, 20000.0f,
                                 (effectiveCut + cutoffMod) * cenvCutoffMod);

    //--------------------------------------------------------------------------
    // OSCILLATORS → scope → filter → amp envelope
    //--------------------------------------------------------------------------
    //--------------------------------------------------------------------------
    // OSCILLATOR DRIFT — independent slow random-walk per VCO
    //--------------------------------------------------------------------------
    const float maxDriftCents = 4.0f;
    vs.osc1DriftVal = std::sin (vs.osc1DriftPhase * juce::MathConstants<float>::twoPi)
                      * vp.driftAmount * maxDriftCents;
    vs.osc2DriftVal = std::sin (vs.osc2DriftPhase * juce::MathConstants<float>::twoPi)
                      * vp.driftAmount * maxDriftCents;
    vs.osc1DriftPhase += vs.osc1DriftRate / (float)currentSampleRate;
    if (vs.osc1DriftPhase >= 1.0f) vs.osc1DriftPhase -= 1.0f;
    vs.osc2DriftPhase += vs.osc2DriftRate / (float)currentSampleRate;
    if (vs.osc2DriftPhase >= 1.0f) vs.osc2DriftPhase -= 1.0f;
    const float drift1Ratio = std::pow (2.0f, vs.osc1DriftVal / 1200.0f);
    const float drift2Ratio = std::pow (2.0f, vs.osc2DriftVal / 1200.0f);

    // FM: OSC2 runs at harmonic ratio of OSC1 when depth > 0
    const float fmRatioVal = std::max (0.0f, vp.fmRatio);
    const double osc2Inc = (effectiveFMDepth > 0.001f && fmRatioVal > 0.0f)
        ? (vs.currentFreq1 * (double)fmRatioVal * pitchMod * (double)drift2Ratio) / currentSampleRate
        : vs.osc2PhaseInc * pitchMod * (double)drift2Ratio;
    const float osc2Raw = generateOsc2Sample (vs, vp, osc2Inc);   // [-1..+1]

    // OSC1 with FM deviation from OSC2 and self-feedback
    const double fmDev = vs.osc1PhaseInc * (double)(effectiveFMDepth * osc2Raw * 3.0f
                         + vp.crossModDepth * crossModIn * 3.0f);
    const double fbDev = vs.osc1PhaseInc * (double)(vp.osc1Feedback  * vs.osc1FeedbackSample * 2.0f);
    const float osc1Raw = generateOsc1Sample (vs, vp, vs.osc1PhaseInc * pitchMod * (double)drift1Ratio + fmDev + fbDev);
    vs.osc1FeedbackSample = juce::jlimit (-1.0f, 1.0f, osc1Raw);  // clamp to prevent blow-up
    crossModSample[vi] = osc1Raw;   // expose raw output for cross-mod next sample

    float osc1 = osc1Raw * vp.osc1Level;
    float osc2 = osc2Raw * vp.osc2Level;

    // Scope ring-buffer (pre-filter, always shows raw waveform)
    oscScopeBuffer[vi][scopeWritePos[vi]] = osc1 + osc2;
    scopeWritePos[vi] = (scopeWritePos[vi] + 1) % scopeSize;

    float filtered = applyFilter (vs, vp, osc1 + osc2, effectiveCut);
    float envelope = vs.adsr.getNextSample();
    return filtered * envelope * cenvAmpMod * 0.3f;
}

//==============================================================================
// OSC 1 — phase accumulator with waveform selector
//==============================================================================
float VoltageSeq2AudioProcessor::generateOsc1Sample (VoiceState& vs, const VoiceParams& vp,
                                                      double phaseInc)
{
    float output = 0.0f;
    switch (vp.osc1Waveform)
    {
        case VoiceParams::Sine:
            output = (float)std::sin (vs.osc1Phase * juce::MathConstants<double>::twoPi);
            break;
        case VoiceParams::Saw:
            output = (float)(vs.osc1Phase * 2.0 - 1.0);
            break;
        case VoiceParams::Square:
            output = (vs.osc1Phase < vs.pulseWidth) ? 1.0f : -1.0f;
            break;
        case VoiceParams::Triangle:
            output = (vs.osc1Phase < 0.5) ? (float)(vs.osc1Phase * 4.0 - 1.0)
                                           : (float)(3.0 - vs.osc1Phase * 4.0);
            break;
        default:
            break;
    }
    vs.osc1Phase += phaseInc;
    if (vs.osc1Phase >= 1.0) vs.osc1Phase -= 1.0;
    return output;
}

//==============================================================================
// OSC 2 — interpolated wavetable morphing
//==============================================================================
float VoltageSeq2AudioProcessor::generateOsc2Sample (VoiceState& vs, const VoiceParams& vp,
                                                      double phaseInc)
{
    float tablePos = vp.osc2Position * (numWavetables - 1);
    int   tableA   = (int)tablePos;
    int   tableB   = juce::jmin (tableA + 1, numWavetables - 1);
    float blend    = tablePos - tableA;

    float readPos = (float)(vs.osc2Phase * wavetableSize);
    int   indexA  = (int)readPos % wavetableSize;
    int   indexB  = (indexA + 1) % wavetableSize;
    float frac    = readPos - (int)readPos;

    float sA = wavetables[tableA][indexA] + frac * (wavetables[tableA][indexB] - wavetables[tableA][indexA]);
    float sB = wavetables[tableB][indexA] + frac * (wavetables[tableB][indexB] - wavetables[tableB][indexA]);

    vs.osc2Phase += phaseInc;
    if (vs.osc2Phase >= 1.0) vs.osc2Phase -= 1.0;
    return sA + blend * (sB - sA);
}

//==============================================================================
// TPT State Variable Filter with drive, mode, and slope
//==============================================================================
float VoltageSeq2AudioProcessor::applyFilter (VoiceState& vs, const VoiceParams& vp,
                                               float input, float effectiveCutoff)
{
    // Pre-filter drive (tanh saturation)
    const float drive = 1.0f + vp.filterDrive * 7.0f;   // 1x … 8x gain into clipper
    float driven = std::tanh (input * drive) / drive;    // normalised so unity gain at drive=0

    // TPT SVF — first stage
    auto runSVF = [](float in, float& ic1, float& ic2, float g, float k, int mode) -> float
    {
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;
        float v3 = in   - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1      = 2.0f * v1 - ic1;
        ic2      = 2.0f * v2 - ic2;
        if (mode == 1) return v1;                        // BP
        if (mode == 2) return in - k * v1 - v2;         // HP
        return v2;                                       // LP (default)
    };

    float cut = juce::jlimit (20.0f, (float)(currentSampleRate * 0.45), effectiveCutoff);
    float g = std::tan (juce::MathConstants<float>::pi * cut / (float)currentSampleRate);
    float k = juce::jlimit (0.01f, 2.0f, 2.0f - 1.99f * vp.filterResonance);

    float out = runSVF (driven, vs.ic1eq, vs.ic2eq, g, k, vp.filterMode);

    // Second stage for 24 dB/oct
    if (vp.filterSlope == 1)
        out = runSVF (out, vs.ic1eq2, vs.ic2eq2, g, k, vp.filterMode);

    return out;
}

//==============================================================================
// Mod Envelope processor
//==============================================================================
float VoltageSeq2AudioProcessor::processModEnv (const ModEnvParams& p, VoiceState& vs,
                                                  bool gateOn, double bpm)
{
    if (p.clockSync)
    {
        const double cycleSeconds = cenvDivBars[juce::jlimit (0, 7, p.clockDiv)] * 4.0 * 60.0 / bpm;
        const double cycleSamples = cycleSeconds * currentSampleRate;
        vs.modEnvClockPos += 1.0;
        if (vs.modEnvClockPos >= cycleSamples)
        {
            vs.modEnvClockPos -= cycleSamples;
            vs.modEnvAdsr.noteOff();
            vs.modEnvAdsr.noteOn();
        }
    }
    else
    {
        // Retrigger (reset + noteOn) is handled per-step in the sequencer block
        // so consecutive gated steps always produce a fresh attack from zero.
        // Here we only need to handle the gate falling edge → release.
        const bool fall = !gateOn && vs.modEnvPrevGate;
        if (fall) vs.modEnvAdsr.noteOff();
    }
    vs.modEnvPrevGate = gateOn;
    return vs.modEnvAdsr.getNextSample();
}

//==============================================================================
// Voltage → quantised frequency
//==============================================================================
float VoltageSeq2AudioProcessor::voltageToQuantizedFreq (const VoiceParams& vp, float voltage,
                                                          float rangeOverride)
{
    float range       = (rangeOverride >= 0.0f) ? rangeOverride : vp.rangeVCA;
    float scaledV     = voltage * range;
    // Anchor zero-voltage to the root note so the sequencer is always
    // tonally grounded in the selected key (rootNote: 0=C … 11=B).
    float rawMidi     = juce::jlimit (0.0f, 127.0f, 60.0f + (float)vp.rootNote + scaledV * 5.0f);
    int   quantized   = quantizeNoteToScale ((int)std::round (rawMidi), vp.rootNote, vp.currentScale);
    return 440.0f * std::pow (2.0f, (quantized - 69.0f) / 12.0f);
}

//==============================================================================
// Note quantiser
//==============================================================================
int VoltageSeq2AudioProcessor::quantizeNoteToScale (int midiNote, int rootNote, int scale)
{
    int bestNote   = midiNote;
    int bestDist   = 127;
    int octaveBase = (midiNote / 12) * 12;

    for (int octOffset = -12; octOffset <= 12; octOffset += 12)
    {
        int base = octaveBase + octOffset;
        for (int i = 0; i < scaleSizes[scale]; ++i)
        {
            int interval = scaleIntervals[scale][i];
            if (interval < 0) break;
            int note = base + rootNote + interval;
            int dist = std::abs (note - midiNote);
            if (dist < bestDist) { bestDist = dist; bestNote = note; }
        }
    }
    return juce::jlimit (0, 127, bestNote);
}

//==============================================================================
// STANDARD JUCE BOILERPLATE
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

//==============================================================================
// STATE SERIALISATION — v2 format
//==============================================================================
static void saveVoiceToXml (juce::XmlElement& el,
                             const VoltageSeq2AudioProcessor::VoiceParams& vp,
                             int numSteps)
{
    for (int i = 0; i < numSteps; ++i)
    {
        el.setAttribute ("v"  + juce::String (i), (double)vp.stepVoltages[i]);
        el.setAttribute ("g"  + juce::String (i), vp.stepGates [i]);
        el.setAttribute ("sl" + juce::String (i), vp.stepGlides[i]);
    }
    el.setAttribute ("porta",    (double)vp.portamentoTime);
    el.setAttribute ("swing",    (double)vp.swingAmount);
    el.setAttribute ("clkDiv",   vp.clockDivision);
    el.setAttribute ("seqLen",   vp.sequenceLength);
    el.setAttribute ("unipolar", vp.unipolar);
    el.setAttribute ("playOrder",vp.playOrder);
    el.setAttribute ("range",    (double)vp.rangeVCA);
    el.setAttribute ("root",     vp.rootNote);
    el.setAttribute ("scale",    vp.currentScale);

    el.setAttribute ("osc1Wave", vp.osc1Waveform);
    el.setAttribute ("osc1Lvl",  (double)vp.osc1Level);
    el.setAttribute ("osc1Oct",  vp.osc1Octave);
    el.setAttribute ("osc1PW",   (double)vp.osc1PulseWidth);
    el.setAttribute ("osc1Feedbk", (double)vp.osc1Feedback);
    el.setAttribute ("drift",      (double)vp.driftAmount);
    el.setAttribute ("fmDepth",    (double)vp.fmDepth);
    el.setAttribute ("fmRatio",    (double)vp.fmRatio);
    el.setAttribute ("crossMod",   (double)vp.crossModDepth);

    el.setAttribute ("osc2Pos",  (double)vp.osc2Position);
    el.setAttribute ("osc2Lvl",  (double)vp.osc2Level);
    el.setAttribute ("osc2Oct",  vp.osc2Octave);

    el.setAttribute ("cut",     (double)vp.filterCutoff);
    el.setAttribute ("res",     (double)vp.filterResonance);
    el.setAttribute ("fEnvAmt", (double)vp.filterEnvAmount);
    el.setAttribute ("fDrive",   (double)vp.filterDrive);
    el.setAttribute ("fMode",    vp.filterMode);
    el.setAttribute ("fSlope",   vp.filterSlope);
    el.setAttribute ("fAtk",    (double)vp.filterEnvParams.attack);
    el.setAttribute ("fDec",    (double)vp.filterEnvParams.decay);
    el.setAttribute ("fSus",    (double)vp.filterEnvParams.sustain);
    el.setAttribute ("fRel",    (double)vp.filterEnvParams.release);

    el.setAttribute ("aAtk",    (double)vp.adsrParams.attack);
    el.setAttribute ("aDec",    (double)vp.adsrParams.decay);
    el.setAttribute ("aSus",    (double)vp.adsrParams.sustain);
    el.setAttribute ("aRel",    (double)vp.adsrParams.release);

    el.setAttribute ("lfo1Rate", (double)vp.lfoRate);
    el.setAttribute ("lfo1Dep",  (double)vp.lfoDepth);
    el.setAttribute ("lfo1Tgt",  vp.lfoTarget);

    el.setAttribute ("lfo2Rate", (double)vp.lfo2Rate);
    el.setAttribute ("lfo2Dep",  (double)vp.lfo2Depth);
    el.setAttribute ("lfo2Tgt",  vp.lfo2Target);

    el.setAttribute ("lfo1Wave",  vp.lfoWaveform);
    el.setAttribute ("lfo1Sync",  vp.lfoSync);
    el.setAttribute ("lfo1SDiv",  vp.lfoSyncDiv);
    el.setAttribute ("lfo2Wave",  vp.lfo2Waveform);
    el.setAttribute ("lfo2Sync",  vp.lfo2Sync);
    el.setAttribute ("lfo2SDiv",  vp.lfo2SyncDiv);
    el.setAttribute ("lfo3Rate",  (double)vp.lfo3Rate);
    el.setAttribute ("lfo3Dep",   (double)vp.lfo3Depth);
    el.setAttribute ("lfo3Tgt",   vp.lfo3Target);
    el.setAttribute ("lfo3Wave",  vp.lfo3Waveform);
    el.setAttribute ("lfo3Sync",  vp.lfo3Sync);
    el.setAttribute ("lfo3SDiv",  vp.lfo3SyncDiv);
    el.setAttribute ("lfo4Rate",  (double)vp.lfo4Rate);
    el.setAttribute ("lfo4Dep",   (double)vp.lfo4Depth);
    el.setAttribute ("lfo4Tgt",   vp.lfo4Target);
    el.setAttribute ("lfo4Wave",  vp.lfo4Waveform);
    el.setAttribute ("lfo4Sync",  vp.lfo4Sync);
    el.setAttribute ("lfo4SDiv",  vp.lfo4SyncDiv);

    el.setAttribute ("mEnvAtk",  (double)vp.modEnv.attack);
    el.setAttribute ("mEnvDec",  (double)vp.modEnv.decay);
    el.setAttribute ("mEnvSus",  (double)vp.modEnv.sustain);
    el.setAttribute ("mEnvRel",  (double)vp.modEnv.release);
    el.setAttribute ("mEnvDep",  (double)vp.modEnv.depth);
    el.setAttribute ("mEnvDst",  vp.modEnv.dest);
    el.setAttribute ("mEnvSync", vp.modEnv.clockSync);
    el.setAttribute ("mEnvDiv",  vp.modEnv.clockDiv);
}

static void loadVoiceFromXml (const juce::XmlElement& el,
                               VoltageSeq2AudioProcessor::VoiceParams& vp,
                               int numSteps)
{
    auto getF = [&](const char* key, float def)
        { return (float)el.getDoubleAttribute (key, (double)def); };
    auto getI = [&](const char* key, int def)
        { return el.getIntAttribute (key, def); };
    auto getB = [&](const char* key, bool def)
        { return el.getBoolAttribute (key, def); };

    for (int i = 0; i < numSteps; ++i)
    {
        vp.stepVoltages[i] = getF (("v"  + juce::String (i)).toRawUTF8(), vp.stepVoltages[i]);
        vp.stepGates   [i] = getB (("g"  + juce::String (i)).toRawUTF8(), vp.stepGates   [i]);
        vp.stepGlides  [i] = getB (("sl" + juce::String (i)).toRawUTF8(), vp.stepGlides  [i]);
    }
    vp.portamentoTime   = getF ("porta",    vp.portamentoTime);
    vp.swingAmount      = getF ("swing",    vp.swingAmount);
    vp.clockDivision    = getI ("clkDiv",   vp.clockDivision);
    vp.sequenceLength   = getI ("seqLen",   vp.sequenceLength);
    vp.unipolar         = getB ("unipolar", vp.unipolar);
    vp.playOrder        = getI ("playOrder",vp.playOrder);
    vp.rangeVCA         = getF ("range",    vp.rangeVCA);
    vp.rootNote         = getI ("root",     vp.rootNote);
    vp.currentScale     = getI ("scale",    vp.currentScale);

    vp.osc1Waveform     = getI ("osc1Wave", vp.osc1Waveform);
    vp.osc1Level        = getF ("osc1Lvl",  vp.osc1Level);
    vp.osc1Octave       = getI ("osc1Oct",  vp.osc1Octave);
    vp.osc1PulseWidth   = getF ("osc1PW",   vp.osc1PulseWidth);
    vp.osc1Feedback     = getF ("osc1Feedbk", vp.osc1Feedback);
    vp.driftAmount      = getF ("drift",      vp.driftAmount);
    vp.fmDepth          = getF ("fmDepth",    vp.fmDepth);
    vp.fmRatio          = getF ("fmRatio",    vp.fmRatio);
    vp.crossModDepth    = getF ("crossMod",   vp.crossModDepth);

    vp.osc2Position     = getF ("osc2Pos",  vp.osc2Position);
    vp.osc2Level        = getF ("osc2Lvl",  vp.osc2Level);
    vp.osc2Octave       = getI ("osc2Oct",  vp.osc2Octave);

    vp.filterCutoff     = getF ("cut",      vp.filterCutoff);
    vp.filterResonance  = getF ("res",      vp.filterResonance);
    vp.filterEnvAmount  = getF ("fEnvAmt",  vp.filterEnvAmount);
    vp.filterDrive      = getF ("fDrive",   vp.filterDrive);
    vp.filterMode       = getI ("fMode",    vp.filterMode);
    vp.filterSlope      = getI ("fSlope",   vp.filterSlope);
    vp.filterEnvParams.attack  = getF ("fAtk", vp.filterEnvParams.attack);
    vp.filterEnvParams.decay   = getF ("fDec", vp.filterEnvParams.decay);
    vp.filterEnvParams.sustain = getF ("fSus", vp.filterEnvParams.sustain);
    vp.filterEnvParams.release = getF ("fRel", vp.filterEnvParams.release);

    vp.adsrParams.attack  = getF ("aAtk", vp.adsrParams.attack);
    vp.adsrParams.decay   = getF ("aDec", vp.adsrParams.decay);
    vp.adsrParams.sustain = getF ("aSus", vp.adsrParams.sustain);
    vp.adsrParams.release = getF ("aRel", vp.adsrParams.release);

    vp.lfoRate    = getF ("lfo1Rate", vp.lfoRate);
    vp.lfoDepth   = getF ("lfo1Dep",  vp.lfoDepth);
    vp.lfoTarget  = getI ("lfo1Tgt",  vp.lfoTarget);

    vp.lfo2Rate   = getF ("lfo2Rate", vp.lfo2Rate);
    vp.lfo2Depth  = getF ("lfo2Dep",  vp.lfo2Depth);
    vp.lfo2Target = getI ("lfo2Tgt",  vp.lfo2Target);

    vp.lfoWaveform   = getI ("lfo1Wave",  vp.lfoWaveform);
    vp.lfoSync       = getB ("lfo1Sync",  vp.lfoSync);
    vp.lfoSyncDiv    = getI ("lfo1SDiv",  vp.lfoSyncDiv);
    vp.lfo2Waveform  = getI ("lfo2Wave",  vp.lfo2Waveform);
    vp.lfo2Sync      = getB ("lfo2Sync",  vp.lfo2Sync);
    vp.lfo2SyncDiv   = getI ("lfo2SDiv",  vp.lfo2SyncDiv);
    vp.lfo3Rate      = getF ("lfo3Rate",  vp.lfo3Rate);
    vp.lfo3Depth     = getF ("lfo3Dep",   vp.lfo3Depth);
    vp.lfo3Target    = getI ("lfo3Tgt",   vp.lfo3Target);
    vp.lfo3Waveform  = getI ("lfo3Wave",  vp.lfo3Waveform);
    vp.lfo3Sync      = getB ("lfo3Sync",  vp.lfo3Sync);
    vp.lfo3SyncDiv   = getI ("lfo3SDiv",  vp.lfo3SyncDiv);
    vp.lfo4Rate      = getF ("lfo4Rate",  vp.lfo4Rate);
    vp.lfo4Depth     = getF ("lfo4Dep",   vp.lfo4Depth);
    vp.lfo4Target    = getI ("lfo4Tgt",   vp.lfo4Target);
    vp.lfo4Waveform  = getI ("lfo4Wave",  vp.lfo4Waveform);
    vp.lfo4Sync      = getB ("lfo4Sync",  vp.lfo4Sync);
    vp.lfo4SyncDiv   = getI ("lfo4SDiv",  vp.lfo4SyncDiv);

    vp.modEnv.attack    = getF ("mEnvAtk",  vp.modEnv.attack);
    vp.modEnv.decay     = getF ("mEnvDec",  vp.modEnv.decay);
    vp.modEnv.sustain   = getF ("mEnvSus",  vp.modEnv.sustain);
    vp.modEnv.release   = getF ("mEnvRel",  vp.modEnv.release);
    vp.modEnv.depth     = getF ("mEnvDep",  vp.modEnv.depth);
    vp.modEnv.dest      = getI ("mEnvDst",  vp.modEnv.dest);
    vp.modEnv.clockSync = getB ("mEnvSync", vp.modEnv.clockSync);
    vp.modEnv.clockDiv  = getI ("mEnvDiv",  vp.modEnv.clockDiv);
}

//==============================================================================
// PATTERN BANK
//==============================================================================
void VoltageSeq2AudioProcessor::savePattern (int vi, int slot)
{
    if (vi < 0 || vi >= numVoices || slot < 0 || slot >= numPatternSlots) return;
    auto& p       = patternBank[vi][slot];
    const auto& v = voice[vi];
    p.used = true;
    for (int i = 0; i < numSteps; ++i) {
        p.stepVoltages[i] = v.stepVoltages[i];
        p.stepGates   [i] = v.stepGates   [i];
        p.stepGlides  [i] = v.stepGlides  [i];
    }
    p.sequenceLength = v.sequenceLength;
    p.clockDivision  = v.clockDivision;
    p.swingAmount    = v.swingAmount;
    p.portamentoTime = v.portamentoTime;
    p.playOrder      = v.playOrder;
    p.unipolar       = v.unipolar;
    p.rootNote       = v.rootNote;
    p.currentScale   = v.currentScale;
    p.rangeVCA       = v.rangeVCA;
}

void VoltageSeq2AudioProcessor::loadPattern (int vi, int slot)
{
    if (vi < 0 || vi >= numVoices || slot < 0 || slot >= numPatternSlots) return;
    const auto& p = patternBank[vi][slot];
    if (!p.used) return;
    auto& v = voice[vi];
    for (int i = 0; i < numSteps; ++i) {
        v.stepVoltages[i] = p.stepVoltages[i];
        v.stepGates   [i] = p.stepGates   [i];
        v.stepGlides  [i] = p.stepGlides  [i];
    }
    v.sequenceLength = p.sequenceLength;
    v.clockDivision  = p.clockDivision;
    v.swingAmount    = p.swingAmount;
    v.portamentoTime = p.portamentoTime;
    v.playOrder      = p.playOrder;
    v.unipolar       = p.unipolar;
    v.rootNote       = p.rootNote;
    v.currentScale   = p.currentScale;
    v.rangeVCA       = p.rangeVCA;
    v.resetOnNextBlock.store (true);
}

void VoltageSeq2AudioProcessor::clearPattern (int vi, int slot)
{
    if (vi < 0 || vi >= numVoices || slot < 0 || slot >= numPatternSlots) return;
    patternBank[vi][slot] = PatternSlot{};
}

//------------------------------------------------------------------------------
void VoltageSeq2AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("VoltageSeq2State");
    xml.setAttribute ("version", 3);
    xml.setAttribute ("bpm", internalBPM);

    for (int vi = 0; vi < numVoices; ++vi)
    {
        auto* voiceEl = xml.createNewChildElement ("Voice");
        voiceEl->setAttribute ("index", vi);
        saveVoiceToXml (*voiceEl, voice[vi], numSteps);
    }

    // Pattern bank
    for (int vi = 0; vi < numVoices; ++vi)
    {
        auto* bankEl = xml.createNewChildElement ("PatternBank");
        bankEl->setAttribute ("voice", vi);
        for (int s = 0; s < numPatternSlots; ++s)
        {
            const auto& p = patternBank[vi][s];
            if (!p.used) continue;
            auto* slotEl = bankEl->createNewChildElement ("Slot");
            slotEl->setAttribute ("index",  s);
            slotEl->setAttribute ("root",   p.rootNote);
            slotEl->setAttribute ("scale",  p.currentScale);
            slotEl->setAttribute ("length", p.sequenceLength);
            slotEl->setAttribute ("clock",  p.clockDivision);
            slotEl->setAttribute ("swing",  p.swingAmount);
            slotEl->setAttribute ("porta",  p.portamentoTime);
            slotEl->setAttribute ("order",  p.playOrder);
            slotEl->setAttribute ("uni",    (int)p.unipolar);
            slotEl->setAttribute ("range",  p.rangeVCA);
            // Step data as comma-separated strings
            juce::String volts, gates, glides;
            for (int i = 0; i < numSteps; ++i)
            {
                volts  += juce::String (p.stepVoltages[i], 4) + (i < 15 ? "," : "");
                gates  += juce::String ((int)p.stepGates[i])  + (i < 15 ? "," : "");
                glides += juce::String ((int)p.stepGlides[i]) + (i < 15 ? "," : "");
            }
            slotEl->setAttribute ("volts",  volts);
            slotEl->setAttribute ("gates",  gates);
            slotEl->setAttribute ("glides", glides);
        }
    }

    copyXmlToBinary (xml, destData);
}

void VoltageSeq2AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (!xml || xml->getTagName() != "VoltageSeq2State") return;

    internalBPM = xml->getDoubleAttribute ("bpm", internalBPM);

    const int version = xml->getIntAttribute ("version", 1);

    if (version >= 2)
    {
        for (auto* child : xml->getChildIterator())
        {
            if (child->getTagName() == "Voice")
            {
                int vi = child->getIntAttribute ("index", -1);
                if (vi >= 0 && vi < numVoices)
                    loadVoiceFromXml (*child, voice[vi], numSteps);
            }
            else if (child->getTagName() == "PatternBank" && version >= 3)
            {
                int vi = child->getIntAttribute ("voice", -1);
                if (vi < 0 || vi >= numVoices) continue;
                for (auto* slotEl : child->getChildIterator())
                {
                    if (slotEl->getTagName() != "Slot") continue;
                    int s = slotEl->getIntAttribute ("index", -1);
                    if (s < 0 || s >= numPatternSlots) continue;
                    auto& p          = patternBank[vi][s];
                    p.used           = true;
                    p.rootNote       = slotEl->getIntAttribute    ("root",   0);
                    p.currentScale   = slotEl->getIntAttribute    ("scale",  0);
                    p.sequenceLength = slotEl->getIntAttribute    ("length", 16);
                    p.clockDivision  = slotEl->getIntAttribute    ("clock",  2);
                    p.swingAmount    = (float)slotEl->getDoubleAttribute ("swing", 0.5);
                    p.portamentoTime = (float)slotEl->getDoubleAttribute ("porta", 0.0);
                    p.playOrder      = slotEl->getIntAttribute    ("order",  0);
                    p.unipolar       = slotEl->getIntAttribute    ("uni",    0) != 0;
                    p.rangeVCA       = (float)slotEl->getDoubleAttribute ("range", 1.0);
                    // Parse step arrays
                    auto parseFloats = [](const juce::String& s, float* arr, int n) {
                        juce::StringArray tok; tok.addTokens (s, ",", "");
                        for (int i = 0; i < juce::jmin (n, tok.size()); ++i)
                            arr[i] = tok[i].getFloatValue();
                    };
                    auto parseBools = [](const juce::String& s, bool* arr, int n) {
                        juce::StringArray tok; tok.addTokens (s, ",", "");
                        for (int i = 0; i < juce::jmin (n, tok.size()); ++i)
                            arr[i] = tok[i].getIntValue() != 0;
                    };
                    parseFloats (slotEl->getStringAttribute ("volts"),  p.stepVoltages, numSteps);
                    parseBools  (slotEl->getStringAttribute ("gates"),  p.stepGates,    numSteps);
                    parseBools  (slotEl->getStringAttribute ("glides"), p.stepGlides,   numSteps);
                }
            }
        }
    }
    else
    {
        // v1 backward compat: flat attributes on root → load into voice[0]
        loadVoiceFromXml (*xml, voice[0], numSteps);
    }

    // Apply to live ADSR objects immediately
    for (int vi = 0; vi < numVoices; ++vi)
    {
        vstate[vi].adsr.setParameters      (voice[vi].adsrParams);
        vstate[vi].filterEnv.setParameters  (voice[vi].filterEnvParams);
        juce::ADSR::Parameters mep3 { voice[vi].modEnv.attack, voice[vi].modEnv.decay,
                                      voice[vi].modEnv.sustain, voice[vi].modEnv.release };
        vstate[vi].modEnvAdsr.setParameters (mep3);
        voice[vi].resetOnNextBlock.store (true);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoltageSeq2AudioProcessor();
}
