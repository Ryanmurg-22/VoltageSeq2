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
    // dt is the per-sample phase advance. Cross-mod / through-zero FM can push the
    // net phase increment negative, near-zero, or huge; guard those cases so the
    // t/dt divisions can never produce inf/NaN (which would poison the filter IIR).
    dt = std::abs (dt);
    if (dt < 1.0e-9 || dt > 0.5) return 0.0f;
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

const char* const VoltageSeq2AudioProcessor::kPlaitsEngineNames[24] = {
    "VA + VCF", "Phase Distortion", "6-op FM A", "6-op FM B", "6-op FM C",
    "Wave Terrain", "String Machine", "Chiptune",
    "Virtual Analog", "Waveshaping", "2-op FM", "Grain", "Additive",
    "Wavetable", "Chord", "Speech",
    "Swarm", "Noise", "Particle", "String (Karplus)",
    "Modal / Resonator", "Bass Drum", "Snare Drum", "Hi-Hat"
};

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
// Macro destination labels — index matches MacroTarget enum order.
const char* const VoltageSeq2AudioProcessor::kMacroTargetNames[MT_Count] = {
    "PWM", "Cutoff", "Pitch", "Range", "FM Depth",
    "Harmonics", "Timbre", "Morph", "Delay Time",
    "Resonance", "Filter Drive", "Delay Mix", "Amp Level",
    "Amp Atk", "Amp Dec", "Amp Sus", "Amp Rel",
    "Flt Atk", "Flt Dec", "Flt Sus", "Flt Rel",
    "Reverb Mix", "Reverb Time"
};

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

        // ── Plaits timbral controls (DAW-automatable) ─────────────────────────
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("plaitsHarm" + s, 1), "Plaits Harmonics " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("plaitsTimb" + s, 1), "Plaits Timbre " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("plaitsMorph" + s, 1), "Plaits Morph " + vn,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    }

    // ── Global macro wheel values (0..1, host-automatable) ───────────────────
    for (int m = 0; m < numMacros; ++m)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("macro" + juce::String (m), 1),
            "Macro " + juce::String (m + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

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
            voice[vi].stepGates[i]    = false;  // all gates off by default (clean slate)
            voice[vi].stepGlides[i]   = false;
            voice[vi].stepAccents[i]  = false;
        }
        voice[vi].unipolar      = true;   // unipolar (0..5 V range)
        voice[vi].currentScale  = 8;      // Chromatic = effectively unquantized
        // LFOs / mod-env start UNMAPPED — no routes until the user assigns one
        // (so no depth rings appear on a fresh instance).
    }

    // Randomise Turing Machine register
    resetTuringMachine();
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

    // Initialise Plaits for each voice
    for (int vi = 0; vi < numVoices; ++vi)
    {
        auto& ps = plaitsState[vi];
        ps.allocator.Init (ps.memory, sizeof(ps.memory));
        ps.voice.Init (&ps.allocator);
        ps.patch = {};
        ps.patch.engine     = voice[vi].plaitsEngine;
        ps.patch.harmonics  = voice[vi].plaitsHarmonics;
        ps.patch.timbre     = voice[vi].plaitsTimbre;
        ps.patch.morph      = voice[vi].plaitsMorph;
        ps.patch.decay      = 0.0f;
        ps.patch.lpg_colour = 0.5f;
        ps.patch.frequency_modulation_amount = 0.0f;
        ps.patch.timbre_modulation_amount    = 0.0f;
        ps.patch.morph_modulation_amount     = 0.0f;
        ps.patch.note       = 48.0f;
        ps.mods = {};
        ps.mods.trigger_patched  = false;  // free-running by default; ADSR shapes amplitude
        ps.mods.level_patched    = false;
        ps.mods.level            = 1.0f;
        ps.frameIdx = PlaitsState::kBufSize; // force first render
        ps.initialized = true;
    }

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

        // ── Tape Delay ────────────────────────────────────────────────────────
        f.dlyL.assign (maxDly, 0.f); f.dlyR.assign (maxDly, 0.f);
        f.dlyWL = f.dlyWR = 0;
        // Pre-load smoother from current param so first play has no glide artefact
        f.smoothedDelayMs = fx[vi].delayTimeMs;

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
                                                  FxState& fxs, const FxParams& p,
                                                  float lfoTimeModMs, float delayMixMod,
                                                  float reverbMixMod, float reverbSizeMod)
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
        // TAPE DELAY
        //----------------------------------------------------------------------
        if (p.delayOn)
        {
            // ── Base delay time ───────────────────────────────────────────────
            float delayMs;
            if (p.delaySync)
            {
                static constexpr double divTable[7] = {
                    1.0, 0.5, 0.25, 1.0/3.0, 1.0/6.0, 0.75, 0.375 };
                delayMs = (float)(divTable[p.delaySyncDiv] * 4.0 * 60000.0 / liveBPM);
            }
            else
                delayMs = p.delayTimeMs;

            // ── LFO → Delay Time (block-rate) ─────────────────────────────────
            // Added to the target so the 4 Hz smoother below removes any block-
            // boundary step, giving click-free tape-style time warble.
            delayMs = juce::jmax (1.0f, delayMs + lfoTimeModMs);

            // ── Smooth delay time — prevents read-pointer click on knob changes ──
            // One-pole LP at ~4 Hz (≈40 ms time constant).  The glide also gives a
            // natural tape pitch-bend when time is adjusted manually.
            // Pre-compute coefficient once per sample (sr is buffer-constant).
            const float dtSmooth = 1.0f - std::exp ((float)(-twoPi * 4.0 / sr));
            fxs.smoothedDelayMs += dtSmooth * (delayMs - fxs.smoothedDelayMs);

            // ── Tape wow / flutter LFOs ────────────────────────────────────────
            // Wow:     ~0.7 Hz,  ±12 ms at full depth
            // Flutter: ~9.0 Hz,  ±2.5 ms at full depth
            fxs.tapeWowPh     += 0.7  / sr;  if (fxs.tapeWowPh     > 1.0) fxs.tapeWowPh     -= 1.0;
            fxs.tapeFlutterPh += 9.0  / sr;  if (fxs.tapeFlutterPh > 1.0) fxs.tapeFlutterPh -= 1.0;

            const float wowMod     = (float)std::sin (fxs.tapeWowPh     * twoPi) * p.delayWow     * 12.0f;
            const float flutterMod = (float)std::sin (fxs.tapeFlutterPh * twoPi) * p.delayFlutter *  2.5f;

            // Use smoothed time as base — never the raw target
            const float modDelayMs = juce::jmax (1.0f, fxs.smoothedDelayMs + wowMod + flutterMod);
            const float dSamplesF  = juce::jlimit (1.0f, (float)(fxs.dlyL.size() - 1),
                                                   modDelayMs * 0.001f * (float)sr);

            // ── Tape bandwidth limiting (1-pole LP, ~8 kHz) ───────────────────
            // Applied to the feedback read to simulate HF loss on each pass.
            const float lpCoeff = 1.0f - std::exp ((float)(-twoPi * 8000.0 / sr));

            // ── Read from delay lines (lerp for smooth pitch modulation) ──────
            float dL = fxReadLerp (fxs.dlyL, fxs.dlyWL, dSamplesF);
            float dR = fxReadLerp (fxs.dlyR, fxs.dlyWR, dSamplesF);

            // Apply tape LP to delay return (cumulative HF damping per repeat)
            fxs.tapeLpL += lpCoeff * (dL - fxs.tapeLpL);
            fxs.tapeLpR += lpCoeff * (dR - fxs.tapeLpR);
            dL = fxs.tapeLpL;
            dR = fxs.tapeLpR;

            // ── Tape saturation — soft-clip input before writing ──────────────
            // Drive boosts level going in, tanh clips, then normalise back.
            float sendL = inL, sendR = inR;
            if (p.delaySat > 0.001f)
            {
                const float driveGain = 1.0f + p.delaySat * 4.0f;
                sendL = std::tanh (sendL * driveGain) / driveGain;
                sendR = std::tanh (sendR * driveGain) / driveGain;
            }

            // ── Bernoulli gate envelope ────────────────────────────────────────
            // Three-phase VCA: instant open → hold → release (like a 5V gate into
            // a short AR envelope opening a VCA into the delay send).
            //
            //  PROB = 1.0  → gate always open, no hold/release logic (normal delay)
            //  PROB < 1.0  → on each successful Bernoulli roll:
            //                  • instant attack (env = 1.0)
            //                  • hold ~150 ms (gate stays fully open)
            //                  • release ~250 ms (smooth tail into delay)
            //                  • then silent until next successful roll
            //
            // Release coefficient: ~5 Hz → τ ≈ 200 ms
            const float bernRelCoeff = 1.0f - std::exp ((float)(-twoPi * 5.0 / sr));

            if (p.delayProb >= 0.999f)
            {
                // Normal mode — gate always open, no gating
                fxs.bernGateEnv     = 1.0f;
                fxs.bernHoldSamples = 0;
                fxs.bernGateTrig    = false;
            }
            else if (fxs.bernGateTrig)
            {
                // Bernoulli roll won — instant open, start hold phase
                fxs.bernGateEnv     = 1.0f;
                fxs.bernGateTrig    = false;
                fxs.bernHoldSamples = (int)(0.15f * (float)sr);  // 150 ms hold
            }
            else if (fxs.bernHoldSamples > 0)
            {
                // Hold phase — gate stays fully open while counter counts down
                --fxs.bernHoldSamples;
                fxs.bernGateEnv = 1.0f;
            }
            else
            {
                // Release phase — env decays toward zero
                fxs.bernGateEnv -= bernRelCoeff * fxs.bernGateEnv;
                if (fxs.bernGateEnv < 0.0001f) fxs.bernGateEnv = 0.0f;
            }

            sendL *= fxs.bernGateEnv;
            sendR *= fxs.bernGateEnv;

            // ── Write with feedback (ping-pong crosses channels) ──────────────
            if (p.delayPingPong) {
                fxWrite (fxs.dlyL, fxs.dlyWL, sendL + dR * p.delayFeedback);
                fxWrite (fxs.dlyR, fxs.dlyWR, sendR + dL * p.delayFeedback);
            } else {
                fxWrite (fxs.dlyL, fxs.dlyWL, sendL + dL * p.delayFeedback);
                fxWrite (fxs.dlyR, fxs.dlyWR, sendR + dR * p.delayFeedback);
            }

            // ── Wet/dry mix ───────────────────────────────────────────────────
            const float dMix = juce::jlimit (0.0f, 1.0f, p.delayMix + delayMixMod);
            inL = inL * (1.f - dMix) + dL * dMix;
            inR = inR * (1.f - dMix) + dR * dMix;
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

            const float revSize = juce::jlimit (0.0f, 1.0f, p.reverbSize + reverbSizeMod);
            const float decay = 0.1f + revSize * 0.87f;  // 0.1..0.97

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
            const float revMix = juce::jlimit (0.0f, 1.0f, p.reverbMix + reverbMixMod);
            inL = inL * (1.f - revMix) + revL * revMix;
            inR = inR * (1.f - revMix) + revR * revMix;
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
            // Smooth the depth so knob moves don't zipper the modulation amplitude.
            fxs.smoothedChorusDepth += (p.chorusDepth - fxs.smoothedChorusDepth) * 0.001f;
            const float depth = fxs.smoothedChorusDepth;
            // 3 voices, spread in stereo: voice 0 = left, 1 = centre, 2 = right
            const float panning[3] = { 1.0f, 0.5f, 0.0f };
            for (int v = 0; v < 3; ++v)
            {
                fxs.chorusPh[v] = std::fmod (fxs.chorusPh[v]
                                  + (float)(p.chorusRate / sr), 1.0f);
                // Delay: 12ms centre ± depth*9ms — stays strictly positive (was
                // 5±depth*10, which went NEGATIVE past depth 0.5 and read past the
                // write pointer → the clicky aliasing).
                const float delayMs  = 12.0f + depth * 9.0f
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
        vp.plaitsHarmonics         = apvts.getRawParameterValue ("plaitsHarm"  + s)->load();
        vp.plaitsTimbre            = apvts.getRawParameterValue ("plaitsTimb"  + s)->load();
        vp.plaitsMorph             = apvts.getRawParameterValue ("plaitsMorph" + s)->load();
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

    liveBPM = effectiveBPM;   // expose the live tempo to tempo-synced FX (delay)

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
                    // MONO/UNISON: flush single held note
                    if (vstate[vi].midiOutNote >= 0)
                    {
                        midiMessages.addEvent (
                            juce::MidiMessage::noteOff (ch, vstate[vi].midiOutNote, (juce::uint8)0), 0);
                        vstate[vi].midiOutNote = -1;
                    }
                    // POLY: flush all shift-register notes
                    for (int si = 0; si < VoiceState::kMaxSlots; ++si)
                    {
                        if (vstate[vi].midiPolyNotes[si] >= 0)
                        {
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOff (ch, vstate[vi].midiPolyNotes[si], (juce::uint8)0), 0);
                            vstate[vi].midiPolyNotes[si] = -1;
                        }
                    }
                    vstate[vi].midiPolyEvict = false;
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

        // ── Block-rate macro modulation (envelope shaping + reverb) ───────────
        // ADSR params are reloaded from APVTS at the top of the block, so adding
        // the macro offset here does not accumulate across blocks.
        {
            float mA[10] = {};   // AmpA,D,S,R, FltA,D,S,R, RevMix, RevSize
            for (int m = 0; m < numMacros; ++m)
            {
                const float mv = macros[m].value.load();
                const int   n  = macros[m].count.load();
                for (int a = 0; a < n && a < kMaxMacroAssign; ++a)
                {
                    const auto& as = macros[m].assign[a];
                    if (! macroScopeHitsVoice (as.scope, vi)) continue;
                    const float c = mv * as.depth;
                    switch (as.target)
                    {
                        case MT_AmpA: mA[0] += c; break;  case MT_AmpD: mA[1] += c; break;
                        case MT_AmpS: mA[2] += c; break;  case MT_AmpR: mA[3] += c; break;
                        case MT_FltA: mA[4] += c; break;  case MT_FltD: mA[5] += c; break;
                        case MT_FltS: mA[6] += c; break;  case MT_FltR: mA[7] += c; break;
                        case MT_ReverbMix:  mA[8] += c; break;
                        case MT_ReverbTime: mA[9] += c; break;
                        default: break;
                    }
                }
            }
            // Additive; full depth ≈ full param range. Times in seconds, levels 0..1.
            vp.adsrParams.attack   = juce::jlimit (0.001f, 2.0f, vp.adsrParams.attack   + mA[0] * 2.0f);
            vp.adsrParams.decay    = juce::jlimit (0.001f, 2.0f, vp.adsrParams.decay    + mA[1] * 2.0f);
            vp.adsrParams.sustain  = juce::jlimit (0.0f,   1.0f, vp.adsrParams.sustain  + mA[2]);
            vp.adsrParams.release  = juce::jlimit (0.001f, 3.0f, vp.adsrParams.release  + mA[3] * 3.0f);
            vp.filterEnvParams.attack  = juce::jlimit (0.001f, 4.0f, vp.filterEnvParams.attack  + mA[4] * 4.0f);
            vp.filterEnvParams.decay   = juce::jlimit (0.001f, 4.0f, vp.filterEnvParams.decay   + mA[5] * 4.0f);
            vp.filterEnvParams.sustain = juce::jlimit (0.0f,   1.0f, vp.filterEnvParams.sustain + mA[6]);
            vp.filterEnvParams.release = juce::jlimit (0.001f, 4.0f, vp.filterEnvParams.release + mA[7] * 4.0f);
            macroReverbMixMod [vi] = mA[8];
            macroReverbSizeMod[vi] = mA[9];
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

    // ── Scan incoming MIDI for pattern triggers ───────────────────────────────
    // Always clear incoming MIDI from the buffer — we are a MIDI generator, not
    // a pass-through device.  When MIDI trigger mode is active we also decode
    // notes C2-D#3 (36-51) as pattern-slot selectors before discarding them.
    {
        const bool anyMidiMode = (patSeq[0].mode == 2 || patSeq[1].mode == 2);
        if (anyMidiMode)
        {
            juce::MidiBuffer passThrough;
            for (const auto meta : midiMessages)
            {
                const auto msg  = meta.getMessage();
                const int  note = msg.getNoteNumber();
                const bool isTrig = (note >= 36 && note <= 51)
                                 && (msg.isNoteOn() || msg.isNoteOff());
                if (msg.isNoteOn() && note >= 36 && note <= 51)
                    for (int vi = 0; vi < 2; ++vi)
                        if (patSeq[vi].mode == 2)
                            patSeq[vi].pendingSlot = note - 36;
                if (!isTrig)
                    passThrough.addEvent (msg, meta.samplePosition);
            }
            midiMessages.swapWith (passThrough);
        }
        else
        {
            // No MIDI trigger mode active — discard all incoming MIDI so it
            // doesn't pollute the generated MIDI output stream.
            midiMessages.clear();
        }
    }

    // Refresh macro wheel values from APVTS (block-rate) into the audio-thread mirror.
    for (int m = 0; m < numMacros; ++m)
        macros[m].value.store (apvts.getRawParameterValue ("macro" + juce::String (m))->load());

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

        // Per-voice Run/Stop gates the transport in BOTH modes: in a DAW the voice
        // follows the host transport *and* must be enabled; standalone uses the
        // button alone. (Previously the host path ignored the button entirely.)
        const bool transportRolling = hostAvailable ? hostPlaying : true;
        const bool runA = transportRolling && voice[0].sequencerRunning.load();
        const bool runB = transportRolling && voice[1].sequencerRunning.load();

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

                if (vp.voiceMode == VoiceParams::Poly)
                {
                    // ── POLY: shift-register polyphony ─────────────────────────
                    const int slots = juce::jmin (vp.unisonCount, (int)VoiceState::kMaxSlots);

                    if (vp.shiftRegChordMode)
                    {
                        // ── CHORD MODE ─────────────────────────────────────────
                        // Re-fire the entire register as a vertical chord each step.
                        // 1. NoteOff all sustained notes (slots 1..N-1 held from prev steps)
                        for (int si = 1; si < slots; ++si)
                        {
                            if (vs.midiPolyNotes[si] >= 0)
                                midiMessages.addEvent (
                                    juce::MidiMessage::noteOff (ch, vs.midiPolyNotes[si], (juce::uint8)0), s);
                        }
                        // 2. NoteOff evicted note
                        if (vs.midiPolyEvict && vs.midiPolyEvictNote >= 0)
                        {
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOff (ch, vs.midiPolyEvictNote, (juce::uint8)0), s);
                            vs.midiPolyEvict = false;
                        }
                        // 3. NoteOn ALL slots simultaneously (only on gated steps)
                        if (vs.midiStepGate)
                        {
                            for (int si = 0; si < slots; ++si)
                            {
                                if (vs.midiPolyNotes[si] >= 0)
                                    midiMessages.addEvent (
                                        juce::MidiMessage::noteOn (ch, vs.midiPolyNotes[si], vs.midiStepVel), s);
                            }
                        }
                    }
                    else
                    {
                        // ── SHIFT REGISTER MODE (default) ──────────────────────
                        // Eviction always fires — on both gate-on and gate-off steps.
                        // A gate-off step pushes -1 into slot 0, so the oldest real note
                        // drifts to the evict position and is retired one slot at a time.
                        if (vs.midiPolyEvict && vs.midiPolyEvictNote >= 0)
                        {
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOff (ch, vs.midiPolyEvictNote, (juce::uint8)0), s);
                            vs.midiPolyEvict = false;
                        }

                        // NoteOn only when a gate is active for this step
                        if (vs.midiStepGate && vs.midiStepNote >= 0)
                        {
                            // After the MIDI register shift, midiPolyNotes[1] holds the
                            // previous slot-0 note — use it as the glide "from" pitch.
                            const int prevNote = vs.midiPolyNotes[1];
                            const bool doGlide = vs.midiStepGlide
                                                 && prevNote >= 0
                                                 && prevNote != vs.midiStepNote;

                            if (doGlide)
                            {
                                const float initBend = juce::jlimit (-kPbRange, kPbRange,
                                    (float)(prevNote - vs.midiStepNote));
                                midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101,  0), s);
                                midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100,  0), s);
                                midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,   6, (int)kPbRange), s);
                                midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,  38,  0), s);
                                midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 127), s);
                                midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100, 127), s);
                                midiMessages.addEvent (
                                    juce::MidiMessage::pitchWheel (ch, semToPB (initBend, kPbRange)), s);
                                midiMessages.addEvent (
                                    juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                                vs.midiGlideActive = true;
                                vs.midiGlideBend   = initBend;
                                vs.midiGlideTick   = 0;
                            }
                            else
                            {
                                if (vs.midiGlideActive)
                                {
                                    vs.midiGlideActive = false;
                                    midiMessages.addEvent (juce::MidiMessage::pitchWheel (ch, 0), s);
                                }
                                midiMessages.addEvent (
                                    juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                            }
                        }
                    }
                }
                else
                {
                    // ── MONO / UNISON ───────────────────────────────────────────
                    if (vs.midiStepTied && vs.midiStepGate && vs.midiOutNote >= 0)
                    {
                        // Tied step → legato: no retrigger, just update pitch
                        if (vs.midiStepGlide && vs.midiStepNote >= 0
                            && vs.midiStepNote != vs.midiOutNote)
                        {
                            // Glide on a tied step — portamento ramp as normal
                            const float initBend = juce::jlimit (-kPbRange, kPbRange,
                                (float)(vs.midiOutNote - vs.midiStepNote));
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101,  0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100,  0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,   6, (int)kPbRange), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,  38,  0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 127), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100, 127), s);
                            midiMessages.addEvent (
                                juce::MidiMessage::pitchWheel (ch, semToPB (initBend, kPbRange)), s);
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                            vs.midiOutNote     = vs.midiStepNote;
                            vs.midiGlideActive = true;
                            vs.midiGlideBend   = initBend;
                            vs.midiGlideTick   = 0;
                        }
                        else if (vs.midiStepNote >= 0 && vs.midiStepNote != vs.midiOutNote)
                        {
                            // Tied pitch-change, no glide: legato overlap
                            // note-on first so the downstream synth sees overlap → no re-attack
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);
                            vs.midiOutNote = vs.midiStepNote;
                        }
                        // Same pitch + tied → do nothing (note sustains naturally)
                    }
                    else
                    {
                        // Normal (non-tied) step
                        const bool canGlide = vs.midiStepGlide
                                              && vs.midiOutNote >= 0
                                              && vs.midiStepNote >= 0
                                              && vs.midiStepGate;

                        if (canGlide)
                        {
                            const float initBend = juce::jlimit (-kPbRange, kPbRange,
                                (float)(vs.midiOutNote - vs.midiStepNote));
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOff (ch, vs.midiOutNote, (juce::uint8)0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101,  0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100,  0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,   6, (int)kPbRange), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch,  38,  0), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 127), s);
                            midiMessages.addEvent (juce::MidiMessage::controllerEvent (ch, 100, 127), s);
                            midiMessages.addEvent (
                                juce::MidiMessage::pitchWheel (ch, semToPB (initBend, kPbRange)), s);
                            midiMessages.addEvent (
                                juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                            vs.midiOutNote     = vs.midiStepNote;
                            vs.midiGlideActive = true;
                            vs.midiGlideBend   = initBend;
                            vs.midiGlideTick   = 0;
                        }
                        else
                        {
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
                                    juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                                vs.midiOutNote = vs.midiStepNote;
                            }
                        }
                    }
                }
                vs.midiStepTied = false;
            }

            // ── 2. Ratchet 50 % point: note-off gap between hits ──────────────
            if (vs.midiRatchetOff)
            {
                vs.midiRatchetOff = false;
                // For POLY use slot-0 note; for MONO/UNISON use midiOutNote
                int& rNote = (vp.voiceMode == VoiceParams::Poly)
                             ? vs.midiPolyNotes[0] : vs.midiOutNote;
                if (rNote >= 0)
                {
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOff (ch, rNote, (juce::uint8)0), s);
                    rNote = -1;
                }
            }

            // ── 3. Ratchet sub-step advance: note-on for next hit ─────────────
            if (vs.midiRatchetOn)
            {
                vs.midiRatchetOn = false;
                int& rNote = (vp.voiceMode == VoiceParams::Poly)
                             ? vs.midiPolyNotes[0] : vs.midiOutNote;
                if (rNote >= 0)
                {
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOff (ch, rNote, (juce::uint8)0), s);
                    rNote = -1;
                }
                if (vs.midiStepNote >= 0)
                {
                    midiMessages.addEvent (
                        juce::MidiMessage::noteOn (ch, vs.midiStepNote, vs.midiStepVel), s);
                    rNote = vs.midiStepNote;
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
    processFxBuffer (chA0, chA1, numSamples, fxs[0], fx[0], lfoDelayMod[0], macroDelayMixMod[0],
                     macroReverbMixMod[0], macroReverbSizeMod[0]);
    processFxBuffer (chB0, chB1, numSamples, fxs[1], fx[1], lfoDelayMod[1], macroDelayMixMod[1],
                     macroReverbMixMod[1], macroReverbSizeMod[1]);

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

            // ── Pattern sequencer ─────────────────────────────────────────────
            {
                auto& ps = patSeq[vi];
                if (ps.mode != 0)
                {
                    const bool isNewCycle = (pos == 0);
                    const bool fireNow    = ps.immediate ? true : isNewCycle;
                    if (fireNow)
                    {
                        if (ps.mode == 2 && ps.pendingSlot >= 0)
                        {
                            const int slot     = ps.pendingSlot;
                            ps.pendingSlot     = -1;
                            loadPatternAudio (vi, slot);
                        }
                        else if (ps.mode == 1 && ps.listLength > 0 && isNewCycle)
                        {
                            ps.currentRepeat++;
                            if (ps.currentRepeat >= ps.loopCount[ps.currentEntry])
                            {
                                ps.currentRepeat = 0;
                                ps.currentEntry  = (ps.currentEntry + 1) % ps.listLength;
                                // resetPos=false: the advance fires at pos==0 so the new
                                // pattern starts from step 1 naturally.  Resetting here
                                // creates a second pos==0 event → immediate re-advance.
                                loadPatternAudio (vi, ps.list[ps.currentEntry], false);
                            }
                        }
                    }
                }
            }

            // ── Turing Machine ────────────────────────────────────────────────────
            {
                auto& tm = turingMachine;
                if (tm.writeEnabled && vi == tmTargetVoice.load())
                {
                    const int   len    = juce::jlimit (2, 16, tm.length);
                    const auto  maxInt = (1u << len) - 1u;

                    // 1. Read current register → voltage for this step
                    uint32_t regInt = 0;
                    for (int b = 0; b < len; ++b)
                        if (tm.bits[b]) regInt |= (1u << b);
                    const float norm    = (maxInt > 0) ? (float)regInt / (float)maxInt : 0.5f;
                    const float voltage = norm * 10.0f - 5.0f;

                    // 2. Overwrite live voice step
                    vp.stepVoltages[newStep] = voltage;
                    if (tm.affectGates)
                        vp.stepGates[newStep] = tm.bits[0];

                    // 3. Shift register: MSB wraps to LSB with lock-weighted mutation
                    const bool msb      = tm.bits[len - 1];
                    for (int b = len - 1; b > 0; --b)
                        tm.bits[b] = tm.bits[b - 1];
                    const bool keepMsb  = juce::Random::getSystemRandom().nextFloat() < tm.lockAmount;
                    tm.bits[0] = keepMsb ? msb
                                         : (juce::Random::getSystemRandom().nextFloat() < 0.5f);

                    // 4. Simulate next `len` locked shifts → preview arrays (no mutation)
                    {
                        bool sim[16] = {};
                        for (int b = 0; b < 16; ++b) sim[b] = tm.bits[b];
                        for (int step = 0; step < 16; ++step)
                        {
                            uint32_t si = 0;
                            for (int b = 0; b < len; ++b)
                                if (sim[b]) si |= (1u << b);
                            const float sn = (maxInt > 0) ? (float)si / (float)maxInt : 0.5f;
                            tm.previewVoltages[step] = sn * 10.0f - 5.0f;
                            tm.previewGates   [step] = sim[0];
                            const bool smb = sim[len - 1];
                            for (int b = len - 1; b > 0; --b) sim[b] = sim[b - 1];
                            sim[0] = smb;
                        }
                    }
                    tm.displayDirty.store (true);
                }
            }

            vs.midiStepFired = true;
            // Per-step probability gate
            const float _prob = vp.stepProbability[vp.currentStep];
            const bool gateWithProb = vp.stepGates[vp.currentStep] &&
                (_prob >= 99.9f || juce::Random::getSystemRandom().nextFloat() * 100.f < _prob);
            vs.midiStepGate  = gateWithProb;

            // Bernoulli delay gate — roll the dice when a gate fires
            if (gateWithProb && fx[vi].delayOn)
            {
                const float dp = fx[vi].delayProb;
                if (dp >= 0.999f || juce::Random::getSystemRandom().nextFloat() < dp)
                    fxs[vi].bernGateTrig = true;
                // else: gate does not feed delay this step
            }
            vs.accentActive  = gateWithProb && vp.stepAccents[vp.currentStep];
            vs.midiStepVel   = (juce::uint8) juce::jlimit (1, 127,
                                   (int)vp.stepVelocity[vp.currentStep]);
            // Update Plaits note + trigger when a new step fires
            if (vp.plaitsEnabled && plaitsState[vi].initialized)
            {
                auto& ps = plaitsState[vi];
                // Store raw voltage + combined octave shift so the render block
                // can recompute baseNote live when an LFO modulates RANGE.
                ps.stepVoltage  = vp.stepVoltages[vp.currentStep];
                ps.stepOctShift = vp.stepOctave[vp.currentStep] * 12 + vp.plaitsOctave * 12;
                // baseNote: quantised MIDI note using current rangeVCA (no LFO yet)
                ps.baseNote = (float)juce::jlimit (0, 127,
                    voltageToMidiNote (vp, ps.stepVoltage) + ps.stepOctShift);

                // Snap smoothedNote immediately when no glide is active on this step
                const bool doGlide = vp.portamentoTime > 0.001f && vp.stepGlides[vp.currentStep];
                if (!doGlide)
                    ps.smoothedNote = ps.baseNote;

                ps.patch.engine   = vp.plaitsEngine;
                ps.patch.harmonics = vp.plaitsHarmonics;
                ps.patch.timbre    = vp.plaitsTimbre;
                ps.patch.morph     = vp.plaitsMorph;
                if (gateWithProb)
                    ps.triggerPending = true;
            }
            vs.midiStepNote  = vs.midiStepGate
                               ? juce::jlimit (0, 127, voltageToMidiNote (vp, vp.stepVoltages[vp.currentStep])
                                               + vp.stepOctave[vp.currentStep] * 12)
                               : -1;
            vs.midiStepTied  = vp.stepTied[vp.currentStep];

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

                // MIDI poly register: parallel shift, capture evicted note
                vs.midiPolyEvict     = (vs.midiPolyNotes[nSlots - 1] >= 0);
                vs.midiPolyEvictNote = vs.midiPolyNotes[nSlots - 1];
                for (int si = nSlots - 1; si > 0; --si)
                    vs.midiPolyNotes[si] = vs.midiPolyNotes[si - 1];
                vs.midiPolyNotes[0] = vs.midiStepNote;   // -1 on gate-off

                vs.slots[0].assignedVoltage = vp.stepVoltages[vp.currentStep];
                if (gateWithProb)
                {
                    float baseFreq = voltageToQuantizedFreq (vp, vp.stepVoltages[vp.currentStep]);
                    baseFreq *= std::pow (2.0f, (float)vp.stepOctave[vp.currentStep]);
                    const bool doGlide = (vp.portamentoTime > 0.001f && vp.stepGlides[vp.currentStep]);

                    // After the register shift, slot[0] still holds its previous freq —
                    // set only the TARGET so the per-sample IIR portamento eases to it.
                    vs.slots[0].targetFreq1 = baseFreq * (float)std::pow (2.0, (double)vp.osc1Octave);
                    vs.slots[0].targetFreq2 = baseFreq * (float)std::pow (2.0, (double)vp.osc2Octave);
                    vs.slots[0].glideActive = doGlide;

                    if (!doGlide)
                    {
                        vs.slots[0].currentFreq1 = vs.slots[0].targetFreq1;
                        vs.slots[0].currentFreq2 = vs.slots[0].targetFreq2;
                        vs.slots[0].osc1PhaseInc = vs.slots[0].currentFreq1 / currentSampleRate;
                        vs.slots[0].osc2PhaseInc = vs.slots[0].currentFreq2 / currentSampleRate;
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
                }
                else
                {
                    vs.slots[0].glideActive = false;
                    vs.midiStepGlide = false;
                    vs.adsr.noteOff();
                    vs.filterEnv.noteOff();
                    if (!vp.modEnv.clockSync)
                        vs.modEnvAdsr.noteOff();
                }

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
                if (gateWithProb)
                {
                    float baseFreq = voltageToQuantizedFreq (vp, vp.stepVoltages[vp.currentStep]);
                    baseFreq *= std::pow (2.0f, (float)vp.stepOctave[vp.currentStep]);
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

                // Retrigger Plaits internal LPG on ratchet sub-steps (TRIG mode only)
                if (vp.plaitsEnabled && plaitsState[vi].initialized && vp.plaitsTrigMode)
                {
                    plaitsState[vi].triggerPending = true;
                    plaitsState[vi].frameIdx = PlaitsState::kBufSize; // force immediate re-render
                }
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

    // ── Unified audio-rate modulation accumulators ───────────────────────────
    // Every source (LFOs, macros, mod-env) adds into these via applyMod().
    float pwmMod      = 0.0f;
    float cutoffMod   = 0.0f;
    float pitchMod    = 1.0f;     // frequency ratio (multiplicative)
    float rangeMod    = 0.0f;
    float fmMod       = 0.0f;
    float harmMod     = 0.0f;
    float timbMod     = 0.0f;
    float morphMod    = 0.0f;
    float delayModAcc = 0.0f;
    float resoMod     = 0.0f;     // added to filter resonance boost
    float driveMod    = 0.0f;     // added to filter drive
    float mixModAcc   = 0.0f;     // added to delay wet/dry mix
    float ampMod      = 0.0f;     // added to amp gain (1 + mod)
    constexpr float maxDelayMs = 250.0f;   // full-depth → Delay-Time swing (±ms)

    // Single apply switch shared by every modulation source. `c` is the signed
    // contribution (source value × route depth); target is a MacroTarget.
    auto applyMod = [&](float c, int target)
    {
        switch (target)
        {
            case MT_PWM:       pwmMod      += c * 0.4f;                  break;
            case MT_Cutoff:    cutoffMod   += c * 4000.0f;              break;
            case MT_Pitch:     pitchMod    *= std::pow (2.0f, c / 12.0f); break;
            case MT_Range:     rangeMod    += c;                        break;
            case MT_FM:        fmMod       += c;                        break;
            case MT_Harm:      harmMod     += c;                        break;
            case MT_Timbre:    timbMod     += c;                        break;
            case MT_Morph:     morphMod    += c;                        break;
            case MT_Delay:     delayModAcc += c * maxDelayMs;           break;
            case MT_Resonance: resoMod     += c;                        break;
            case MT_Drive:     driveMod    += c;                        break;
            case MT_DelayMix:  mixModAcc   += c;                        break;
            case MT_AmpLevel:  ampMod      += c;                        break;
            default: break;
        }
    };

    // ── LFO routing ───────────────────────────────────────────────────────────
    // Each LFO's value (already scaled by its master depth) fans out across its
    // assignment list, each route applying a further signed depth.
    {
        const float lfoVals[4] = { lfo1Val, lfo2Val, lfo3Val, lfo4Val };
        for (int li = 0; li < 4; ++li)
        {
            const auto& rt = vp.lfoRouting[li];
            const int   n  = rt.count.load();
            for (int a = 0; a < n && a < kMaxModRoutes; ++a)
                applyMod (lfoVals[li] * rt.routes[a].depth, rt.routes[a].target);
        }
    }

    // ── Macro routing ──────────────────────────────────────────────────────────
    for (int m = 0; m < numMacros; ++m)
    {
        const float mv = macros[m].value.load();
        const int   n  = macros[m].count.load();
        for (int a = 0; a < n && a < kMaxMacroAssign; ++a)
        {
            const auto& as = macros[m].assign[a];
            if (! macroScopeHitsVoice (as.scope, vi)) continue;
            applyMod (mv * as.depth, as.target);
        }
    }

    // Hand the (block-rate) delay-time + delay-mix modulation off to the FX stage.
    lfoDelayMod[vi]      = delayModAcc;
    macroDelayMixMod[vi] = mixModAcc;

    vs.pulseWidth = juce::jlimit (0.05f, 0.95f, vp.osc1PulseWidth + pwmMod);

    //--------------------------------------------------------------------------
    // MOD ENVELOPE — routes through the same accumulators as LFOs/macros.
    //--------------------------------------------------------------------------
    const bool gateOn   = running && vp.stepGates[vp.currentStep];
    const float modEnvOut = processModEnv (vp.modEnv, vs, gateOn, effectiveBPM)
                            * vp.modEnv.depth;
    {
        const auto& rt = vp.modEnvRouting;
        const int   n  = rt.count.load();
        for (int a = 0; a < n && a < kMaxModRoutes; ++a)
            applyMod (modEnvOut * rt.routes[a].depth, rt.routes[a].target);
    }

    const float effectiveFMDepth = juce::jlimit (0.0f, 1.0f, vp.fmDepth + fmMod);

    const float totalRangeMod = rangeMod;
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
    const float accentEnvAmt = vp.filterEnvAmount + (vs.accentActive ? 0.25f : 0.0f);
    // Env depth: up to 8 octaves at full amount so a low base cutoff can sweep
    // the full audible range (4 octaves felt ~30% of expected — beta feedback).
    float effectiveCut = vp.filterCutoff
                         * std::pow (2.0f, juce::jlimit (0.0f, 1.0f, accentEnvAmt) * 8.0f * fEnvSample);
    effectiveCut = juce::jlimit (20.0f, 20000.0f, effectiveCut + cutoffMod);

    //--------------------------------------------------------------------------
    // PER-SLOT OSCILLATORS → sum to mono (with stereo pan weights)
    //--------------------------------------------------------------------------
    const float fmRatioVal = std::max (0.0f, vp.fmRatio);
    const float maxDriftCents = 4.0f;

    float sumL = 0.0f, sumR = 0.0f;
    float primaryOsc1Raw = 0.0f;  // slot 0 for scope + cross-mod

    if (vp.plaitsEnabled && plaitsState[vi].initialized)
    {
        // ── Plaits block render ──────────────────────────────────────────
        auto& ps = plaitsState[vi];

        // Range modulation: recompute baseNote live so LFO→Range produces the
        // same pitch-scaling effect it does on the native oscillators.
        if (std::abs (totalRangeMod) > 0.001f)
        {
            const float effRange = juce::jlimit (0.0f, 2.0f, vp.rangeVCA + totalRangeMod);
            ps.baseNote = (float)juce::jlimit (0, 127,
                voltageToMidiNote (vp, ps.stepVoltage, effRange) + ps.stepOctShift);
        }

        // Portamento: IIR-smooth toward baseNote when glide is active
        if (vs.slots[0].glideActive)
            ps.smoothedNote = ps.smoothedNote * glideCoeff
                            + ps.baseNote     * (1.0f - glideCoeff);
        else
            ps.smoothedNote = ps.baseNote;

        if (ps.frameIdx >= PlaitsState::kBufSize)
        {
            // Apply pitch LFO: pitchMod is a frequency ratio — convert to semitones
            const float pitchLFOSemitones = std::log2 (pitchMod) * 12.0f;
            ps.patch.note = juce::jlimit (0.0f, 127.0f,
                                          ps.smoothedNote + pitchLFOSemitones);

            // Apply live timbral modulation (LFO + ModEnv) — clamped to [0,1]
            ps.patch.harmonics = juce::jlimit (0.0f, 1.0f,
                vp.plaitsHarmonics + harmMod);
            ps.patch.timbre    = juce::jlimit (0.0f, 1.0f,
                vp.plaitsTimbre    + timbMod);
            ps.patch.morph     = juce::jlimit (0.0f, 1.0f,
                vp.plaitsMorph     + morphMod
                + (vs.accentActive ? 0.20f : 0.0f));

            // Trigger mode: LPG fired by gate (pluck/drum behaviour)
            // Free mode:    LPG bypassed, ADSR shapes amplitude externally
            ps.mods.trigger_patched = vp.plaitsTrigMode;
            ps.mods.trigger         = (vp.plaitsTrigMode && ps.triggerPending) ? 1.0f : 0.0f;
            ps.patch.decay          = vp.plaitsTrigMode ? 0.5f : 0.0f;
            ps.triggerPending = false;
            ps.voice.Render (ps.patch, ps.mods, ps.frames, PlaitsState::kBufSize);
            ps.frameIdx = 0;
        }
        const float mainOut = ps.frames[ps.frameIdx].out / 32768.0f;
        const float auxOut  = ps.frames[ps.frameIdx].aux / 32768.0f;
        ++ps.frameIdx;
        const float plaitsOut = mainOut * (1.0f - vp.plaitsAuxBlend)
                              + auxOut  * vp.plaitsAuxBlend;
        sumL = plaitsOut;
        sumR = plaitsOut;
        primaryOsc1Raw = plaitsOut;
    }
    else
    {
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
    const float accentRes = (vs.accentActive ? 0.15f : 0.0f) + resoMod;
    float filtL = applyFilter (vs.ic1eq,  vs.ic2eq,  vs.ic1eq2,  vs.ic2eq2,
                               vp, sumL * norm, effectiveCut, accentRes, driveMod);
    float filtR = applyFilter (vs.ic1eqR, vs.ic2eqR, vs.ic1eq2R, vs.ic2eq2R,
                               vp, sumR * norm, effectiveCut, accentRes, driveMod);

    float envelope = vs.adsr.getNextSample();
    const float accentVCA  = vs.accentActive ? 1.4125f : 1.0f;   // +3 dB = 10^(3/20)
    const float velocityVCA = (float)vs.midiStepVel / 100.0f;    // vel 100 = unity, 127 = +2.7dB
    const float macroAmpGain = juce::jlimit (0.0f, 2.0f, 1.0f + ampMod);
    const float gain = envelope * accentVCA * velocityVCA * macroAmpGain * 0.3f;

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
    // Wrap into [0,1) for ANY increment — cross-mod/FM can make phaseInc negative
    // or larger than 1.0, which a single subtraction cannot handle.
    slot.osc1Phase -= std::floor (slot.osc1Phase);

    // Final safety net: never let a non-finite sample escape into the filter,
    // where it would permanently poison the IIR state and silence the voice.
    if (! std::isfinite (output)) output = 0.0f;
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
    slot.osc2Phase -= std::floor (slot.osc2Phase);   // wrap [0,1) for any increment
    return sA + blend * (sB - sA);
}

//==============================================================================
// TPT State Variable Filter with drive, mode, and slope
//==============================================================================
float VoltageSeq2AudioProcessor::applyFilter (float& ic1, float& ic2,
                                               float& ic1_2, float& ic2_2,
                                               const VoiceParams& vp,
                                               float input, float effectiveCutoff,
                                               float resBoost, float driveMod)
{
    // Pre-filter drive (tanh saturation)
    // Normalise by tanh(drive) so unity input always gives unity output amplitude;
    // drive only adds harmonic content, it never thins or compresses the level.
    const float driveAmt = juce::jlimit (0.0f, 1.0f, vp.filterDrive + driveMod);
    const float drive = 1.0f + driveAmt * 7.0f;   // 1x … 8x
    const float dNorm = std::tanh (drive);               // approaches 1 as drive→∞
    float driven = std::tanh (input * drive) / dNorm;

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
    float k   = juce::jlimit (0.01f, 2.0f, 2.0f - 1.99f * juce::jlimit (0.0f, 1.0f, vp.filterResonance + resBoost));

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
    // 4.8 semitones per volt → 5 V = exactly 24 semitones (2 octaves) at full range
    float rawMidi     = juce::jlimit (0.0f, 127.0f, 60.0f + (float)vp.rootNote + scaledV * 4.8f);
    int   quantized   = quantizeNoteToScale ((int)std::round (rawMidi), vp.rootNote, vp.currentScale);
    return 440.0f * std::pow (2.0f, (quantized - 69.0f) / 12.0f);
}

// voltageToMidiNote — same pitch calculation as voltageToQuantizedFreq but
// returns the MIDI note number directly (avoids a round-trip through frequency).
// rangeOverride < 0 → use vp.rangeVCA (same convention as voltageToQuantizedFreq).
int VoltageSeq2AudioProcessor::voltageToMidiNote (const VoiceParams& vp, float voltage,
                                                   float rangeOverride) const
{
    float range   = (rangeOverride >= 0.0f) ? rangeOverride : vp.rangeVCA;
    float scaledV = voltage * range;
    float rawMidi = juce::jlimit (0.0f, 127.0f, 60.0f + (float)vp.rootNote + scaledV * 4.8f);
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
bool VoltageSeq2AudioProcessor::acceptsMidi() const  { return true; }
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
        el.setAttribute ("g"   + juce::String (i), vp.stepGates   [i]);
        el.setAttribute ("sl"  + juce::String (i), vp.stepGlides  [i]);
        el.setAttribute ("acc" + juce::String (i), vp.stepAccents [i]);
        el.setAttribute ("vel" + juce::String (i), (double)vp.stepVelocity[i]);
        el.setAttribute ("ti" + juce::String (i), vp.stepTied  [i]);
        el.setAttribute ("rt" + juce::String (i), vp.stepRepeats[i]);
        el.setAttribute ("pl" + juce::String (i), vp.stepPulses [i]);
        el.setAttribute ("oc" + juce::String (i), vp.stepOctave[i]);
        el.setAttribute ("pr" + juce::String (i), (double)vp.stepProbability[i]);
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

    el.setAttribute ("voiceMode",    (int)vp.voiceMode);
    el.setAttribute ("uniCount",     vp.unisonCount);
    el.setAttribute ("uniSpread",    (double)vp.unisonSpread);
    el.setAttribute ("uniWidth",     (double)vp.unisonWidth);
    el.setAttribute ("srChordMode",  (int)vp.shiftRegChordMode);

    // Macro-style mod routing (LFOs + mod-env). New in v5; old presets without
    // these child elements migrate from the legacy single-target fields on load.
    auto saveRouting = [&el](const char* tag, const VoltageSeq2AudioProcessor::ModRouting& rt)
    {
        auto* r = el.createNewChildElement (tag);
        const int n = rt.count.load();
        r->setAttribute ("n", n);
        for (int i = 0; i < n && i < VoltageSeq2AudioProcessor::kMaxModRoutes; ++i)
        {
            auto* a = r->createNewChildElement ("R");
            a->setAttribute ("t", rt.routes[i].target);
            a->setAttribute ("d", (double)rt.routes[i].depth);
        }
    };
    saveRouting ("LFO1Rt", vp.lfoRouting[0]);
    saveRouting ("LFO2Rt", vp.lfoRouting[1]);
    saveRouting ("LFO3Rt", vp.lfoRouting[2]);
    saveRouting ("LFO4Rt", vp.lfoRouting[3]);
    saveRouting ("MEnvRt", vp.modEnvRouting);
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
        vp.stepGates   [i] = getB (("g"   + juce::String (i)).toRawUTF8(), vp.stepGates   [i]);
        vp.stepGlides  [i] = getB (("sl"  + juce::String (i)).toRawUTF8(), vp.stepGlides  [i]);
        vp.stepAccents [i] = getB (("acc" + juce::String (i)).toRawUTF8(), vp.stepAccents [i]);
        vp.stepVelocity[i] = (float)el.getDoubleAttribute (("vel" + juce::String (i)), 100.0);
        vp.stepTied    [i] = getB (("ti" + juce::String (i)).toRawUTF8(), false);
        vp.stepRepeats [i] = getI (("rt" + juce::String (i)).toRawUTF8(), 0);
        vp.stepPulses  [i] = juce::jlimit (1, 8, getI (("pl" + juce::String (i)).toRawUTF8(), 1));
        vp.stepOctave  [i] = juce::jlimit (-4, 4, getI (("oc" + juce::String (i)).toRawUTF8(), 0));
        vp.stepProbability[i] = juce::jlimit (0.f, 100.f, getF (("pr" + juce::String (i)).toRawUTF8(), 100.f));
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

    vp.voiceMode          = (VoltageSeq2AudioProcessor::VoiceParams::VoiceMode)getI ("voiceMode", 0);
    vp.unisonCount        = juce::jlimit (2, 4, getI ("uniCount", 4));
    vp.unisonSpread       = getF ("uniSpread", 0.15f);
    vp.unisonWidth        = getF ("uniWidth",  0.7f);
    vp.shiftRegChordMode  = getB ("srChordMode", false);

    // ── Macro-style mod routing ───────────────────────────────────────────────
    // Read the child elements if present (v5+). For older presets, migrate the
    // legacy single-target into a one-route list at full depth.
    auto legacyModEnvTarget = [](int dest) -> int
    {
        switch (dest)
        {
            case 0:  return VoltageSeq2AudioProcessor::MT_FM;
            case 1:  return VoltageSeq2AudioProcessor::MT_Range;
            case 2:  return VoltageSeq2AudioProcessor::MT_Cutoff;
            case 3:  return VoltageSeq2AudioProcessor::MT_Harm;
            case 4:  return VoltageSeq2AudioProcessor::MT_Timbre;
            case 5:  return VoltageSeq2AudioProcessor::MT_Morph;
            default: return VoltageSeq2AudioProcessor::MT_FM;
        }
    };
    auto loadRouting = [&el](const char* tag, VoltageSeq2AudioProcessor::ModRouting& rt, int legacyTarget)
    {
        rt.count.store (0);
        if (auto* r = el.getChildByName (tag))
        {
            const int n = juce::jlimit (0, VoltageSeq2AudioProcessor::kMaxModRoutes, r->getIntAttribute ("n", 0));
            int idx = 0;
            for (auto* a : r->getChildIterator())
            {
                if (idx >= n) break;
                rt.routes[idx].target = a->getIntAttribute    ("t", 0);
                rt.routes[idx].depth  = (float) a->getDoubleAttribute ("d", 1.0);
                ++idx;
            }
            rt.count.store (idx);
        }
        else if (legacyTarget >= 0)   // migrate old single target
        {
            rt.routes[0].target = legacyTarget;
            rt.routes[0].depth  = 1.0f;
            rt.count.store (1);
        }
    };
    loadRouting ("LFO1Rt", vp.lfoRouting[0], vp.lfoTarget);
    loadRouting ("LFO2Rt", vp.lfoRouting[1], vp.lfo2Target);
    loadRouting ("LFO3Rt", vp.lfoRouting[2], vp.lfo3Target);
    loadRouting ("LFO4Rt", vp.lfoRouting[3], vp.lfo4Target);
    loadRouting ("MEnvRt", vp.modEnvRouting, legacyModEnvTarget (vp.modEnv.dest));
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
        p.stepVoltages   [i] = v.stepVoltages   [i];
        p.stepVelocity   [i] = v.stepVelocity   [i];
        p.stepGates      [i] = v.stepGates      [i];
        p.stepGlides     [i] = v.stepGlides     [i];
        p.stepAccents    [i] = v.stepAccents    [i];
        p.stepTied       [i] = v.stepTied       [i];
        p.stepRepeats    [i] = v.stepRepeats    [i];
        p.stepPulses     [i] = v.stepPulses     [i];
        p.stepOctave     [i] = v.stepOctave     [i];
        p.stepProbability[i] = v.stepProbability[i];
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
        v.stepVoltages   [i] = p.stepVoltages   [i];
        v.stepVelocity   [i] = p.stepVelocity   [i];
        v.stepGates      [i] = p.stepGates      [i];
        v.stepGlides     [i] = p.stepGlides     [i];
        v.stepAccents    [i] = p.stepAccents    [i];
        v.stepTied       [i] = p.stepTied       [i];
        v.stepRepeats    [i] = p.stepRepeats    [i];
        v.stepPulses     [i] = p.stepPulses     [i];
        v.stepOctave     [i] = p.stepOctave     [i];
        v.stepProbability[i] = p.stepProbability[i];
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
    setP ("fmRatio"   + s, vp.fmRatio);
    setP ("fmDepth"   + s, vp.fmDepth);
    setP ("porta"     + s, vp.portamentoTime);
    setP ("range"     + s, vp.rangeVCA);
    setP ("plaitsHarm"  + s, vp.plaitsHarmonics);
    setP ("plaitsTimb"  + s, vp.plaitsTimbre);
    setP ("plaitsMorph" + s, vp.plaitsMorph);
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
        voiceEl->setAttribute ("plaitsOn",   (int)voice[vi].plaitsEnabled);
        voiceEl->setAttribute ("plaitsEng",  voice[vi].plaitsEngine);
        voiceEl->setAttribute ("plaitsHarm", (double)voice[vi].plaitsHarmonics);
        voiceEl->setAttribute ("plaitsTimb", (double)voice[vi].plaitsTimbre);
        voiceEl->setAttribute ("plaitsMrph", (double)voice[vi].plaitsMorph);
        voiceEl->setAttribute ("plaitsAux",  (double)voice[vi].plaitsAuxBlend);
        voiceEl->setAttribute ("plaitsTrig", (int)voice[vi].plaitsTrigMode);
        voiceEl->setAttribute ("plaitsOct",  voice[vi].plaitsOctave);
        voiceEl->setAttribute ("psMode",      patSeq[vi].mode);
        voiceEl->setAttribute ("psImmediate", (int)patSeq[vi].immediate);
        voiceEl->setAttribute ("psLen",       patSeq[vi].listLength);
        for (int i = 0; i < 16; ++i) {
            voiceEl->setAttribute ("psList"  + juce::String(i), patSeq[vi].list[i]);
            voiceEl->setAttribute ("psLoop"  + juce::String(i), patSeq[vi].loopCount[i]);
        }
    }

    // Turing Machine (single shared instance, saved at root level)
    xml.setAttribute ("tmWrite",       (int)turingMachine.writeEnabled);
    xml.setAttribute ("tmLock",        (double)turingMachine.lockAmount);
    xml.setAttribute ("tmLen",         turingMachine.length);
    xml.setAttribute ("tmGate",        (int)turingMachine.affectGates);
    xml.setAttribute ("tmTargetVoice", tmTargetVoice.load());
    for (int i = 0; i < 16; ++i)
        xml.setAttribute ("tmBit" + juce::String(i), (int)turingMachine.bits[i]);

    // ── Macro assignments (values live in APVTS) ─────────────────────────────
    for (int m = 0; m < numMacros; ++m)
    {
        auto* macEl = xml.createNewChildElement ("Macro");
        macEl->setAttribute ("index", m);
        const int n = macros[m].count.load();
        macEl->setAttribute ("count", n);
        for (int a = 0; a < n && a < kMaxMacroAssign; ++a)
        {
            auto* asEl = macEl->createNewChildElement ("Assign");
            asEl->setAttribute ("target", macros[m].assign[a].target);
            asEl->setAttribute ("scope",  macros[m].assign[a].scope);
            asEl->setAttribute ("depth",  (double)macros[m].assign[a].depth);
        }
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
            juce::String volts, gates, glides, ties, repeats, octs, probs;
            for (int i = 0; i < numSteps; ++i)
            {
                volts   += juce::String (p.stepVoltages[i], 4) + (i < 15 ? "," : "");
                gates   += juce::String ((int)p.stepGates[i])  + (i < 15 ? "," : "");
                glides  += juce::String ((int)p.stepGlides[i]) + (i < 15 ? "," : "");
                ties    += juce::String ((int)p.stepTied[i])   + (i < 15 ? "," : "");
                repeats += juce::String (p.stepRepeats[i])     + (i < 15 ? "," : "");
                octs    += juce::String (p.stepOctave[i])      + (i < 15 ? "," : "");
                probs   += juce::String (p.stepProbability[i], 1) + (i < 15 ? "," : "");
            }
            slotEl->setAttribute ("volts",   volts);
            slotEl->setAttribute ("gates",   gates);
            slotEl->setAttribute ("glides",  glides);
            slotEl->setAttribute ("ties",    ties);
            slotEl->setAttribute ("repeats", repeats);
            slotEl->setAttribute ("octs",    octs);
            slotEl->setAttribute ("probs",   probs);
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
        fxEl->setAttribute ("delayWow",      p.delayWow);
        fxEl->setAttribute ("delayFlutter",  p.delayFlutter);
        fxEl->setAttribute ("delaySat",      p.delaySat);
        fxEl->setAttribute ("delayProb",     p.delayProb);
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
                    voice[vi].plaitsEnabled   = (bool)child->getIntAttribute    ("plaitsOn",   0);
                    voice[vi].plaitsEngine    = child->getIntAttribute           ("plaitsEng",  8);
                    voice[vi].plaitsHarmonics = (float)child->getDoubleAttribute ("plaitsHarm", 0.5);
                    voice[vi].plaitsTimbre    = (float)child->getDoubleAttribute ("plaitsTimb", 0.5);
                    voice[vi].plaitsMorph     = (float)child->getDoubleAttribute ("plaitsMrph", 0.5);
                    voice[vi].plaitsAuxBlend  = (float)child->getDoubleAttribute ("plaitsAux",  0.0);
                    voice[vi].plaitsTrigMode  = (bool)child->getIntAttribute    ("plaitsTrig", 0);
                    voice[vi].plaitsOctave    = child->getIntAttribute           ("plaitsOct",  0);
                    patSeq[vi].mode        = child->getIntAttribute   ("psMode",      0);
                    patSeq[vi].immediate   = (bool)child->getIntAttribute ("psImmediate", 0);
                    patSeq[vi].listLength  = child->getIntAttribute   ("psLen",       0);
                    for (int i = 0; i < 16; ++i) {
                        patSeq[vi].list[i]      = child->getIntAttribute ("psList"  + juce::String(i), i);
                        patSeq[vi].loopCount[i] = child->getIntAttribute ("psLoop"  + juce::String(i), 1);
                    }
                    // TM state is now root-level; skip per-voice TM on load
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
                    p.unipolar       = slotEl->getIntAttribute    ("uni",    1) != 0;
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
                    // parse octs
                    {
                        juce::StringArray toks;
                        toks.addTokens (slotEl->getStringAttribute ("octs"), ",", "");
                        for (int i = 0; i < juce::jmin (toks.size(), numSteps); ++i)
                            p.stepOctave[i] = juce::jlimit (-4, 4, toks[i].getIntValue());
                    }
                    // parse probs
                    {
                        juce::StringArray toks;
                        toks.addTokens (slotEl->getStringAttribute ("probs"), ",", "");
                        for (int i = 0; i < juce::jmin (toks.size(), numSteps); ++i)
                            p.stepProbability[i] = juce::jlimit (0.f, 100.f, (float)toks[i].getDoubleValue());
                    }
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

    // Turing Machine — single shared instance, stored at root level
    turingMachine.writeEnabled = (bool)xml->getIntAttribute    ("tmWrite",       0);
    turingMachine.lockAmount   = (float)xml->getDoubleAttribute ("tmLock",       0.75);
    turingMachine.length       = xml->getIntAttribute           ("tmLen",        16);
    turingMachine.affectGates  = (bool)xml->getIntAttribute    ("tmGate",        0);
    tmTargetVoice.store (xml->getIntAttribute                   ("tmTargetVoice", 0));
    for (int i = 0; i < 16; ++i)
        turingMachine.bits[i] = (bool)xml->getIntAttribute ("tmBit" + juce::String(i), 0);

    // ── Macro assignments ────────────────────────────────────────────────────
    for (int m = 0; m < numMacros; ++m) macros[m].count.store (0);
    for (auto* macEl : xml->getChildIterator())
    {
        if (macEl->getTagName() != "Macro") continue;
        const int m = macEl->getIntAttribute ("index", -1);
        if (m < 0 || m >= numMacros) continue;
        int a = 0;
        for (auto* asEl : macEl->getChildIterator())
        {
            if (asEl->getTagName() != "Assign" || a >= kMaxMacroAssign) continue;
            macros[m].assign[a].target = juce::jlimit (0, (int)MT_Count - 1,
                                                       asEl->getIntAttribute ("target", MT_Cutoff));
            macros[m].assign[a].scope  = juce::jlimit (0, 2, asEl->getIntAttribute ("scope", MS_Both));
            macros[m].assign[a].depth  = (float)asEl->getDoubleAttribute ("depth", 1.0);
            ++a;
        }
        macros[m].count.store (a);
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
        p.delayWow      = (float)fxEl->getDoubleAttribute("delayWow",      0.0);
        p.delayFlutter  = (float)fxEl->getDoubleAttribute("delayFlutter",  0.0);
        p.delaySat      = (float)fxEl->getDoubleAttribute("delaySat",      0.0);
        p.delayProb     = (float)fxEl->getDoubleAttribute("delayProb",     1.0);
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
void VoltageSeq2AudioProcessor::generateRandomSequence (int vi, bool doGates, bool doPitch)
{
    auto& vp  = voice[vi];
    juce::Random rng;
    const juce::String voiceSuffix = "_" + juce::String (vi);

    if (doGates)
        vp.sequenceLength = 4 + rng.nextInt (13);   // 4–16 steps

    for (int i = 0; i < 16; ++i)
    {
        const bool inLen = (i < vp.sequenceLength);

        if (doGates)
        {
            vp.stepGates   [i] = inLen && (rng.nextFloat() < 0.72f);
            vp.stepOctave  [i] = inLen
                                 ? ([&]{ float r = rng.nextFloat(); return r < 0.12f ? -1 : r > 0.88f ? 1 : 0; }())
                                 : 0;
            vp.stepRepeats [i] = (inLen && rng.nextFloat() < 0.15f) ? 1 : 0;
            vp.stepPulses  [i] = 1;
            vp.stepGlides  [i] = inLen && vp.stepGates[i] && (rng.nextFloat() < 0.20f);
            vp.stepTied    [i] = inLen && vp.stepGates[i] && (rng.nextFloat() < 0.08f);
            vp.stepProbability[i] = (inLen && rng.nextFloat() < 0.18f)
                                    ? (25.f + rng.nextFloat() * 70.f) : 100.f;
            if (!inLen) vp.stepGates[i] = false;
        }

        if (doPitch)
        {
            // Use APVTS so the slider UI updates and the value sticks
            const float rawV = vp.unipolar
                               ? rng.nextFloat() * 5.0f
                               : rng.nextFloat() * 10.0f - 5.0f;
            const juce::String key = "step" + juce::String (i) + voiceSuffix;
            if (auto* param = apvts.getParameter (key))
                param->setValueNotifyingHost (param->convertTo0to1 (rawV));
        }
    }

    if (doGates)
    {
        // Push seqLen to APVTS so UI slider updates
        const juce::String lenKey = juce::String ("seqLen") + (vi == 0 ? "A" : "B");
        if (auto* p = apvts.getParameter (lenKey))
            p->setValueNotifyingHost (p->convertTo0to1 ((float)vp.sequenceLength));
    }
}

//==============================================================================
// Bjorklund (Euclidean rhythm) — distributes k hits as evenly as possible
// across n steps.  Returns a vector<int> of length n (1 = hit, 0 = rest).
// Verified: E(3,8)=[1,0,0,1,0,0,1,0]  E(5,8)=[1,0,1,0,1,0,1,1]
//==============================================================================
void VoltageSeq2AudioProcessor::loadPatternAudio (int vi, int slot, bool resetPos)
{
    if (vi < 0 || vi >= numVoices || slot < 0 || slot >= numPatternSlots) return;
    const auto& p = patternBank[vi][slot];
    if (!p.used) return;
    auto& v = voice[vi];
    for (int i = 0; i < numSteps; ++i)
    {
        v.stepVoltages   [i] = p.stepVoltages   [i];
        v.stepVelocity   [i] = p.stepVelocity   [i];
        v.stepGates      [i] = p.stepGates      [i];
        v.stepGlides     [i] = p.stepGlides     [i];
        v.stepAccents    [i] = p.stepAccents    [i];
        v.stepTied       [i] = p.stepTied       [i];
        v.stepRepeats    [i] = p.stepRepeats    [i];
        v.stepPulses     [i] = p.stepPulses     [i];
        v.stepOctave     [i] = p.stepOctave     [i];
        v.stepProbability[i] = p.stepProbability[i];
    }
    v.sequenceLength = p.sequenceLength;
    v.clockDivision  = p.clockDivision;
    v.swingAmount    = p.swingAmount;
    v.portamentoTime = p.portamentoTime;
    v.playOrder      = p.playOrder;
    // NOTE: v.unipolar is intentionally NOT loaded here. It's a live display/edit
    // preference (UNI vs BI), not pattern data — auto-advancing the pattern
    // sequencer was reverting a user's BI choice to UNI every cycle.
    v.rootNote       = p.rootNote;
    v.currentScale   = p.currentScale;
    v.rangeVCA       = p.rangeVCA;

    // ── Immediately push APVTS atomics so processBlock reads the correct
    // values on its very next call.  param->setValue() is audio-thread safe
    // (it writes to the internal atomic via the normalised 0-1 range);
    // it does NOT dispatch host notifications, but patternChangedForUI below
    // ensures the 30-Hz timer calls syncAPVTSFromVoice on the message thread
    // so that DAW automation lanes and UI sliders update shortly after.
    {
        const juce::String s = "_" + juce::String (vi);

        // Step voltages (range -5 … +5 V)
        for (int i = 0; i < numSteps; ++i)
        {
            const juce::String key = "step" + juce::String (i) + s;
            if (auto* par = apvts.getParameter (key))
                par->setValue (par->convertTo0to1 (v.stepVoltages[i]));
        }

        // Portamento time (0 … 2 s)
        if (auto* par = apvts.getParameter ("porta" + s))
            par->setValue (par->convertTo0to1 (v.portamentoTime));

        // Output range / VCA level (0 … 1)
        if (auto* par = apvts.getParameter ("range" + s))
            par->setValue (par->convertTo0to1 (v.rangeVCA));
    }

    // resetPos=true  → restart from step 1 (manual slot click, MIDI trigger).
    // resetPos=false → keep sampleCounter running (SEQ auto-advance): the
    //   advance already fires at pos==0, so the new pattern starts naturally
    //   from step 1 without a reset; resetting here would create a *second*
    //   pos==0 event on the next block and cause the sequencer to skip ahead.
    if (resetPos)
        v.resetOnNextBlock.store (true);

    patternChangedForUI[vi].store (true);
    // Note: intentionally does NOT call syncAPVTSFromVoice — audio thread only
}

void VoltageSeq2AudioProcessor::resetTuringMachine()
{
    auto& tm  = turingMachine;
    auto& rng = juce::Random::getSystemRandom();
    for (int b = 0; b < 16; ++b)
        tm.bits[b] = rng.nextFloat() < 0.5f;
    tm.displayDirty.store (true);
}

void VoltageSeq2AudioProcessor::captureTuringMachine()
{
    // Called on the MESSAGE thread (button click).
    // Snapshots the current TM output into the target voice, then disables WRITE
    // so the captured pattern plays back without further TM mutation.
    const int vi = tmTargetVoice.load();
    auto& tm = turingMachine;
    auto& vp = voice[vi];

    // Disable WRITE immediately — capture and write are mutually exclusive
    tm.writeEnabled = false;

    const int    len    = juce::jlimit (2, 16, tm.length);
    const uint32_t maxInt = (1u << len) - 1u;

    bool sim[16] = {};
    for (int b = 0; b < 16; ++b) sim[b] = tm.bits[b];

    for (int step = 0; step < len; ++step)
    {
        uint32_t si = 0;
        for (int b = 0; b < len; ++b)
            if (sim[b]) si |= (1u << b);
        const float norm = (maxInt > 0) ? (float)si / (float)maxInt : 0.5f;
        vp.stepVoltages[step] = norm * 10.0f - 5.0f;
        if (tm.affectGates)
            vp.stepGates[step] = sim[0];
        const bool smb = sim[len - 1];
        for (int b = len - 1; b > 0; --b) sim[b] = sim[b - 1];
        sim[0] = smb;
    }
    vp.sequenceLength = len;
    syncAPVTSFromVoice (vi);
}

static std::vector<int> bjorklund (int k, int n)
{
    if (n <= 0 || k <= 0) return std::vector<int> (std::max (n, 0), 0);
    k = std::min (k, n);
    if (k == n) return std::vector<int> (n, 1);

    const int q = n / k;   // base zeros per hit-group
    const int r = n % k;   // first r groups get one extra zero

    std::vector<int> result;
    result.reserve (n);
    for (int i = 0; i < k; ++i)
    {
        result.push_back (1);
        const int zeros = q - 1 + (i < r ? 1 : 0);
        for (int j = 0; j < zeros; ++j)
            result.push_back (0);
    }
    return result;
}

//==============================================================================
void VoltageSeq2AudioProcessor::applyEuclidean (int vi, int steps, int hits, int maxRatchets)
{
    auto& vp = voice[vi];
    steps       = juce::jlimit (2, 16, steps);
    maxRatchets = juce::jlimit (1,  4, maxRatchets);
    hits        = juce::jlimit (0, steps * maxRatchets, hits);

    // Run Bjorklund on the expanded grid (steps × maxRatchets slots)
    const auto slots = bjorklund (hits, steps * maxRatchets);

    for (int i = 0; i < 16; ++i)
    {
        if (i >= steps)
        {
            vp.stepGates  [i] = false;
            vp.stepRepeats[i] = 0;
            continue;
        }

        // Count how many ratchet-slots fired for this step
        int hitCount = 0;
        for (int r = 0; r < maxRatchets; ++r)
            if (slots[i * maxRatchets + r]) ++hitCount;

        vp.stepGates  [i] = (hitCount > 0);
        vp.stepRepeats[i] = std::max (0, hitCount - 1);   // 0=×1, 1=×2, 2=×3, 3=×4
    }

    vp.sequenceLength = steps;

    // Push seqLen into APVTS so the page-1 slider updates
    const juce::String lenKey = "seqLen" + juce::String (vi == 0 ? "A" : "B");
    if (auto* p = apvts.getParameter (lenKey))
        p->setValueNotifyingHost (p->convertTo0to1 ((float)steps));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoltageSeq2AudioProcessor();
}
