#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// PolyBLEP residual — smooths the hard discontinuity in saw/square waves.
// t   = current normalised phase [0,1)
// dt  = phase increment per sample (freq / sampleRate)
// Returns a correction value to add/subtract at each discontinuity.
//==============================================================================
static inline float polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return (float)(t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return (float)(t * t + t + t + 1.0);
    }
    return 0.0f;
}

// Circular buffer helpers for FX
static inline float fxRead (const std::vector<float>& b, int wp, int off)
{
    int sz = (int)b.size();
    return b[(wp - off - 1 + sz * 4) % sz];
}
static inline float fxReadLerp (const std::vector<float>& b, int wp, float off)
{
    int sz  = (int)b.size();
    int o0  = (int)off;
    float f = off - o0;
    float s0 = b[(wp - o0 - 1 + sz * 4) % sz];
    float s1 = b[(wp - o0 - 2 + sz * 4) % sz];
    return s0 + f * (s1 - s0);
}
static inline void fxWrite (std::vector<float>& b, int& wp, float v)
{
    b[wp] = v;
    if (++wp >= (int)b.size()) wp = 0;
}
// Schroeder all-pass: delay=len samples, coeff=g
static inline float fxAP (std::vector<float>& b, int& wp, int len, float in, float g)
{
    int sz  = (int)b.size();
    int rp  = (wp - len + sz * 4) % sz;
    float d = b[rp];
    float w = in + g * d;
    b[wp]   = w;
    if (++wp >= sz) wp = 0;
    return d - g * w;
}

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

    // ── Default patch — blank slate, both voices identical ────────────────────
    for (int vi = 0; vi < numVoices; ++vi)
    {
        for (int i = 0; i < numSteps; ++i)
        {
            voice[vi].stepVoltages[i] = 0.0f;
            voice[vi].stepGates[i]    = true;   // all gates on → hear root note on play
            voice[vi].stepGlides[i]   = false;
        }
        voice[vi].unipolar      = true;   // unipolar (0..5 V range)
        voice[vi].currentScale  = 8;      // Chromatic = effectively unquantized
    }
}

VoltageSeq2AudioProcessor::~VoltageSeq2AudioProcessor() {}

//==============================================================================
void VoltageSeq2AudioProcessor::buildWavetables()
{
    // Table 0: Sine — already band-limited by nature
    for (int i = 0; i < wavetableSize; ++i)
    {
        const double phase = (double)i / wavetableSize;
        wavetables[0][i] = (float)std::sin (phase * juce::MathConstants<double>::twoPi);
    }

    // Tables 1-3: additive synthesis — sum harmonics up to Nyquist of the table.
    // Using wavetableSize/4 harmonics avoids Gibbs ringing at the discontinuities.
    const int   numH  = wavetableSize / 4;   // 512 harmonics
    const double twoPi = juce::MathConstants<double>::twoPi;

    double tri[wavetableSize] {}, saw[wavetableSize] {}, sq[wavetableSize] {};

    for (int i = 0; i < wavetableSize; ++i)
    {
        const double t = (double)i / wavetableSize;
        for (int h = 1; h <= numH; ++h)
        {
            const double s = std::sin (h * twoPi * t);
            // Saw: all harmonics, alternating sign → ramps up
            saw[i] += ((h & 1) ? 1.0 : -1.0) * s / h;
            if (h & 1)   // odd harmonics only for triangle + square
            {
                const int k = (h - 1) / 2;
                tri[i] += ((k & 1) ? -1.0 : 1.0) * s / ((double)h * h);
                sq[i]  += s / h;
            }
        }
    }

    // Normalise each table to [-1, +1]
    auto normalise = [&](double* buf, int tableIdx)
    {
        double peak = 0.0;
        for (int i = 0; i < wavetableSize; ++i)
            peak = std::max (peak, std::abs (buf[i]));
        if (peak > 0.0)
            for (int i = 0; i < wavetableSize; ++i)
                wavetables[tableIdx][i] = (float)(buf[i] / peak);
    };

    normalise (tri, 1);   // Table 1: Triangle
    normalise (saw, 2);   // Table 2: Saw
    normalise (sq,  3);   // Table 3: Square
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
        vs.sampleCounter     = 0.0;
        vs.lastPos           = -1;
        vp.currentStep       = 0;
        vs.randomStep        = 0;
        vs.ratchetSubStep    = 0;
        vs.ratchetSubStepDur = 0.0;
        vs.ratchetStepPos    = 0.0;
        vs.ratchetNoteOff    = false;
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

    prepareFx (sampleRate);
}

