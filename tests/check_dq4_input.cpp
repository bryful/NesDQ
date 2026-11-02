#include "core_bridge.h"
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
    using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
    Core core(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
    if(!core)return 3;
    // 冒険の書の選択画面へ進める。保存データは一切読み書きしない。
    for(unsigned i=0;i<1800;++i)
    {
        wiiz_core_input(core.get(),i>=1400 && i<1460?PAD_A:0);
        if(!wiiz_core_frame(core.get()))return 4;
    }
    for(unsigned i=0;i<180;++i)
    {
        wiiz_core_input(core.get(),i<30?PAD_A:0);
        if(!wiiz_core_frame(core.get()))return 5;
    }
    wiiz_core_dq4_source(core.get(),0);
    std::vector<uint8_t> state(wiiz_core_state_size(core.get()));
    if(!wiiz_core_state_write(core.get(),state.data(),state.size()))return 6;
    if(wiiz_core_peek(core.get(),0x3cf)!=0x10)return 7;
    using Move=std::pair<unsigned,unsigned>;
    std::vector<Move> baseline;
    for(unsigned speed=1;speed<=6;++speed)
    {
        if(!wiiz_core_state_read(core.get(),state.data(),state.size()))return 8;
        std::vector<Move> moves;
        unsigned cursor=0x10;
        std::vector<uint8_t> heldState(state.size());
        for(unsigned host=0;host<180;++host)
        {
            const unsigned buttons=host<120 ? ((host/30)%2?PAD_UP:PAD_DOWN) : host<150?PAD_LEFT:PAD_RIGHT;
            wiiz_core_input_speed(core.get(),buttons,speed);
            for(unsigned sub=0;sub<speed;++sub)
            {
                if(!wiiz_core_frame(core.get()))return 9;
                while(wiiz_core_take_sound(core.get())){}
            }
            // コールバックの受付数ではなく、ゲームが読み取った実際のボタン値を確認する。
            if(host%30>=12 && wiiz_core_peek(core.get(),0x14)!=buttons)
            {
                std::cerr<<"FAIL held direction speed="<<speed<<" host="<<host<<" value="<<wiiz_core_peek(core.get(),0x14)<<" expected="<<buttons<<"\n";return 10;
            }
            if(host==10 && !wiiz_core_state_write(core.get(),heldState.data(),heldState.size()))return 15;
            const unsigned next=wiiz_core_peek(core.get(),0x3cf);
            if(next!=cursor && host<120){moves.push_back({host,next});cursor=next;}
        }
        std::cout<<"moves speed="<<speed;
        for(auto [frame,index]:moves)std::cout<<" "<<frame<<":"<<index-0x10;
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
            const unsigned buttons=host<120?((host/30)%2?PAD_UP:PAD_DOWN):host<150?PAD_LEFT:PAD_RIGHT;
            wiiz_core_input_speed(core.get(),buttons,speed);
            for(unsigned sub=0;sub<speed;++sub){if(!wiiz_core_frame(core.get()))return 17;while(wiiz_core_take_sound(core.get())){}}
        }
        std::array<uint32_t,256*240> restored;
        wiiz_core_pixels(core.get(),restored.data());
        if(pixels!=restored)return 18;
        auto* image=SDL_CreateSurfaceFrom(256,240,SDL_PIXELFORMAT_RGBA32,pixels.data(),256*4);
        if(!image)return 13;
        const std::string path="build/input-fixed-"+std::to_string(speed)+".bmp";
        const bool saved=SDL_SaveBMP(image,path.c_str());SDL_DestroySurface(image);
        if(!saved)return 14;
        std::cout<<"PASS speed="<<speed<<" directions=4 held=preserved cursor_moves="<<moves.size()<<" repeat_timing=matched state_replay=identical\n";
    }
    return 0;
}
