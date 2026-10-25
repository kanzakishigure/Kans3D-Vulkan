#pragma once
#include "Kans3D/Core/Window.h"
#include "Kans3D/RHI/Backend.h"
#include "Resource/Texture.h"
namespace Kans
{
    class Renderer
    {
    public:
        static void           Init();
        static void           Init(const Scope<Window>& window);
        static void Shutdown();
        static Ref<Texture2D> GetWhiteTexture();
    };
} // namespace Kans
