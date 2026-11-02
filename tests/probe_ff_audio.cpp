#include "ff2_audio_api.h"
#include "picosha2.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

struct Trace
{
    std::ofstream events;
    // 8 KiB単位の物理バンクと、オペランド読取後のPCを保存する。
    std::map<std::tuple<unsigned,unsigned,unsigned>,std::array<unsigned,3>> sites;
};
static void observe(void* data,unsigned frame,unsigned bank,unsigned pc,unsigned addr,unsigned value)
{
    auto& trace=*static_cast<Trace*>(data);
    trace.events<<frame<<','<<bank<<','<<pc<<','<<addr<<','<<value<<'\n';
    auto& site=trace.sites[{bank,pc,addr}];
    if(site[0]++==0)site[1]=frame;
    site[2]=frame;
}
int main(int argc,char** argv)
{
    try
    {
        if(argc!=5)throw std::runtime_error("usage: nesdq_ff_audio_probe ROM frames replay.txt output-directory");
        std::ifstream file(argv[1],std::ios::binary);
        if(!file)throw std::runtime_error(std::string("cannot open ROM: ")+argv[1]);
        std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file),{}};
        const auto hash=picosha2::hash256_hex_string(rom);
        if(hash!="0846bf08ee66d84f72f9bcd7fde55de1c080cdd99e8d422b7a655e79e15a86ea" &&
           hash!="1a87487b1d702e85ae2cbc002182740d5c34b2b9ae145b1a0722a78936706f39")
            throw std::runtime_error("unsupported ROM SHA-256: "+hash);
        const std::string frameText=argv[2];
        size_t parsed=0;
        const auto frames=std::stoul(frameText,&parsed);
        if(parsed!=frameText.size() || frames==0 || frames>10000000)throw std::runtime_error("invalid frame count");
        std::ifstream replay(argv[3]);
        if(!replay)throw std::runtime_error(std::string("cannot open replay: ")+argv[3]);
        unsigned first,last,buttons;
        std::vector<std::tuple<unsigned,unsigned,unsigned>> commands;
        while(replay>>first)
        {
            if(!(replay>>last>>buttons))throw std::runtime_error("incomplete replay interval");
            if(first>=last || buttons>255 || last>frames)throw std::runtime_error("invalid replay interval");
            commands.emplace_back(first,last,buttons);
        }
        if(!replay.eof())throw std::runtime_error("invalid replay data");
        std::filesystem::create_directories(argv[4]);
        const auto output=std::filesystem::path(argv[4]);
        Trace trace;
        trace.events.exceptions(std::ios::failbit|std::ios::badbit);
        trace.events.open(output/"apu.csv");
        trace.events<<"frame,bank8k,pc_after_operand,register,value\n";
        using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
        Core core(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
        Core reference(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
        if(!core || !reference)throw std::runtime_error("cannot create core");
        wiiz_core_audio_observer(core.get(),observe,&trace);
        for(unsigned frame=0;frame<frames;++frame)
        {
            unsigned mask=0;
            for(auto [start,end,pad]:commands)if(frame>=start && frame<end)mask|=pad;
            wiiz_core_input(core.get(),mask);wiiz_core_input(reference.get(),mask);
            if(!wiiz_core_frame(core.get()) || !wiiz_core_frame(reference.get()))throw std::runtime_error("core stopped at frame "+std::to_string(frame));
            size_t count,referenceCount;
            const auto* audio=wiiz_core_audio(core.get(),&count);
            const auto* expected=wiiz_core_audio(reference.get(),&referenceCount);
            if(count!=referenceCount || !std::equal(audio,audio+count,expected))throw std::runtime_error("observer changed audio");
        }
        std::array<uint32_t,256*240> pixels,expectedPixels;
        wiiz_core_pixels(core.get(),pixels.data());wiiz_core_pixels(reference.get(),expectedPixels.data());
        if(pixels!=expectedPixels)throw std::runtime_error("observer changed video");
        trace.events.close();
        std::ofstream summary;
        summary.exceptions(std::ios::failbit|std::ios::badbit);
        summary.open(output/"sites.csv");
        summary<<"bank8k,pc_after_operand,register,writes,first_frame,last_frame\n";
        for(const auto& [key,stats]:trace.sites)
        {
            const auto [bank,pc,addr]=key;
            summary<<bank<<','<<pc<<','<<addr<<','<<stats[0]<<','<<stats[1]<<','<<stats[2]<<'\n';
        }
        summary.close();
        std::cout<<"PASS sha256="<<hash<<" frames="<<frames<<" sites="<<trace.sites.size()<<" observer_audio_video=identical\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
