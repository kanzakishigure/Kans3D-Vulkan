#pragma once
#include <cstdint>
#include <string>
namespace Kans::RenderHardware
{
    void WriteFrameCapture(const std::string& path,
                           uint32_t           width,
                           uint32_t           height,
                           const uint8_t*     rgba,
                           bool               bottomUp = false,
                           bool               bgra     = false);
}
