#include "kspch.h"
#include "FrameCapture.h"
#include <fstream>
#include <stdexcept>
namespace Kans::RenderHardware
{
    void WriteFrameCapture(const std::string& path,
                           uint32_t           width,
                           uint32_t           height,
                           const uint8_t*     rgba,
                           bool               bottomUp,
                           bool               bgra)
    {
        std::ofstream output(path, std::ios::binary);
        if (!output)
            throw std::runtime_error("Cannot open frame capture: " + path);
        output << "P6\n" << width << ' ' << height << "\n255\n";
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                auto pixel = rgba + (size_t(bottomUp ? height - 1 - y : y) * width + x) * 4;
                char rgb[] = {char(pixel[bgra ? 2 : 0]), char(pixel[1]), char(pixel[bgra ? 0 : 2])};
                output.write(rgb, 3);
            }
        if (!output)
            throw std::runtime_error("Cannot write frame capture: " + path);
    }
} // namespace Kans::RenderHardware
