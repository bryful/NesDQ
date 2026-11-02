#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <stdexcept>

class AudioOutput
{
public:
    static constexpr int SAMPLE_RATE=48000;
    explicit AudioOutput(bool muted=false,bool buffered=true) : buffered_(buffered)
    {
        const SDL_AudioSpec input{SDL_AUDIO_F32,1,SAMPLE_RATE};
        stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&input,observe,this);
        if(!stream_)throw std::runtime_error(SDL_GetError());
        if(muted)SDL_SetAudioStreamGain(stream_,0.0f);
        SDL_AudioSpec device{};int frames=0;
        if(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_),&device,&frames) && device.freq>0)
        {
            const int inputFrames=int((int64_t(frames)*SAMPLE_RATE+device.freq-1)/device.freq);
            targetSamples_=std::max(targetSamples_,inputFrames*2);
            startSamples_=std::max(startSamples_,inputFrames*3);
        }
        if(!buffered_)
        {
            observing_.store(true);
            if(!SDL_ResumeAudioStreamDevice(stream_))throw std::runtime_error(SDL_GetError());
            started_=true;
        }
    }
    ~AudioOutput()
    {
        observing_.store(false);
        SDL_DestroyAudioStream(stream_);
    }
    AudioOutput(const AudioOutput&)=delete;
    AudioOutput& operator=(const AudioOutput&)=delete;
    void reset()
    {
        if(!buffered_)
        {
            if(!SDL_ClearAudioStream(stream_))throw std::runtime_error(SDL_GetError());
            return;
        }
        observing_.store(false);
        if(!SDL_PauseAudioStreamDevice(stream_) || !SDL_ClearAudioStream(stream_))throw std::runtime_error(SDL_GetError());
        started_=false;
    }
    void push(const float* samples,size_t count)
    {
        if(!buffered_ && queuedSamples()>12000 && !SDL_ClearAudioStream(stream_))throw std::runtime_error(SDL_GetError());
        if(count && !SDL_PutAudioStreamData(stream_,samples,int(count*sizeof(float))))throw std::runtime_error(SDL_GetError());
        // 空のキューから再生しない。再開時にも同じ余裕を確保する。
        if(!started_ && queuedSamples()>=startSamples_)
        {
            observing_.store(true);
            if(!SDL_ResumeAudioStreamDevice(stream_)){observing_.store(false);throw std::runtime_error(SDL_GetError());}
            started_=true;
        }
    }
    bool pace(Uint64 frameStart=0)
    {
        if(!buffered_ || !started_)return false;
        // 音声デバイスの消費を基準にし、VSyncとの二重待機や定期的なキュー破棄を避ける。
        // 音声デバイスの遅れでゲーム全体を長時間待たせない。
        const Uint64 until=(frameStart?frameStart:SDL_GetTicksNS())+16666667;
        while(queuedSamples()>targetSamples_)
        {
            if(SDL_GetTicksNS()>=until)return false;
            SDL_DelayNS(500000);
        }
        return true;
    }
    bool started() const{return started_;}
    uint64_t shortfalls() const{return shortfalls_.load();}
    uint64_t callbacks() const{return callbacks_.load();}
    int queuedSamples() const
    {
        const int bytes=SDL_GetAudioStreamQueued(stream_);
        if(bytes<0)throw std::runtime_error(SDL_GetError());
        return bytes/int(sizeof(float));
    }
private:
    static void SDLCALL observe(void* userdata,SDL_AudioStream*,int additional,int)
    {
        auto& self=*static_cast<AudioOutput*>(userdata);
        if(!self.observing_.load())return;
        ++self.callbacks_;
        if(additional>0)++self.shortfalls_;
    }
    SDL_AudioStream* stream_=nullptr;
    std::atomic<bool> observing_{false};
    std::atomic<uint64_t> shortfalls_{0},callbacks_{0};
    bool started_=false;
    bool buffered_=true;
    int targetSamples_=2880,startSamples_=3840;
};
