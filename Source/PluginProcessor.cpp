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
// APVTS parameter layout
// Skew factors are the NormalisableRange equivalent of Slider::setSkewFactorFromMidPoint:
//   skew = log(0.5) / log((midPoint - min) / (max - min))
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VoltageSeq2AudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int vi = 0; vi < 2; ++vi)
    {
        juce::String s  = "_" + juce::String (vi);
        juce::String vn = (vi == 0) ? "A" : "B";

        // ── Step voltages (16 per voice) ──────────────────────────────────────
        for (int i = 0; i < 16; ++i)
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID ("step" + juce::String (i) + s, 1),
                "Step " + juce::String (i + 1) + " " + vn,
                juce::NormalisableRange<float> (-5.0f, 5.0f, 0.01f),
                0.0f));

        // ── Filter cutoff  (20–16000 Hz, midpoint 1000 → skew ≈ 0.248) ──────
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("cutoff" + s, 1), "Cutoff " + vn,
            juce::NormalisableRange<float> (20.0f, 16000.0f, 0.1f, 0.248f), 2000.0f));

        // ── Amp ADSR  (times: midpoint 0.3 over 0.001–2.0 → skew ≈ 0.365) ──
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("ampA" + s, 1), "Amp Attack " + vn,
            juce::NormalisableRange<float> (0.001f, 2.0f, 0.001f, 0.365f), 0.01f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("ampD" + s, 1), "Amp Decay " + vn,
            juce::NormalisableRange<float> (0.001f, 2.0f, 0.001f, 0.365f), 0.1f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("ampS" + s, 1), "Amp Sustain " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("ampR" + s, 1), "Amp Release " + vn,
            juce::NormalisableRange<float> (0.001f, 3.0f, 0.001f, 0.301f), 0.08f));

        // ── Filter ADSR  (times: midpoint 0.3 over 0.001–4.0 → skew ≈ 0.267)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fA" + s, 1), "Flt Attack " + vn,
            juce::NormalisableRange<float> (0.001f, 4.0f, 0.001f, 0.267f), 0.01f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fD" + s, 1), "Flt Decay " + vn,
            juce::NormalisableRange<float> (0.001f, 4.0f, 0.001f, 0.267f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fS" + s, 1), "Flt Sustain " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fR" + s, 1), "Flt Release " + vn,
            juce::NormalisableRange<float> (0.001f, 4.0f, 0.001f, 0.267f), 0.3f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fEnvAmt" + s, 1), "Flt Env Amt " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

        // ── LFO 1  (rate 0.1–20 Hz, midpoint 4.0 → skew ≈ 0.425) ───────────
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("lfo1Rate" + s, 1), "LFO1 Rate " + vn,
            juce::NormalisableRange<float> (0.1f, 20.0f, 0.01f, 0.425f), 2.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("lfo1Dep" + s, 1), "LFO1 Depth " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

        // ── LFO 2 ─────────────────────────────────────────────────────────────
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("lfo2Rate" + s, 1), "LFO2 Rate " + vn,
            juce::NormalisableRange<float> (0.1f, 20.0f, 0.01f, 0.425f), 3.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("lfo2Dep" + s, 1), "LFO2 Depth " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

        // ── FM ────────────────────────────────────────────────────────────────
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fmRatio" + s, 1), "FM Ratio " + vn,
            juce::NormalisableRange<float> (0.0f, 6.0f, 0.25f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("fmDepth" + s, 1), "FM Depth " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

        // ── Portamento + Range ────────────────────────────────────────────────
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("porta" + s, 1), "Portamento " + vn,
            juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("range" + s, 1), "Range " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    }

    return { params.begin(), params.end() };
}

//==============================================================================
VoltageSeq2AudioProcessor::VoltageSeq2AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                     .withOutput ("Voice A", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Voice B", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
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
        vs.pulseWidth   = vp.osc1PulseWidth;
        vs.srFilled     = 0;
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

        // Initialize all slots
        float freq = voltageToQuantizedFreq (vp, vp.stepVoltages[0]);
        for (int si = 0; si < VoiceState::kMaxSlots; ++si)
        {
            UnisonSlot& slot = vs.slots[si];
            slot.currentFreq1  = freq * (float)std::pow (2.0, (double)vp.osc1Octave);
            slot.currentFreq2  = freq * (float)std::pow (2.0, (double)vp.osc2Octave);
            slot.targetFreq1   = slot.currentFreq1;
            slot.targetFreq2   = slot.currentFreq2;
            slot.osc1PhaseInc  = slot.currentFreq1 / sampleRate;
            slot.osc2PhaseInc  = slot.currentFreq2 / sampleRate;
            slot.osc1FeedbackSample = 0.0f;
            slot.glideActive   = false;
            slot.osc1DriftRate = 0.08f + (float)si * 0.03f + juce::Random::getSystemRandom().nextFloat() * 0.10f;
            slot.osc2DriftRate = 0.11f + (float)si * 0.04f + juce::Random::getSystemRandom().nextFloat() * 0.10f;
            slot.osc1DriftPhase = juce::Random::getSystemRandom().nextFloat();
            slot.osc2DriftPhase = juce::Random::getSystemRandom().nextFloat();
            slot.panL = 1.0f; slot.panR = 1.0f;
            slot.detuneRatio = 1.0f;
            slot.assignedVoltage = 0.0f;
        }
    }

    prepareFx (sampleRate);

    // Pre-size Voice B temp buffers to avoid allocation in the audio thread.
    voiceBTempL.assign (samplesPerBlock + 64, 0.f);
    voiceBTempR.assign (samplesPerBlock + 64, 0.f);
}

void VoltageSeq2AudioProcessor::releaseResources() {}

