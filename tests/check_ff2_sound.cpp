#include "ff2_audio_api.h"
#include "ff2_sound_rom.hpp"
#include "picosha2.h"
#include <cmath>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file),{}};
    auto soundRom=rom;
    if(picosha2::hash256_hex_string(rom)!="0846bf08ee66d84f72f9bcd7fde55de1c080cdd99e8d422b7a655e79e15a86ea")return 2;
    if(!prepareFf2SoundRom(soundRom))return 35;
    using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
    Core game(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
    Core player(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    if(!game || !player)return 3;
    wiiz_core_profile_source(game.get(),22,1);wiiz_core_profile_player(player.get(),22);
    for(int i=0;i<4;++i)if(!wiiz_core_frame(player.get()))return 4;
    double energy=0,gameEnergy=0;size_t samples=0;unsigned events=0;
    for(unsigned frame=0;frame<3300;++frame)
    {
        wiiz_core_input(game.get(),frame==720?PAD_START:frame>=1200 && frame%120<4?PAD_A:0);
        if(!wiiz_core_frame(game.get()))return 5;
        size_t gameCount;const auto* gameAudio=wiiz_core_audio(game.get(),&gameCount);
        for(size_t i=0;i<gameCount;++i)gameEnergy+=gameAudio[i]*gameAudio[i];
        while(const unsigned event=wiiz_core_take_sound(game.get())){++events;if(!wiiz_core_sound_command(player.get(),event))return 6;}
        if(!wiiz_core_sound_clock(player.get(),wiiz_core_music_steps(game.get()),wiiz_core_effect_steps(game.get())))return 52;
        if(!wiiz_core_frame(player.get()))return 7;
        size_t count;const float* audio=wiiz_core_audio(player.get(),&count);samples+=count;
        for(size_t i=0;i<count;++i){if(!std::isfinite(audio[i]))return 8;energy+=audio[i]*audio[i];}
    }
    std::cout<<"events="<<events<<" samples="<<samples<<" energy="<<energy<<" game_energy="<<gameEnergy<<" BGM="<<wiiz_core_current_music(player.get())<<" mailbox="<<wiiz_core_peek(player.get(),0x500)<<"\n";
    if(events<8 || energy<=0 || gameEnergy<=0 || std::abs(energy-gameEnergy)/gameEnergy>0.1)return 9;
    std::vector<float> baseline;
    for(unsigned speed=1;speed<=6;++speed)
    {
        Core fast(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
        Core reference(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
        Core sound(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
        wiiz_core_profile_player(sound.get(),22);
        for(unsigned i=0;i<4;++i)if(!wiiz_core_frame(sound.get()))return 10;
        std::vector<float> recording;
        for(unsigned host=0;host<300;++host)
        {
            if(host==10 && !wiiz_core_sound_command(sound.get(),0x141))return 11;
            if(host==30 && !wiiz_core_sound_command(sound.get(),0x9a8))return 36;
            if(host==31 && !wiiz_core_sound_command(sound.get(),0x933))return 37;
            if(host==90 && !wiiz_core_sound_command(sound.get(),0x200))return 12;
            if(host==40 && !wiiz_core_sound_command(sound.get(),0x640))return 21;
            if(host==100 && !wiiz_core_sound_command(sound.get(),0x700))return 22;
            if(host==120 && !wiiz_core_sound_command(sound.get(),0x400))return 23;
            if(host==150 && !wiiz_core_sound_command(sound.get(),0x300))return 13;
            if(host==150 && !wiiz_core_sound_command(sound.get(),0xa60))return 38;
            if(host==160 && !wiiz_core_sound_command(sound.get(),0xa11))return 39;
            if(host==180 && !wiiz_core_sound_command(sound.get(),0xb01))return 40;
            if(host==200 && !wiiz_core_sound_command(sound.get(),0x144))return 14;
            if(host==210 && !wiiz_core_sound_command(sound.get(),0xc00))return 41;
            if(host==240 && !wiiz_core_sound_command(sound.get(),0x802))return 42;
            if(host==250 && !wiiz_core_sound_command(sound.get(),0x141))return 43;
            if(host==260 && !wiiz_core_sound_command(sound.get(),0x801))return 44;
            if(host==270 && !wiiz_core_sound_command(sound.get(),0x500))return 45;
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
    for(unsigned type:{1u,6u})
    {
        const unsigned entries=type==1?31:33;
        for(unsigned index=0;index<entries;++index)
        {
            Core sound(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
            if(!sound || !wiiz_core_profile_player(sound.get(),22))return 24;
            for(unsigned frame=0;frame<4;++frame)if(!wiiz_core_frame(sound.get()))return 25;
            if(!wiiz_core_sound_command(sound.get(),(type<<8)|0x40|index))return 26;
            for(unsigned frame=0;frame<240;++frame)
            {
                if(!wiiz_core_frame(sound.get()))return 27;
                size_t count;const float* audio=wiiz_core_audio(sound.get(),&count);
                for(size_t i=0;i<count;++i)if(!std::isfinite(audio[i]))return 28;
            }
            if(wiiz_core_sound_pending(sound.get()) || wiiz_core_peek(sound.get(),0x500))return 29;
        }
        std::cout<<"PASS catalogue type="<<type<<" entries="<<entries<<"\n";
    }
    Core sound(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    wiiz_core_profile_player(sound.get(),22);
    for(unsigned frame=0;frame<4;++frame)if(!wiiz_core_frame(sound.get()))return 30;
    if(!wiiz_core_sound_command(sound.get(),0x141) || !wiiz_core_sound_command(sound.get(),0x700))return 31;
    for(unsigned frame=0;frame<5;++frame)if(!wiiz_core_frame(sound.get()))return 32;
    std::vector<uint8_t> state(wiiz_core_state_size(sound.get()));
    if(!wiiz_core_state_write(sound.get(),state.data(),state.size()))return 33;
    auto advance=[&]()
    {
        std::vector<float> result;
        for(unsigned frame=0;frame<100;++frame)
        {
            if(!wiiz_core_frame(sound.get()))throw std::runtime_error("FF2 effect stopped");
            size_t count;const float* audio=wiiz_core_audio(sound.get(),&count);result.insert(result.end(),audio,audio+count);
        }
        return result;
    };
    const auto expected=advance();
    if(!wiiz_core_state_read(sound.get(),state.data(),state.size()) || advance()!=expected)return 34;
    std::cout<<"PASS blocking_effect_state audio_replay=identical\n";
    Core terrain(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    if(!terrain || !wiiz_core_profile_player(terrain.get(),22))return 46;
    for(unsigned frame=0;frame<4;++frame)if(!wiiz_core_frame(terrain.get()))return 47;
    unsigned previous=0;
    for(unsigned frame=0;frame<30;++frame)
    {
        // ゲーム側から高速に届く位相を、連続再生中に毎回採用してはいけない。
        if(!wiiz_core_sound_command(terrain.get(),0x900|((0xa8+frame*6)&255)) || !wiiz_core_frame(terrain.get()))return 48;
        const unsigned phase=wiiz_core_peek(terrain.get(),0x4fb);
        if(frame>2 && (wiiz_core_peek(terrain.get(),0x4fc)!=8 || phase!=((previous+1)&255)))return 49;
        previous=phase;
    }
    std::vector<uint8_t> terrainState(wiiz_core_state_size(terrain.get()));
    if(!wiiz_core_state_write(terrain.get(),terrainState.data(),terrainState.size()))return 50;
    sound.swap(terrain);
    const auto terrainExpected=advance();
    if(!wiiz_core_state_read(sound.get(),terrainState.data(),terrainState.size()) || advance()!=terrainExpected)return 51;
    std::cout<<"PASS terrain_phase one_step_per_audio_frame=1 state_audio_replay=identical\n";
    Core clocked(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    Core reference(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
    if(!clocked || !reference || !wiiz_core_profile_player(clocked.get(),22) || !wiiz_core_profile_player(reference.get(),22))return 53;
    for(unsigned frame=0;frame<4;++frame)if(!wiiz_core_frame(clocked.get()) || !wiiz_core_frame(reference.get()))return 54;
    if(!wiiz_core_sound_command(clocked.get(),0x141) || !wiiz_core_sound_command(reference.get(),0x141))return 55;
    if(!wiiz_core_frame(clocked.get()) || !wiiz_core_frame(reference.get()))return 56;
    if(!wiiz_core_sound_clock(clocked.get(),2,0) || !wiiz_core_sound_clock(reference.get(),1,0))return 57;
    for(unsigned frame=0;frame<10;++frame)if(!wiiz_core_frame(clocked.get()))return 58;
    for(unsigned frame=0;frame<20;++frame)if(!wiiz_core_frame(reference.get()))return 59;
    for(unsigned addr=0x6f00;addr<0x6f4c;++addr)
        if(wiiz_core_peek(clocked.get(),addr)!=wiiz_core_peek(reference.get(),addr))return 60;
    if(!wiiz_core_sound_clock(clocked.get(),0,0))return 61;
    for(unsigned frame=0;frame<2;++frame)if(!wiiz_core_frame(clocked.get()))return 62;
    std::vector<unsigned> paused;
    for(unsigned addr=0x6f00;addr<0x6f4c;++addr)paused.push_back(wiiz_core_peek(clocked.get(),addr));
    for(unsigned frame=0;frame<10;++frame)if(!wiiz_core_frame(clocked.get()))return 63;
    for(unsigned i=0;i<paused.size();++i)if(wiiz_core_peek(clocked.get(),0x6f00+i)!=paused[i])return 64;
    std::vector<uint8_t> clockState(wiiz_core_state_size(clocked.get()));
    if(!wiiz_core_state_write(clocked.get(),clockState.data(),clockState.size()))return 65;
    sound.swap(clocked);
    const auto pausedExpected=advance();
    if(!wiiz_core_state_read(sound.get(),clockState.data(),clockState.size()) || advance()!=pausedExpected)return 66;
    std::cout<<"PASS music_clock double_step=matched paused_driver=stable state_audio_replay=identical\n";
    return 0;
}

