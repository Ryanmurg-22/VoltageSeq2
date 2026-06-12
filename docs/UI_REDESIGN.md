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

### OSC section — ALWAYS visible, OSC1/OSC2 toggle (per voice)
- The full OSC controls live on the front panel (no popup). A radio
  **[ OSC 1 ] [ OSC 2 ]** toggles *which oscillator's* params are shown in the
  OSC footprint (wave, level, oct, PWM/FB/drift, pos, FM, etc.).
- An **ENGINE** (Plaits) toggle replaces the native OSC controls with the engine
  controls and **greys out** the native OSC radio while active.

### Filter section — hero Cutoff
- **Cutoff = significantly larger "hero" knob** (most-reached control, visual
  anchor of the section). Res / Drive / Mode / Slope around it.

### Envelopes column (always visible)
- **AMP ADSR** on top, **FILTER ADSR** directly below (shared "shapers" column).
- **ENV AMT** (filter-env depth) knob with the filter envelope (or beside Cutoff
  — TBD on first render). Now scaled to 8 octaves so it has real range.

### Knob sizing
- **All knobs slightly larger** than current for approachability.
- **Filter Cutoff notably larger** than the rest.

### Set-and-forget (demoted, compact)
- Range knob, **Root / Scale compact**, **Clock as a small knob** (not a wide
  dropdown). Target ~half current width.

### Modulation slot — [ LFO | MOD ENV ] radio (per voice)
One inline slot toggled by a radio (replaces the LFO + MOD-ENV popups):
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
- [x] Default both voices to UNIPOLAR (was inconsistent: struct default bipolar,
      ctor unipolar, RESET bipolar). Now UNI everywhere (struct + ctor + RESET).
- [x] Random pitch range correlates with UNI/BIPOLAR (already did: UNI→0..5 V,
      BI→-5..+5 V; the visible "only positive" was just the unipolar default).
- [ ] Run/Stop button not working.
- [ ] "Total" steps not updating except on randomize.
- [ ] Delay Time not synced.
- [ ] Chorus Depth clicky aliasing.
- [ ] LFO-popup-close overlay bug (removed with popups).
- [ ] Voice/Unison settings don't affect Plaits (consider moving into OSC section).

---

## Phases
1. **Foundation** — radio-toggle infrastructure. ✅ OSC inline (OSC1/OSC2/ENGINE),
   MOD + LFO popups killed → inline `[LFO | MOD ENV]` slot (LFO 1-4 selector).
   All three section popups now inline; popup open/close fns remain but unused.
2. **Sequencer** — box-style sliders ✅ + compact right cluster ✅ (TOOLS reveal,
   QUANT↔ORDER radio) + taller lanes ✅. Readout fixes still pending (Total steps).
3. **Set-and-forget** — RANGE→performative top slider ✅, ROOT/SCALE/CLOCK moved to
   the pattern section's QUANT slot ✅. (Top = pattern, bottom = synth split.)
4. **FX page** — both voices, fill space, global modulators. ⏳ not started.
5. **Polish** — tooltips, preset browser, macro caption, consistency, rename Plaits. ⏳
6. **Bug pass** — Run/Stop, Total steps, Delay sync, Chorus. ⏳ (unipolar default ✅).

### Structural summary (current state)
- **TOP = the pattern**: step lanes (taller now) + TOTAL/STAGES, then performance
  cluster (RANGE/LENGTH/SWING/transport/VELO), QUANT↔ORDER radio slot, TOOLS column.
- **BOTTOM = the synth**: GLIDE(porta) · OSC (OSC1/OSC2/ENGINE) · FILTER (hero Cutoff)
  · ENVELOPES column (Amp ADSR over Filter ADSR) · `[LFO | MOD ENV]` modulation slot
  · branding (A) / macros (B).

### Known rough edges to review
- Modulation slot's right edge (~1084) may slightly underlap the VOLTAGESEQ logo on
  Voice A's row (Voice B is clear — macros start at 1090). Narrow slot or move logo.
- Filter panel's bottom row is now empty (ADSR moved out) — could tighten the panel.
- Modulation slot inner labels are minimal (LFO view relies on the dropdowns/selector).
