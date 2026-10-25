#pragma once
#include "Kans3D/RHI/RHI.h"

namespace Kans
{
    // Effective legacy editor state. Standalone callers must request variations
    // explicitly instead of relying on ambient native graphics state.
    struct ImmediatePassState
    {
        bool                      Blend        = false;
        bool                      DepthWrite   = true;
        RenderHardware::CompareOp DepthCompare = RenderHardware::CompareOp::LessEqual;
        uint8_t                   WriteMask    = 15;
    };
} // namespace Kans
