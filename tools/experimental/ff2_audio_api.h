#pragma once
#include "core_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
unsigned wiiz_core_music_steps(WiizCore*);
unsigned wiiz_core_effect_steps(WiizCore*);
int wiiz_core_sound_clock(WiizCore*,unsigned music_steps,unsigned effect_steps);
typedef void (*FfAudioObserver)(void*,unsigned frame,unsigned bank8k,unsigned pc,unsigned addr,unsigned value);
void wiiz_core_audio_observer(WiizCore*,FfAudioObserver,void*);
#ifdef __cplusplus
}
#endif
