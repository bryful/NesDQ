#pragma once
#include <array>
#include <cstdint>
#include <vector>

// Display-only CPU port of Timothy Lottes' public-domain CRT filter.
// Original source and attribution: third_party/crt-lottes/.
class CrtFilter {
public:
    static constexpr int width=768, height=720;
    CrtFilter();
    const uint32_t* render(const uint32_t* rgba, unsigned mode);
private:
    struct RGB {float r=0,g=0,b=0;};
    struct Kernel {std::array<float,5> h5{};std::array<float,3> h3{};std::array<float,7> h7{};
                   std::array<float,3> scan{};std::array<float,5> bloom{};};
    std::array<std::array<Kernel,3>,2> kernels_{};
    std::array<float,256> linear_{};
    std::array<uint8_t,4097> srgb_{};
    std::vector<RGB> h3_,h5_,h7_,linearFrame_;
    std::array<float,width> xSquared_{};
    std::vector<uint32_t> image_;
    std::array<uint32_t,256*240> previous_{};
    unsigned lastMode_=99;
};
