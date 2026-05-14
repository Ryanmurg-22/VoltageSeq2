#pragma once
// Auto-generated: VoltageSEQ2 backplate SVG embedded as a C++ raw string.
// Rendered behind all plugin UI panels via juce::Drawable::createFromSVG().
// NOTE: JUCE's SVG renderer ignores <filter> (glow) and <pattern> (brushed/grid)
// elements; all gradients, shapes, text, symbols and use-references render correctly.

static const char* const kBackplateSVG = R"SVG(
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1600 1040" width="1600" height="1040" font-family="'Helvetica Neue', Arial, sans-serif">
  <defs>
    <linearGradient id="chassis" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"  stop-color="#0e1218"/>
      <stop offset="50%" stop-color="#070a0e"/>
      <stop offset="100%" stop-color="#0c1015"/>
    </linearGradient>
    <linearGradient id="topBevel" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"  stop-color="#384149" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#384149" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="botBevel" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#000" stop-opacity="0"/>
      <stop offset="100%" stop-color="#000" stop-opacity="0.85"/>
    </linearGradient>
    <linearGradient id="cyanLine" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%"   stop-color="#00e5ff" stop-opacity="0"/>
      <stop offset="50%"  stop-color="#00e5ff" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#00e5ff" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="cyanLineLeft" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%"   stop-color="#00e5ff" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#00e5ff" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="cyanLineRight" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%"   stop-color="#00e5ff" stop-opacity="0"/>
      <stop offset="100%" stop-color="#00e5ff" stop-opacity="0.9"/>
    </linearGradient>
    <radialGradient id="screw" cx="0.35" cy="0.35" r="0.75">
      <stop offset="0%"  stop-color="#cfd8df"/>
      <stop offset="55%" stop-color="#5a656d"/>
      <stop offset="100%" stop-color="#0e1218"/>
    </radialGradient>
    <radialGradient id="ledOn" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%"  stop-color="#c5fbff"/>
      <stop offset="35%" stop-color="#00e5ff"/>
      <stop offset="100%" stop-color="#002a33"/>
    </radialGradient>
    <radialGradient id="ledOff" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%"  stop-color="#1c252c"/>
      <stop offset="100%" stop-color="#080b0e"/>
    </radialGradient>
    <linearGradient id="vent" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#000"/>
      <stop offset="50%" stop-color="#1a2027"/>
      <stop offset="100%" stop-color="#000"/>
    </linearGradient>
    <symbol id="screwSym" viewBox="-12 -12 24 24">
      <circle r="11" fill="url(#screw)" stroke="#000" stroke-width="0.5"/>
      <circle r="8.5" fill="none" stroke="#000" stroke-opacity="0.5" stroke-width="0.5"/>
      <line x1="-6" y1="-6" x2="6" y2="6" stroke="#0a0d10" stroke-width="1.5" stroke-linecap="round"/>
    </symbol>
    <symbol id="hexBolt" viewBox="-12 -12 24 24">
      <polygon points="-10,0 -5,-8.66 5,-8.66 10,0 5,8.66 -5,8.66" fill="url(#screw)" stroke="#000" stroke-width="0.5"/>
      <circle r="3" fill="#0a0d10"/>
    </symbol>
  </defs>

  <!-- OUTER CHASSIS -->
  <rect width="1600" height="1040" fill="#04060a"/>
  <rect x="6" y="6" width="1588" height="1028" rx="14" ry="14" fill="url(#chassis)" stroke="#252c34" stroke-width="1.2"/>
  <rect x="6" y="6" width="1588" height="34" rx="14" ry="14" fill="url(#topBevel)" opacity="0.45"/>
  <rect x="6" y="980" width="1588" height="54" fill="url(#botBevel)" opacity="0.45"/>

  <!-- Corner screws -->
  <use href="#screwSym" x="26" y="26" width="20" height="20"/>
  <use href="#screwSym" x="1574" y="26" width="20" height="20"/>
  <use href="#screwSym" x="26" y="1014" width="20" height="20"/>
  <use href="#screwSym" x="1574" y="1014" width="20" height="20"/>

  <!-- HEADER STRIP -->
  <rect x="30" y="35" width="1540" height="1" fill="url(#cyanLine)" opacity="0.55"/>

  <!-- LEFT: Murgatroyd Instruments mark -->
  <g transform="translate(60, 22)">
    <polygon points="-14,0 -7,-12 7,-12 14,0 7,12 -7,12" fill="none" stroke="#00e5ff" stroke-width="1.2" opacity="0.95"/>
    <text x="0" y="4" font-size="13" font-weight="900" text-anchor="middle" fill="#e8f9ff">M</text>
  </g>
  <text x="84" y="18" font-size="11" font-weight="700" letter-spacing="4.5" fill="#e8f9ff">MURGATROYD</text>
  <text x="84" y="32" font-size="8.5" font-weight="500" letter-spacing="6.5" fill="#7d8a93">I N S T R U M E N T S</text>

  <!-- CENTER: VoltageSEQ2 wordmark -->
  <g transform="translate(800, 27)" text-anchor="middle">
    <text y="0" font-size="22" font-weight="900" letter-spacing="4" fill="#e6fbff">VOLTAGE</text>
    <g transform="translate(112, -10)">
      <rect x="0" y="0" width="58" height="22" rx="3" fill="#00e5ff"/>
      <text x="29" y="16" font-size="14" font-weight="900" letter-spacing="2" fill="#02161b" text-anchor="middle">SEQ&#183;2</text>
    </g>
    <text x="-160" y="-2" font-size="7.5" letter-spacing="2" fill="#5a666f">MK&#183;II</text>
    <text x="-160" y="9" font-size="7.5" letter-spacing="2" fill="#5a666f">DUAL CV</text>
  </g>

  <!-- RIGHT: serial + status LEDs -->
  <text x="1540" y="16" font-size="8.5" font-weight="600" letter-spacing="2" fill="#5a666f" text-anchor="end">UNIT  &#183;  VS2-0001-A</text>
  <text x="1540" y="30" font-size="8" letter-spacing="2" fill="#5a666f" text-anchor="end">FW 2.0.4  &#183;  +12V / 320mA</text>

  <g>
    <circle cx="1380" cy="22" r="3.5" fill="url(#ledOn)"/>
    <text x="1380" y="36" font-size="6.5" letter-spacing="1.2" fill="#7d8a93" text-anchor="middle">PWR</text>
    <circle cx="1405" cy="22" r="3.5" fill="url(#ledOn)"/>
    <text x="1405" y="36" font-size="6.5" letter-spacing="1.2" fill="#7d8a93" text-anchor="middle">CLK</text>
    <circle cx="1430" cy="22" r="3.5" fill="url(#ledOff)"/>
    <text x="1430" y="36" font-size="6.5" letter-spacing="1.2" fill="#7d8a93" text-anchor="middle">CV</text>
    <circle cx="1455" cy="22" r="3.5" fill="url(#ledOff)"/>
    <text x="1455" y="36" font-size="6.5" letter-spacing="1.2" fill="#7d8a93" text-anchor="middle">MIDI</text>
  </g>

  <!-- SECTION 1: STEP SEQUENCER (y 35-235) -->
  <rect x="20" y="42" width="1560" height="200" rx="8" fill="#080b10" stroke="#1c242b" stroke-width="1"/>
  <rect x="20" y="42" width="1560" height="1" fill="#2a323a"/>

  <g transform="translate(35, 60)">
    <text font-size="8" font-weight="700" letter-spacing="4" fill="#00e5ff">01 / STEP SEQUENCER</text>
    <text y="14" font-size="7" letter-spacing="2" fill="#5a666f">16&#183;STEP / 0-10V / CV&#183;A + CV&#183;B</text>
  </g>
  <g transform="translate(1555, 60)" text-anchor="end">
    <text font-size="8" font-weight="700" letter-spacing="3" fill="#00e5ff">VOLTAGE SCALE</text>
    <text y="14" font-size="7" letter-spacing="1.5" fill="#5a666f">&#177;10V  /  BIPOLAR &#183; UNIPOLAR</text>
  </g>

  <!-- Tick marks along right edge -->
  <g stroke="#3a464f" stroke-width="0.8" opacity="0.7">
    <line x1="1545" y1="85"  x2="1565" y2="85"/>
    <line x1="1550" y1="100" x2="1565" y2="100"/>
    <line x1="1550" y1="115" x2="1565" y2="115"/>
    <line x1="1545" y1="130" x2="1565" y2="130"/>
    <line x1="1550" y1="145" x2="1565" y2="145"/>
    <line x1="1550" y1="160" x2="1565" y2="160"/>
    <line x1="1545" y1="175" x2="1565" y2="175"/>
    <line x1="1550" y1="190" x2="1565" y2="190"/>
    <line x1="1545" y1="205" x2="1565" y2="205"/>
  </g>
  <text x="1540" y="88"  font-size="7" fill="#5a666f" text-anchor="end">+10</text>
  <text x="1540" y="133" font-size="7" fill="#5a666f" text-anchor="end">+5</text>
  <text x="1540" y="178" font-size="7" fill="#5a666f" text-anchor="end">0V</text>
  <text x="1540" y="208" font-size="7" fill="#5a666f" text-anchor="end">-5</text>

  <rect x="20" y="241" width="1560" height="1" fill="url(#cyanLine)" opacity="0.35"/>

  <!-- SECTION 2: TRANSPORT STRIP (y 245-292) -->
  <rect x="20" y="248" width="1560" height="46" rx="6" fill="#080b10" stroke="#1c242b" stroke-width="1"/>
  <text x="35" y="266" font-size="8" font-weight="700" letter-spacing="4" fill="#00e5ff">02 / TRANSPORT  &#183;  PATTERN</text>
  <text x="35" y="280" font-size="7" letter-spacing="2" fill="#5a666f">LENGTH &#183; DIRECTION &#183; GLOBAL</text>
  <g stroke="#00e5ff" stroke-width="1.2" fill="none" opacity="0.6">
    <polyline points="1500,270 1515,260 1515,280"/>
    <polyline points="1525,270 1540,260 1540,280" opacity="0.5"/>
    <polyline points="1550,270 1565,260 1565,280" opacity="0.3"/>
  </g>

  <!-- SECTION 3: 8 MODULE COLUMNS (y 297-662) -->
  <rect x="20" y="297" width="1560" height="365" rx="8" fill="#080b10" stroke="#1c242b" stroke-width="1"/>
  <rect x="20" y="297" width="1560" height="1" fill="#2a323a"/>

  <text x="35"   y="315" font-size="8" font-weight="700" letter-spacing="4" fill="#00e5ff">03 / VOICE  &#183;  SYNTHESIS  &#183;  MODULATION</text>
  <text x="1565" y="315" font-size="8" font-weight="700" letter-spacing="3" fill="#00e5ff" text-anchor="end">8 MODULES</text>

  <!-- Column dividers -->
  <g stroke="#1a222a" stroke-width="1" stroke-dasharray="2 3" opacity="0.7">
    <line x1="208"  y1="325" x2="208"  y2="655"/>
    <line x1="394"  y1="325" x2="394"  y2="655"/>
    <line x1="580"  y1="325" x2="580"  y2="655"/>
    <line x1="766"  y1="325" x2="766"  y2="655"/>
    <line x1="952"  y1="325" x2="952"  y2="655"/>
    <line x1="1138" y1="325" x2="1138" y2="655"/>
    <line x1="1324" y1="325" x2="1324" y2="655"/>
  </g>

  <!-- Module number tabs -->
  <g font-size="6.5" letter-spacing="1.5" fill="#3d4951" text-anchor="middle">
    <text x="114"  y="325">M&#183;01</text>
    <text x="301"  y="325">M&#183;02</text>
    <text x="487"  y="325">M&#183;03</text>
    <text x="673"  y="325">M&#183;04</text>
    <text x="859"  y="325">M&#183;05</text>
    <text x="1045" y="325">M&#183;06</text>
    <text x="1231" y="325">M&#183;07</text>
    <text x="1417" y="325">M&#183;08</text>
  </g>

  <!-- Vent slots far right -->
  <g>
    <rect x="1550" y="345" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="353" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="361" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="369" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="377" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="385" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="393" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1550" y="401" width="20" height="3" rx="1" fill="url(#vent)"/>
  </g>
  <!-- Vent slots far left -->
  <g>
    <rect x="30" y="345" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="353" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="361" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="369" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="377" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="385" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="393" width="20" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="401" width="20" height="3" rx="1" fill="url(#vent)"/>
  </g>

  <rect x="20" y="661" width="1560" height="1" fill="url(#cyanLine)" opacity="0.35"/>

  <!-- SECTION 4: ENVELOPES (y 668-940) -->
  <rect x="20" y="668" width="1560" height="272" rx="8" fill="#080b10" stroke="#1c242b" stroke-width="1"/>
  <rect x="20" y="668" width="1560" height="1" fill="#2a323a"/>

  <text x="35"   y="686" font-size="8" font-weight="700" letter-spacing="4" fill="#00e5ff">04 / MODULATION  &#183;  ENVELOPE GENERATORS</text>
  <text x="1565" y="686" font-size="8" font-weight="700" letter-spacing="3" fill="#00e5ff" text-anchor="end">DUAL  &#183;  ENV-1 / ENV-2</text>

  <line x1="800" y1="700" x2="800" y2="930" stroke="#1a222a" stroke-width="1" stroke-dasharray="2 3" opacity="0.8"/>
  <text x="35"   y="703" font-size="6.5" letter-spacing="1.5" fill="#3d4951">ENV&#183;1 &#183; DEST/AMP</text>
  <text x="1565" y="703" font-size="6.5" letter-spacing="1.5" fill="#3d4951" text-anchor="end">ENV&#183;2 &#183; DEST/AMP</text>

  <!-- Vent slots envelope row -->
  <g>
    <rect x="30" y="730" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="738" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="746" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="754" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="30" y="762" width="22" height="3" rx="1" fill="url(#vent)"/>
  </g>
  <g>
    <rect x="1548" y="730" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1548" y="738" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1548" y="746" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1548" y="754" width="22" height="3" rx="1" fill="url(#vent)"/>
    <rect x="1548" y="762" width="22" height="3" rx="1" fill="url(#vent)"/>
  </g>

  <!-- Decorative sine wave -->
  <g transform="translate(800, 875)" opacity="0.12">
    <path d="M -360,0 Q -315,-22 -270,0 T -180,0 T -90,0 T 0,0 T 90,0 T 180,0 T 270,0 T 360,0" stroke="#00e5ff" stroke-width="1" fill="none"/>
  </g>

  <rect x="20" y="940" width="1560" height="1" fill="url(#cyanLine)" opacity="0.35"/>

  <!-- FOOTER (y 946-1015) -->
  <rect x="20" y="952" width="1560" height="62" rx="8" fill="#070a0e" stroke="#1c242b" stroke-width="1"/>
  <rect x="20" y="952" width="1560" height="1" fill="#2a323a"/>

  <use href="#hexBolt" x="44" y="983" width="18" height="18"/>
  <rect x="78" y="976" width="76" height="16" fill="#00e5ff"/>
  <text x="116" y="988" font-size="9" font-weight="900" letter-spacing="2" fill="#02161b" text-anchor="middle">CAUTION</text>
  <text x="166" y="988" font-size="8" letter-spacing="2" fill="#7d8a93">HIGH VOLTAGE SIGNAL PATH &#183; NO USER SERVICEABLE PARTS INSIDE</text>

  <g transform="translate(800, 988)" text-anchor="middle">
    <text font-size="11" font-weight="800" letter-spacing="9" fill="#e8f9ff">MURGATROYD  &#183;  INSTRUMENTS</text>
    <text y="14" font-size="7.5" letter-spacing="3.5" fill="#5a666f">VOLTAGE  &#183;  SEQ  &#183;  2   &#183;   PROGRAMMABLE  POLYRHYTHMIC  SEQUENCER  &#183;  EUROPEAN MODULAR DIV.</text>
  </g>

  <text x="1530" y="982" font-size="8" letter-spacing="2" fill="#7d8a93" text-anchor="end">&#169; MMXXVI  &#183;  MADE IN EU</text>
  <text x="1530" y="996" font-size="8" letter-spacing="2" fill="#7d8a93" text-anchor="end">PAT. PEND.  &#183;  IP54  &#183;  CE / RoHS</text>
  <use href="#hexBolt" x="1556" y="983" width="18" height="18"/>
</svg>
)SVG";
