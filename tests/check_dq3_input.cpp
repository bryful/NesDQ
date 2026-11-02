#include "core_bridge.h"
#include "rom_metadata.hpp"
#include "picosha2.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(in),{}};
    normalizeDq3Metadata(rom,picosha2::hash256_hex_string(rom));
    using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
    Core core(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
    if(!core)return 3;
    // 冒険の書の選択画面へ進める。保存データは一切読み書きしない。
    for(unsigned i=0;i<1800;++i)
    {
        wiiz_core_input(core.get(),i>=1400 && i<1402?PAD_A:0);
        if(!wiiz_core_frame(core.get()))return 4;
    }
    wiiz_core_profile_source(core.get(),3,0);
    std::vector<uint8_t> state(wiiz_core_state_size(core.get()));
    if(!wiiz_core_state_write(core.get(),state.data(),state.size()))return 6;
    if(wiiz_core_peek(core.get(),0x75)!=0)return 7;
    using Move=std::pair<unsigned,unsigned>;
    std::vector<Move> baseline;
    for(unsigned speed=1;speed<=6;++speed)
    {
        if(!wiiz_core_state_read(core.get(),state.data(),state.size()))return 8;
        std::vector<Move> moves;
        unsigned cursor=0;
        std::vector<uint8_t> heldState(state.size());
        for(unsigned host=0;host<180;++host)
        {
            const unsigned buttons=host%30>=20?0u:host<120?((host/30)%2?PAD_UP:PAD_DOWN):host<150?PAD_LEFT:PAD_RIGHT;
            wiiz_core_input_speed(core.get(),buttons,speed);
            for(unsigned sub=0;sub<speed;++sub)
            {
                if(!wiiz_core_frame(core.get()))return 9;
                while(wiiz_core_take_sound(core.get())){}
            }
            if(host==10 && !wiiz_core_state_write(core.get(),heldState.data(),heldState.size()))return 15;
            const unsigned next=wiiz_core_peek(core.get(),0x75);
            if(next!=cursor && host<120){moves.push_back({host,next});cursor=next;}
        }
        std::cout<<"moves speed="<<speed;
        for(auto [frame,index]:moves)std::cout<<" "<<frame<<":"<<index;
        std::cout<<"\n";
        if(speed==1)baseline=moves;
        if(moves.size()!=baseline.size() || moves.size()<8)return 11;
        for(size_t i=0;i<moves.size();++i)
            if(moves[i].second!=baseline[i].second || std::abs(int(moves[i].first)-int(baseline[i].first))>1)return 12;
        std::array<uint32_t,256*240> pixels;
        wiiz_core_pixels(core.get(),pixels.data());
        if(!wiiz_core_state_read(core.get(),heldState.data(),heldState.size()))return 16;
        for(unsigned host=11;host<180;++host)
        {
            const unsigned buttons=host%30>=20?0u:host<120?((host/30)%2?PAD_UP:PAD_DOWN):host<150?PAD_LEFT:PAD_RIGHT;
            wiiz_core_input_speed(core.get(),buttons,speed);
            for(unsigned sub=0;sub<speed;++sub){if(!wiiz_core_frame(core.get()))return 17;while(wiiz_core_take_sound(core.get())){}}
        }
        std::array<uint32_t,256*240> restored;
        wiiz_core_pixels(core.get(),restored.data());
        if(pixels!=restored)return 18;
        auto* image=SDL_CreateSurfaceFrom(256,240,SDL_PIXELFORMAT_RGBA32,pixels.data(),256*4);
        if(!image)return 13;
        const std::string path="build/dq3-input-fixed-"+std::to_string(speed)+".bmp";
        const bool saved=SDL_SaveBMP(image,path.c_str());SDL_DestroySurface(image);
        if(!saved)return 14;
        std::cout<<"PASS speed="<<speed<<" menu_input=accepted cursor_moves="<<moves.size()<<" repeat_timing=matched state_replay=identical\n";
    }
    return 0;
}
