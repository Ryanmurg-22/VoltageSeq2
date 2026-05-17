#pragma once
// Auto-generated: VoltageSEQ2 backplate SVG embedded as a C++ raw string.
// Rendered behind all plugin UI panels via juce::Drawable::createFromSVG().
// NOTE: JUCE's SVG renderer ignores <filter> (glow) elements;
// all gradients, shapes, text and use-references render correctly.
//
// Header strip is 40 SVG units tall out of 1300 total height.
// Mapped to the 710px render area (y=28..738), that = ~21.8px on screen,
// which fits neatly inside the 22px gap before the VOICE A sequencer strip.

static const char* const kBackplateSVG = R"SVG(
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2600 1300" width="2600" height="1300"
     font-family="'Helvetica Neue', Arial, sans-serif">
  <defs>
    <linearGradient id="headerBg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"  stop-color="#0d1218"/>
      <stop offset="60%" stop-color="#070a0e"/>
      <stop offset="100%" stop-color="#04060a"/>
    </linearGradient>
    <linearGradient id="topBevel" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"  stop-color="#3a444c" stop-opacity="0.85"/>
      <stop offset="100%" stop-color="#3a444c" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="cyanLine" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%"   stop-color="#00e5ff" stop-opacity="0"/>
      <stop offset="50%"  stop-color="#00e5ff" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#00e5ff" stop-opacity="0"/>
    </linearGradient>
    <radialGradient id="ledOn" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%"   stop-color="#c5fbff"/>
      <stop offset="35%"  stop-color="#00e5ff"/>
      <stop offset="100%" stop-color="#002a33"/>
    </radialGradient>
    <radialGradient id="ledOff" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%"   stop-color="#1c252c"/>
      <stop offset="100%" stop-color="#080b0e"/>
    </radialGradient>
  </defs>

  <!-- Full plate: pure black -->
  <rect width="2600" height="1300" fill="#000000"/>

  <!-- ── Header strip: 40 SVG units tall ─────────────────────────────────── -->
  <rect x="0" y="0" width="2600" height="40" fill="url(#headerBg)"/>
  <!-- top bevel -->
  <rect x="0" y="0" width="2600" height="2"  fill="url(#topBevel)" opacity="0.55"/>
  <!-- cyan baseline — sits right at the bottom of the header -->
  <rect x="0" y="39" width="2600" height="1" fill="url(#cyanLine)" opacity="0.7"/>

  <!-- ── LEFT: M-hex logo + wordmark ─────────────────────────────────────── -->
  <g transform="translate(40, 20)">
    <polygon points="-10,0 -5,-8.7 5,-8.7 10,0 5,8.7 -5,8.7"
             fill="none" stroke="#00e5ff" stroke-width="1.2" opacity="0.95"/>
    <polygon points="-6.5,0 -3.25,-5.6 3.25,-5.6 6.5,0 3.25,5.6 -3.25,5.6"
             fill="none" stroke="#00e5ff" stroke-width="0.4" opacity="0.45"/>
    <text x="0" y="4" font-size="10" font-weight="900" text-anchor="middle" fill="#e8f9ff">M</text>
  </g>
  <text x="60" y="23" font-size="10" font-weight="700" letter-spacing="3" fill="#e8f9ff">MURGATROYD INSTRUMENTS</text>
  <line x1="60" y1="28" x2="268" y2="28" stroke="#00e5ff" stroke-opacity="0.22" stroke-width="0.5"/>

  <!-- ── CENTER: VOLTAGE + SEQ·2 pill ────────────────────────────────────── -->
  <text x="1300" y="26" font-size="20" font-weight="900" letter-spacing="4"
        fill="#e6fbff" text-anchor="middle">VOLTAGE</text>
  <g transform="translate(1411, 11)">
    <rect x="0" y="0" width="58" height="20" rx="3" fill="#00e5ff"/>
    <text x="29" y="14" font-size="12" font-weight="900" letter-spacing="2"
          fill="#02161b" text-anchor="middle">SEQ·2</text>
  </g>

  <!-- Chevron bookends -->
  <g stroke="#00e5ff" stroke-width="1.1" fill="none">
    <polyline points="1105,20 1118,12 1118,28" opacity="0.55"/>
    <polyline points="1090,20 1103,12 1103,28" opacity="0.35"/>
    <polyline points="1075,20 1088,12 1088,28" opacity="0.18"/>
  </g>
  <g stroke="#00e5ff" stroke-width="1.1" fill="none">
    <polyline points="1487,20 1474,12 1474,28" opacity="0.55"/>
    <polyline points="1502,20 1489,12 1489,28" opacity="0.35"/>
    <polyline points="1517,20 1504,12 1504,28" opacity="0.18"/>
  </g>

  <!-- ── RIGHT: status LEDs + serial ─────────────────────────────────────── -->
  <circle cx="2200" cy="19" r="3"   fill="url(#ledOn)"/>
  <text x="2200" y="31" font-size="6" letter-spacing="1" fill="#7d8a93" text-anchor="middle">PWR</text>
  <circle cx="2228" cy="19" r="3"   fill="url(#ledOn)"/>
  <text x="2228" y="31" font-size="6" letter-spacing="1" fill="#7d8a93" text-anchor="middle">CLK</text>
  <circle cx="2256" cy="19" r="3"   fill="url(#ledOff)"/>
  <text x="2256" y="31" font-size="6" letter-spacing="1" fill="#7d8a93" text-anchor="middle">CV</text>
  <circle cx="2284" cy="19" r="3"   fill="url(#ledOff)"/>
  <text x="2284" y="31" font-size="6" letter-spacing="1" fill="#7d8a93" text-anchor="middle">MIDI</text>

  <text x="2540" y="17" font-size="8"  font-weight="600" letter-spacing="2"
        fill="#9aa7b0" text-anchor="end">UNIT · VS2-0001-A</text>
  <text x="2540" y="29" font-size="7"  letter-spacing="2"
        fill="#5a666f"  text-anchor="end">FW 2.0.4  ·  +12V / 320mA</text>
</svg>
)SVG";