//==============================================================================
void VoltageSeq2AudioProcessor::prepareFx (double sr)
{
    // All delay lengths from Dattorro 1997 (reference rate 29761 Hz)
    const double sc = sr / 29761.0;
    auto sz = [&](int n) { return (int)(n * sc) + 4; };

    const int maxDly  = (int)(sr * 2.0) + 4;
    const int preSz   = (int)(sr * 0.1) + 4;
    const int iapLens[4] = { sz(142), sz(107), sz(379), sz(277) };
    const int tdLens [4] = { sz(4453), sz(3720), sz(4217), sz(3163) };
    const int tapLens[4] = { sz(672),  sz(1800), sz(908),  sz(2656) };

    for (int vi = 0; vi < 2; ++vi)
    {
        FxState& f = fxs[vi];

        // ── Delay ─────────────────────────────────────────────────────────────
        f.dlyL.assign (maxDly, 0.f); f.dlyR.assign (maxDly, 0.f);
        f.dlyWL = f.dlyWR = 0;

        // ── Dattorro Plate Reverb ──────────────────────────────────────────────
        for (int i = 0; i < 4; ++i) {
            f.iapLen[i] = iapLens[i];
            f.iap[i].assign (iapLens[i] + 4, 0.f);
            f.iapW[i] = 0;
            f.tdLen[i] = tdLens[i];
            f.td[i].assign (tdLens[i] + 4, 0.f);
            f.tdW[i] = 0;
            f.tapLen[i] = tapLens[i];
            f.tap[i].assign (tapLens[i] + 64, 0.f);
            f.tapW[i] = 0;
        }
        f.lpL = f.lpR = 0.f;
        f.modPh1 = 0.0; f.modPh2 = 0.5;
        f.preL.assign (preSz, 0.f); f.preR.assign (preSz, 0.f);
        f.preWL = f.preWR = 0;

        // ── Chorus ────────────────────────────────────────────────────────────
        f.chorusBuf.assign (8192, 0.f);
        f.chorusW = 0;
        f.chorusPh[0] = 0.f; f.chorusPh[1] = 1.f/3.f; f.chorusPh[2] = 2.f/3.f;
    }
}

