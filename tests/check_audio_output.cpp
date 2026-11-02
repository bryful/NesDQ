#include "core_bridge.h"
#include "audio_output.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc,char** argv)
{
    try
    {
        if(argc!=2)return 2;
        if(!SDL_Init(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
        struct Quit{~Quit(){SDL_Quit();}}quit;
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file),{}},soundRom=rom;
        if(rom.size()!=524304)return 3;
        const auto vectors=soundRom.size()-6;
        soundRom[vectors]=0x80;soundRom[vectors+1]=7;soundRom[vectors+2]=0;soundRom[vectors+3]=7;soundRom[vectors+4]=0xf0;soundRom[vectors+5]=7;
        using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
        for(unsigned speed:{1u,4u})
        {
            Core game(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
            Core sound(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
            if(!game || !sound || !wiiz_core_dq4_player(sound.get()))return 4;
            for(unsigned i=0;i<4;++i)if(!wiiz_core_frame(sound.get()))return 5;
            wiiz_core_sound_command(sound.get(),0x102);
            AudioOutput output;
            if(output.started() || output.callbacks())return 6;
            const Uint64 start=SDL_GetTicks();Uint64 stallAt=start+2000;
            double next=SDL_GetTicksNS()/1e9;
            unsigned frames=0,stalls=0;size_t samples=0;double energy=0;
            int minimum=1000000,maximum=0;
            while(SDL_GetTicks()-start<12000)
            {
                const Uint64 frameStart=SDL_GetTicksNS();
                for(unsigned sub=0;sub<speed;++sub)if(!wiiz_core_frame(game.get()))return 7;
                if(!wiiz_core_frame(sound.get()))return 8;
                size_t count;const auto* data=wiiz_core_audio(sound.get(),&count);samples+=count;
                for(size_t i=0;i<count;++i){if(!std::isfinite(data[i]))return 9;energy+=data[i]*data[i];}
                output.push(data,count);++frames;
                if(SDL_GetTicks()>=stallAt){SDL_Delay(33);stallAt+=2000;++stalls;}
                if(output.pace(frameStart))
                {
                    const int queued=output.queuedSamples();minimum=std::min(minimum,queued);maximum=std::max(maximum,queued);
                    next=SDL_GetTicksNS()/1e9;
                }
                else
                {
                    next+=1.0/60.0988;const double now=SDL_GetTicksNS()/1e9;
                    if(next>now)SDL_DelayNS(Uint64((next-now)*1e9));
                }
            }
            std::cout<<"speed="<<speed<<" frames="<<frames<<" samples="<<samples<<" callbacks="<<output.callbacks()
                     <<" shortfalls="<<output.shortfalls()<<" queue_min="<<minimum<<" queue_max="<<maximum<<" stalls_33ms="<<stalls<<"\n";
            if(!output.callbacks() || output.shortfalls() || energy<=0 || minimum<=0 || frames<680 || frames>760)return 10;
            output.reset();
            if(output.started() || output.queuedSamples())return 11;
            for(unsigned frame=0;frame<30;++frame)
            {
                if(!wiiz_core_frame(sound.get()))return 12;
                size_t count;const auto* data=wiiz_core_audio(sound.get(),&count);output.push(data,count);
                if(!output.pace())SDL_Delay(16);
            }
            if(!output.started() || output.shortfalls())return 13;
            std::cout<<"PASS speed="<<speed<<" playback=continuous resume=buffered\n";
        }
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
