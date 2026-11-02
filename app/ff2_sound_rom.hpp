#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// 音声用コピーだけで、地形音・扉音の呼出しを元のゲームループへ戻さない。
inline bool prepareFf2SoundRom(std::vector<uint8_t>& rom)
{
    constexpr std::size_t terrain=16+15*16384+0x156;
    constexpr std::size_t door=16+15*16384+0x8df;
    if(rom.size()!=262160 || rom[terrain]!=0x4c || rom[terrain+1]!=0xc9 || rom[terrain+2]!=0xc0 || rom[door]!=0xa5 || rom[door+1]!=0x34)
        return false;
    rom[terrain]=0x60;
    rom[door]=0x60;
    const std::size_t vectors=rom.size()-6;
    rom[vectors]=0xf8;rom[vectors+1]=7;rom[vectors+2]=0;rom[vectors+3]=7;rom[vectors+4]=0xfb;rom[vectors+5]=7;
    return true;
}