//==============================================================================
void VoltageSeq2AudioProcessor::processFxBuffer (float* L, float* R, int numSamples,
                                                  FxState& fxs, const FxParams& p)
{
    // Early-out when this voice has FX bypassed — leave L/R untouched.
    if (p.fxBypass) return;

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
    // Bus 0 (Voice A) must always be stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    // Bus 1 (Voice B) may be stereo or disabled.
    const auto& bus1 = layouts.getNumChannels (false, 1);
    if (bus1 != 0 && bus1 != 2)
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
    // APVTS → VoiceParams sync  (DAW automation + MIDI-learn support)
    // getRawParameterValue returns a lock-free atomic<float>* — safe to read
    // here on the audio thread without any allocations.
    //--------------------------------------------------------------------------
    for (int vi = 0; vi < numVoices; ++vi)
    {
        auto& vp = voice[vi];
        const juce::String s = "_" + juce::String (vi);

        for (int i = 0; i < numSteps; ++i)
            vp.stepVoltages[i] = apvts.getRawParameterValue ("step" + juce::String (i) + s)->load();

        vp.filterCutoff            = apvts.getRawParameterValue ("cutoff"   + s)->load();
        vp.adsrParams.attack       = apvts.getRawParameterValue ("ampA"     + s)->load();
        vp.adsrParams.decay        = apvts.getRawParameterValue ("ampD"     + s)->load();
        vp.adsrParams.sustain      = apvts.getRawParameterValue ("ampS"     + s)->load();
        vp.adsrParams.release      = apvts.getRawParameterValue ("ampR"     + s)->load();
        vp.filterEnvParams.attack  = apvts.getRawParameterValue ("fA"       + s)->load();
        vp.filterEnvParams.decay   = apvts.getRawParameterValue ("fD"       + s)->load();
        vp.filterEnvParams.sustain = apvts.getRawParameterValue ("fS"       + s)->load();
        vp.filterEnvParams.release = apvts.getRawParameterValue ("fR"       + s)->load();
        vp.filterEnvAmount         = apvts.getRawParameterValue ("fEnvAmt"  + s)->load();
        vp.lfoRate                 = apvts.getRawParameterValue ("lfo1Rate" + s)->load();
        vp.lfoDepth                = apvts.getRawParameterValue ("lfo1Dep"  + s)->load();
        vp.lfo2Rate                = apvts.getRawParameterValue ("lfo2Rate" + s)->load();
        vp.lfo2Depth               = apvts.getRawParameterValue ("lfo2Dep"  + s)->load();
        vp.fmRatio                 = apvts.getRawParameterValue ("fmRatio"  + s)->load();
        vp.fmDepth                 = apvts.getRawParameterValue ("fmDepth"  + s)->load();
        vp.portamentoTime          = apvts.getRawParameterValue ("porta"    + s)->load();
        vp.rangeVCA                = apvts.getRawParameterValue ("range"    + s)->load();
    }

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
            if (!hostPlaying)             // stopping: silence envelopes + flush MIDI note
            {
                vstate[vi].adsr.noteOff();
                vstate[vi].filterEnv.noteOff();
                if (voice[vi].midiOutEnabled)
                {
                    const int ch = juce::jlimit (1, 16, voice[vi].midiOutChannel);
                    if (vstate[vi].midiOutNote >= 0)
                    {
                        midiMessages.addEvent (
                            juce::MidiMessage::noteOff (ch, vstate[vi].midiOutNote, (juce::uint8)0), 0);
                        vstate[vi].midiOutNote = -1;
                    }
                    if (vstate[vi].midiGlideActive)
                    {
                        vstate[vi].midiGlideActive = false;
                        midiMessages.addEvent (juce::MidiMessage::pitchWheel (ch, 0), 0);
                    }
                }
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
        // PULSE MODE: reset after exactly pulseLength clock ticks regardless of stages
        if (vp.pulseLengthMode && vp.pulseLength > 0)
        {
            totalSwingCycle[vi] = (double)vp.pulseLength * samplesPerStep;
            totalSwingPPQ  [vi] = (double)vp.pulseLength * ppqStep;
        }
        else
        {
            totalSwingCycle[vi] = swingBounds   [vi][vp.sequenceLength];
            totalSwingPPQ  [vi] = swingPPQBounds[vi][vp.sequenceLength];
        }

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
    // UNISON SLOT DETUNE + PAN PRE-COMPUTATION
    //--------------------------------------------------------------------------
    for (int vi = 0; vi < numVoices; ++vi)
    {
        const VoiceParams& vp = voice[vi];
        const int nSlots = (vp.voiceMode == VoiceParams::Mono) ? 1 : vp.unisonCount;
        for (int si = 0; si < nSlots; ++si)
        {
            UnisonSlot& slot = vstate[vi].slots[si];
            // Symmetric detune: outermost slots get ±unisonSpread semitones
            float detuneSemi = (nSlots == 1) ? 0.0f
                : (si - (nSlots - 1) * 0.5f) / ((nSlots - 1) * 0.5f) * vp.unisonSpread;
            slot.detuneRatio = std::pow (2.0f, detuneSemi / 12.0f);
            // Constant-power pan
            float panPos = (nSlots == 1) ? 0.0f
                : (si - (nSlots - 1) * 0.5f) / ((nSlots - 1) * 0.5f) * vp.unisonWidth;
            panPos = juce::jlimit (-1.0f, 1.0f, panPos);
            slot.panL = std::sqrt (0.5f * (1.0f - panPos));
            slot.panR = std::sqrt (0.5f * (1.0f + panPos));
        }
    }

    //--------------------------------------------------------------------------
    // SAMPLE LOOP — Voice A → channels 0/1, Voice B → channels 2/3
    //--------------------------------------------------------------------------
    const int numSamples = buffer.getNumSamples();

    // Voice A always goes to the main stereo bus (channels 0/1).
    float* chA0 = buffer.getWritePointer (0);
    float* chA1 = buffer.getWritePointer (1);

    // Voice B goes to the second stereo bus if the host has enabled it,
    // otherwise falls back to a pre-allocated temp buffer that we mix into bus 0.
    const bool hasDualOut = (buffer.getNumChannels() >= 4);
    float* chB0 = hasDualOut ? buffer.getWritePointer (2) : voiceBTempL.data();
    float* chB1 = hasDualOut ? buffer.getWritePointer (3) : voiceBTempR.data();

    for (int s = 0; s < numSamples; ++s)
    {
        const double samplePPQ = startPPQ + (double)s * ppqPerSample;

        const bool runA = hostAvailable ? hostPlaying : voice[0].sequencerRunning.load();
        const bool runB = hostAvailable ? hostPlaying : voice[1].sequencerRunning.load();

        float outAL, outAR, outBL, outBR;
        processSingleVoiceSample (0,
            runA, useHostSync, samplePPQ, effectiveBPM,
            swingBounds[0], swingPPQBounds[0],
            totalSwingCycle[0], totalSwingPPQ[0],
            stepOrder[0], glideCoeff[0],
            crossModSample[1], outAL, outAR);

        processSingleVoiceSample (1,
            runB, useHostSync, samplePPQ, effectiveBPM,
            swingBounds[1], swingPPQBounds[1],
            totalSwingCycle[1], totalSwingPPQ[1],
            stepOrder[1], glideCoeff[1],
            crossModSample[0], outBL, outBR);

        chA0[s] = outAL;  chA1[s] = outAR;
        chB0[s] = outBL;  chB1[s] = outBR;

        // ── MIDI out: emit note + pitch-bend events at the exact sample offset ──
        for (int vi = 0; vi < numVoices; ++vi)
        {
            auto& vp = voice[vi];
            auto& vs = vstate[vi];

            if (!vp.midiOutEnabled) continue;

            const int   ch       = juce::jlimit (1, 16, vp.midiOutChannel);
            // Pitch-bend range assumed / set on downstream synth: ±12 semitones
            constexpr float kPbRange    = 12.0f;
            constexpr int   kPbInterval = 64;   // send a PB message every 64 samples

            // Helper: semitones → 14-bit MIDI pitch-bend value
            auto semToPB = [](float semi, float range) -> int {
                return juce::jlimit (-8192, 8191,
                                     (int)(semi / range * 8191.0f));
            };

            // ── 1. Step advance ────────────────────────────────────────────────
            if (vs.midiStepFired)
            {
                vs.midiStepFired = false;

                const bool canGlide = vs.midiStepGlide
                                      && vs.midiOutNote >= 0
                                      && vs.midiStepNote >= 0
                                      && vs.midiStepGate;

                if (canGlide)
                {
                    // Clamp bend to ±kPbRange semitones
                    const float initBend = juce::jlimit (-kPbRange, kPbRange,
                        (float)(vs.midiOutNote - vs.midiStepNote));

                    // Note-off for old note
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);

                    // Tell downstream synth to use ±12 semitone PB range (RPN 0)
                    midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101,  0), s);
                    midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100,  0), s);
                    midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,   6, (int)kPbRange), s);
                    midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,  38,  0), s);
                    midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 127), s);
                    midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100, 127), s);

                    // Bend to old pitch before note-on (sounds like we're still on old note)
                    midiMessages.addEvent (
                        juce::MidiMessage::pitchWheel (ch, semToPB (initBend, kPbRange)), s);

                    // Note-on for new note (bent to old pitch, will ramp to 0)
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOn (ch, vs.midiStepNote, (juce::uint8)100), s);

                    vs.midiOutNote    = vs.midiStepNote;
                    vs.midiGlideActive = true;
                    vs.midiGlideBend   = initBend;
                    vs.midiGlideTick   = 0;
                }
                else
                {
                    // Non-glide step: cancel any active PB ramp first
                    if (vs.midiGlideActive)
                    {
                        vs.midiGlideActive = false;
                        midiMessages.addEvent (juce::MidiMessage::pitchWheel (ch, 0), s);
                    }

                    if (vs.midiOutNote >= 0)
                    {
                        midiMessages.addEvent (
                            juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);
                        vs.midiOutNote = -1;
                    }
                    if (vs.midiStepGate && vs.midiStepNote >= 0)
                    {
                        midiMessages.addEvent (
                            juce::MidiMessage::noteOn (ch, vs.midiStepNote, (juce::uint8)100), s);
                        vs.midiOutNote = vs.midiStepNote;
                    }
                }
            }

            // ── 2. Ratchet 50 % point: note-off gap between hits ──────────────
            if (vs.midiRatchetOff)
            {
                vs.midiRatchetOff = false;
                if (vs.midiOutNote >= 0)
                {
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);
                    vs.midiOutNote = -1;
                }
            }

            // ── 3. Ratchet sub-step advance: note-on for next hit ─────────────
            if (vs.midiRatchetOn)
            {
                vs.midiRatchetOn = false;
                if (vs.midiOutNote >= 0)
                {
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);
                    vs.midiOutNote = -1;
                }
                if (vs.midiStepNote >= 0)
                {
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOn (ch, vs.midiStepNote, (juce::uint8)100), s);
                    vs.midiOutNote = vs.midiStepNote;
                }
            }

            // ── 4. Pitch-bend portamento ramp (fires every kPbInterval samples) ─
            if (vs.midiGlideActive)
            {
                vs.midiGlideBend *= glideCoeff[vi];   // same IIR as audio portamento
                ++vs.midiGlideTick;

                const bool done = std::abs (vs.midiGlideBend) < 0.02f;
                if (done || vs.midiGlideTick >= kPbInterval)
                {
                    vs.midiGlideTick = 0;
                    midiMessages.addEvent (
                        juce::MidiMessage::pitchWheel (ch, done ? 0 : semToPB (vs.midiGlideBend, kPbRange)), s);
                    if (done) vs.midiGlideActive = false;
                }
            }
        }
    }

    // Apply independent FX chain to each voice.
    processFxBuffer (chA0, chA1, numSamples, fxs[0], fx[0]);
    processFxBuffer (chB0, chB1, numSamples, fxs[1], fx[1]);

    // Single-bus fallback: mix Voice B into Voice A (legacy behaviour).
    if (!hasDualOut)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            chA0[s] = (chA0[s] + chB0[s]) * 0.5f;
            chA1[s] = (chA1[s] + chB1[s]) * 0.5f;
        }
    }
}

