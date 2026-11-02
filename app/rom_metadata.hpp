#pragma once
#include "picosha2.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ファイル名や付加情報によらずPRG全体を照合し、既知の処理位置を使う機能を限定する。
// 保存用の識別子は呼出側で元ファイルから計算し、このメモリ上の補正とは分ける。
inline unsigned normalizeVerifiedRom(std::vector<uint8_t>& rom)
{
    if(rom.size()<16 || !std::equal(rom.begin(),rom.begin()+4,"NES\x1a") ||
       rom[5]!=0 || (rom[6]&4) || (rom[7]&0x0c)==8)return 0;
    struct Program {const char* hash; unsigned profile,units,flags,ram;};
    static constexpr Program programs[]={
        {"ba223cc3aa7992ace45dd9c50bc8b29100e18dd77d338b5383d006af47d05ed3",3,16,0x12,0},
        {"5ea46c6b05661114f37c2440e9010f6537ab4d518671acd4aa81d6458c73ca64",3,16,0x23,1},
        {"2c9786c4fc04fe476e83c0e3e06b1577dc244a6c388cd4b31c86f045ef146d12",4,32,0x12,0},
        {"70526ea8ecd37b687d62365f69fe84607524c403ad4cc80438ea6f38f3601b08",122,16,0x23,0},
        // FF2のタイトル文字列違い。実行コードと音声データは一致する。
        {"633877b21745a6cae9d85ff4bd6e8b34acd86afb2cca7436b30854e7f674ea9b",122,16,0x23,0},
        {"7ff89b50156b6f5b3d78d3d2eeec8a9221d9f7b18f8350abf89b7867a205f710",123,32,0x42,0},
        // FF3のデータ違いとリセットパッチ。機能の接続先は変わらない。
        {"84603c7f42add024b98309a320552051ce1e85c19036b5fc4bc33a53473126d0",123,32,0x42,0},
        {"e9f09c759451ddf89ecd1b2374a5d87ddf9aa27833c3e2c8896523012a1c266c",123,32,0x42,0},
        {"175da196ef95e271518acdcfea9139942b4ad35a1046ef97450a611594ea2de1",123,32,0x40,0},
    };
    for(const unsigned units:{16u,32u})
    {
        const std::size_t size=std::size_t(units)*16384;
        if(rom.size()<16+size)continue;
        std::string hash;
        picosha2::hash256_hex_string(rom.begin()+16,rom.begin()+16+size,hash);
        for(const auto& program:programs)
        {
            if(program.units!=units || hash!=program.hash)continue;
            // 末尾情報と同じPRGの重複は除去できるが、未知の追加コードは除去しない。
            if(rom.size()>16+size+512)
            {
                bool repeated=true;
                for(std::size_t i=16+size;i<rom.size();++i)
                    if(rom[i]!=rom[16+(i-16)%size]){repeated=false;break;}
                if(!repeated)
                {
                    // 既知の2ファイルは先頭に完全なDQ4があり、後半の重複部分だけが破損している。
                    // うち1ファイルはDQ3という誤った名前・ヘッダーで収録されている。
                    const auto original=picosha2::hash256_hex_string(rom);
                    if(program.profile!=4 || (original!="d6674e5a53ebe1dbf7d06bca367257ceda19b736c7a17dc3977782eab190665a" &&
                       original!="962699d92a3eb52e9c904613e781f12e311ba78f634e7931338ce5da539deec4"))continue;
                }
            }
            rom.resize(16+size);
            std::fill(rom.begin()+4,rom.begin()+16,uint8_t{0});
            rom[4]=static_cast<uint8_t>(units);
            rom[6]=static_cast<uint8_t>(program.flags);
            rom[8]=static_cast<uint8_t>(program.ram);
            return program.profile;
        }
    }
    return 0;
}

inline bool normalizeDq3Metadata(std::vector<uint8_t>& rom,std::string_view)
{
    return normalizeVerifiedRom(rom)==3;
}
