#include "core_bridge.h"
#include "rom_metadata.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input),{}};
    const unsigned profile=normalizeVerifiedRom(rom);
    if(profile!=122 && profile!=123)return 3;
    const bool ff3=profile==123;
    using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
    Core core(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
    if(!core)return 4;
    wiiz_core_input_profile(core.get(),ff3?123:122);
    std::array<unsigned,2048> cold;
    for(unsigned addr=0;addr<cold.size();++addr)cold[addr]=wiiz_core_peek(core.get(),addr);
    wiiz_core_input_speed(core.get(),0,6);
    for(unsigned addr=0;addr<cold.size();++addr)if(wiiz_core_peek(core.get(),addr)!=cold[addr])return 17;
    wiiz_core_input_speed(core.get(),0,1);
    // 最初の名前入力画面へ進める。保存データを使用しない。
    for(unsigned frame=0;frame<2100;++frame)
    {
        const unsigned buttons=ff3?(frame>=720 && frame<724?PAD_START:frame>=1400 && frame<1402?PAD_A:0)
            :(frame==720?PAD_START:frame>=1200 && frame<=2040 && frame%120<4?PAD_A:0);
        wiiz_core_input(core.get(),buttons);
        if(!wiiz_core_frame(core.get()))return 5;
    }
    std::vector<uint8_t> state(wiiz_core_state_size(core.get()));
    if(!wiiz_core_state_write(core.get(),state.data(),state.size()) || wiiz_core_peek(core.get(),0x7af0)!=0)return 6;
    using Move=std::pair<unsigned,unsigned>;
    std::vector<Move> baseline;
    for(unsigned speed=1;speed<=6;++speed)
    {
        if(!wiiz_core_state_read(core.get(),state.data(),state.size()))return 7;
        std::vector<Move> moves;
        unsigned cursor=0;
        std::vector<uint8_t> heldState(state.size());
        auto advance=[&](unsigned host)
        {
            const unsigned buttons=host%30>=20?0u:host<120?((host/30)%2?PAD_UP:PAD_DOWN):host<150?PAD_LEFT:PAD_RIGHT;
            wiiz_core_input_speed(core.get(),buttons,speed);
            for(unsigned sub=0;sub<speed;++sub)
            {
                if(!wiiz_core_frame(core.get()))return false;
                while(wiiz_core_take_sound(core.get())){}
            }
            return true;
        };
        for(unsigned host=0;host<180;++host)
        {
            if(!advance(host))return 8;
            if(host==10 && !wiiz_core_state_write(core.get(),heldState.data(),heldState.size()))return 9;
            const unsigned next=wiiz_core_peek(core.get(),0x7af0);
            if(next!=cursor && host<120){moves.emplace_back(host,next);cursor=next;}
        }
        std::cout<<"moves speed="<<speed;
        for(auto [host,index]:moves)std::cout<<" "<<host<<":"<<index;
        std::cout<<"\n";
        if(speed==1)baseline=moves;
        if(moves.size()!=baseline.size() || moves.size()<8)return 10;
        for(size_t i=0;i<moves.size();++i)
            if(moves[i].second!=baseline[i].second || std::abs(int(moves[i].first)-int(baseline[i].first))>1)return 11;
        std::array<uint32_t,256*240> expected,actual;
        wiiz_core_pixels(core.get(),expected.data());
        if(!wiiz_core_state_read(core.get(),heldState.data(),heldState.size()))return 12;
        for(unsigned host=11;host<180;++host)if(!advance(host))return 13;
        wiiz_core_pixels(core.get(),actual.data());
        if(actual!=expected)return 14;
        auto* image=SDL_CreateSurfaceFrom(256,240,SDL_PIXELFORMAT_RGBA32,actual.data(),256*4);
        if(!image)return 15;
        const auto path="build/ff"+std::to_string(ff3?3:2)+"-input-"+std::to_string(speed)+".bmp";
        const bool saved=SDL_SaveBMP(image,path.c_str());SDL_DestroySurface(image);
        if(!saved)return 16;
        std::cout<<"PASS speed="<<speed<<" repeat_timing=matched state_replay=identical\n";
    }
    return 0;
}
