#pragma once

#include <SDL3/SDL.h>
#ifdef SDL_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

inline void setAppWindowIcon(SDL_Window* window) {
#ifdef SDL_PLATFORM_WINDOWS
    const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    const auto instance = GetModuleHandleW(nullptr);
    const int kinds[] = {ICON_BIG, ICON_SMALL};
    for (const auto kind : kinds) {
        const auto icon = LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
            GetSystemMetrics(kind == ICON_BIG ? SM_CXICON : SM_CXSMICON),
            GetSystemMetrics(kind == ICON_BIG ? SM_CYICON : SM_CYSMICON), LR_SHARED);
        if (hwnd && icon) SendMessageW(hwnd, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
    }
#else
    (void)window;
#endif
}
