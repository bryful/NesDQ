#pragma once
#include <cstddef>
#include <vector>

// FFの音声はゲーム速度へ連動させ、途中のフレームを捨てずに等間隔へ縮める。
struct TurboAudio
{
    std::vector<float> samples;
    float sum=0;
    unsigned pending=0;
    void reset(){samples.clear();sum=0;pending=0;}
    void push(const float* input,std::size_t count,unsigned speed)
    {
        for(std::size_t i=0;i<count;++i)
        {
            sum+=input[i];
            if(++pending==speed)
            {
                samples.push_back(sum/static_cast<float>(speed));sum=0;pending=0;
            }
        }
    }
};
