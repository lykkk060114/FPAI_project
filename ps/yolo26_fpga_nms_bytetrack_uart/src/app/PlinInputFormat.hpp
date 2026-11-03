#pragma once
#include <cstddef>
#include <cstdint>
namespace xunjing {
// ImageMake signed pixel representation, not a generic quantization scale.
inline void SignedBgrToFp32(const int8_t* src, float* dst, std::size_t count) {
    for(std::size_t i=0;i<count;++i) dst[i]=static_cast<int>(src[i])+128.0F;
}
constexpr int PlinWidth=640, PlinHeight=352, PlinChannels=3;
constexpr std::size_t PlinPixelBytes=PlinWidth*PlinHeight*PlinChannels;
}