void VoltageSeq2AudioProcessor::releaseResources() {}

//==============================================================================
void VoltageSeq2AudioProcessor::prepareFx (double sr)
{
    // ── Delay ────────────────────────────────────────────────────────────────
    const int maxDly = (int)(sr * 2.0) + 4;
    fxs.dlyL.assign (maxDly, 0.f); fxs.dlyR.assign (maxDly, 0.f);
    fxs.dlyWL = fxs.dlyWR = 0;

    // ── Dattorro Plate Reverb ─────────────────────────────────────────────────
    // All delay lengths from Dattorro 1997 (reference rate 29761 Hz)
    const double sc = sr / 29761.0;
    auto sz = [&](int n) { return (int)(n * sc) + 4; };

    // Input diffusers: AP1=142, AP2=107, AP3=379, AP4=277
    const int iapLens[4] = { sz(142), sz(107), sz(379), sz(277) };
    for (int i = 0; i < 4; ++i) {
        fxs.iapLen[i] = iapLens[i];
        fxs.iap[i].assign (iapLens[i] + 4, 0.f);
        fxs.iapW[i] = 0;
    }
    // Tank main delays: D1=4453, D2=3720, D3=4217, D4=3163
    const int tdLens[4] = { sz(4453), sz(3720), sz(4217), sz(3163) };
    for (int i = 0; i < 4; ++i) {
        fxs.tdLen[i] = tdLens[i];
        fxs.td[i].assign (tdLens[i] + 4, 0.f);
        fxs.tdW[i] = 0;
    }
    // Tank all-pass delays: AP5=672(mod), AP6=1800, AP7=908(mod), AP8=2656
    const int tapLens[4] = { sz(672), sz(1800), sz(908), sz(2656) };
    for (int i = 0; i < 4; ++i) {
        fxs.tapLen[i] = tapLens[i];
        fxs.tap[i].assign (tapLens[i] + 64, 0.f); // +64 for modulation headroom
        fxs.tapW[i] = 0;
    }
    fxs.lpL = fxs.lpR = 0.f;
    fxs.modPh1 = 0.0; fxs.modPh2 = 0.5;

    // Pre-delay (max 100ms)
    const int preSz = (int)(sr * 0.1) + 4;
    fxs.preL.assign (preSz, 0.f); fxs.preR.assign (preSz, 0.f);
    fxs.preWL = fxs.preWR = 0;

    // ── Chorus ────────────────────────────────────────────────────────────────
    fxs.chorusBuf.assign (8192, 0.f);
    fxs.chorusW = 0;
    fxs.chorusPh[0] = 0.f; fxs.chorusPh[1] = 1.f/3.f; fxs.chorusPh[2] = 2.f/3.f;
}

