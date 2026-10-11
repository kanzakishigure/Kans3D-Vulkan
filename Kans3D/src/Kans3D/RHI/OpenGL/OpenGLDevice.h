#pragma once
#include "Kans3D/RHI/RHI.h"

namespace Kans::RenderHardware
{
    // Backend entry point. Requires a current 4.3 core context and loaded entry points.
    std::unique_ptr<Device> CreateOpenGLDevice(const DeviceDesc& desc = {});
} // namespace Kans::RenderHardware
