#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "core_bridge.h"
#include "gamepad_input.hpp"
#include "audio_output.hpp"
#include "picosha2.h"
#include "rom_metadata.hpp"
#include "user_storage.hpp"
#include "ff_sound_rom.hpp"
#include "window_icon.hpp"
#include <bit>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "rom_picker_text.hpp"

#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>
#include <mutex>
static std::mutex androidMutex;
static std::string androidPending,androidRom;
extern "C" JNIEXPORT void JNICALL Java_com_nesdq_app_NesDQActivity_openRom(JNIEnv* env,jclass,jbyteArray path)
{
    const auto size=env->GetArrayLength(path);
    std::string value(static_cast<size_t>(size),'\0');
    env->GetByteArrayRegion(path,0,size,reinterpret_cast<jbyte*>(value.data()));
    std::lock_guard<std::mutex> guard(androidMutex);androidPending=std::move(value);
}
extern "C" JNIEXPORT void JNICALL Java_com_nesdq_app_NesDQActivity_touchKey(JNIEnv*,jclass,jint action,jboolean down)
{
    static const SDL_Keycode keys[]={SDLK_UP,SDLK_DOWN,SDLK_LEFT,SDLK_RIGHT,SDLK_X,SDLK_Z,SDLK_A,SDLK_S,SDLK_Q,SDLK_F1,SDLK_F1,SDLK_O};
    if(action<0 || action>=12)return;
    SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.key=keys[action];e.key.down=down;e.key.which=SDL_KeyboardID(-2);e.key.mod=action==10?SDL_KMOD_SHIFT:SDL_KMOD_NONE;SDL_PushEvent(&e);
}
#endif