//==============================================================================
// SINGLE-VOICE SAMPLE PROCESSOR
// Contains the full per-sample voice chain for voice[vi].
//==============================================================================
void VoltageSeq2AudioProcessor::processSingleVoiceSample (
    int vi, bool running,
    bool useHostSync, double samplePPQ,
    double effectiveBPM,
    const double* swingBounds,
    const double* swingPPQBounds,
    double totalSwingCycle,
    double totalSwingPPQ,
    const int* stepOrder,
    float glideCoeff,
    float crossModIn,
    float& outL,
    float& outR)
{
    VoiceState& vs = vstate[vi];
    VoiceParams& vp = voice[vi];
    const int nSlots = (vp.voiceMode == VoiceParams::Mono) ? 1 : vp.unisonCount;

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

            vs.midiStepFired = true;
            vs.midiStepGate  = vp.stepGates[vp.currentStep];
            vs.midiStepNote  = vs.midiStepGate
                               ? voltageToMidiNote (vp, vp.stepVoltages[vp.currentStep])
                               : -1;

            if (vp.voiceMode == VoiceParams::Poly)
            {
                // SHIFT REGISTER: push all slots back, slot 0 gets new note
                for (int si = nSlots - 1; si > 0; --si)
                {
                    vs.slots[si].currentFreq1    = vs.slots[si-1].currentFreq1;
                    vs.slots[si].currentFreq2    = vs.slots[si-1].currentFreq2;
                    vs.slots[si].osc1PhaseInc    = vs.slots[si-1].osc1PhaseInc;
                    vs.slots[si].osc2PhaseInc    = vs.slots[si-1].osc2PhaseInc;
                    vs.slots[si].assignedVoltage = vs.slots[si-1].assignedVoltage;
                }
                vs.srFilled = juce::jmin (vs.srFilled + 1, nSlots);

                vs.slots[0].assignedVoltage = vp.stepVoltages[vp.currentStep];
                if (vp.stepGates[vp.currentStep])
                {
                    float baseFreq = voltageToQuantizedFreq (vp, vp.stepVoltages[vp.currentStep]);
                    vs.slots[0].currentFreq1 = baseFreq * (float)std::pow (2.0, (double)vp.osc1Octave);
                    vs.slots[0].currentFreq2 = baseFreq * (float)std::pow (2.0, (double)vp.osc2Octave);
                    vs.slots[0].osc1PhaseInc = vs.slots[0].currentFreq1 / currentSampleRate;
                    vs.slots[0].osc2PhaseInc = vs.slots[0].currentFreq2 / currentSampleRate;
                    vs.slots[0].glideActive  = false;

                    if (!vp.stepTied[vp.currentStep])
                    {
                        if (vp.envReset) { vs.adsr.reset(); vs.filterEnv.reset(); }
                        vs.adsr.noteOff();      vs.adsr.noteOn();
                        vs.filterEnv.noteOff(); vs.filterEnv.noteOn();
                        if (!vp.modEnv.clockSync)
                        {
                            vs.modEnvAdsr.reset();
                            vs.modEnvAdsr.noteOn();
                        }
                    }
                }
                else
                {
                    vs.adsr.noteOff();
                    vs.filterEnv.noteOff();
                    if (!vp.modEnv.clockSync)
                        vs.modEnvAdsr.noteOff();
                }

                vs.midiStepGlide = false;

                // Ratchet setup (poly mode — uses shared envelope)
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
                // MONO or UNISON: all slots get same base freq, triggered simultaneously
                if (vp.stepGates[vp.currentStep])
                {
                    float baseFreq = voltageToQuantizedFreq (vp, vp.stepVoltages[vp.currentStep]);
                    const bool doGlide = (vp.portamentoTime > 0.001f && vp.stepGlides[vp.currentStep]);

                    for (int si = 0; si < nSlots; ++si)
                    {
                        UnisonSlot& slot = vs.slots[si];
                        slot.targetFreq1 = baseFreq * (float)std::pow (2.0, (double)vp.osc1Octave) * slot.detuneRatio;
                        slot.targetFreq2 = baseFreq * (float)std::pow (2.0, (double)vp.osc2Octave) * slot.detuneRatio;
                        slot.glideActive = doGlide;
                        if (!doGlide)
                        {
                            slot.currentFreq1 = slot.targetFreq1;
                            slot.currentFreq2 = slot.targetFreq2;
                            slot.osc1PhaseInc = slot.currentFreq1 / currentSampleRate;
                            slot.osc2PhaseInc = slot.currentFreq2 / currentSampleRate;
                        }
                    }

                    vs.midiStepGlide = doGlide;

                    if (!vp.stepTied[vp.currentStep])
                    {
                        if (vp.envReset) { vs.adsr.reset(); vs.filterEnv.reset(); }
                        vs.adsr.noteOff();      vs.adsr.noteOn();
                        vs.filterEnv.noteOff(); vs.filterEnv.noteOn();
                        if (!vp.modEnv.clockSync)
                        {
                            vs.modEnvAdsr.reset();
                            vs.modEnvAdsr.noteOn();
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
                    for (int si = 0; si < nSlots; ++si)
                        vs.slots[si].glideActive = false;
                    vs.midiStepGlide = false;
                    vs.adsr.noteOff();
                    vs.filterEnv.noteOff();
                    if (!vp.modEnv.clockSync)
                        vs.modEnvAdsr.noteOff();
                }
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
                vs.ratchetNoteOff   = true;
                vs.midiRatchetOff   = true;
            }

            // Advance to next sub-step
            const int repeats = juce::jlimit (1, 4, vp.stepRepeats[vp.currentStep] + 1);
            if (subPos >= vs.ratchetSubStepDur && vs.ratchetSubStep < repeats - 1)
            {
                ++vs.ratchetSubStep;
                if (vp.envReset) { vs.adsr.reset(); vs.filterEnv.reset(); }
                vs.adsr.noteOff(); vs.adsr.noteOn();
                vs.filterEnv.noteOff(); vs.filterEnv.noteOn();
                vs.ratchetNoteOff   = false;
                vs.midiRatchetOn    = true;
            }
        }

        vs.sampleCounter += 1.0;
        if (vs.sampleCounter >= totalSwingCycle)
            vs.sampleCounter -= totalSwingCycle;
    }

    //--------------------------------------------------------------------------
    // PORTAMENTO (glide) — per slot
    //--------------------------------------------------------------------------
    for (int si = 0; si < nSlots; ++si)
    {
        UnisonSlot& slot = vs.slots[si];
        if (slot.glideActive)
        {
            slot.currentFreq1 = slot.currentFreq1 * glideCoeff + slot.targetFreq1 * (1.0f - glideCoeff);
            slot.currentFreq2 = slot.currentFreq2 * glideCoeff + slot.targetFreq2 * (1.0f - glideCoeff);
            slot.osc1PhaseInc = slot.currentFreq1 / currentSampleRate;
            slot.osc2PhaseInc = slot.currentFreq2 / currentSampleRate;
        }
    }

    //--------------------------------------------------------------------------
    // LFOs
    //--------------------------------------------------------------------------
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

    auto syncRate = [&](int divIdx) -> float
    {
        return (float)(effectiveBPM / 60.0 / (cenvDivBars[juce::jlimit (0, 7, divIdx)] * 4.0));
    };

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
        for (int si = 0; si < nSlots; ++si)
        {
            UnisonSlot& slot = vs.slots[si];
            float slotVoltage = (vp.voiceMode == VoiceParams::Poly)
                                ? slot.assignedVoltage
                                : vp.stepVoltages[vp.currentStep];
            const float baseFreq = voltageToQuantizedFreq (vp, slotVoltage, effRange);
            const float f1 = baseFreq * (float)std::pow (2.0, (double)vp.osc1Octave) * slot.detuneRatio;
            const float f2 = baseFreq * (float)std::pow (2.0, (double)vp.osc2Octave) * slot.detuneRatio;
            if (slot.glideActive) { slot.targetFreq1 = f1; slot.targetFreq2 = f2; }
            else { slot.osc1PhaseInc = (double)f1 / currentSampleRate;
                   slot.osc2PhaseInc = (double)f2 / currentSampleRate; }
        }
    }

    //--------------------------------------------------------------------------
    // FILTER ENVELOPE → effective cutoff (shared, computed once)
    //--------------------------------------------------------------------------
    float fEnvSample   = vs.filterEnv.getNextSample();
    float effectiveCut = vp.filterCutoff
                         * std::pow (2.0f, vp.filterEnvAmount * 4.0f * fEnvSample);
    effectiveCut = juce::jlimit (20.0f, 20000.0f,
                                 (effectiveCut + cutoffMod) * cenvCutoffMod);

    //--------------------------------------------------------------------------
    // PER-SLOT OSCILLATORS → sum to mono (with stereo pan weights)
    //--------------------------------------------------------------------------
    const float fmRatioVal = std::max (0.0f, vp.fmRatio);
    const float maxDriftCents = 4.0f;

    float sumL = 0.0f, sumR = 0.0f;
    float primaryOsc1Raw = 0.0f;  // slot 0 for scope + cross-mod

    for (int si = 0; si < nSlots; ++si)
    {
        UnisonSlot& slot = vs.slots[si];

        // Drift — independent per slot
        slot.osc1DriftVal = std::sin (slot.osc1DriftPhase * juce::MathConstants<float>::twoPi)
                            * vp.driftAmount * maxDriftCents;
        slot.osc2DriftVal = std::sin (slot.osc2DriftPhase * juce::MathConstants<float>::twoPi)
                            * vp.driftAmount * maxDriftCents;
        slot.osc1DriftPhase += slot.osc1DriftRate / (float)currentSampleRate;
        if (slot.osc1DriftPhase >= 1.0f) slot.osc1DriftPhase -= 1.0f;
        slot.osc2DriftPhase += slot.osc2DriftRate / (float)currentSampleRate;
        if (slot.osc2DriftPhase >= 1.0f) slot.osc2DriftPhase -= 1.0f;
        const float drift1Ratio = std::pow (2.0f, slot.osc1DriftVal / 1200.0f);
        const float drift2Ratio = std::pow (2.0f, slot.osc2DriftVal / 1200.0f);

        // OSC2
        const double osc2Inc = (effectiveFMDepth > 0.001f && fmRatioVal > 0.0f)
            ? (slot.currentFreq1 * (double)fmRatioVal * pitchMod * (double)drift2Ratio) / currentSampleRate
            : slot.osc2PhaseInc * pitchMod * (double)drift2Ratio;
        const float osc2Raw = generateOsc2Sample (slot, vp, osc2Inc);

        // OSC1 with FM + feedback
        const double fmDev = slot.osc1PhaseInc * (double)(effectiveFMDepth * osc2Raw * 3.0f
                             + vp.crossModDepth * crossModIn * 3.0f);
        const double fbDev = slot.osc1PhaseInc * (double)(vp.osc1Feedback * slot.osc1FeedbackSample * 2.0f);
        const float osc1Raw = generateOsc1Sample (slot, vp,
            slot.osc1PhaseInc * pitchMod * (double)drift1Ratio + fmDev + fbDev, vs.pulseWidth);
        slot.osc1FeedbackSample = juce::jlimit (-1.0f, 1.0f, osc1Raw);

        if (si == 0) primaryOsc1Raw = osc1Raw;

        float slotOut = (osc1Raw * vp.osc1Level) + (osc2Raw * vp.osc2Level);
        sumL += slotOut * slot.panL;
        sumR += slotOut * slot.panR;
    }

    // Scope (slot 0 only, pre-filter)
    oscScopeBuffer[vi][scopeWritePos[vi]] = primaryOsc1Raw * vp.osc1Level;
    scopeWritePos[vi] = (scopeWritePos[vi] + 1) % scopeSize;

    // Cross-mod: expose slot 0 for next sample
    crossModSample[vi] = primaryOsc1Raw;

    // Normalize, filter, envelope — L and R filtered independently (avoids divide artefacts)
    const float norm = 1.0f / (float)nSlots;

    // Run the SVF on each channel separately using independent state registers.
    // For MONO (nSlots==1) sumL==sumR so both channels produce identical output.
    float filtL = applyFilter (vs.ic1eq,  vs.ic2eq,  vs.ic1eq2,  vs.ic2eq2,
                               vp, sumL * norm, effectiveCut);
    float filtR = applyFilter (vs.ic1eqR, vs.ic2eqR, vs.ic1eq2R, vs.ic2eq2R,
                               vp, sumR * norm, effectiveCut);

    float envelope = vs.adsr.getNextSample();
    const float gain = envelope * cenvAmpMod * 0.3f;

    outL = filtL * gain;
    outR = filtR * gain;
}

