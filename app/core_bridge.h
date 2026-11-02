#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiizCore WiizCore;
enum { PAD_A=1, PAD_B=2, PAD_SELECT=4, PAD_START=8, PAD_UP=16, PAD_DOWN=32, PAD_LEFT=64, PAD_RIGHT=128 };
WiizCore* wiiz_core_new(uint8_t* rom, size_t size);
void wiiz_core_delete(WiizCore*);
void wiiz_core_input(WiizCore*, unsigned buttons);
void wiiz_core_reset(WiizCore*);
int wiiz_core_frame(WiizCore*);
void wiiz_core_pixels(WiizCore*, uint32_t* rgba);
const float* wiiz_core_audio(WiizCore*, size_t* count);
size_t wiiz_core_sram_size(WiizCore*);
int wiiz_core_sram_read(WiizCore*, uint8_t* bytes, size_t size);
int wiiz_core_sram_write(WiizCore*, uint8_t* bytes, size_t size);
unsigned wiiz_core_mapper(WiizCore*);
size_t wiiz_core_state_size(WiizCore*);
int wiiz_core_state_write(WiizCore*, uint8_t*, size_t);
int wiiz_core_state_read(WiizCore*, const uint8_t*, size_t);
void wiiz_core_dq4_source(WiizCore*,int trace);
int wiiz_core_dq4_player(WiizCore*);
unsigned wiiz_core_take_sound(WiizCore*);
int wiiz_core_sound_command(WiizCore*,unsigned event);
void wiiz_core_input_speed(WiizCore*,unsigned buttons,unsigned speed);
void wiiz_core_input_profile(WiizCore*,unsigned profile);
unsigned wiiz_core_peek(WiizCore*,unsigned addr);
int wiiz_core_rewards(WiizCore*,unsigned experience,unsigned gold);
int wiiz_core_max_skills(WiizCore*,unsigned enabled);
int wiiz_core_free_jobs(WiizCore*,unsigned enabled);
unsigned wiiz_core_sound_pending(WiizCore*);
unsigned wiiz_core_sound_at(WiizCore*,unsigned index);
void wiiz_core_profile_source(WiizCore*,unsigned profile,int trace);
int wiiz_core_profile_player(WiizCore*,unsigned profile);
unsigned wiiz_core_current_music(WiizCore*);
unsigned wiiz_core_music_steps(WiizCore*);
int wiiz_core_ff_clock(WiizCore*,unsigned music_steps);
#ifdef __cplusplus
}
#endif
