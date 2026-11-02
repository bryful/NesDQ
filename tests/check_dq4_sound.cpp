#include "core_bridge.h"
#include <cmath>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file),{}},soundRom=rom;
    if(rom.size()!=524304)return 2;
    const size_t vectors=soundRom.size()-6;
    soundRom[vectors]=0x80;soundRom[vectors+1]=7;soundRom[vectors+2]=0;soundRom[vectors+3]=7;soundRom[vectors+4]=0xf0;soundRom[vectors+5]=7;
    using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
    Core game(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
    Core player(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    if(!game || !player)return 3;
    wiiz_core_dq4_source(game.get(),1);wiiz_core_dq4_player(player.get());
    for(int i=0;i<4;++i)if(!wiiz_core_frame(player.get()))return 4;
    double energy=0;size_t samples=0;unsigned events=0;
    for(unsigned frame=0;frame<1200;++frame)
    {
        wiiz_core_input(game.get(),frame>600 && frame%120<6?PAD_START:0);
        if(!wiiz_core_frame(game.get()))return 5;
        while(const unsigned event=wiiz_core_take_sound(game.get())){++events;if(!wiiz_core_sound_command(player.get(),event))return 6;}
        if(!wiiz_core_frame(player.get()))return 7;
        size_t count;const float* audio=wiiz_core_audio(player.get(),&count);samples+=count;
        for(size_t i=0;i<count;++i){if(!std::isfinite(audio[i]))return 8;energy+=audio[i]*audio[i];}
    }
    std::cout<<"events="<<events<<" samples="<<samples<<" energy="<<energy<<" BF="<<wiiz_core_peek(player.get(),0xbf)<<" mailbox="<<wiiz_core_peek(player.get(),0x600)<<"\n";
    if(!events || energy<=0)return 9;
    std::vector<float> baseline;
    for(unsigned speed=1;speed<=6;++speed)
    {
        Core fast(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
        Core reference(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
        Core sound(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
        wiiz_core_dq4_player(sound.get());
        for(unsigned i=0;i<4;++i)if(!wiiz_core_frame(sound.get()))return 10;
        std::vector<float> recording;
        for(unsigned host=0;host<300;++host)
        {
            if(host==10 && !wiiz_core_sound_command(sound.get(),0x102))return 11;
            if(host==90 && !wiiz_core_sound_command(sound.get(),0x18a))return 12;
            if(host==150 && !wiiz_core_sound_command(sound.get(),0x300))return 13;
            if(host==200 && !wiiz_core_sound_command(sound.get(),0x11f))return 14;
            for(unsigned j=0;j<speed;++j)if(!wiiz_core_frame(fast.get()) || !wiiz_core_frame(reference.get()))return 15;
            if(!wiiz_core_frame(sound.get()))return 16;
            size_t count;const auto* audio=wiiz_core_audio(sound.get(),&count);recording.insert(recording.end(),audio,audio+count);
        }
        std::array<uint32_t,256*240> actual,expected;
        wiiz_core_pixels(fast.get(),actual.data());wiiz_core_pixels(reference.get(),expected.data());
        if(actual!=expected)return 17;
        if(speed==1)baseline=recording;else if(recording!=baseline)return 18;
        double soundEnergy=0;for(auto sample:recording){if(!std::isfinite(sample))return 19;soundEnergy+=sample*sample;}
        if(soundEnergy<=0)return 20;
        std::cout<<"PASS speed="<<speed<<" game_frames="<<300*speed<<" audio_samples="<<recording.size()<<" audio_identical=1\n";
    }
    unsigned bgm=0,se=0,audible=0;
    Core catalogue(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    wiiz_core_dq4_player(catalogue.get());
    for(unsigned i=0;i<4;++i)if(!wiiz_core_frame(catalogue.get()))return 24;
    for(unsigned group=0;group<2;++group)
    {
        const unsigned end=group?56:54;
        for(unsigned id=0;id<end;++id)
        {
            if(group && id==50)continue; // 元テーブルの未使用NULL項目。
            if(!wiiz_core_sound_command(catalogue.get(),0x500) || !wiiz_core_frame(catalogue.get()))return 25;
            if(!wiiz_core_sound_command(catalogue.get(),0x100|(group?0x80|id:id)))return 26;
            double level=0;
            for(unsigned frame=0;frame<180;++frame)
            {
                if(!wiiz_core_frame(catalogue.get()))return 27;
                size_t count;const auto* audio=wiiz_core_audio(catalogue.get(),&count);
                for(size_t i=0;i<count;++i){if(!std::isfinite(audio[i]))return 28;level+=audio[i]*audio[i];}
            }
            if(level>0)++audible;
            if(group)++se;else ++bgm;
        }
    }
    std::cout<<"PASS driver_catalogue BGM_entries="<<bgm<<" SE_entries="<<se<<" finite_audio=1 entries_with_audio="<<audible<<"\n";
    return 0;
}
