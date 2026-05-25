// PlaitsDSP.cpp — physical modelling + speech synthesis
// Compiled with -DTEST to strip STM32-specific attributes.
#include "plaits/dsp/physical_modelling/modal_voice.cc"
#include "plaits/dsp/physical_modelling/resonator.cc"
#include "plaits/dsp/physical_modelling/string.cc"
#include "plaits/dsp/physical_modelling/string_voice.cc"
#include "plaits/dsp/speech/lpc_speech_synth.cc"
#include "plaits/dsp/speech/lpc_speech_synth_controller.cc"
#include "plaits/dsp/speech/lpc_speech_synth_phonemes.cc"
#include "plaits/dsp/speech/lpc_speech_synth_words.cc"
#include "plaits/dsp/speech/naive_speech_synth.cc"
#include "plaits/dsp/speech/sam_speech_synth.cc"
