// Gaussian reconstruction, Scan, Tri and Bloom adapted from Timothy Lottes'
// PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER (crt-lottes.glsl).
// See third_party/crt-lottes/PROVENANCE.json and the retained upstream source.
// SDL upload, cached LUTs, preset mixing and vignette are Wiiz display glue.
#include "crt_filter.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

CrtFilter::CrtFilter():h3_(width*240),h5_(width*240),h7_(width*240),linearFrame_(262*240),image_(width*height) {
    for(int i=0;i<256;++i){const float c=i/255.f;linear_[i]=c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
    for(int i=0;i<=4096;++i){const float c=i/4096.f;srgb_[i]=uint8_t(std::clamp(std::lround(255*(c<.0031308f?c*12.92f:1.055f*std::pow(c,.41666f)-.055f)),0l,255l));}
    for(int x=0;x<width;++x){const float nx=(x+.5f)/width*2-1;xSquared_[x]=nx*nx;}
    for(int preset=0;preset<2;++preset)for(int phase=0;phase<3;++phase) {
        auto& k=kernels_[preset][phase];const float dst=.5f-(phase+.5f)/3;
        auto gauss=[](float x,float scale){return std::exp2(scale*x*x);};
        auto horizontal=[&](auto& weights,float scale){const int radius=int(weights.size())/2;float sum=0;
            for(int i=-radius;i<=radius;++i)sum+=weights[i+radius]=gauss(dst+i,scale);
            for(auto& w:weights)w/=sum;};
        horizontal(k.h3,preset==0?-4.5f:-3.f);horizontal(k.h5,preset==0?-4.5f:-3.f);horizontal(k.h7,-1.5f);
        for(int i=-1;i<=1;++i)k.scan[i+1]=gauss(dst+i,preset==0?-5.f:-8.f);
        for(int i=-2;i<=2;++i)k.bloom[i+2]=gauss(dst+i,-2.f);
    }
}
const uint32_t* CrtFilter::render(const uint32_t* rgba,unsigned mode) {
    if(mode==0)return rgba;
    mode=std::min(mode,2u);
    if(lastMode_==mode&&!std::memcmp(previous_.data(),rgba,sizeof(previous_)))return image_.data();
    std::memcpy(previous_.data(),rgba,sizeof(previous_));lastMode_=mode;
    const auto* source=reinterpret_cast<const uint8_t*>(rgba);
    for(int y=0;y<240;++y) {
        auto* row=linearFrame_.data()+y*262;
        for(int x=0;x<256;++x){const auto* p=source+4*(y*256+x);row[x+3]={linear_[p[0]],linear_[p[1]],linear_[p[2]]};}
        for(int x=0;x<3;++x){row[x]=row[3];row[259+x]=row[258];}
        for(int x=0;x<width;++x) {
            const auto& k=kernels_[mode-1][x%3];const auto* samples=row+3+x/3;
            auto filter=[&](const auto& weights){RGB sum;const int radius=int(weights.size())/2;
                for(int i=-radius;i<=radius;++i){const auto p=samples[i];const float w=weights[i+radius];sum.r+=p.r*w;sum.g+=p.g*w;sum.b+=p.b*w;}return sum;};
            h3_[y*width+x]=filter(k.h3);h5_[y*width+x]=filter(k.h5);h7_[y*width+x]=filter(k.h7);
        }
    }
    auto* out=reinterpret_cast<uint8_t*>(image_.data());
    const float mix=mode==1?.65f:1.f,bloom=mode==1?.025f:.07f;
    for(int y=0;y<height;++y) {
        const int sy=y/3;const auto& k=kernels_[mode-1][y%3];
        const auto row=[&](const std::vector<RGB>& rows,int dy){return rows.data()+std::clamp(sy+dy,0,239)*width;};
        const RGB* rows[]={row(h3_,-1),row(h5_,0),row(h3_,1),row(h5_,-2),row(h7_,-1),row(h7_,0),row(h7_,1),row(h5_,2)};
        const float weights[]={k.scan[0],k.scan[1],k.scan[2],k.bloom[0]*bloom,k.bloom[1]*bloom,k.bloom[2]*bloom,k.bloom[3]*bloom,k.bloom[4]*bloom};
        const float ny=(y+.5f)/height*2-1,edgeShade=(mode==1?.035f:.07f)*ny*ny;
        for(int x=0;x<width;++x) {
            RGB result;
            for(int i=0;i<8;++i){const auto p=rows[i][x];result.r+=p.r*weights[i];result.g+=p.g*weights[i];result.b+=p.b*weights[i];}
            const float shade=1-edgeShade*xSquared_[x];const auto original=linearFrame_[sy*262+3+x/3];
            auto* pixel=out+4*(y*width+x);
            const float values[]={(original.r*(1-mix)+result.r*mix)*shade,(original.g*(1-mix)+result.g*mix)*shade,(original.b*(1-mix)+result.b*mix)*shade};
            for(int c=0;c<3;++c)pixel[c]=srgb_[std::clamp(int(values[c]*4096+.5f),0,4096)];pixel[3]=255;
        }
    }
    return image_.data();
}