namespace fs = std::filesystem;
static fs::path utf8Path(const std::string& value)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
static std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open: " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
static void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes)
{
    userstore::write(path,reinterpret_cast<const char*>(bytes.data()),bytes.size());
    userstore::flush();
}
#ifdef SDL_PLATFORM_ANDROID
static void requestAndroidRom()
{
    userstore::Bridge bridge;
    const auto method=bridge.env->GetMethodID(bridge.type,"requestRomSelection","()V");
    bridge.check();bridge.env->CallVoidMethod(bridge.activity,method);bridge.check();
}
static void discardAndroidRom(const fs::path& path)
{
    userstore::Bridge bridge;const auto bytes=userstore::utf8(path);
    auto value=bridge.env->NewByteArray(static_cast<jsize>(bytes.size()));
    bridge.env->SetByteArrayRegion(value,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data()));
    const auto method=bridge.env->GetMethodID(bridge.type,"romConsumed","([B)V");
    bridge.check();bridge.env->CallVoidMethod(bridge.activity,method,value);bridge.env->DeleteLocalRef(value);bridge.check();
}
#endif
#ifndef SDL_PLATFORM_ANDROID
static fs::path chooseRomFile(const fs::path& data)
{
    struct Selection
    {
        std::mutex mutex;
        bool done=false;
        std::string path,error;
    } selection;
    auto folder=utf8Path(SDL_GetBasePath())/"Rom";
    const auto setting=data/"rom-folder.txt";
    if (fs::exists(setting))
    {
        const auto bytes=readFile(setting);
        const auto previous=utf8Path(std::string(bytes.begin(),bytes.end()));
        if (fs::is_directory(previous)) folder=previous;
    }
    const std::string initial=userstore::utf8(folder);
    static const SDL_DialogFileFilter filter={"NES ROM", "nes"};
    SDL_ShowOpenFileDialog([](void* userdata,const char* const* files,int)
    {
        auto& result=*static_cast<Selection*>(userdata);
        std::lock_guard<std::mutex> guard(result.mutex);
        if (!files) result.error=SDL_GetError();
        else if (*files) result.path=*files;
        result.done=true;
    },&selection,nullptr,&filter,1,initial.c_str(),false);
    for (;;)
    {
        SDL_PumpEvents();
        {std::lock_guard<std::mutex> guard(selection.mutex);if (selection.done) break;}
        SDL_Delay(16);
    }
    if (!selection.error.empty()) throw std::runtime_error(selection.error);
    if (selection.path.empty()) return {};
    const auto path=utf8Path(selection.path);
    fs::create_directories(data);
    const auto parent=userstore::utf8(path.parent_path());
    writeFile(setting,std::vector<uint8_t>(parent.begin(),parent.end()));
    return path;
}
static fs::path chooseRom(const fs::path& data)
{
    auto folder=utf8Path(SDL_GetBasePath())/"Rom";
    const auto setting=data/"rom-folder.txt";
    if(fs::exists(setting))
    {
        const auto bytes=readFile(setting);
        const auto previous=utf8Path(std::string(bytes.begin(),bytes.end()));
        if(fs::is_directory(previous))folder=previous;
    }
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;
    if(!SDL_CreateWindowAndRenderer("NesDQ | Select ROM",768,720,SDL_WINDOW_RESIZABLE,&window,&renderer))throw std::runtime_error(SDL_GetError());
    struct Graphics {SDL_Window* w;SDL_Renderer* r;~Graphics(){SDL_DestroyRenderer(r);SDL_DestroyWindow(w);}} graphics{window,renderer};
    setAppWindowIcon(window);
    SDL_SetRenderLogicalPresentation(renderer,512,480,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    GamepadInput gamepad;
    std::vector<fs::path> files;
    std::vector<std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>> labels;
    auto refresh=[&]
    {
        files.clear();
        if(fs::is_directory(folder))for(const auto& entry:fs::directory_iterator(folder))
        {
            auto extension=entry.path().extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(entry.is_directory() || (entry.is_regular_file() && extension==".nes"))files.push_back(entry.path());
        }
        std::sort(files.begin(),files.end(),[](const auto& a,const auto& b)
        {
            const bool ad=fs::is_directory(a),bd=fs::is_directory(b);
            return ad!=bd?ad:a.filename()<b.filename();
        });
#ifdef SDL_PLATFORM_WINDOWS
        labels.clear();
        labels.emplace_back(romPickerText(renderer,"../"),SDL_DestroyTexture);
        for(const auto& path:files)labels.emplace_back(romPickerText(renderer,userstore::utf8(path.filename())+(fs::is_directory(path)?"/":"")),SDL_DestroyTexture);
        labels.emplace_back(romPickerText(renderer,"Browse files..."),SDL_DestroyTexture);
#endif
    };
    refresh();
    gamepad.poll(false,true);
    size_t selected=0;
    bool focused=true;
    for(;;)
    {
        gamepad.poll(focused,true);
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID==SDL_GetWindowID(window)))return {};
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST)focused=false;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;
            if(event.type!=SDL_EVENT_KEY_DOWN || !focused)continue;
            const auto key=event.key.key;
            const size_t count=files.size()+2;
            if(key==SDLK_UP)selected=(selected+count-1)%count;
            if(key==SDLK_DOWN)selected=(selected+1)%count;
            if(event.key.repeat && key!=SDLK_UP && key!=SDLK_DOWN)continue;
            if(key==SDLK_ESCAPE || key==SDLK_Z || key==SDLK_BACKSPACE)return {};
            if(key==SDLK_LEFT)
            {
                if(folder.has_parent_path() && folder.parent_path()!=folder){folder=folder.parent_path();selected=0;refresh();}
                else return {};
            }
            if(key==SDLK_RETURN || key==SDLK_X || key==SDLK_RIGHT)
            {
                if(selected==0){folder=folder.parent_path();selected=0;refresh();}
                else if(selected==files.size()+1)
                {
                    const auto path=chooseRomFile(data);
                    if(!path.empty())return path;
                }
                else if(fs::is_directory(files[selected-1])){folder=files[selected-1];selected=0;refresh();}
                else
                {
                    const auto path=files[selected-1];
                    fs::create_directories(data);
                    const auto parent=userstore::utf8(path.parent_path());
                    writeFile(setting,std::vector<uint8_t>(parent.begin(),parent.end()));
                    return path;
                }
            }
        }
        SDL_SetWindowTitle(window,("NesDQ | "+userstore::utf8(folder)+" | "+(selected>0 && selected<=files.size()?userstore::utf8(files[selected-1].filename()):selected==0?"..":"Browse files...")).c_str());
        SDL_SetRenderDrawColor(renderer,16,23,34,255);SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,255,255,255,255);
        SDL_RenderDebugText(renderer,16,16,"Select ROM");
        SDL_RenderDebugText(renderer,16,36,"Up/Down: Move  X: OK  Z: Back  Left: Parent");
        SDL_RenderDebugText(renderer,16,48,gamepad.navigationHint());
        const size_t first=selected<18?0:selected-17;
        for(size_t i=first;i<files.size()+2 && i<first+18;++i)
        {
            const Uint8 shade=i==selected?255:160;SDL_SetRenderDrawColor(renderer,shade,shade,shade,255);
#ifdef SDL_PLATFORM_WINDOWS
            SDL_RenderDebugText(renderer,8,66+20*float(i-first),i==selected?">":" ");
            SDL_SetTextureColorMod(labels[i].get(),shade,shade,shade);
            const SDL_FRect box{24,64+20*float(i-first),480,20};SDL_RenderTexture(renderer,labels[i].get(),nullptr,&box);
#else
            const auto label=i==0?std::string("../"):i==files.size()+1?std::string("Browse files..."):
                userstore::utf8(files[i-1].filename())+(fs::is_directory(files[i-1])?"/":"");
            SDL_RenderDebugText(renderer,16,64+20*float(i-first),((i==selected?"> ":"  ")+label).c_str());
#endif
        }
        SDL_RenderPresent(renderer);SDL_Delay(16);
    }
}
#endif
static unsigned button(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_X: case SDLK_RETURN: return PAD_A;
    case SDLK_Z: case SDLK_BACKSPACE: return PAD_B;
    case SDLK_A: case SDLK_TAB: return PAD_SELECT;
    case SDLK_S: case SDLK_SPACE: return PAD_START;
    case SDLK_UP: return PAD_UP; case SDLK_DOWN: return PAD_DOWN;
    case SDLK_LEFT: return PAD_LEFT; case SDLK_RIGHT: return PAD_RIGHT;
    default: return 0;
    }
}
struct Session
{
    std::vector<uint8_t> rom;
    std::unique_ptr<WiizCore, decltype(&wiiz_core_delete)> core{nullptr, wiiz_core_delete};
    std::vector<uint8_t> soundRom;
    std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)> music{nullptr,wiiz_core_delete};
    bool supportsTurbo=false,musicActive=false,swapAb=false,maxSkills=false,freeJobs=false;
    unsigned speed=1,profile=0,experienceFactor=1,goldFactor=1;
    fs::path folder;
    fs::path saveName;
    std::string identity;
    std::vector<uint8_t> lastSaved;
    bool ffAudio() const {return profile==122 || profile==123;}
    bool supportsRewards() const {return profile==3 || profile==4 || profile==122 || profile==123;}
    bool supportsExperienceRewards() const {return supportsRewards() && profile!=122;}
    bool equalSpeedAudio() const {return profile==3 || profile==4 || ffAudio();}
    void bindProfile(WiizCore* game)
    {
        if(equalSpeedAudio())wiiz_core_profile_source(game,profile,0);
        if(!wiiz_core_rewards(game,experienceFactor,goldFactor))throw std::runtime_error("Invalid battle reward settings");
        if(!wiiz_core_max_skills(game,maxSkills?1:0))throw std::runtime_error("Invalid skill settings");
        if(!wiiz_core_free_jobs(game,freeJobs?1:0))throw std::runtime_error("Invalid job settings");
    }
    Session(const fs::path& path, const fs::path& data, const fs::path& previous={}) : rom(readFile(path)), folder(data), saveName(path.stem())
    {
        saveName += ".sav";
        if (rom.size() < 16 || std::string(reinterpret_cast<char*>(rom.data()), 4) != std::string("NES\x1a", 4))
            throw std::runtime_error("Invalid iNES ROM: " + path.string());
        identity = picosha2::hash256_hex_string(rom);
        profile=normalizeVerifiedRom(rom);
        const size_t expected = 16 + ((rom[6] & 4) ? 512 : 0) + size_t(rom[4])*16384 + size_t(rom[5])*8192;
        if (rom.size() < expected) throw std::runtime_error("Truncated ROM: " + path.string());
        if((rom[7]&0x0c)!=8 && rom[8]>8)
            throw std::runtime_error("Unsupported PRG-RAM size in ROM header: "+path.string());
        // 同一PRGの二重収録はメモリ上で正規化し、元ROMと保存識別子は保持する。
        if (rom[4]==64 && rom[5]==0 && (rom[6]>>4)==1 && (rom[7]&0xf0)==0 && !(rom[6]&4) &&
            rom.size()==16+1048576 && std::equal(rom.begin()+16,rom.begin()+16+524288,rom.begin()+16+524288))
        {
            rom.resize(16+524288);rom[4]=32;
            SDL_Log("Normalized identical duplicated MMC1 PRG in memory (1 MiB -> 512 KiB)");
        }
        core.reset(wiiz_core_new(rom.data(), rom.size()));
        if (!core) throw std::runtime_error("Unsupported or invalid ROM: " + path.string());
        SDL_Log("ROM profile=%u, verified turbo=%s",profile,profile?"yes":"no");
        supportsTurbo=profile!=0;
        bindProfile(core.get());
        if (equalSpeedAudio())
        {
            soundRom=rom;
            if(ffAudio())
            {
                if(!prepareFfSoundRom(soundRom,profile))throw std::runtime_error("Cannot prepare FF sound ROM");
            }
            else
            {
                const auto vector=soundRom.size()-6;
                soundRom[vector]=0x80;soundRom[vector+1]=7;soundRom[vector+2]=0;soundRom[vector+3]=7;soundRom[vector+4]=0xf0;soundRom[vector+5]=7;
            }
            wiiz_core_profile_source(core.get(),profile,0);
            music=makePlayer();
        }
        userstore::mkdir(folder);
        userstore::mkdir(folder/"Save");
        userstore::mkdir(folder/"state");
        const auto controls=folder/"ab-swap.cfg";
        if (userstore::exists(controls))
        {
            const auto bytes=userstore::read(controls,4);
            swapAb=bytes.size()==1 && bytes[0]=='1';
        }
        // Android初期版の内部保存は共有先に同名データがないときだけコピーする。
        if (!previous.empty() && previous!=folder)
        {
            for (unsigned slot=0;slot<=5;++slot)
            {
                const auto name=identity+(slot?"."+std::to_string(slot)+".state":".sav");
                const auto target=folder/(slot?"state":"Save")/name;
                auto source=previous/(slot?"state":"Save")/name;
                if (!userstore::exists(source)) source=previous/name;
                if (!userstore::exists(target) && userstore::exists(source)) writeFile(target,userstore::read(source,16*1024*1024));
            }
        }
        const auto save=savePath();
        // ROM名の保存を優先し、旧ハッシュ名のデータは元を残して引き継ぐ。
        for (const auto& source : {folder/"Save"/(identity+".sav"),folder/saveName})
        {
            if (!userstore::exists(save) && userstore::exists(source)) writeFile(save,userstore::read(source,1024*1024));
        }
        if (!previous.empty() && previous!=folder)
        {
            for (const auto& source : {previous/"Save"/saveName,previous/saveName})
                if (!userstore::exists(save) && userstore::exists(source)) writeFile(save,userstore::read(source,1024*1024));
        }
        // 初期試作版の配置からはコピーして引き継ぐ。
        const auto legacy=folder/(identity+".sav");
        if (!userstore::exists(save) && userstore::exists(legacy)) writeFile(save,userstore::read(legacy,1024*1024));
        if (userstore::exists(save))
        {
            auto bytes = userstore::read(save,1024*1024);
            if (bytes.size() != wiiz_core_sram_size(core.get()) || !wiiz_core_sram_write(core.get(), bytes.data(), bytes.size()))
                throw std::runtime_error("Invalid SRAM: " + save.string());
            lastSaved=std::move(bytes);
        }
        const auto setting=folder/(identity+".speed");
        if (supportsTurbo && userstore::exists(setting))
        {
            const auto bytes=userstore::read(setting,4);
            if (bytes.size()==1 && bytes[0]>='1' && bytes[0]<='6') {speed=bytes[0]-'0';musicActive=bool(music) && speed>1;}
        }
        const auto rewards=folder/(identity+".rewards");
        if(supportsRewards() && userstore::exists(rewards))
        {
            const auto bytes=userstore::read(rewards,2);
            if(bytes.size()!=2 || !wiiz_core_rewards(core.get(),bytes[0],bytes[1]))
                throw std::runtime_error("Invalid battle reward settings: "+rewards.string());
            experienceFactor=bytes[0];goldFactor=bytes[1];
        }
        const auto skills=folder/(identity+".max-skills");
        if(ffAudio() && userstore::exists(skills))
        {
            const auto bytes=userstore::read(skills,1);
            if(bytes.size()!=1 || (bytes[0]!='0' && bytes[0]!='1'))
                throw std::runtime_error("Invalid skill settings: "+skills.string());
            maxSkills=bytes[0]=='1';
            if(!wiiz_core_max_skills(core.get(),maxSkills?1:0))throw std::runtime_error("Invalid skill settings");
        }
        const auto jobs=folder/(identity+".free-jobs");
        if(profile==123 && userstore::exists(jobs))
        {
            const auto bytes=userstore::read(jobs,1);
            if(bytes.size()!=1 || (bytes[0]!='0' && bytes[0]!='1'))
                throw std::runtime_error("Invalid job settings: "+jobs.string());
            freeJobs=bytes[0]=='1';
            if(!wiiz_core_free_jobs(core.get(),freeJobs?1:0))throw std::runtime_error("Invalid job settings");
        }
    }
    std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)> makePlayer()
    {
        std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)> result(wiiz_core_new(soundRom.data(),soundRom.size()),wiiz_core_delete);
        if (!result || !wiiz_core_profile_player(result.get(),profile)) throw std::runtime_error("Cannot initialize NES sound driver");
        for (unsigned i=0;i<4;++i) if (!wiiz_core_frame(result.get())) throw std::runtime_error("NES sound initialization failed");
        return result;
    }
    void resumeMusic(WiizCore* game,WiizCore* player)
    {
        if(!player)return;
        const auto current=wiiz_core_current_music(game);
        if(profile==122)
        {
            if(current<31 && !wiiz_core_sound_command(player,0x140|current))throw std::runtime_error("Cannot resume FF2 BGM");
        }
        else if(profile==123)
        {
            if(!wiiz_core_sound_command(player,0x300|current) || !wiiz_core_sound_command(player,0x201))throw std::runtime_error("Cannot resume FF3 BGM");
        }
        else if(!wiiz_core_sound_command(player,0x100|current))throw std::runtime_error("Cannot resume BGM");
    }
    void setSpeed(unsigned value)
    {
        if (value<1 || value>6 || (!supportsTurbo && value!=1)) throw std::runtime_error("This ROM does not support verified turbo");
        speed=value;
        if (music && value>1) musicActive=true;
        writeFile(folder/(identity+".speed"),std::vector<uint8_t>{uint8_t('0'+value)});
    }
    void setSwapAb(bool value)
    {
        writeFile(folder/"ab-swap.cfg",std::vector<uint8_t>{uint8_t(value?'1':'0')});
        swapAb=value;
    }
    void cycleReward(bool experience,bool forward)
    {
        if(!supportsRewards() || (experience && !supportsExperienceRewards()))return;
        constexpr std::array<unsigned,6> factors={1,4,8,16,64,128};
        const auto current=experience?experienceFactor:goldFactor;
        const auto index=static_cast<size_t>(std::find(factors.begin(),factors.end(),current)-factors.begin());
        const auto value=factors[(index+(forward?1:factors.size()-1))%factors.size()];
        const unsigned nextExperience=experience?value:experienceFactor,nextGold=experience?goldFactor:value;
        writeFile(folder/(identity+".rewards"),{uint8_t(nextExperience),uint8_t(nextGold)});
        experienceFactor=nextExperience;goldFactor=nextGold;
        if(!wiiz_core_rewards(core.get(),experienceFactor,goldFactor))throw std::runtime_error("Invalid battle reward settings");
    }
    void toggleMaxSkills()
    {
        if(!ffAudio())return;
        writeFile(folder/(identity+".max-skills"),{uint8_t(maxSkills?'0':'1')});
        maxSkills=!maxSkills;
        if(!wiiz_core_max_skills(core.get(),maxSkills?1:0))throw std::runtime_error("Invalid skill settings");
    }
    void toggleFreeJobs()
    {
        if(profile!=123)return;
        writeFile(folder/(identity+".free-jobs"),{uint8_t(freeJobs?'0':'1')});
        freeJobs=!freeJobs;
        if(!wiiz_core_free_jobs(core.get(),freeJobs?1:0))throw std::runtime_error("Invalid job settings");
    }
    void reset()
    {
        wiiz_core_reset(core.get());
        if (equalSpeedAudio())music=makePlayer();
        bindProfile(core.get());
    }
    void tick(unsigned buttons)
    {
        // 全入力経路を合成した後、ゲームへ渡すA/Bだけを交換する。
        if (swapAb) buttons=(buttons&~(PAD_A|PAD_B))|((buttons&PAD_A)?PAD_B:0)|((buttons&PAD_B)?PAD_A:0);
        wiiz_core_input_speed(core.get(),buttons,speed);
        unsigned musicSteps=0;
        for (unsigned i=0;i<speed;++i)
        {
            if (!wiiz_core_frame(core.get())) throw std::runtime_error("Game core stopped");
            musicSteps=std::max(musicSteps,wiiz_core_music_steps(core.get()));
            if (music) while (const auto command=wiiz_core_take_sound(core.get()))
                if (!wiiz_core_sound_command(music.get(),command)) throw std::runtime_error("NES sound command queue overflow");
        }
        if(ffAudio() && !wiiz_core_ff_clock(music.get(),musicSteps))throw std::runtime_error("Invalid FF sound clock");
        if (music && !wiiz_core_frame(music.get())) throw std::runtime_error("NES sound driver stopped");
    }
    const float* audio(size_t* count)
    {
        return wiiz_core_audio(musicActive?music.get():core.get(),count);
    }
    fs::path savePath() const {return folder/"Save"/saveName;}
    void saveRam(bool wait=true)
    {
        std::vector<uint8_t> bytes(wiiz_core_sram_size(core.get()));
        if (bytes.empty()) return;
        if (!wiiz_core_sram_read(core.get(), bytes.data(), bytes.size())) throw std::runtime_error("SRAM serialization failed");
        if (bytes!=lastSaved)
        {
            if(wait)writeFile(savePath(),bytes);
            else userstore::write(savePath(),reinterpret_cast<const char*>(bytes.data()),bytes.size());
            lastSaved=std::move(bytes);
        }
        if(wait)userstore::flush();else userstore::check();
    }
    void state(unsigned slot, bool load)
    {
        if (slot<1 || slot>5) throw std::runtime_error("Invalid state slot");
        const auto name=identity+"."+std::to_string(slot)+".state";
        const auto path=folder/"state"/name;
        if (load && !userstore::exists(path) && userstore::exists(folder/name)) writeFile(path,userstore::read(folder/name,16*1024*1024));
        const auto size=wiiz_core_state_size(core.get());
        auto put=[](std::vector<uint8_t>& bytes,uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));};
        auto get=[](const std::vector<uint8_t>& bytes,size_t at){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(bytes.at(at+i))<<(i*8);return value;};
        if (load)
        {
            const auto bytes=userstore::read(path,16*1024*1024);
            std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)> game(wiiz_core_new(rom.data(),rom.size()),wiiz_core_delete);
            if (!game) throw std::runtime_error("Cannot restore game core");
            bindProfile(game.get());
            auto player=equalSpeedAudio()?makePlayer():decltype(music)(nullptr,wiiz_core_delete);
            unsigned restoredSpeed=speed;bool restoredMusic=musicActive;
            if (bytes.size()==size)
            {
                if (!wiiz_core_state_read(game.get(),bytes.data(),bytes.size())) throw std::runtime_error("Invalid legacy state");
                resumeMusic(game.get(),player.get());
            }
            else
            {
                if (bytes.size()<160 || std::string(bytes.begin(),bytes.begin()+8)!="NESDQST2") throw std::runtime_error("Incompatible state: "+path.string());
                const size_t gameSize=get(bytes,8),soundSize=get(bytes,12),gameQueue=get(bytes,24),soundQueue=get(bytes,28);
                restoredSpeed=get(bytes,16);const unsigned flags=get(bytes,20);restoredMusic=bool(flags&1);
                const bool legacyFf=ffAudio() && soundSize==0;
                if (gameSize!=size || (!legacyFf && soundSize!=(player?wiiz_core_state_size(player.get()):0)) ||
                    gameQueue>42 || soundQueue>42 || bytes.size()!=32+gameSize+soundSize+4*(gameQueue+soundQueue)+128+((flags&2)?8:0) || restoredSpeed<1 || restoredSpeed>6 || flags>3 ||
                    ((flags&2) && !ffAudio()) || ((flags&1) && (!player || legacyFf)) || (legacyFf && (gameQueue || soundQueue)) || (!supportsTurbo && (restoredSpeed!=1 || flags)))
                    throw std::runtime_error("State profile mismatch");
                const std::vector<uint8_t> payload(bytes.begin(),bytes.end()-64);
                if (std::string(bytes.end()-128,bytes.end()-64)!=identity || picosha2::hash256_hex_string(payload)!=std::string(bytes.end()-64,bytes.end())) throw std::runtime_error("State checksum mismatch");
                if (!wiiz_core_state_read(game.get(),bytes.data()+32,gameSize) || (player && !legacyFf && !wiiz_core_state_read(player.get(),bytes.data()+32+gameSize,soundSize)))
                    throw std::runtime_error("Invalid state payload");
                if(legacyFf){resumeMusic(game.get(),player.get());restoredMusic=restoredSpeed>1;}
                for(size_t i=0;i<gameQueue+soundQueue;++i)
                {
                    const unsigned event=get(bytes,32+gameSize+soundSize+i*4);
                    auto* target=i<gameQueue?game.get():player.get();
                    if(!target || !wiiz_core_sound_command(target,event)) throw std::runtime_error("Invalid sound queue");
                }
                if(flags&2)
                {
                    const auto offset=32+gameSize+soundSize+4*(gameQueue+soundQueue);
                    const auto restoredSum=std::bit_cast<float>(get(bytes,offset));const auto restoredPending=get(bytes,offset+4);
                    if(!std::isfinite(restoredSum) || restoredPending>=restoredSpeed)throw std::runtime_error("Invalid audio state");
                }
            }
            bindProfile(game.get());
            core=std::move(game);music=std::move(player);speed=restoredSpeed;musicActive=restoredMusic;
        }
        else
        {
            const auto soundSize=music?wiiz_core_state_size(music.get()):0;
            std::vector<uint8_t> bytes={'N','E','S','D','Q','S','T','2'};
            put(bytes,static_cast<uint32_t>(size));put(bytes,static_cast<uint32_t>(soundSize));put(bytes,speed);put(bytes,musicActive?1:0);
            const unsigned gameQueue=wiiz_core_sound_pending(core.get()),soundQueue=music?wiiz_core_sound_pending(music.get()):0;
            put(bytes,gameQueue);put(bytes,soundQueue);
            bytes.resize(32+size+soundSize);
            if (!wiiz_core_state_write(core.get(),bytes.data()+32,size) || (music && !wiiz_core_state_write(music.get(),bytes.data()+32+size,soundSize))) throw std::runtime_error("State serialization failed");
            for(unsigned i=0;i<gameQueue;++i)put(bytes,wiiz_core_sound_at(core.get(),i));
            for(unsigned i=0;i<soundQueue;++i)put(bytes,wiiz_core_sound_at(music.get(),i));
            bytes.insert(bytes.end(),identity.begin(),identity.end());
            const auto checksum=picosha2::hash256_hex_string(bytes);bytes.insert(bytes.end(),checksum.begin(),checksum.end());
            writeFile(path,bytes);
        }
    }
};
// 読込が成功するまで現在のゲームを所有し続ける。同じROMの再選択も保存後に読む。
static bool replaceSession(std::unique_ptr<Session>& session,const fs::path& rom,const fs::path& data,const fs::path& previous)
{
    if(rom.empty())return false;
    session->saveRam();userstore::flush();
    auto next=std::make_unique<Session>(rom,data,previous);
    session.swap(next);return true;
}
static int checkSwitch(std::unique_ptr<Session>& session,const fs::path& first,const fs::path& second,const fs::path& data)
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto identity=session->identity;
    const auto firstSave=session->savePath();
    const unsigned firstSpeed=session->supportsTurbo?3:1;
    for(unsigned i=0;i<90;++i)session->tick(0);
    session->setSpeed(firstSpeed);session->setSwapAb(true);session->state(1,false);
    const auto firstState=data/"state"/(identity+".1.state");
    const auto firstStateBytes=userstore::read(firstState,16*1024*1024);
    std::vector<uint8_t> original(wiiz_core_sram_size(session->core.get()));
    require(original.empty() || wiiz_core_sram_read(session->core.get(),original.data(),original.size()),"Cannot read first SRAM");
    auto pattern=original;for(size_t i=0;i<pattern.size();++i)pattern[i]=uint8_t(i*17+23);
    require(pattern.empty() || wiiz_core_sram_write(session->core.get(),pattern.data(),pattern.size()),"Cannot seed SRAM");
    auto* active=session.get();
    require(!replaceSession(session,{},data,{}) && session.get()==active,"Cancel changed game");
    std::vector<uint8_t> before(wiiz_core_state_size(session->core.get())),after(before.size());
    require(wiiz_core_state_write(session->core.get(),before.data(),before.size()),"Cannot capture active game");
    const auto invalid=data/"invalid.nes";writeFile(invalid,{'b','a','d'});
    bool rejected=false;
    try {replaceSession(session,invalid,data,{});}catch(const std::exception&){rejected=true;}
    require(rejected && session.get()==active && session->identity==identity,"Failed ROM replaced active game");
    require(wiiz_core_state_write(session->core.get(),after.data(),after.size()) && before==after,"Failed selection changed game state");
    require(replaceSession(session,second,data,{}),"Second ROM not loaded");
    const auto secondIdentity=session->identity;
    require(secondIdentity!=identity && session->swapAb,"Wrong second game or controls");
    require(pattern.empty() || userstore::read(firstSave,1024*1024)==pattern,"First SRAM was not flushed");
    const unsigned secondSpeed=session->supportsTurbo?4:1;
    session->setSpeed(secondSpeed);
    for(unsigned i=0;i<90;++i)session->tick(0);
    session->state(1,false);
    require(userstore::read(firstState,16*1024*1024)==firstStateBytes,"Other game overwrote state slot");
    require(replaceSession(session,first,data,{}) && session->identity==identity && session->speed==firstSpeed && session->swapAb,"First game settings lost");
    std::vector<uint8_t> restored(pattern.size());
    require(pattern.empty() || (wiiz_core_sram_read(session->core.get(),restored.data(),restored.size()) && restored==pattern),"Returning game lost SRAM");
    require(original.empty() || wiiz_core_sram_write(session->core.get(),original.data(),original.size()),"Cannot restore original SRAM");
    require(replaceSession(session,second,data,{}) && session->identity==secondIdentity && session->speed==secondSpeed,"Second game settings lost");
    std::vector<uint8_t> secondOriginal(wiiz_core_sram_size(session->core.get()));
    require(secondOriginal.empty() || wiiz_core_sram_read(session->core.get(),secondOriginal.data(),secondOriginal.size()),"Cannot read second SRAM");
    auto samePattern=secondOriginal;for(size_t i=0;i<samePattern.size();++i)samePattern[i]=uint8_t(i*11+7);
    require(samePattern.empty() || wiiz_core_sram_write(session->core.get(),samePattern.data(),samePattern.size()),"Cannot seed reload SRAM");
    require(replaceSession(session,second,data,{}) && session->identity==secondIdentity && session->speed==secondSpeed,"Same ROM reload failed");
    restored.resize(samePattern.size());
    require(samePattern.empty() || (wiiz_core_sram_read(session->core.get(),restored.data(),restored.size()) && restored==samePattern),"Same ROM read before saving");
    require(secondOriginal.empty() || wiiz_core_sram_write(session->core.get(),secondOriginal.data(),secondOriginal.size()),"Cannot restore second SRAM");
    session->saveRam();userstore::flush();
    std::cout<<"PASS ROM switch: cancel, invalid ROM, SRAM, settings, state isolation, return, same-ROM reload\n";
    return 0;
}
struct Replay
{
    unsigned frame;
    SDL_Keycode key;
    bool down,shift;
};
static std::vector<Replay> readReplay(const fs::path& path)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read replay: "+path.string());
    std::vector<Replay> result;std::string line;unsigned previous=0;
    while (std::getline(file,line))
    {
        if (line.empty() || line[0]=='#') continue;
        unsigned frame;std::string key,action;std::istringstream row(line);
        if (!(row>>frame>>key>>action) || frame<previous || (action!="down" && action!="up")) throw std::runtime_error("Invalid replay line: "+line);
        const bool shift=key.starts_with("Shift+");if(shift)key.erase(0,6);
        const auto code=SDL_GetKeyFromName(key.c_str());
        if (!code) throw std::runtime_error("Unknown replay key: "+key);
        result.push_back({frame,code,action=="down",shift});previous=frame;
    }
    return result;
}
static int check(Session& session)
{
    auto* core = session.core.get();
    std::array<uint32_t, 256*240> pixels{};
    size_t samples = 0; double energy = 0;
    for (int frame = 0; frame < 1200; ++frame)
    {
        wiiz_core_input(core, frame > 600 && frame % 120 < 6 ? PAD_START : 0);
        if (!wiiz_core_frame(core)) throw std::runtime_error("Core stopped at frame " + std::to_string(frame));
        size_t count; const auto* audio = wiiz_core_audio(core, &count); samples += count;
        for (size_t i = 0; i < count; ++i) { if (!std::isfinite(audio[i])) throw std::runtime_error("Invalid audio"); energy += audio[i]*audio[i]; }
    }
    wiiz_core_input(core, 0);
    session.state(1, false);
    for (int i=0;i<60;++i) if (!wiiz_core_frame(core)) throw std::runtime_error("Frame failed");
    wiiz_core_pixels(core, pixels.data()); const auto reference = pixels;
    session.state(1, true);core=session.core.get();
    for (int i=0;i<60;++i) if (!wiiz_core_frame(core)) throw std::runtime_error("Restored frame failed");
    wiiz_core_pixels(core, pixels.data());
    if (pixels != reference) throw std::runtime_error("State replay diverged");
    if (std::all_of(pixels.begin(), pixels.end(), [&](auto p){return p == pixels[0];}) || !samples || energy == 0)
        throw std::runtime_error("Blank video or silent audio");
    for (unsigned slot=2;slot<=5;++slot) {session.state(slot,false);session.state(slot,true);}
    core=session.core.get();
    std::vector<uint8_t> before(wiiz_core_sram_size(core));
    if (!before.empty() && !wiiz_core_sram_read(core,before.data(),before.size())) throw std::runtime_error("SRAM read failed");
    session.saveRam();
    if (!before.empty())
    {
        auto saved=userstore::read(session.savePath(),1024*1024);
        if(saved!=before || !wiiz_core_sram_write(core,saved.data(),saved.size())) throw std::runtime_error("SRAM roundtrip failed");
    }
    if (session.supportsTurbo)
    {
        for(unsigned speed=2;speed<=6;++speed)
        {
            session.setSpeed(speed);
            for(unsigned i=0;i<60;++i)session.tick(0);
            session.state(1,false);
            auto advance=[&]()
            {
                std::vector<float> result;
                for(unsigned i=0;i<60;++i)
                {
                    session.tick(0);
                    size_t count;const auto* audio=session.audio(&count);result.insert(result.end(),audio,audio+count);
                }
                return result;
            };
            const auto expectedAudio=advance();
            std::array<uint32_t,256*240> expectedPixels;
            wiiz_core_pixels(session.core.get(),expectedPixels.data());
            session.state(1,true);
            if(advance()!=expectedAudio)throw std::runtime_error("Sound state replay diverged");
            wiiz_core_pixels(session.core.get(),pixels.data());
            if(pixels!=expectedPixels)throw std::runtime_error("Turbo video state replay diverged");
            std::cout<<"PASS state speed="<<speed<<" video=identical audio=identical\n";
        }
        core=session.core.get();
        // チェックサム不一致では実行中のゲームを差し替えない。
        const auto path=session.folder/"state"/(session.identity+".1.state");
        auto bytes=userstore::read(path,16*1024*1024);const auto valid=bytes;bytes[40]^=1;writeFile(path,bytes);
        auto* original=session.core.get();bool rejected=false;
        try {session.state(1,true);}catch(const std::exception&){rejected=true;}
        writeFile(path,valid);
        if(!rejected || original!=session.core.get())throw std::runtime_error("Invalid state changed active game");
        std::vector<uint8_t> legacy(wiiz_core_state_size(session.core.get()));
        if(!wiiz_core_state_write(session.core.get(),legacy.data(),legacy.size()))throw std::runtime_error("Legacy state serialization failed");
        const auto legacyPath=session.folder/"state"/(session.identity+".5.state");writeFile(legacyPath,legacy);
        session.state(5,true);core=session.core.get();
        session.tick(0);
        std::cout<<"PASS legacy_state=loaded\n";
        std::cout<<"PASS damaged_state=rejected active_game=preserved\n";
    }
    auto* surface=SDL_CreateSurfaceFrom(256,240,SDL_PIXELFORMAT_RGBA32,pixels.data(),256*4);
    if (!surface) throw std::runtime_error(SDL_GetError());
    const auto capture=(session.folder/("dq"+std::to_string(session.profile)+"-frame.bmp")).string();
    const bool captured=SDL_SaveBMP(surface,capture.c_str()); SDL_DestroySurface(surface);
    if (!captured) throw std::runtime_error(SDL_GetError());
    std::cout << "PASS mapper=" << wiiz_core_mapper(core) << " frames=1320 audio_samples=" << samples
              << " energy=" << energy << " state_replay=identical slots=5 SRAM=roundtrip\n";
    return 0;
}
int main(int argc, char** argv)
{
    struct Quit {bool initialized=false;~Quit(){if(initialized)SDL_Quit();}} quit;
    try
    {
        const bool checking=argc>1 && std::string(argv[1])=="--check";
        const bool switchChecking=argc>1 && std::string(argv[1])=="--switch-check";
        const bool realTime=argc>1 && std::string(argv[1])=="--realtime-check";
        const bool smoke=realTime || (argc>1 && std::string(argv[1])=="--smoke");
        if (smoke && argc!=7) throw std::runtime_error("Usage: nesdq --smoke ROM FRAMES REPLAY DATA BMP");
        const unsigned smokeFrames=smoke?static_cast<unsigned>(std::stoul(argv[3])):0;
        if (smoke && (!smokeFrames || smokeFrames>36000)) throw std::runtime_error("Invalid smoke frame limit");
        GamepadInput::configure();
        if (!SDL_Init(checking || switchChecking ? 0 : SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
        quit.initialized=true;
        fs::path data;
        if (!checking && !switchChecking && !smoke)
        {
            char* pref = SDL_GetPrefPath("", "NesDQ");
            if (!pref) throw std::runtime_error(SDL_GetError());
            data=utf8Path(pref);SDL_free(pref);
        }
        const auto previousData=data;
        fs::path rom;
        if (checking) { if (argc < 4) throw std::runtime_error("Usage: nesdq --check ROM TEST_FOLDER"); rom=utf8Path(argv[2]); data=utf8Path(argv[3]); }
        else if(switchChecking) {if(argc!=5)throw std::runtime_error("Usage: nesdq --switch-check ROM1 ROM2 TEST_FOLDER");rom=utf8Path(argv[2]);data=utf8Path(argv[4]);}
        else if (smoke) {rom=utf8Path(argv[2]);data=utf8Path(argv[5]);}
        else if (argc > 1) rom=utf8Path(argv[1]);
        else
        {
#ifdef SDL_PLATFORM_ANDROID
            while (androidRom.empty())
            {
                SDL_Event event;
                while (SDL_PollEvent(&event)) if (event.type==SDL_EVENT_QUIT) return 0;
                SDL_Delay(16);
                std::lock_guard<std::mutex> guard(androidMutex);
                if (!androidPending.empty()) {androidRom=androidPending;androidPending.clear();}
            }
            rom=utf8Path(androidRom);
            if (!userstore::ready()) throw std::runtime_error("NesDQの保存フォルダーを指定してください");
            data=utf8Path("/nesdq-shared/");
#else
            rom=chooseRom(data);
            if (rom.empty()) return 0;
#endif
        }
        auto session=std::make_unique<Session>(rom,data,previousData);
        if (checking) return check(*session);
        if(switchChecking)return checkSwitch(session,rom,utf8Path(argv[3]),data);
#ifdef SDL_PLATFORM_ANDROID
        discardAndroidRom(rom);
#endif
        SDL_Window* window=nullptr; SDL_Renderer* renderer=nullptr;
        if (!SDL_CreateWindowAndRenderer("NesDQ", 768,720,realTime?SDL_WINDOW_HIDDEN:SDL_WINDOW_RESIZABLE,&window,&renderer)) throw std::runtime_error(SDL_GetError());
        setAppWindowIcon(window);
        struct Graphics { SDL_Window* w; SDL_Renderer* r; ~Graphics(){SDL_DestroyRenderer(r);SDL_DestroyWindow(w);} } graphics{window,renderer};
#ifdef SDL_PLATFORM_ANDROID
        const bool bufferedPlayback=true;
#else
        const bool bufferedPlayback=realTime;
#endif
        SDL_SetRenderVSync(renderer,smoke || bufferedPlayback?0:1); SDL_SetRenderLogicalPresentation(renderer,256,240,SDL_LOGICAL_PRESENTATION_LETTERBOX);
        auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,256,240);
        if (!texture) throw std::runtime_error(SDL_GetError());
        std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> textureOwner(texture,SDL_DestroyTexture);
        SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST);
        AudioOutput audio(realTime,bufferedPlayback);
        GamepadInput gamepad;
        gamepad.poll(false,false);
        SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);
        std::set<std::pair<SDL_KeyboardID,SDL_Keycode>> held;
        std::array<uint32_t,256*240> pixels{};
        bool running=true,paused=false,focused=true; unsigned selected=0,pendingPress=0;
        std::string status;Uint64 statusUntil=0;
        double next=SDL_GetTicksNS()/1e9;Uint64 lastSave=SDL_GetTicks();
        std::array<std::string,20> menu={"Resume","Save slot 1","Load slot 1","Save slot 2","Load slot 2","Save slot 3","Load slot 3","Save slot 4","Load slot 4","Save slot 5","Load slot 5","Speed: 1x","Swap A/B: Off","Select ROM","Battle EXP: 1x","Battle Gold: 1x","Max skills: Off","Free Jobs: Off","Reset","Exit"};
        auto title=[&] { menu[11]=session->supportsTurbo?"Speed: "+std::to_string(session->speed)+"x":"Speed: unavailable"; menu[12]=session->swapAb?"Swap A/B: On":"Swap A/B: Off"; menu[14]=session->supportsExperienceRewards()?"Battle EXP: "+std::to_string(session->experienceFactor)+"x":"Battle EXP: unavailable"; menu[15]=session->supportsRewards()?"Battle Gold: "+std::to_string(session->goldFactor)+"x":"Battle Gold: unavailable"; menu[16]=session->ffAudio()?std::string(session->profile==122?"Max Wpn/Magic: ":"Max Job skill: ")+(session->maxSkills?"On":"Off"):"Max skills: unavailable"; menu[17]=session->profile==123?std::string("Free Jobs: ")+(session->freeJobs?"On":"Off"):"Free Jobs: unavailable"; SDL_SetWindowTitle(window, paused ? ("NesDQ | "+menu[selected]+" | Up/Down, Left/Right, Enter, Esc").c_str() : ("NesDQ | "+std::to_string(session->speed)+"x | Q: Menu | F6: Speed | F1-F5: Save").c_str()); };
        auto action=[&](unsigned slot,bool load)
        {
            session->state(slot,load);if(load)audio.reset();held.clear();pendingPress=0;
            status=(load?"Loaded slot ":"Saved slot ")+std::to_string(slot);statusUntil=SDL_GetTicks()+1800;
        };
        auto loadRom=[&](const fs::path& chosen)
        {
            if(!replaceSession(session,chosen,data,previousData))return;
            rom=chosen;audio.reset();held.clear();pendingPress=0;selected=0;paused=false;
            gamepad.poll(false,false);
            SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);
            lastSave=SDL_GetTicks();next=SDL_GetTicksNS()/1e9;
            status="ROM loaded";statusUntil=SDL_GetTicks()+1800;title();
        };
        auto selectRom=[&]
        {
            session->saveRam();userstore::flush();paused=true;held.clear();pendingPress=0;audio.reset();title();
#ifdef SDL_PLATFORM_ANDROID
            requestAndroidRom();
#else
            loadRom(chooseRom(data));
#endif
        };
        const auto replay=smoke?readReplay(utf8Path(argv[4])):std::vector<Replay>{};
        unsigned frame=0;size_t replayIndex=0;
        Uint64 maxInput=0,maxGame=0,maxRender=0,maxWait=0,maxFrame=0;
        unsigned stalls=0;
        const Uint64 measuredStart=SDL_GetTicks();
        title();
        while (running)
        {
            const Uint64 frameStart=SDL_GetTicksNS();
#ifdef SDL_PLATFORM_ANDROID
            std::string chosen;
            {std::lock_guard<std::mutex> guard(androidMutex);chosen.swap(androidPending);}
            if(!chosen.empty())
            {
                const auto path=utf8Path(chosen);
                try {loadRom(path);}
                catch(const std::exception& error){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"NesDQ",error.what(),window);}
                discardAndroidRom(path);
            }
