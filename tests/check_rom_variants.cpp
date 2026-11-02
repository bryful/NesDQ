#include "rom_metadata.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::vector<uint8_t> read(const fs::path& path)
{
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open ROM sample: "+path.string());
    return {std::istreambuf_iterator<char>(file),{}};
}
int main(int argc,char** argv)
{
    try
    {
        require(argc==3,"usage: nesdq_rom_check ROM_FOLDER NORMALIZED_FOLDER");
        const fs::path folder=argv[1],output=argv[2];fs::create_directories(output);
        struct Sample {const char* name;unsigned profile;};
        const Sample samples[]={
            {"Dragon Quest 3 (J).nes",3},{"DragonQuest3nes.NES",3},
            {"Dragon Quest 3 (J) [hM02].nes",3},
            {"Dragon Quest 3 (J) [hM02][o1].nes",3},
            {"Dragon Quest 3 (J) [hM02][o2].nes",3},
            {"Dragon Quest 3 (J) [hM02][o3].nes",3},
            // 名前・ヘッダーはDQ3だが、中身はDQ4と破損した重複部分。
            {"Dragon Quest 3 (J) [hM02][b1].nes",4},
            {"Dragon Quest 4 (J).nes",4},{"DragonQuest4nes.NES",4},
            {"Dragon Quest 4 (J) [o1].nes",4},{"Dragon Quest 4 (J) [b2].nes",4},
            {"Dragon Quest 4 (J) [b1].nes",0},
            {"FF2.nes",122},{"Final Fantasy 2 (J) [hM02].nes",122},
            {"Final Fantasy 2 (J) [hM02][b1].nes",122},
            {"Final Fantasy 2 (J) [hM02][o1].nes",122},
            {"Final Fantasy 2 (J) [b1].nes",0},
            {"Final Fantasy 2 (J) [hM02][b2].nes",0},
            {"Final Fantasy 2 (J) [hM02][b3].nes",0},
            {"FF3.NES",123},{"Final Fantasy 3 (J) [b1].nes",123},
            {"final fantasy 3 (j) [h1].nes",123},
            {"final fantasy 3 (j) [p1].nes",123},
            {"Final Fantasy 3 (J) [o1].nes",123},
            {"Final Fantasy 3 (J) [b2].nes",0},
            {"Final Fantasy 3 (J) [b3].nes",0},
        };
        for(const auto& sample:samples)
        {
            const auto source=read(folder/sample.name);auto rom=source;
            const unsigned profile=normalizeVerifiedRom(rom);
            require(profile==sample.profile,"unexpected sample profile");
            if(!profile)require(rom==source,"unverified ROM changed");
            else
            {
                require(std::equal(rom.begin()+16,rom.end(),source.begin()+16),"PRG changed");
                auto again=rom;require(normalizeVerifiedRom(again)==profile && again==rom,"not idempotent");
                std::ofstream file(output/sample.name,std::ios::binary);
                file.write(reinterpret_cast<const char*>(rom.data()),rom.size());
                require(bool(file),"cannot write normalized test ROM");
            }
            require(read(folder/sample.name)==source,"source file changed");
            std::cout<<"PASS profile="<<profile<<" "<<sample.name<<"\n";
        }
        const auto baseline=read(folder/"FF3.NES");
        auto modified=baseline;modified[16]^=1;
        require(!normalizeVerifiedRom(modified),"unknown program accepted");
        auto trainer=baseline;trainer[6]|=4;
        require(!normalizeVerifiedRom(trainer),"trainer accepted");
        auto chr=baseline;chr[5]=1;
        require(!normalizeVerifiedRom(chr),"CHR ROM discarded");
        auto extra=baseline;extra.insert(extra.end(),1024,0);
        require(!normalizeVerifiedRom(extra),"extra program discarded");
        auto shortRom=baseline;shortRom.pop_back();
        require(!normalizeVerifiedRom(shortRom),"truncated program accepted");
        // FF3の追加対応版でも機能の接続先を含むバンク全体が一致する。
        for(const auto* name:{"Final Fantasy 3 (J) [b1].nes","final fantasy 3 (j) [h1].nes","final fantasy 3 (j) [p1].nes"})
        {
            const auto variant=read(folder/name);
            for(unsigned bank:{26u,27u,30u})
            {
                const auto start=16+bank*16384;
                require(std::equal(baseline.begin()+start,baseline.begin()+start+16384,variant.begin()+start),"FF3 feature bank changed");
            }
            require(std::equal(baseline.begin()+16+31*16384+0x3b80,baseline.begin()+16+31*16384+0x3c00,
                variant.begin()+16+31*16384+0x3b80),"FF3 fixed input routine changed");
        }
        const auto ff2=read(folder/"FF2.nes");
        const auto titleVariant=read(folder/"Final Fantasy 2 (J) [hM02][b1].nes");
        require(ff2.size()==titleVariant.size() &&
            std::equal(ff2.begin(),ff2.begin()+262128,titleVariant.begin()) &&
            std::equal(ff2.begin()+262144,ff2.end(),titleVariant.begin()+262144),"FF2 executable or sound changed");
        std::cout<<"PASS rejection, source preservation, PRG preservation, feature banks\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
