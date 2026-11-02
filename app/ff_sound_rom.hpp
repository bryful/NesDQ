#pragma once
#include "ff2_sound_rom.hpp"

inline bool prepareFfSoundRom(std::vector<uint8_t>& rom,unsigned profile)
{
    if(profile==122)return prepareFf2SoundRom(rom);
    if(profile!=123 || rom.size()!=524304)return false;
    // MMC3の固定最終バンクにある割込ベクターだけを音声用コピー上で差し替える。
    const auto vectors=rom.size()-6;
    rom[vectors]=0xf0;rom[vectors+1]=7;rom[vectors+2]=0;rom[vectors+3]=7;rom[vectors+4]=0xf0;rom[vectors+5]=7;
    return true;
}
