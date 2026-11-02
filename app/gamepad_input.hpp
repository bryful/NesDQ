#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <set>
#include <string>

// The Android Switch Pro key path on the tested device reports labeled face
// buttons, while SDL 3.2.28's Android mapping marks them positional. Correct
// only this known mapping; leave Windows, HIDAPI and older Android maps alone.
inline std::string androidProMapping(std::string mapping, Uint16 vendor, Uint16 product, bool android) {
    if (!android || vendor != 0x057e || product != 0x2009 ||
        mapping.find(",a:b0,") == std::string::npos || mapping.find(",b:b1,") == std::string::npos ||
        mapping.find(",x:b2,") == std::string::npos || mapping.find(",y:b3,") == std::string::npos ||
        mapping.find("sdk>=:29,") == std::string::npos) return mapping;
    for (const auto& pair : {std::pair{",a:b0,", ",a:b1,"}, {",b:b1,", ",b:b0,"},
                             {",x:b2,", ",x:b3,"}, {",y:b3,", ",y:b2,"}})
        mapping.replace(mapping.find(pair.first), SDL_strlen(pair.first), pair.second);
    return mapping;
}

// SDL owns device protocols and mapping. Wiiz only binds physical positions to actions.
class GamepadInput {
public:
    static void configure() {
#ifdef SDL_PLATFORM_WINDOWS
        // Keep SDL's supported Switch protocol for USB/Bluetooth. Merely
        // enumerating via the Windows fallback did not restore real input.
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
#endif
    }
    static constexpr SDL_KeyboardID keyboardId = SDL_KeyboardID(-1);
    static constexpr std::array<SDL_Keycode, 12> keys = {
        SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_X, SDLK_Z,
        SDLK_A, SDLK_S, SDLK_Q, SDLK_M, SDLK_F1, SDLK_F1
    };
    ~GamepadInput() { close(); }
    const char* navigationHint() const {
        if (pads_.empty()) return "UP/DOWN  A:OK  B:BACK";
        auto* pad = pads_.begin()->second;
        return SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_A
            ? "UP/DOWN  A:OK  B:BACK" : "UP/DOWN  B:OK  A:BACK";
    }
    void close() { for (auto [id, pad] : pads_) SDL_CloseGamepad(pad); pads_.clear(); }
    void poll(bool enabled, bool navigation) {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i)
            if (!pads_.contains(ids[i])) if (auto* pad = SDL_OpenGamepad(ids[i])) {
                openErrors_.erase(ids[i]);
                pads_[ids[i]] = pad;
                char* mapping = SDL_GetGamepadMapping(pad);
#ifdef SDL_PLATFORM_ANDROID
                if (mapping) {
                    const auto corrected = androidProMapping(mapping, SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad), true);
                    if (corrected != mapping) {
                        if (!SDL_SetGamepadMapping(ids[i], corrected.c_str())) SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Pro mapping: %s", SDL_GetError());
                        SDL_free(mapping); mapping = SDL_GetGamepadMapping(pad);
                    }
                }
#endif
                SDL_Log("Wiiz gamepad: %s vendor=%04x product=%04x mapping=%s", SDL_GetGamepadName(pad),
                    SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad), mapping ? mapping : "unknown");
                SDL_free(mapping);
            } else if (openErrors_.insert(ids[i]).second) SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                "Wiiz gamepad open failed: id=%u error=%s", ids[i], SDL_GetError());
        SDL_free(ids);
        for (auto it = pads_.begin(); it != pads_.end();)
            if (!SDL_GamepadConnected(it->second)) { SDL_CloseGamepad(it->second); it = pads_.erase(it); }
            else ++it;
        std::array<bool, 12> next{};
        for (auto [id, pad] : pads_) {
            const auto down = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
            // 50% activation, 35% release avoids drift and threshold chatter.
            auto& axis = axes_[id];
            const int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            const int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            const int values[] = {-y, y, -x, x, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)};
            for (int i = 0; i < 5; ++i) axis[i] = values[i] > (axis[i] ? 11468 : 16384);
            const bool state[] = {
                down(SDL_GAMEPAD_BUTTON_DPAD_UP) || axis[0], down(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || axis[1],
                down(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || axis[2], down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || axis[3],
                down(SDL_GAMEPAD_BUTTON_EAST), down(SDL_GAMEPAD_BUTTON_SOUTH),
                down(SDL_GAMEPAD_BUTTON_BACK), down(SDL_GAMEPAD_BUTTON_START),
                down(SDL_GAMEPAD_BUTTON_LEFT_STICK), down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                axis[4], down(SDL_GAMEPAD_BUTTON_RIGHT_STICK)
            };
            for (int i = 0; i < 12; ++i) next[i] = next[i] || state[i];
        }
        for (auto it = axes_.begin(); it != axes_.end();)
            if (!pads_.contains(it->first)) it = axes_.erase(it); else ++it;
        const Uint64 now = SDL_GetTicks();
        for (int i = 0; i < 12; ++i) {
            if (next[i] != held_[i]) {
                if (enabled) emit(i, next[i]);
                repeatAt_[i] = now + 350;
            } else if (enabled && navigation && i < 4 && next[i] && now >= repeatAt_[i]) {
                emit(i, true); repeatAt_[i] = now + 100;
            }
        }
        held_ = next;
    }
private:
    static void emit(int action, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.which = keyboardId; event.key.key = keys[action]; event.key.down = down;
        event.key.mod = action == 11 ? SDL_KMOD_SHIFT : SDL_KMOD_NONE;
        SDL_PushEvent(&event);
    }
    std::map<SDL_JoystickID, SDL_Gamepad*> pads_;
    std::set<SDL_JoystickID> openErrors_;
    std::map<SDL_JoystickID, std::array<bool, 5>> axes_;
    std::array<bool, 12> held_{};
    std::array<Uint64, 12> repeatAt_{};
};
