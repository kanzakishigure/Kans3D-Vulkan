#pragma once
#include "Backend.h"
struct ImDrawData;
namespace Kans::RenderHardware
{
    class ImGuiRenderer
    {
    public:
        virtual ~ImGuiRenderer()              = default;
        virtual void NewFrame()               = 0;
        virtual void Render(ImDrawData* data) = 0;
        virtual void RenderPlatformWindows()  = 0;
        virtual void EndFrame()               = 0;
    };
    std::unique_ptr<ImGuiRenderer> CreateImGuiRenderer(Backend backend, Window* window);
    // Backend texture-ID exports retain images while UI draw lists reference them.
    void RetainImGuiTexture(const Handle<Texture>& texture);
    void BeginImGuiTextures();
    void EndImGuiTextures();
} // namespace Kans::RenderHardware