//==============================================================================
// OSC 1 — phase accumulator with waveform selector
//==============================================================================
float VoltageSeq2AudioProcessor::generateOsc1Sample (UnisonSlot& slot, const VoiceParams& vp,
                                                      double phaseInc, float pulseWidth)
{
    float output = 0.0f;
    const double t  = slot.osc1Phase;
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
            const double pw = (double)pulseWidth;
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

    slot.osc1Phase += phaseInc;
    if (slot.osc1Phase >= 1.0) slot.osc1Phase -= 1.0;
    return output;
}

//==============================================================================
// OSC 2 — interpolated wavetable morphing
//==============================================================================
float VoltageSeq2AudioProcessor::generateOsc2Sample (UnisonSlot& slot, const VoiceParams& vp,
                                                      double phaseInc)
{
    float tablePos = vp.osc2Position * (numWavetables - 1);
    int   tableA   = (int)tablePos;
    int   tableB   = juce::jmin (tableA + 1, numWavetables - 1);
    float blend    = tablePos - tableA;

    float readPos = (float)(slot.osc2Phase * wavetableSize);
    int   indexA  = (int)readPos % wavetableSize;
    int   indexB  = (indexA + 1) % wavetableSize;
    float frac    = readPos - (int)readPos;

    float sA = wavetables[tableA][indexA] + frac * (wavetables[tableA][indexB] - wavetables[tableA][indexA]);
    float sB = wavetables[tableB][indexA] + frac * (wavetables[tableB][indexB] - wavetables[tableB][indexA]);

    slot.osc2Phase += phaseInc;
    if (slot.osc2Phase >= 1.0) slot.osc2Phase -= 1.0;
    return sA + blend * (sB - sA);
}

