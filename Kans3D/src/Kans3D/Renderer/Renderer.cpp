#include "kspch.h"
#include "Renderer.h"
#include "Renderer2D.h"
namespace Kans
{
    namespace
    {
        Ref<Texture2D> White;
    }
    void Renderer::Init() { RenderHardware::InitializeDevice(RenderHardware::GetBackend()); }
    void Renderer::Init(const Scope<Window>& window)
    {
        RenderHardware::InitializeDevice(RenderHardware::GetBackend(), window.get());
    }
    void Renderer::Shutdown()
    {
        Renderer2D::Shutdown();
        White.reset();
        RenderHardware::ShutdownDevice();
    }
    Ref<Texture2D> Renderer::GetWhiteTexture()
    {
        if (!White)
        {
            TextureSpecification spec;
            spec.Width = spec.Height = 1;
            uint32_t pixel           = 0xffffffff;
            White                    = Texture2D::Create(spec, Buffer(&pixel, sizeof(pixel)));
        }
        return White;
    }
} // namespace Kans
