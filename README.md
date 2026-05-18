# VoltageSeq2

**Dual-voice 16-step voltage sequencer with FM synthesis** — VST3 / AU plugin for macOS and Windows.

Built with JUCE 8.

![VoltageSeq2](https://img.shields.io/badge/version-3.5-blue) ![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey)

---

## Features

- **2 independent sequencer voices** — each with 16 steps, individual gate, glide and tie controls
- **Ratchets** — right-click any gate to set 1–4× repeats per step
- **Seq Nudge** — offset sequence start position per voice
- **Play modes** — Forward, Reverse, Converge, Random
- **Swing** — per-voice swing amount
- **Quantizer** — root note + scale (Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Pentatonic, Chromatic)
- **OSC 1** — Sine / Saw / Square / Triangle with PWM, feedback self-FM and analogue drift
- **OSC 2** — 4-wavetable morphing oscillator with FM ratio
- **FM synthesis** — OSC2→OSC1 FM depth, cross-modulation between voices
- **Filter** — 12/24dB SVF (LP / BP / HP) with drive and envelope
- **4× LFO** — each with sync, multiple targets and waveforms
- **Mod Envelope** — assignable ADSR with clock sync
- **Pattern Bank** — 16 saveable pattern slots per voice
- **DAW transport sync** — sequences reset to step 1 on transport start/stop

---

## Download

Head to [Releases](https://github.com/Ryanmurg-22/VoltageSeq2/releases) and grab the latest installer for your platform.

| Platform | File | Notes |
|----------|------|-------|
| macOS (Apple Silicon) | `.pkg` installer | Installs VST3 to `/Library/Audio/Plug-Ins/VST3` |
| Windows (x64) | `.zip` | Extract `.vst3` folder to `C:\Program Files\Common Files\VST3\` |

### macOS Gatekeeper
This build is unsigned. If macOS blocks the installer:
**System Settings → Privacy & Security → Open Anyway**

---

## Building from Source

Requires CMake 3.22+ and a C++17 compiler. JUCE is fetched automatically.

```bash
git clone https://github.com/Ryanmurg-22/VoltageSeq2.git
cd VoltageSeq2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## Credits

Designed and built by **Murgatroyd Instruments**.
