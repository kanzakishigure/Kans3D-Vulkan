#pragma once
#include "Kans3D/Core/Window.h"
#include "Kans3D/RHI/Backend.h"
#include "Resource/Texture.h"
namespace Kans
{
    class RenderScene;
    // Editor-only source compatibility; this obsolete pipeline never issued draws.
    struct RenderPipelineSpecification
    {
        Ref<RenderScene> render_scene;
    };
    class Renderer
    {
    public:
        static void           Init();
        static void           Init(const Scope<Window>& window);
        static void           InitRenderPipline(const RenderPipelineSpecification&) {}
        static void Shutdown();
        static Ref<Texture2D> GetWhiteTexture();
    };
} // namespace Kans
