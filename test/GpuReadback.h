#pragma once
#include <algorithm>
#include <cstring>
#include <glm/gtc/packing.hpp>
#include "Kans3D/RHI/Backend.h"
inline std::vector<uint8_t> ReadGpuTexture(const Kans::RenderHardware::Handle<Kans::RenderHardware::Texture>& image,
                                           uint32_t                                                           mip = 0)
{
    using namespace Kans::RenderHardware;
    const auto& desc       = image->GetDesc();
    size_t      pixelBytes = desc.PixelFormat == Format::R8Unorm     ? 1 :
                             desc.PixelFormat == Format::RG8Unorm    ? 2 :
                             desc.PixelFormat == Format::RGBA16Float ? 8 :
                             desc.PixelFormat == Format::RGBA32Float ? 16 :
                                                                       4;
    size_t      size       = size_t(std::max(1u, desc.Width >> mip)) * std::max(1u, desc.Height >> mip) * pixelBytes;
    auto        device     = GetSharedDevice();
    auto        buffer     = device->CreateBuffer({size, BufferUsage::CopyDestination, MemoryUsage::Readback});
    auto        commands   = device->CreateCommandList();
    commands->CopyTextureToBuffer(image, {mip, 0}, buffer);
    device->Submit(commands);
    device->WaitIdle();
    return buffer->Read(0, size);
}
inline std::vector<float> ReadGpuFloatTexture(const Kans::RenderHardware::Handle<Kans::RenderHardware::Texture>& image,
                                              uint32_t mip = 0)
{
    const auto         bytes = ReadGpuTexture(image, mip);
    const bool         half  = image->GetDesc().PixelFormat == Kans::RenderHardware::Format::RGBA16Float;
    std::vector<float> values(bytes.size() / (half ? 2 : 4));
    for (size_t i = 0; i < values.size(); ++i)
        if (half)
        {
            uint16_t word;
            std::memcpy(&word, bytes.data() + i * 2, 2);
            values[i] = glm::unpackHalf1x16(word);
        }
        else
            std::memcpy(&values[i], bytes.data() + i * 4, 4);
    return values;
}