//==============================================================================
// TPT State Variable Filter with drive, mode, and slope
//==============================================================================
float VoltageSeq2AudioProcessor::applyFilter (float& ic1, float& ic2,
                                               float& ic1_2, float& ic2_2,
                                               const VoiceParams& vp,
                                               float input, float effectiveCutoff)
{
    // Pre-filter drive (tanh saturation)
    const float drive = 1.0f + vp.filterDrive * 7.0f;   // 1x … 8x gain into clipper
    float driven = std::tanh (input * drive) / drive;    // normalised so unity gain at drive=0

    // TPT SVF — first stage
    auto runSVF = [](float in, float& s1, float& s2, float g, float k, int mode) -> float
    {
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;
        float v3 = in  - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1       = 2.0f * v1 - s1;
        s2       = 2.0f * v2 - s2;
        if (mode == 1) return v1;                        // BP
        if (mode == 2) return in - k * v1 - v2;         // HP
        return v2;                                       // LP (default)
    };

    float cut = juce::jlimit (20.0f, (float)(currentSampleRate * 0.45), effectiveCutoff);
    float g   = std::tan (juce::MathConstants<float>::pi * cut / (float)currentSampleRate);
    float k   = juce::jlimit (0.01f, 2.0f, 2.0f - 1.99f * vp.filterResonance);

    float out = runSVF (driven, ic1, ic2, g, k, vp.filterMode);

    // Second stage for 24 dB/oct
    if (vp.filterSlope == 1)
        out = runSVF (out, ic1_2, ic2_2, g, k, vp.filterMode);

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

// voltageToMidiNote — same pitch calculation as voltageToQuantizedFreq but
// returns the MIDI note number directly (avoids a round-trip through frequency).
int VoltageSeq2AudioProcessor::voltageToMidiNote (const VoiceParams& vp, float voltage) const
{
    float scaledV = voltage * vp.rangeVCA;
    float rawMidi = juce::jlimit (0.0f, 127.0f, 60.0f + (float)vp.rootNote + scaledV * 5.0f);
    return quantizeNoteToScale ((int)std::round (rawMidi), vp.rootNote, vp.currentScale);
}

//==============================================================================
// Note quantiser
//==============================================================================
int VoltageSeq2AudioProcessor::quantizeNoteToScale (int midiNote, int rootNote, int scale) const
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
bool VoltageSeq2AudioProcessor::producesMidi() const { return true; }
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
    el.setAttribute ("unipolar",       vp.unipolar);
    el.setAttribute ("envReset",       vp.envReset);
    el.setAttribute ("pulseLenMode",   vp.pulseLengthMode);
    el.setAttribute ("pulseLen",       vp.pulseLength);
    el.setAttribute ("playOrder",      vp.playOrder);
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

    el.setAttribute ("midiOutEn",  (int)vp.midiOutEnabled);
    el.setAttribute ("midiOutCh",  vp.midiOutChannel);

    el.setAttribute ("voiceMode",  (int)vp.voiceMode);
    el.setAttribute ("uniCount",   vp.unisonCount);
    el.setAttribute ("uniSpread",  (double)vp.unisonSpread);
    el.setAttribute ("uniWidth",   (double)vp.unisonWidth);
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
    vp.unipolar         = getB ("unipolar",     vp.unipolar);
    vp.envReset         = getB ("envReset",     false);
    vp.pulseLengthMode  = getB ("pulseLenMode", false);
    vp.pulseLength      = juce::jlimit (1, 512, getI ("pulseLen", 16));
    vp.playOrder        = getI ("playOrder",    vp.playOrder);
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

    vp.midiOutEnabled = getB ("midiOutEn", false);
    vp.midiOutChannel = getI ("midiOutCh", 1);

    vp.voiceMode   = (VoltageSeq2AudioProcessor::VoiceParams::VoiceMode)getI ("voiceMode", 0);
    vp.unisonCount = juce::jlimit (2, 4, getI ("uniCount", 4));
    vp.unisonSpread= getF ("uniSpread", 0.15f);
    vp.unisonWidth = getF ("uniWidth",  0.7f);
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
    // Push the newly-loaded step voltages + synth params into APVTS so that
    // SliderAttachments and the DAW automation system both see the new values.
    syncAPVTSFromVoice (vi);
}

