#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <stdexcept>
#ifdef SDL_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ROM名は日本語も表示できるよう、Windowsのフォントで描画する。
inline SDL_Texture* romPickerText(SDL_Renderer* renderer,const std::string& text)
{
    constexpr int width=480,height=20;
    const int length=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    std::wstring wide(length,L' ');
    MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),wide.data(),length);
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc)throw std::runtime_error("ROM font DC creation failed");
    struct DC {HDC value;~DC(){DeleteDC(value);}} dcOwner{dc};
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HFONT font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    struct Objects {HBITMAP bitmap;HFONT font;~Objects(){if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);}} objects{bitmap,font};
    if(!bitmap || !font)throw std::runtime_error("ROM font creation failed");
    auto oldBitmap=SelectObject(dc,bitmap);auto oldFont=SelectObject(dc,font);
    SetBkColor(dc,RGB(16,23,34));SetTextColor(dc,RGB(255,255,255));
    RECT box{0,0,width,height};
    ExtTextOutW(dc,0,0,ETO_OPAQUE,&box,wide.data(),length,nullptr);
    GdiFlush();
    SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);
    for(int i=0;i<width*height;++i)static_cast<Uint32*>(pixels)[i]|=0xff000000;
    auto* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_BGRA32,pixels,width*4);
    if(!surface)throw std::runtime_error(SDL_GetError());
    auto* texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);
    if(!texture)throw std::runtime_error(SDL_GetError());
    return texture;
}
#endif
