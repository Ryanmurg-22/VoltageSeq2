#pragma once
// VoltageSEQ2 backplate v15 — two-column layout fills freed zone reliably.
// JUCE SVG renderer ignores letter-spacing AND textLength, so we fill space with
// actual design elements: VoltageSEQ left + large cyan "2" pill right.
// Zone: SVG x 1950→2575  screen x ~1125→~1485

static const char* const kBackplateSVG = R"SVG(
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2600 1300" width="2600" height="1300" font-family="'Helvetica Neue', Arial, sans-serif">
  <defs>
    <linearGradient id="mintLine" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%"   stop-color="#5fc2cf" stop-opacity="0"/>
      <stop offset="50%"  stop-color="#5fc2cf" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#5fc2cf" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="mintLineL" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%"   stop-color="#5fc2cf" stop-opacity="0.8"/>
      <stop offset="100%" stop-color="#5fc2cf" stop-opacity="0"/>
    </linearGradient>
    <radialGradient id="ledOn" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%"  stop-color="#daf6fa"/>
      <stop offset="35%" stop-color="#5fc2cf"/>
      <stop offset="100%" stop-color="#0a2f33"/>
    </radialGradient>
    <radialGradient id="ledOff" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%"  stop-color="#15131e"/>
      <stop offset="100%" stop-color="#08070c"/>
    </radialGradient>
    <radialGradient id="brandHalo" cx="0.5" cy="0.5" r="0.6">
      <stop offset="0%"   stop-color="#5fc2cf" stop-opacity="0.14"/>
      <stop offset="60%"  stop-color="#5fc2cf" stop-opacity="0.03"/>
      <stop offset="100%" stop-color="#5fc2cf" stop-opacity="0"/>
    </radialGradient>
    <!-- Square-wave S gradient: white on outer legs, mint on the crossbar -->
    <linearGradient id="sWave" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"   stop-color="#e6fafc"/>
      <stop offset="45%"  stop-color="#5fc2cf"/>
      <stop offset="55%"  stop-color="#5fc2cf"/>
      <stop offset="100%" stop-color="#e6fafc"/>
    </linearGradient>
  </defs>

  <!-- BASE -->
  <rect width="2600" height="1300" fill="#0e0b1c"/>

  <!-- TOP HEADER -->
  <ellipse cx="1300" cy="46" rx="300" ry="20" fill="#5fc2cf" opacity="0.08"/>
  <g transform="translate(1135, 46)">
    <polygon points="-14,0 -7,-12.1 7,-12.1 14,0 7,12.1 -7,12.1"
             fill="none" stroke="#5fc2cf" stroke-width="1.4" opacity="0.95"/>
    <text x="0" y="5" font-size="14" font-weight="900" text-anchor="middle" fill="#e6fafc">M</text>
  </g>
  <text x="1162" y="52" font-size="18" font-weight="800" fill="#e6fafc">MURGATROYD INSTRUMENTS</text>
  <rect x="790"  y="46" width="310" height="1" fill="url(#mintLine)" opacity="0.45"/>
  <rect x="1545" y="46" width="310" height="1" fill="url(#mintLine)" opacity="0.45"/>

  <!-- TOP-RIGHT STATUS -->
  <circle cx="2440" cy="42" r="5" fill="url(#ledOn)"/>
  <circle cx="2465" cy="42" r="5" fill="url(#ledOn)"/>
  <circle cx="2490" cy="42" r="5" fill="url(#ledOff)"/>
  <circle cx="2515" cy="42" r="5" fill="url(#ledOff)"/>
  <text x="2540" y="46" font-size="12" font-weight="600" fill="#8a96a0" text-anchor="end">UNIT  ·  VS2-0001-A</text>
  <text x="2540" y="62" font-size="10" fill="#5a666f" text-anchor="end">FW 4.3   ·   +12V / 320 mA</text>

  <!-- ================================================================
       NAMEPLATE — two-column design fills full freed zone
       Left column  (x 1960–2350): Manufacturer + VoltageSEQ wordmark
       Right column (x 2370–2570): Large "2" pill + unit info
       Full-width rows at top and bottom
       ================================================================ -->

  <!-- Composition shifted +100 SVG units right vs v15 to balance in freed zone -->
  <ellipse cx="2340" cy="950" rx="380" ry="275" fill="url(#brandHalo)"/>

  <!-- Top hairline -->
  <rect x="2060" y="672" width="510" height="1.5" fill="url(#mintLine)" opacity="0.6"/>

  <!-- ── LEFT COLUMN ── -->

  <!-- Manufacturer row  y=715 -->
  <g transform="translate(2082, 715)">
    <polygon points="-16,0 -8,-13.9 8,-13.9 16,0 8,13.9 -8,13.9"
             fill="none" stroke="#5fc2cf" stroke-width="1.4" opacity="0.95"/>
    <polygon points="-10,0 -5,-8.7 5,-8.7 10,0 5,8.7 -5,8.7"
             fill="none" stroke="#5fc2cf" stroke-width="0.5" opacity="0.35"/>
    <text x="0" y="5" font-size="15" font-weight="900" text-anchor="middle" fill="#e6fafc">M</text>
  </g>
  <text x="2108" y="708" font-size="18" font-weight="800" fill="#e6fafc">MURGATROYD INSTRUMENTS</text>
  <text x="2108" y="728" font-size="9.5" font-weight="500" fill="#5a666f">MANUFACTURER  ·  EUROPEAN MODULAR DIVISION</text>

  <!-- Divider  y=752 -->
  <rect x="2060" y="752" width="510" height="1" fill="url(#mintLine)" opacity="0.28"/>

  <!-- PRODUCT caption  y=778 -->
  <text x="2108" y="778" font-size="10" font-weight="700" fill="#5fc2cf">· PRODUCT ·</text>

  <!-- VoltageSEQ logo — "VOLTAGE" all-caps + square-wave S + "EQ"
       VOLTAGE: all-caps Helvetica Neue Heavy — bolder, more designed feel than mixed-case.
       S: stepped square-wave path with mint crossbar accent.
       EQ: matches VOLTAGE weight/size.
       V1 pill: version badge replaces old "2".
  -->

  <!-- "VOLTAGE" — all-caps, heavy weight -->
  <text x="2048" y="875" font-size="64" font-weight="900"
        font-family="'Helvetica Neue', 'Arial Black', Arial, sans-serif"
        fill="#5fc2cf" opacity="0.18">VOLTAGE</text>
  <text x="2048" y="875" font-size="64" font-weight="900"
        font-family="'Helvetica Neue', 'Arial Black', Arial, sans-serif"
        fill="#e6fafc">VOLTAGE</text>

  <!-- Square-wave S
       "VOLTAGE" at 64pt ≈ 298 units wide → ends ~x=2346.
       Path sits in the natural gap between VOLTAGE and EQ.
       x_l=2350  x_r=2386  y_top=831  y_mid=853  y_bot=876
  -->
  <!-- Glow -->
  <path d="M 2386,831 H 2350 V 853 H 2386 V 876 H 2350"
        stroke="#5fc2cf" stroke-width="16" fill="none" opacity="0.13"
        stroke-linecap="round" stroke-linejoin="round"/>
  <!-- Main S — gradient white→mint→white -->
  <path d="M 2386,831 H 2350 V 853 H 2386 V 876 H 2350"
        stroke="url(#sWave)" stroke-width="7" fill="none"
        stroke-linecap="round" stroke-linejoin="round"/>

  <!-- "EQ" — all-caps, matches VOLTAGE -->
  <text x="2394" y="875" font-size="64" font-weight="900"
        font-family="'Helvetica Neue', 'Arial Black', Arial, sans-serif"
        fill="#5fc2cf" opacity="0.18">EQ</text>
  <text x="2394" y="875" font-size="64" font-weight="900"
        font-family="'Helvetica Neue', 'Arial Black', Arial, sans-serif"
        fill="#e6fafc">EQ</text>

  <!-- Tagline  y=910 -->
  <text x="2065" y="910" font-size="9.5" fill="#7d8a93">POLYRHYTHMIC  ·  DUAL VOICE  ·  CV-A + CV-B  ·  16 STEPS</text>

  <!-- ── RIGHT COLUMN: "2" pill anchored to right edge ── -->

  <!-- V1 version pill — compact badge, sits after EQ text -->
  <rect x="2490" y="843" width="42" height="40" rx="5" fill="#5fc2cf" opacity="0.92"/>
  <text x="2511" y="869" font-size="17" font-weight="900"
        fill="#071518" text-anchor="middle">V1</text>

  <!-- Unit info below pill -->
  <text x="2511" y="900" font-size="8.5" fill="#5a666f" text-anchor="middle">VS1-0001-A</text>

  <!-- Mid hairline (ctrl strip boundary) -->
  <rect x="2060" y="948" width="510" height="1" fill="url(#mintLine)" opacity="0.25"/>

  <!-- ── LOWER SECTION (ctrl strip B) ── -->

  <!-- PROUDLY MADE IN SOUTH AFRICA centred in zone -->
  <text x="2315" y="1010" font-size="21" font-weight="800" fill="#5fc2cf" text-anchor="middle">PROUDLY MADE IN SOUTH AFRICA</text>

  <!-- Sub-banner -->
  <text x="2315" y="1044" font-size="10.5" font-weight="500" fill="#5a666f" text-anchor="middle">ELECTRONIC  ·  INSTRUMENT  ·  DESIGN</text>

  <!-- Lower hairline -->
  <rect x="2060" y="1078" width="510" height="1" fill="url(#mintLine)" opacity="0.28"/>

  <!-- Technical footer rows -->
  <text x="2068" y="1105" font-size="9" font-weight="600" fill="#5a666f">MK · II  ·  SERIES 2  ·  © MMXXVI  ·  MADE IN EU</text>
  <text x="2562" y="1105" font-size="9" fill="#5a666f" text-anchor="end">VS2-REV-C</text>

  <text x="2068" y="1135" font-size="8.5" fill="#313840">AUDIO OUT: 2× STEREO TRS  ·  CV OUT: 2× 0–10V  ·  GATE: 5V TTL  ·  USB MIDI  ·  VST3/AU</text>

  <!-- Bottom hairline -->
  <rect x="2060" y="1165" width="510" height="1" fill="url(#mintLine)" opacity="0.18"/>

</svg>
)SVG";
