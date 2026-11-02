#include "ff2_audio_api.h"
#include "ff2_sound_rom.hpp"
#include "picosha2.h"
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <tuple>
#include <vector>

int main(int argc,char** argv)
{
    if(argc!=5)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input),{}};
    if(picosha2::hash256_hex_string(rom)!="0846bf08ee66d84f72f9bcd7fde55de1c080cdd99e8d422b7a655e79e15a86ea")return 3;
    std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)> core(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
    if(!core)return 4;
    auto soundRom=rom;
    if(!prepareFf2SoundRom(soundRom))return 10;
    std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)> sound(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    if(!sound || !wiiz_core_profile_player(sound.get(),22))return 11;
    for(unsigned frame=0;frame<4;++frame)if(!wiiz_core_frame(sound.get()))return 12;
    wiiz_core_profile_source(core.get(),22,3);
    std::ifstream replay(argv[3]);
    if(!replay)return 7;
    unsigned first,last,buttons;
    std::vector<std::tuple<unsigned,unsigned,unsigned>> commands;
    while(replay>>first>>last>>buttons)
    {
        if(first>=last || buttons>255)return 8;
        commands.emplace_back(first,last,buttons);
    }
    if(!replay.eof())return 9;
    std::filesystem::create_directories(argv[4]);
    std::array<uint32_t,256*240> pixels;
    double gameEnergy=0,soundEnergy=0;
    for(unsigned frame=0;frame<std::stoul(argv[2]);++frame)
    {
        unsigned buttons=0;
        for(auto [first,last,mask]:commands)if(frame>=first && frame<last)buttons|=mask;
        wiiz_core_input(core.get(),buttons);
        if(!wiiz_core_frame(core.get()))return 5;
        while(const unsigned event=wiiz_core_take_sound(core.get()))if(!wiiz_core_sound_command(sound.get(),event))return 13;
        if(!wiiz_core_sound_clock(sound.get(),wiiz_core_music_steps(core.get()),wiiz_core_effect_steps(core.get())))return 15;
        if(!wiiz_core_frame(sound.get()))return 14;
        size_t count;const auto* audio=wiiz_core_audio(core.get(),&count);
        for(size_t i=0;i<count;++i)gameEnergy+=audio[i]*audio[i];
        audio=wiiz_core_audio(sound.get(),&count);
        for(size_t i=0;i<count;++i)soundEnergy+=audio[i]*audio[i];
        if(frame%300==299)
        {
            std::cout<<"FRAME "<<frame<<" buttons="<<buttons<<" input="<<wiiz_core_peek(core.get(),0x34)<<" held="<<wiiz_core_peek(core.get(),0x35)<<" timer="<<wiiz_core_peek(core.get(),0x36)
                <<" character="<<wiiz_core_peek(core.get(),7)<<" name_position="<<wiiz_core_peek(core.get(),8)<<" cursor="<<wiiz_core_peek(core.get(),0x7af0)
                <<" game_energy="<<gameEnergy<<" sound_energy="<<soundEnergy<<"\n";
            gameEnergy=soundEnergy=0;
            wiiz_core_pixels(core.get(),pixels.data());
            auto* surface=SDL_CreateSurfaceFrom(256,240,SDL_PIXELFORMAT_RGBA32,pixels.data(),256*4);
            const auto path=std::filesystem::path(argv[4])/(std::to_string(frame)+".bmp");
            if(!surface || !SDL_SaveBMP(surface,path.string().c_str()))return 6;
            SDL_DestroySurface(surface);
        }
    }
    return 0;
}
