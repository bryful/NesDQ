#include "core_bridge.h"
#include "ff_sound_rom.hpp"
#include "rom_metadata.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)
{
    try
    {
        require(argc==2,"usage: nesdq_ff_audio_check ROM");
        std::ifstream file(argv[1],std::ios::binary);require(bool(file),"cannot open ROM");
        std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file),{}};
        const unsigned profile=normalizeVerifiedRom(rom);
        require(profile==122 || profile==123,"unsupported ROM");
        auto soundRom=rom;require(prepareFfSoundRom(soundRom,profile),"invalid sound ROM");
        auto make=[&](bool player)
        {
            auto& data=player?soundRom:rom;
            Core result(wiiz_core_new(data.data(),data.size()),wiiz_core_delete);require(bool(result),"cannot create core");
            if(player)
            {
                require(wiiz_core_profile_player(result.get(),profile),"cannot initialize player");
                for(unsigned i=0;i<4;++i)require(wiiz_core_frame(result.get()),"player boot stopped");
            }
            return result;
        };
        auto game=make(false),reference=make(false),player=make(true);
        wiiz_core_profile_source(game.get(),profile,0);
        wiiz_core_input_profile(reference.get(),profile);
        unsigned requests=0,updates=0;double gameEnergy=0,soundEnergy=0;
        for(unsigned frame=0;frame<9000;++frame)
        {
            const unsigned buttons=frame>=720 && frame<724?PAD_START:frame>=1200 && frame%120<4?PAD_A:0;
            wiiz_core_input(game.get(),buttons);wiiz_core_input(reference.get(),buttons);
            require(wiiz_core_frame(game.get()) && wiiz_core_frame(reference.get()),"game stopped");
            size_t count,refCount;const auto* audio=wiiz_core_audio(game.get(),&count);const auto* ref=wiiz_core_audio(reference.get(),&refCount);
            require(count==refCount && std::equal(audio,audio+count,ref),"capture changed game audio");
            for(size_t i=0;i<count;++i)gameEnergy+=audio[i]*audio[i];
            while(const auto event=wiiz_core_take_sound(game.get())){++requests;require(wiiz_core_sound_command(player.get(),event),"request overflow");}
            const auto steps=wiiz_core_music_steps(game.get());updates+=steps;
            require(wiiz_core_ff_clock(player.get(),steps),"invalid sound clock");
            require(wiiz_core_frame(player.get()),"sound stopped");
            audio=wiiz_core_audio(player.get(),&count);
            for(size_t i=0;i<count;++i){require(std::isfinite(audio[i]),"non-finite audio");soundEnergy+=audio[i]*audio[i];}
        }
        std::array<uint32_t,256*240> pixels,expected;
        wiiz_core_pixels(game.get(),pixels.data());wiiz_core_pixels(reference.get(),expected.data());
        require(pixels==expected,"capture changed game video");
        std::cout<<"source requests="<<requests<<" updates="<<updates<<" game_energy="<<gameEnergy<<" sound_energy="<<soundEnergy<<'\n';
        require(requests>5 && updates>1000 && soundEnergy>100,"source capture silent or inactive");
        std::vector<float> baseline;
        for(unsigned speed=1;speed<=6;++speed)
        {
            auto sound=make(true),fast=make(false);
            std::vector<float> recording;
            for(unsigned host=0;host<360;++host)
            {
                for(unsigned sub=0;sub<speed;++sub)require(wiiz_core_frame(fast.get()),"fast game stopped");
                if(profile==122)
                {
                    if(host==5)require(wiiz_core_sound_command(sound.get(),0x141),"BGM request failed");
                    if(host==40)require(wiiz_core_sound_command(sound.get(),0x640),"battle SE failed");
                    if(host==140)require(wiiz_core_sound_command(sound.get(),0x9a8),"terrain failed");
                    if(host==160)require(wiiz_core_sound_command(sound.get(),0xc00),"door failed");
                    if(host==180)require(wiiz_core_sound_command(sound.get(),0x700),"blocking SE failed");
                }
                else
                {
                    if(host==5){require(wiiz_core_sound_command(sound.get(),0x301),"BGM failed");require(wiiz_core_sound_command(sound.get(),0x201),"BGM flags failed");}
                    if(host==40)require(wiiz_core_sound_command(sound.get(),0x481),"SE failed");
                    if(host==140)require(wiiz_core_sound_command(sound.get(),0x485),"SE failed");
                    if(host==200){require(wiiz_core_sound_command(sound.get(),0x303),"BGM failed");require(wiiz_core_sound_command(sound.get(),0x201),"BGM flags failed");}
                }
                // ゲーム側の進行速度に依存せず、音声側を実時間で1回更新する。
                require(wiiz_core_ff_clock(sound.get(),1) && wiiz_core_frame(sound.get()),"sound stopped");
                size_t count;const auto* audio=wiiz_core_audio(sound.get(),&count);recording.insert(recording.end(),audio,audio+count);
            }
            if(speed==1)baseline=recording;else require(recording==baseline,"speed changed audio");
            double energy=0;
            for(const auto sample:recording){require(std::isfinite(sample),"invalid sequence audio");energy+=sample*sample;}
            require(energy>1,"silent sequence audio");
            std::vector<uint8_t> state(wiiz_core_state_size(sound.get()));
            require(wiiz_core_state_write(sound.get(),state.data(),state.size()),"cannot save sound");
            auto advance=[&]()
            {
                std::vector<float> samples;
                for(unsigned i=0;i<60;++i){require(wiiz_core_frame(sound.get()),"sound stopped");size_t count;const auto* audio=wiiz_core_audio(sound.get(),&count);samples.insert(samples.end(),audio,audio+count);}
                return samples;
            };
            const auto expectedAudio=advance();require(wiiz_core_state_read(sound.get(),state.data(),state.size()),"cannot restore sound");
            require(advance()==expectedAudio,"sound state diverged");
            std::cout<<"PASS speed="<<speed<<" samples="<<recording.size()<<" identical=1 sound_state=identical\n";
        }
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
