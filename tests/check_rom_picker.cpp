#define SDL_MAIN_HANDLED
#define main nesdqAppMain
#include "../app/main.cpp"
#undef main

struct PickerScript
{
    std::vector<SDL_Keycode> keys;
    size_t step=0;
    SDL_Joystick* pad=nullptr;
};
static Uint32 SDLCALL pickerInput(void* data,SDL_TimerID,Uint32)
{
    auto& script=*static_cast<PickerScript*>(data);
    if(script.step<script.keys.size())
    {
        const auto key=script.keys[script.step++];
        if(script.pad)
        {
            SDL_SetJoystickVirtualButton(script.pad,SDL_GAMEPAD_BUTTON_DPAD_DOWN,key==SDLK_DOWN);
            SDL_SetJoystickVirtualButton(script.pad,SDL_GAMEPAD_BUTTON_EAST,key==SDLK_X);
            SDL_SetJoystickVirtualButton(script.pad,SDL_GAMEPAD_BUTTON_SOUTH,key==SDLK_Z);
        }
        else if(key)
        {
            SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=key;event.key.down=true;SDL_PushEvent(&event);
        }
        return 100;
    }
    SDL_Event event{};event.type=SDL_EVENT_QUIT;SDL_PushEvent(&event);
    return 0;
}
int main()
{
    SDL_SetMainReady();SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))return 1;
    const auto folder=fs::current_path()/"build"/"checks"/("picker-"+std::to_string(SDL_GetTicksNS()));
    fs::create_directories(folder/"Rom"/"Sub");
    const auto rom=folder/"Rom"/utf8Path("日本語.nes");
    writeFile(rom,{});writeFile(folder/"Rom"/"Sub"/"nested.nes",{});
    writeFile(folder/"Rom"/"ignored.txt",{});
    auto run=[&](std::vector<SDL_Keycode> keys,const fs::path& expected,SDL_Joystick* pad=nullptr)
    {
        const auto initial=userstore::utf8(folder/"Rom");
        writeFile(folder/"rom-folder.txt",std::vector<uint8_t>(initial.begin(),initial.end()));
        SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
        PickerScript script{std::move(keys),0,pad};
        const auto timer=SDL_AddTimer(150,pickerInput,&script);
        const auto result=chooseRom(folder);SDL_RemoveTimer(timer);
        if(result!=expected)throw std::runtime_error("ROM picker selection mismatch: "+userstore::utf8(result));
    };
    try
    {
        run({SDLK_DOWN,SDLK_DOWN,SDLK_UP,SDLK_DOWN,SDLK_X},rom);
        run({SDLK_DOWN,SDLK_RIGHT,SDLK_DOWN,SDLK_X},folder/"Rom"/"Sub"/"nested.nes");
        run({SDLK_Z},{});run({SDLK_ESCAPE},{});
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.name="ROM picker check";
        const auto id=SDL_AttachVirtualJoystick(&desc);
        SDL_Joystick* pad=SDL_OpenJoystick(id);
        if(!pad)throw std::runtime_error(SDL_GetError());
        run({0,0,0,0,0,0,0,0,0,0,SDLK_DOWN,0,SDLK_DOWN,0,SDLK_X},rom,pad);
        SDL_SetJoystickVirtualButton(pad,SDL_GAMEPAD_BUTTON_EAST,false);
        run({0,0,0,0,0,0,0,0,0,0,SDLK_Z},{},pad);
        SDL_CloseJoystick(pad);SDL_DetachVirtualJoystick(id);
        std::cout<<"PASS ROM picker: arrows, A, B, parent/directory, Japanese filename, virtual gamepad\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}
    SDL_Quit();return 0;
}