void VoltageSeq2AudioProcessor::clearPattern (int vi, int slot)
{
    if (vi < 0 || vi >= numVoices || slot < 0 || slot >= numPatternSlots) return;
    patternBank[vi][slot] = PatternSlot{};
}

//==============================================================================
// syncAPVTSFromVoice — pushes VoiceParams values into the APVTS so that
// SliderAttachments in the editor and the DAW host both see updated values.
// Call this after loadPattern() or after restoring a pre-v4 preset.
// Uses setValueNotifyingHost() which is safe to call from the message thread.
//==============================================================================
void VoltageSeq2AudioProcessor::syncAPVTSFromVoice (int vi)
{
    jassert (vi >= 0 && vi < numVoices);
    const juce::String s  = "_" + juce::String (vi);
    const auto&        vp = voice[vi];

    auto setP = [&](const juce::String& id, float val)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (val));
    };

    for (int i = 0; i < numSteps; ++i)
        setP ("step" + juce::String (i) + s, vp.stepVoltages[i]);

    setP ("cutoff"   + s, vp.filterCutoff);
    setP ("ampA"     + s, vp.adsrParams.attack);
    setP ("ampD"     + s, vp.adsrParams.decay);
    setP ("ampS"     + s, vp.adsrParams.sustain);
    setP ("ampR"     + s, vp.adsrParams.release);
    setP ("fA"       + s, vp.filterEnvParams.attack);
    setP ("fD"       + s, vp.filterEnvParams.decay);
    setP ("fS"       + s, vp.filterEnvParams.sustain);
    setP ("fR"       + s, vp.filterEnvParams.release);
    setP ("fEnvAmt"  + s, vp.filterEnvAmount);
    setP ("lfo1Rate" + s, vp.lfoRate);
    setP ("lfo1Dep"  + s, vp.lfoDepth);
    setP ("lfo2Rate" + s, vp.lfo2Rate);
    setP ("lfo2Dep"  + s, vp.lfo2Depth);
    setP ("fmRatio"  + s, vp.fmRatio);
    setP ("fmDepth"  + s, vp.fmDepth);
    setP ("porta"    + s, vp.portamentoTime);
    setP ("range"    + s, vp.rangeVCA);
}