//==============================================================================
void VoltageSeq2AudioProcessor::processFxBuffer (float* L, float* R, int numSamples)
{
    const FxParams& p   = fx;
    const double    sr  = currentSampleRate;
    const double    sc  = sr / 29761.0;   // reverb scale factor
    const double    twoPi = juce::MathConstants<double>::twoPi;

    for (int s = 0; s < numSamples; ++s)
    {
        float inL = L[s], inR = R[s];

        //----------------------------------------------------------------------
        // DELAY
        //----------------------------------------------------------------------
        if (p.delayOn)
        {
            // Compute delay in samples
            float delayMs;
            if (p.delaySync)
            {
                static constexpr double divTable[7] = {
                    1.0, 0.5, 0.25, 1.0/3.0, 1.0/6.0, 0.75, 0.375 };
                delayMs = (float)(divTable[p.delaySyncDiv] * 4.0 * 60000.0 / internalBPM);
            }
            else
                delayMs = p.delayTimeMs;

            const int dSamples = juce::jlimit (1, (int)fxs.dlyL.size() - 1,
                                               (int)(delayMs * 0.001f * (float)sr));

            // Read
            float dL = fxRead (fxs.dlyL, fxs.dlyWL, dSamples);
            float dR = fxRead (fxs.dlyR, fxs.dlyWR, dSamples);

            // Write with feedback (ping-pong crosses channels)
            if (p.delayPingPong) {
                fxWrite (fxs.dlyL, fxs.dlyWL, inL + dR * p.delayFeedback);
                fxWrite (fxs.dlyR, fxs.dlyWR, inR + dL * p.delayFeedback);
            } else {
                fxWrite (fxs.dlyL, fxs.dlyWL, inL + dL * p.delayFeedback);
                fxWrite (fxs.dlyR, fxs.dlyWR, inR + dR * p.delayFeedback);
            }

            inL = inL * (1.f - p.delayMix) + dL * p.delayMix;
            inR = inR * (1.f - p.delayMix) + dR * p.delayMix;
        }

        //----------------------------------------------------------------------
        // REVERB — Dattorro plate (stereo in/out)
        //----------------------------------------------------------------------
        if (p.reverbOn)
        {
            // Pre-delay (separate write pointers so L and R advance independently)
            const int preSamples = juce::jlimit (0, (int)fxs.preL.size() - 1,
                                                  (int)(p.reverbPreDelay * 0.001f * (float)sr));
            float preOutL = fxRead (fxs.preL, fxs.preWL, preSamples);
            float preOutR = fxRead (fxs.preR, fxs.preWR, preSamples);
            fxWrite (fxs.preL, fxs.preWL, inL);
            fxWrite (fxs.preR, fxs.preWR, inR);
            // Mono sum → input diffusers
            float z = (preOutL + preOutR) * 0.5f;
            z = fxAP (fxs.iap[0], fxs.iapW[0], fxs.iapLen[0], z, 0.75f);
            z = fxAP (fxs.iap[1], fxs.iapW[1], fxs.iapLen[1], z, 0.75f);
            z = fxAP (fxs.iap[2], fxs.iapW[2], fxs.iapLen[2], z, 0.625f);
            z = fxAP (fxs.iap[3], fxs.iapW[3], fxs.iapLen[3], z, 0.625f);

            const float decay = 0.1f + p.reverbSize * 0.87f;  // 0.1..0.97

            // Modulation (±16 samples at ref rate, ~1 Hz)
            const float modDepth = (float)(16.0 * sc);
            const float mod1 = modDepth * (float)std::sin (fxs.modPh1 * twoPi);
            const float mod2 = modDepth * (float)std::sin (fxs.modPh2 * twoPi);
            fxs.modPh1 = std::fmod (fxs.modPh1 + 1.0 / sr, 1.0);
            fxs.modPh2 = std::fmod (fxs.modPh2 + 1.1 / sr, 1.0);

            // Read tank finals (for cross-coupling)
            const float d2out = fxRead (fxs.td[1], fxs.tdW[1], fxs.tdLen[1]);
            const float d4out = fxRead (fxs.td[3], fxs.tdW[3], fxs.tdLen[3]);

            // ── Left tank path ─────────────────────────────────────────────
            float tankA = z + decay * d4out;
            // Modulated AP5
            {
                float mo = (float)fxs.tapLen[0] + mod1;
                int sz2  = (int)fxs.tap[0].size();
                float d = fxReadLerp (fxs.tap[0], fxs.tapW[0], mo);
                float w = tankA + 0.70f * d;
                fxs.tap[0][fxs.tapW[0]] = w;
                if (++fxs.tapW[0] >= sz2) fxs.tapW[0] = 0;
                tankA = d - 0.70f * w;
            }
            fxWrite (fxs.td[0], fxs.tdW[0], tankA);    // D1
            // LP damping
            fxs.lpL = fxs.lpL * p.reverbDamping + tankA * (1.f - p.reverbDamping);
            tankA = fxAP (fxs.tap[1], fxs.tapW[1], fxs.tapLen[1], fxs.lpL, 0.50f); // AP6
            fxWrite (fxs.td[1], fxs.tdW[1], tankA);    // D2

            // ── Right tank path ────────────────────────────────────────────
            float tankB = z + decay * d2out;
            // Modulated AP7
            {
                float mo = (float)fxs.tapLen[2] + mod2;
                int sz2  = (int)fxs.tap[2].size();
                float d = fxReadLerp (fxs.tap[2], fxs.tapW[2], mo);
                float w = tankB + 0.70f * d;
                fxs.tap[2][fxs.tapW[2]] = w;
                if (++fxs.tapW[2] >= sz2) fxs.tapW[2] = 0;
                tankB = d - 0.70f * w;
            }
            fxWrite (fxs.td[2], fxs.tdW[2], tankB);    // D3
            // LP damping
            fxs.lpR = fxs.lpR * p.reverbDamping + tankB * (1.f - p.reverbDamping);
            tankB = fxAP (fxs.tap[3], fxs.tapW[3], fxs.tapLen[3], fxs.lpR, 0.50f); // AP8
            fxWrite (fxs.td[3], fxs.tdW[3], tankB);    // D4

            // ── Output taps (Dattorro Table I, scaled) ─────────────────────
            // Left output reads from right tank:  td[2]=D3(4217) td[3]=D4(3163) tap[3]=AP8(2656)
            // Right output reads from left tank:  td[0]=D1(4453) td[1]=D2(3720) tap[1]=AP6(1800)
            // All offsets verified in-bounds at reference rate 29761 Hz.
            auto otap = [&](const std::vector<float>& b, int w, double refOff) -> float {
                return fxRead (b, w, (int)(refOff * sc));
            };
            float revL =  otap(fxs.td[2],  fxs.tdW[2],  266)
                        + otap(fxs.td[2],  fxs.tdW[2],  2974)
                        - otap(fxs.tap[3], fxs.tapW[3], 1913)
                        + otap(fxs.tap[3], fxs.tapW[3], 1996)
                        + otap(fxs.td[3],  fxs.tdW[3],  1990)
                        - otap(fxs.td[3],  fxs.tdW[3],  187)
                        + otap(fxs.td[2],  fxs.tdW[2],  320);
            float revR =  otap(fxs.td[0],  fxs.tdW[0],  353)
                        + otap(fxs.td[0],  fxs.tdW[0],  3627)
                        - otap(fxs.tap[1], fxs.tapW[1], 1228)
                        + otap(fxs.td[1],  fxs.tdW[1],  2673)
                        + otap(fxs.td[0],  fxs.tdW[0],  1990)
                        - otap(fxs.td[0],  fxs.tdW[0],  187)
                        + otap(fxs.td[0],  fxs.tdW[0],  320);

            revL *= 0.6f; revR *= 0.6f;
            inL = inL * (1.f - p.reverbMix) + revL * p.reverbMix;
            inR = inR * (1.f - p.reverbMix) + revR * p.reverbMix;
            // Guard against feedback explosion (inf/nan → silence rather than crash)
            inL = juce::jlimit (-4.0f, 4.0f, inL);
            inR = juce::jlimit (-4.0f, 4.0f, inR);
        }

        //----------------------------------------------------------------------
        // CHORUS — 3-voice BBD-style
        //----------------------------------------------------------------------
        if (p.chorusOn)
        {
            fxWrite (fxs.chorusBuf, fxs.chorusW, (inL + inR) * 0.5f);
            float outL = 0.f, outR = 0.f;
            // 3 voices, spread in stereo: voice 0 = left, 1 = centre, 2 = right
            const float panning[3] = { 1.0f, 0.5f, 0.0f };
            for (int v = 0; v < 3; ++v)
            {
                fxs.chorusPh[v] = std::fmod (fxs.chorusPh[v]
                                  + (float)(p.chorusRate / sr), 1.0f);
                // Delay: 5ms center ± depth*10ms
                const float delayMs  = 5.0f + p.chorusDepth * 10.0f
                                       * std::sin (fxs.chorusPh[v] * juce::MathConstants<float>::twoPi);
                const float delaySmp = delayMs * 0.001f * (float)sr;
                const float sv = fxReadLerp (fxs.chorusBuf, fxs.chorusW, delaySmp);
                const float panR = panning[v];
                const float panL = 1.0f - panR;
                outL += sv * panL;
                outR += sv * panR;
            }
            outL /= 3.f; outR /= 3.f;
            inL = inL * (1.f - p.chorusMix) + outL * p.chorusMix;
            inR = inR * (1.f - p.chorusMix) + outR * p.chorusMix;
        }

        //----------------------------------------------------------------------
        // MASTER DRIVE + GAIN
        //----------------------------------------------------------------------
        if (p.masterDrive > 0.001f)
        {
            const float d = 1.0f + p.masterDrive * 7.0f;
            inL = std::tanh (inL * d) / d;
            inR = std::tanh (inR * d) / d;
        }
        inL *= p.masterGain;
        inR *= p.masterGain;

        L[s] = inL;
        R[s] = inR;
    }
}

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
    double effectiveBPM  = internalBPM;
    double startPPQ      = -1.0;
    bool   useHostSync   = false;
    bool   hostAvailable = false;   // true = running inside a DAW with a playhead
    bool   hostPlaying   = false;   // true = DAW transport is rolling

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostAvailable = true;

            if (auto bpm = pos->getBpm())
                effectiveBPM = *bpm;

            hostPlaying = pos->getIsPlaying();
            if (hostPlaying)
                if (auto ppq = pos->getPpqPosition())
                { startPPQ = *ppq; useHostSync = (startPPQ >= 0.0); }
        }
    }

    const double ppqPerSample = effectiveBPM / 60.0 / currentSampleRate;

    //--------------------------------------------------------------------------
    // TRANSPORT RESET — whenever host start/stop is detected, snap both
    // sequencers back to the beginning of their pattern.
    //--------------------------------------------------------------------------
    if (hostAvailable && hostPlaying != prevHostPlaying)
    {
        for (int vi = 0; vi < numVoices; ++vi)
        {
            vstate[vi].sampleCounter    = 0.0;
            vstate[vi].lastPos          = -1;
            voice [vi].currentStep      = 0;
            vstate[vi].ratchetSubStep   = 0;
            vstate[vi].ratchetSubStepDur= 0.0;
            vstate[vi].ratchetStepPos   = 0.0;
            vstate[vi].ratchetNoteOff   = false;
            if (!hostPlaying)             // stopping: silence envelopes immediately
            {
                vstate[vi].adsr.noteOff();
                vstate[vi].filterEnv.noteOff();
            }
        }
    }
    prevHostPlaying = hostPlaying;

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
            const double pulses = (double)juce::jlimit (1, 8, vp.stepPulses[i]);
            swingBounds   [vi][i + 1] = swingBounds   [vi][i] + samplesPerStep * factor * pulses;
            swingPPQBounds[vi][i + 1] = swingPPQBounds[vi][i] + ppqStep        * factor * pulses;
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

        // Nudge — rotate the step order so playback starts at nudgeOffset
        if (vp.nudgeOffset != 0)
        {
            int tmp[16];
            const int seqLen = vp.sequenceLength;
            for (int i = 0; i < seqLen; ++i)
                tmp[i] = stepOrder[vi][(i + vp.nudgeOffset) % seqLen];
            for (int i = 0; i < seqLen; ++i)
                stepOrder[vi][i] = tmp[i];
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

        // In a DAW: follow host transport exclusively.
        // Standalone (no host): use the per-voice RUN/STOP button.
        const bool runA = hostAvailable ? hostPlaying : voice[0].sequencerRunning.load();
        const bool runB = hostAvailable ? hostPlaying : voice[1].sequencerRunning.load();

        float outA = processSingleVoiceSample (0,
            runA, useHostSync, samplePPQ, effectiveBPM,
            swingBounds[0], swingPPQBounds[0],
            totalSwingCycle[0], totalSwingPPQ[0],
            stepOrder[0], glideCoeff[0],
            crossModSample[1]);   // voice B modulates voice A

        float outB = processSingleVoiceSample (1,
            runB, useHostSync, samplePPQ, effectiveBPM,
            swingBounds[1], swingPPQBounds[1],
            totalSwingCycle[1], totalSwingPPQ[1],
            stepOrder[1], glideCoeff[1],
            crossModSample[0]);   // voice A modulates voice B

        leftCh[s]  = (outA + outB) * 0.5f;
        rightCh[s] = leftCh[s];
    }

    processFxBuffer (leftCh, rightCh, numSamples);
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

                    // Tied step: sustain envelope from previous step — no retrigger
                    if (!vp.stepTied[vp.currentStep])
                    {
                        vs.adsr.noteOff();      vs.adsr.noteOn();
                        vs.filterEnv.noteOff(); vs.filterEnv.noteOn();
                        if (!vp.modEnv.clockSync)
                        {
                            vs.modEnvAdsr.reset();
                            vs.modEnvAdsr.noteOn();
                        }
                    }
                }

                // ── Ratchet setup ──────────────────────────────────────────────
                {
                    const int repeats = juce::jlimit (1, 4, vp.stepRepeats[vp.currentStep] + 1);
                    const double stepDur = swingBounds[pos + 1] - swingBounds[pos];
                    vs.ratchetSubStepDur = (repeats > 1) ? (stepDur / repeats) : 0.0;
                    vs.ratchetStepPos    = 0.0;
                    vs.ratchetSubStep    = 0;
                    vs.ratchetNoteOff    = false;
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

        // ── Ratchet sub-step pulses ────────────────────────────────────────────
        if (vp.stepGates[vp.currentStep] && !vp.stepTied[vp.currentStep]
            && vs.ratchetSubStepDur > 0.0)
        {
            vs.ratchetStepPos += 1.0;
            const double subPos = vs.ratchetStepPos
                                  - vs.ratchetSubStep * vs.ratchetSubStepDur;

            // Note-off at 50 % of sub-step
            if (!vs.ratchetNoteOff && subPos >= vs.ratchetSubStepDur * 0.5)
            {
                vs.adsr.noteOff();
                vs.filterEnv.noteOff();
                vs.ratchetNoteOff = true;
            }

            // Advance to next sub-step
            const int repeats = juce::jlimit (1, 4, vp.stepRepeats[vp.currentStep] + 1);
            if (subPos >= vs.ratchetSubStepDur && vs.ratchetSubStep < repeats - 1)
            {
                ++vs.ratchetSubStep;
                vs.adsr.noteOff(); vs.adsr.noteOn();
                vs.filterEnv.noteOff(); vs.filterEnv.noteOn();
                vs.ratchetNoteOff = false;
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
    const double t  = vs.osc1Phase;
    const double dt = phaseInc;

    switch (vp.osc1Waveform)
    {
        case VoiceParams::Sine:
            output = (float)std::sin (t * juce::MathConstants<double>::twoPi);
            break;

        case VoiceParams::Saw:
            // Raw ramp, then subtract PolyBLEP at the wrap discontinuity
            output  = (float)(t * 2.0 - 1.0);
            output -= polyBlep (t, dt);
            break;

        case VoiceParams::Square:
        {
            // Raw square, then correct both edges with PolyBLEP
            const double pw = (double)vs.pulseWidth;
            output  = (t < pw) ? 1.0f : -1.0f;
            output += polyBlep (t, dt);                              // rising edge at t=0
            output -= polyBlep (std::fmod (t - pw + 1.0, 1.0), dt); // falling edge at t=pw
            break;
        }

        case VoiceParams::Triangle:
            // Triangle is naturally band-limited — no PolyBLEP needed
            output = (t < 0.5) ? (float)(t * 4.0 - 1.0)
                                : (float)(3.0 - t * 4.0);
            break;

        default: break;
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
        el.setAttribute ("ti" + juce::String (i), vp.stepTied  [i]);
        el.setAttribute ("rt" + juce::String (i), vp.stepRepeats[i]);
        el.setAttribute ("pl" + juce::String (i), vp.stepPulses [i]);
    }
    el.setAttribute ("porta",    (double)vp.portamentoTime);
    el.setAttribute ("swing",    (double)vp.swingAmount);
    el.setAttribute ("clkDiv",   vp.clockDivision);
    el.setAttribute ("seqLen",   vp.sequenceLength);
    el.setAttribute ("unipolar",   vp.unipolar);
    el.setAttribute ("playOrder",  vp.playOrder);
    el.setAttribute ("nudgeOffset",vp.nudgeOffset);
    el.setAttribute ("range",      (double)vp.rangeVCA);
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
        vp.stepTied    [i] = getB (("ti" + juce::String (i)).toRawUTF8(), false);
        vp.stepRepeats [i] = getI (("rt" + juce::String (i)).toRawUTF8(), 0);
        vp.stepPulses  [i] = juce::jlimit (1, 8, getI (("pl" + juce::String (i)).toRawUTF8(), 1));
    }
    vp.portamentoTime   = getF ("porta",    vp.portamentoTime);
    vp.swingAmount      = getF ("swing",    vp.swingAmount);
    vp.clockDivision    = getI ("clkDiv",   vp.clockDivision);
    vp.sequenceLength   = getI ("seqLen",   vp.sequenceLength);
    vp.unipolar         = getB ("unipolar",   vp.unipolar);
    vp.playOrder        = getI ("playOrder",  vp.playOrder);
    vp.nudgeOffset      = getI ("nudgeOffset",0);
    vp.rangeVCA         = getF ("range",      vp.rangeVCA);
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
        p.stepTied    [i] = v.stepTied    [i];
        p.stepRepeats [i] = v.stepRepeats [i];
        p.stepPulses  [i] = v.stepPulses  [i];
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
        v.stepTied    [i] = p.stepTied    [i];
        v.stepRepeats [i] = p.stepRepeats [i];
        v.stepPulses  [i] = p.stepPulses  [i];
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
            juce::String volts, gates, glides, ties, repeats;
            for (int i = 0; i < numSteps; ++i)
            {
                volts   += juce::String (p.stepVoltages[i], 4) + (i < 15 ? "," : "");
                gates   += juce::String ((int)p.stepGates[i])  + (i < 15 ? "," : "");
                glides  += juce::String ((int)p.stepGlides[i]) + (i < 15 ? "," : "");
                ties    += juce::String ((int)p.stepTied[i])   + (i < 15 ? "," : "");
                repeats += juce::String (p.stepRepeats[i])     + (i < 15 ? "," : "");
            }
            slotEl->setAttribute ("volts",   volts);
            slotEl->setAttribute ("gates",   gates);
            slotEl->setAttribute ("glides",  glides);
            slotEl->setAttribute ("ties",    ties);
            slotEl->setAttribute ("repeats", repeats);
        }
    }

    auto* fxEl = xml.createNewChildElement ("FX");
    fxEl->setAttribute ("delayOn",       (int)fx.delayOn);
    fxEl->setAttribute ("delaySync",     (int)fx.delaySync);
    fxEl->setAttribute ("delaySyncDiv",  fx.delaySyncDiv);
    fxEl->setAttribute ("delayTimeMs",   fx.delayTimeMs);
    fxEl->setAttribute ("delayFeedback", fx.delayFeedback);
    fxEl->setAttribute ("delayPingPong", (int)fx.delayPingPong);
    fxEl->setAttribute ("delayMix",      fx.delayMix);
    fxEl->setAttribute ("reverbOn",      (int)fx.reverbOn);
    fxEl->setAttribute ("reverbSize",    fx.reverbSize);
    fxEl->setAttribute ("reverbDamping", fx.reverbDamping);
    fxEl->setAttribute ("reverbPreDly",  fx.reverbPreDelay);
    fxEl->setAttribute ("reverbMix",     fx.reverbMix);
    fxEl->setAttribute ("chorusOn",      (int)fx.chorusOn);
    fxEl->setAttribute ("chorusRate",    fx.chorusRate);
    fxEl->setAttribute ("chorusDepth",   fx.chorusDepth);
    fxEl->setAttribute ("chorusMix",     fx.chorusMix);
    fxEl->setAttribute ("masterDrive",   fx.masterDrive);
    fxEl->setAttribute ("masterGain",    fx.masterGain);

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
                    auto parseInts = [](const juce::String& s, int* arr, int n) {
                        juce::StringArray tok; tok.addTokens (s, ",", "");
                        for (int i = 0; i < juce::jmin (n, tok.size()); ++i)
                            arr[i] = tok[i].getIntValue();
                    };
                    parseFloats (slotEl->getStringAttribute ("volts"),   p.stepVoltages, numSteps);
                    parseBools  (slotEl->getStringAttribute ("gates"),   p.stepGates,    numSteps);
                    parseBools  (slotEl->getStringAttribute ("glides"),  p.stepGlides,   numSteps);
                    parseBools  (slotEl->getStringAttribute ("ties"),    p.stepTied,     numSteps);
                    parseInts   (slotEl->getStringAttribute ("repeats"), p.stepRepeats,  numSteps);
                }
            }
        }
    }
    else
    {
        // v1 backward compat: flat attributes on root → load into voice[0]
        loadVoiceFromXml (*xml, voice[0], numSteps);
    }

    if (auto* fxEl = xml->getChildByName ("FX"))
    {
        fx.delayOn       = (bool)fxEl->getIntAttribute    ("delayOn",       0);
        fx.delaySync     = (bool)fxEl->getIntAttribute    ("delaySync",     1);
        fx.delaySyncDiv  =        fxEl->getIntAttribute   ("delaySyncDiv",  2);
        fx.delayTimeMs   = (float)fxEl->getDoubleAttribute("delayTimeMs",   375.0);
        fx.delayFeedback = (float)fxEl->getDoubleAttribute("delayFeedback", 0.40);
        fx.delayPingPong = (bool)fxEl->getIntAttribute    ("delayPingPong", 0);
        fx.delayMix      = (float)fxEl->getDoubleAttribute("delayMix",      0.30);
        fx.reverbOn      = (bool)fxEl->getIntAttribute    ("reverbOn",      0);
        fx.reverbSize    = (float)fxEl->getDoubleAttribute("reverbSize",    0.75);
        fx.reverbDamping = (float)fxEl->getDoubleAttribute("reverbDamping", 0.40);
        fx.reverbPreDelay= (float)fxEl->getDoubleAttribute("reverbPreDly",  20.0);
        fx.reverbMix     = (float)fxEl->getDoubleAttribute("reverbMix",     0.25);
        fx.chorusOn      = (bool)fxEl->getIntAttribute    ("chorusOn",      0);
        fx.chorusRate    = (float)fxEl->getDoubleAttribute("chorusRate",    0.50);
        fx.chorusDepth   = (float)fxEl->getDoubleAttribute("chorusDepth",   0.50);
        fx.chorusMix     = (float)fxEl->getDoubleAttribute("chorusMix",     0.50);
        fx.masterDrive   = (float)fxEl->getDoubleAttribute("masterDrive",   0.0);
        fx.masterGain    = (float)fxEl->getDoubleAttribute("masterGain",    1.0);
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