#endif
            gamepad.poll(focused,paused);
            while (replayIndex<replay.size() && replay[replayIndex].frame==frame)
            {
                const auto& key=replay[replayIndex++];SDL_Event input{};
                input.type=key.down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;input.key.key=key.key;input.key.down=key.down;
                input.key.mod=key.shift?SDL_KMOD_SHIFT:SDL_KMOD_NONE;SDL_PushEvent(&input);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type==SDL_EVENT_QUIT) running=false;
                if (event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WILL_ENTER_BACKGROUND)
                {
                    focused=false;held.clear();pendingPress=0;session->saveRam();userstore::flush();audio.reset();
                }
                if (event.type==SDL_EVENT_WINDOW_FOCUS_GAINED || event.type==SDL_EVENT_DID_ENTER_FOREGROUND) {focused=true;next=SDL_GetTicksNS()/1e9;}
                if (event.type!=SDL_EVENT_KEY_DOWN && event.type!=SDL_EVENT_KEY_UP) continue;
                auto key=event.key.key;
                if (!event.key.down) {held.erase({event.key.which,key});continue;}
                if (!focused || event.key.repeat) continue;
                if (key==SDLK_Q || key==SDLK_ESCAPE) {paused=!paused;held.clear();pendingPress=0;audio.reset();title();continue;}
                try
                {
                    if(key==SDLK_O)selectRom();
                    else if (paused)
                    {
                        if (key==SDLK_Z || key==SDLK_BACKSPACE) {paused=false;held.clear();pendingPress=0;}
                        if (key==SDLK_UP) selected=(selected+menu.size()-1)%menu.size();
                        if (key==SDLK_DOWN) selected=(selected+1)%menu.size();
                        if (selected==11 && session->supportsTurbo && (key==SDLK_LEFT || key==SDLK_RIGHT))
                            session->setSpeed(key==SDLK_RIGHT?session->speed%6+1:(session->speed+4)%6+1);
                        if (selected==12 && (key==SDLK_LEFT || key==SDLK_RIGHT)) session->setSwapAb(!session->swapAb);
                        if ((selected==14 || selected==15) && (key==SDLK_LEFT || key==SDLK_RIGHT)) session->cycleReward(selected==14,key==SDLK_RIGHT);
                        if (selected==16 && (key==SDLK_LEFT || key==SDLK_RIGHT)) session->toggleMaxSkills();
                        if (selected==17 && (key==SDLK_LEFT || key==SDLK_RIGHT)) session->toggleFreeJobs();
                        if (key==SDLK_RETURN || key==SDLK_X)
                        {
                            if (selected==0) paused=false;
                            else if (selected<11) action((selected+1)/2,selected%2==0);
                            else if (selected==12) session->setSwapAb(!session->swapAb);
                            else if (selected==13) selectRom();
                            else if (selected==16) session->toggleMaxSkills();
                            else if (selected==17) session->toggleFreeJobs();
                            else if (selected==18) {session->reset();held.clear();pendingPress=0;audio.reset();paused=false;}
                            else if (selected==19) running=false;
                        }
                        title();
                    }
                    else if (key==SDLK_F6 && session->supportsTurbo) {session->setSpeed(session->speed%6+1);status="Speed: "+std::to_string(session->speed)+"x";statusUntil=SDL_GetTicks()+1800;title();}
                    else if (key>=SDLK_F1 && key<=SDLK_F5) action(unsigned(key-SDLK_F1+1),bool(event.key.mod&SDL_KMOD_SHIFT));
                    else {held.insert({event.key.which,key});pendingPress|=button(key);}
                }
                catch (const std::exception& error) { if(smoke)throw;SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"NesDQ",error.what(),window); }
            }
            const Uint64 inputEnd=SDL_GetTicksNS();
            if (!paused && focused && running)
            {
                unsigned buttons=pendingPress;pendingPress=0; for (auto [device,key]:held) { (void)device; buttons|=button(key); }
                session->tick(buttons);
                size_t count; const float* samples=session->audio(&count);
                audio.push(samples,count);
            }
            const Uint64 gameEnd=SDL_GetTicksNS();
            wiiz_core_pixels(session->core.get(),pixels.data());
            SDL_UpdateTexture(texture,nullptr,pixels.data(),256*4);
            SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer); SDL_RenderTexture(renderer,texture,nullptr,nullptr);
            if (paused)
            {
                SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,0,0,0,220);SDL_RenderFillRect(renderer,nullptr);
                SDL_SetRenderDrawColor(renderer,255,255,255,255);SDL_RenderDebugText(renderer,16,12,"NesDQ");
                for (unsigned i=0;i<menu.size();++i)
                {
                    const Uint8 shade=i==selected?255:160;SDL_SetRenderDrawColor(renderer,shade,shade,shade,255);
                    SDL_RenderDebugText(renderer,16,26+9.5f*i,((i==selected?"> ":"  ")+menu[i]).c_str());
                }
            }
            if (SDL_GetTicks()<statusUntil)
            {
                SDL_SetRenderDrawColor(renderer,0,0,0,220);SDL_FRect box{0,220,256,20};SDL_RenderFillRect(renderer,&box);
                SDL_SetRenderDrawColor(renderer,255,255,255,255);SDL_RenderDebugText(renderer,8,226,status.c_str());
            }
            if (smoke && frame+1==smokeFrames)
            {
                auto* image=SDL_RenderReadPixels(renderer,nullptr);
                if (!image) throw std::runtime_error(SDL_GetError());
                const bool saved=SDL_SaveBMP(image,argv[6]);SDL_DestroySurface(image);
                if (!saved) throw std::runtime_error(SDL_GetError());
                running=false;
            }
            SDL_RenderPresent(renderer);
            const Uint64 renderEnd=SDL_GetTicksNS();
            auto measure=[&]
            {
                if(!realTime || frame<60)return;
                const Uint64 end=SDL_GetTicksNS();
                maxInput=std::max(maxInput,inputEnd-frameStart);maxGame=std::max(maxGame,gameEnd-inputEnd);
                maxRender=std::max(maxRender,renderEnd-gameEnd);maxWait=std::max(maxWait,end-renderEnd);maxFrame=std::max(maxFrame,end-frameStart);
                if(end-frameStart>50000000)
                {
                    ++stalls;std::cout<<"SLOW frame="<<frame<<" input_ms="<<(inputEnd-frameStart)/1e6<<" game_ms="<<(gameEnd-inputEnd)/1e6
                        <<" render_ms="<<(renderEnd-gameEnd)/1e6<<" wait_ms="<<(end-renderEnd)/1e6<<"\n";
                }
            };
            ++frame;
            if (SDL_GetTicks()-lastSave>10000) {session->saveRam(false);lastSave=SDL_GetTicks();}
            if (smoke && !realTime) continue;
            if (!paused && focused && running && audio.pace(frameStart))
            {
                next=SDL_GetTicksNS()/1e9;
                measure();continue;
            }
            next+=1.0/60.0988; const double now=SDL_GetTicksNS()/1e9;
            if (next>now) SDL_DelayNS(Uint64((next-now)*1e9)); else if (now-next>0.1) next=now;
            measure();
        }
        session->saveRam();userstore::flush();
        if(realTime)
        {
            std::cout<<"REALTIME frames="<<frame<<" speed="<<session->speed<<" elapsed_ms="<<SDL_GetTicks()-measuredStart<<" slow_frames="<<stalls
                <<" max_input_ms="<<maxInput/1e6<<" max_game_ms="<<maxGame/1e6<<" max_render_ms="<<maxRender/1e6<<" max_wait_ms="<<maxWait/1e6
                <<" max_frame_ms="<<maxFrame/1e6<<" audio_callbacks="<<audio.callbacks()<<" audio_shortfalls="<<audio.shortfalls()<<"\n";
            if(stalls || !audio.callbacks() || audio.shortfalls())return 1;
        }
        if (smoke) std::cout<<"PASS SDL replay frames="<<frame<<"\n";
        return 0;
    }
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';if (!(argc>1 && (std::string(argv[1])=="--check" || std::string(argv[1])=="--switch-check" || std::string(argv[1])=="--smoke" || std::string(argv[1])=="--realtime-check"))) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"NesDQ",error.what(),nullptr);return 1;}
}