//------------------------------------------------------------------------------
void VoltageSeq2AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("VoltageSeq2State");
    xml.setAttribute ("version", 4);      // v4 adds <Parameters> APVTS child
    xml.setAttribute ("bpm", internalBPM);

    // ── APVTS state (automatable parameters) ─────────────────────────────────
    {
        auto apvtsState = apvts.copyState();
        if (auto apvtsXml = apvtsState.createXml())
            xml.addChildElement (apvtsXml.release());
    }

    // ── Full voice data (all params, for backward-compat with pre-v4 loaders) ─
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

    // Save per-voice FX (two child elements: <FX voice="0">, <FX voice="1">)
    for (int vi = 0; vi < 2; ++vi)
    {
        const auto& p  = fx[vi];
        auto* fxEl = xml.createNewChildElement ("FX");
        fxEl->setAttribute ("voice",         vi);
        fxEl->setAttribute ("bypass",        (int)p.fxBypass);
        fxEl->setAttribute ("delayOn",       (int)p.delayOn);
        fxEl->setAttribute ("delaySync",     (int)p.delaySync);
        fxEl->setAttribute ("delaySyncDiv",  p.delaySyncDiv);
        fxEl->setAttribute ("delayTimeMs",   p.delayTimeMs);
        fxEl->setAttribute ("delayFeedback", p.delayFeedback);
        fxEl->setAttribute ("delayPingPong", (int)p.delayPingPong);
        fxEl->setAttribute ("delayMix",      p.delayMix);
        fxEl->setAttribute ("reverbOn",      (int)p.reverbOn);
        fxEl->setAttribute ("reverbSize",    p.reverbSize);
        fxEl->setAttribute ("reverbDamping", p.reverbDamping);
        fxEl->setAttribute ("reverbPreDly",  p.reverbPreDelay);
        fxEl->setAttribute ("reverbMix",     p.reverbMix);
        fxEl->setAttribute ("chorusOn",      (int)p.chorusOn);
        fxEl->setAttribute ("chorusRate",    p.chorusRate);
        fxEl->setAttribute ("chorusDepth",   p.chorusDepth);
        fxEl->setAttribute ("chorusMix",     p.chorusMix);
        fxEl->setAttribute ("masterDrive",   p.masterDrive);
        fxEl->setAttribute ("masterGain",    p.masterGain);
    }

    copyXmlToBinary (xml, destData);
}

void VoltageSeq2AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (!xml || xml->getTagName() != "VoltageSeq2State") return;

    internalBPM = xml->getDoubleAttribute ("bpm", internalBPM);

    const int version = xml->getIntAttribute ("version", 1);

    // ── v4+: restore APVTS state (automatable params) ────────────────────────
    if (version >= 4)
    {
        if (auto* apvtsEl = xml->getChildByName ("Parameters"))
        {
            auto vt = juce::ValueTree::fromXml (*apvtsEl);
            if (vt.isValid())
                apvts.replaceState (vt);
        }
    }

    if (version >= 2)
    {
        for (auto* child : xml->getChildIterator())
        {
            if (child->getTagName() == "Voice")
            {
                int vi = child->getIntAttribute ("index", -1);
                if (vi >= 0 && vi < numVoices)
                {
                    loadVoiceFromXml (*child, voice[vi], numSteps);
                    // For pre-v4 presets: push loaded values into APVTS so the
                    // host sees them and the UI attachments reflect them.
                    if (version < 4)
                        syncAPVTSFromVoice (vi);
                }
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
        syncAPVTSFromVoice (0);
    }

    // Load per-voice FX.  Supports both:
    //  - New format (v4+): two <FX voice="0/1"> child elements
    //  - Old format (v1-v3): single <FX> child (no voice attr) → loaded into fx[0]
    for (auto* fxEl : xml->getChildIterator())
    {
        if (fxEl->getTagName() != "FX") continue;
        const int vi = fxEl->getIntAttribute ("voice", 0);   // defaults to 0 for old format
        if (vi < 0 || vi >= 2) continue;
        auto& p = fx[vi];
        p.fxBypass      = (bool)fxEl->getIntAttribute    ("bypass",        0);
        p.delayOn       = (bool)fxEl->getIntAttribute    ("delayOn",       0);
        p.delaySync     = (bool)fxEl->getIntAttribute    ("delaySync",     1);
        p.delaySyncDiv  =        fxEl->getIntAttribute   ("delaySyncDiv",  2);
        p.delayTimeMs   = (float)fxEl->getDoubleAttribute("delayTimeMs",   375.0);
        p.delayFeedback = (float)fxEl->getDoubleAttribute("delayFeedback", 0.40);
        p.delayPingPong = (bool)fxEl->getIntAttribute    ("delayPingPong", 0);
        p.delayMix      = (float)fxEl->getDoubleAttribute("delayMix",      0.30);
        p.reverbOn      = (bool)fxEl->getIntAttribute    ("reverbOn",      0);
        p.reverbSize    = (float)fxEl->getDoubleAttribute("reverbSize",    0.75);
        p.reverbDamping = (float)fxEl->getDoubleAttribute("reverbDamping", 0.40);
        p.reverbPreDelay= (float)fxEl->getDoubleAttribute("reverbPreDly",  20.0);
        p.reverbMix     = (float)fxEl->getDoubleAttribute("reverbMix",     0.25);
        p.chorusOn      = (bool)fxEl->getIntAttribute    ("chorusOn",      0);
        p.chorusRate    = (float)fxEl->getDoubleAttribute("chorusRate",    0.50);
        p.chorusDepth   = (float)fxEl->getDoubleAttribute("chorusDepth",   0.50);
        p.chorusMix     = (float)fxEl->getDoubleAttribute("chorusMix",     0.50);
        p.masterDrive   = (float)fxEl->getDoubleAttribute("masterDrive",   0.0);
        p.masterGain    = (float)fxEl->getDoubleAttribute("masterGain",    1.0);
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
