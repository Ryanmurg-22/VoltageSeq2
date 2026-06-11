# VoltageSeq UI Redesign — Layout B spec

Driven by beta feedback (Hour 1+2). Core problem: **too much visible at once**.
Decision: **Layout B** — both voices stay visible on the SYNTH page; popups are
replaced by **radio-toggle inline panels**; reclaim space; fold in fixes.

Branch: `ui-redesign` (keep `main`/v4.6 releasable until merge).

---

## Guiding principles
1. **Essentials always visible**, depth tucked behind one radio switch per voice.
2. **No popups.** The 3 popup buttons (OSC / MOD / LFO) collapse into ONE radio
   group that swaps an inline "detail" panel in the same footprint.
3. **Reclaim horizontal space** — box-style sequencer sliders; shrink
   set-and-forget controls (quantizer, clock).
4. **Consistency** — same interaction pattern for similar functions (macro-style
   assign for LFOs too).

---

## SYNTH page — per voice (A on top, B below)

### Sequencer row
- **Box-style step sliders** (value shown inside the fader) ×16 — readable, no
  look-away, stretches to fit. Reclaims the ~5× horizontal waste testers flagged.
- Gate/slide row beneath each step (as now).
- Right cluster (compact): Length (fix the "…" readout), Order, Swing, Nudge,
  Transport (RUN/STOP), Total-steps readout (fix: update live, not just on rand).

### Always-visible core (per voice)
- Filter: Cutoff, Res, Drive, Mode/Slope.
- Amp ENV: A D S R.
- OSC quick: Wave, Level, Oct (OSC 1) — with the OSC1/OSC2/ENGINE radio (below).

### Set-and-forget (demoted, compact)
- Range knob, Root, Scale, **Clock as a small knob** (not a wide dropdown).
- Target ~half current width.

### Detail panel — ONE radio group replaces all popups
Radio buttons per voice: **[ OSC ] [ LFO ] [ MOD ENV ]** → swaps the inline
detail panel (fixed footprint, no popup):
- **OSC**: OSC1 adv (PWM/FB/drift) + OSC2/FM + sub-radio **[OSC1][OSC2][ENGINE]**;
  ENGINE (Plaits) greys the native OSC controls when active.
- **LFO**: the 4 LFOs; give them the **macro-style assign** workflow.
- **MOD ENV**: the mod envelope + destination.

---

## FX page
- **Show both voices at once** — remove the A/B switch ("the space is available").
- Fill freed space with **global modulators** (LFO / S&H) and/or visual flair
  (cheap CPU now that hidden scopes are disabled).

---

## Global / cross-cutting
- **Tooltips** everywhere (`setToolTip`, shared `TooltipWindow`).
- **Preset browser** — combobox + next/prev.
- **Macro**: remove the "right-click to assign" caption (keep the menu).
- **Consistency fixes**: "VoltageSEQ" logo colour (grey vs orange); overlapping
  Mod button / save-load buttons (these die with the popups anyway).
- **Rename "Plaits"** → generic label ("Macro Osc"/"Engine") — trademark.

---

## Functional bugs (fold in)
- [x] Filter envelope depth 4→8 octaves (range felt ~30%).
- [ ] Run/Stop button not working.
- [ ] "Total" steps not updating except on randomize.
- [ ] Delay Time not synced.
- [ ] Chorus Depth clicky aliasing.
- [ ] LFO-popup-close overlay bug (removed with popups).
- [ ] Voice/Unison settings don't affect Plaits (consider moving into OSC section).

---

## Phases
1. **Foundation** — radio-toggle infrastructure (replace OSC/MOD/LFO popups with
   an inline switched panel); remove popup machinery.
2. **Sequencer** — box-style sliders + compact right cluster + readout fixes.
3. **Set-and-forget** — shrink quantizer/clock.
4. **FX page** — both voices, fill space, global modulators.
5. **Polish** — tooltips, preset browser, macro caption, consistency, rename Plaits.
6. **Bug pass** — Run/Stop, Total steps, Delay sync, Chorus.

Each phase builds + is verified visually before the next.
