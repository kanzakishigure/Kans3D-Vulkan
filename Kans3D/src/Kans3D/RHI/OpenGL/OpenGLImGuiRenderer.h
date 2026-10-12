#pragma once
#include "Kans3D/RHI/ImGuiRenderer.h"
namespace Kans::RenderHardware
{
    std::unique_ptr<ImGuiRenderer> CreateOpenGLImGuiRenderer(Window* window);
}
