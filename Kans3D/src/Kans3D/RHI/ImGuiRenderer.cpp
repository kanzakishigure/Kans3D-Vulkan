#include "kspch.h"
#include "ImGuiRenderer.h"
#include "Vulkan/VulkanPresentation.h"
#include <stdexcept>
#include "OpenGL/OpenGLImGuiRenderer.h"
namespace Kans::RenderHardware
{
    namespace
    {
        bool                         InUIFrame = false;
        std::vector<Handle<Texture>> Images;
    } // namespace
    std::unique_ptr<ImGuiRenderer> CreateImGuiRenderer(Backend backend, Window* window)
    {
        if (backend == Backend::Vulkan)
            return CreateVulkanImGuiRenderer(window);
        if (backend != Backend::OpenGL)
            throw std::invalid_argument("ImGui renderer backend is not implemented");
        return CreateOpenGLImGuiRenderer(window);
    }
    void RetainImGuiTexture(const Handle<Texture>& texture)
    {
        if (InUIFrame)
            Images.push_back(texture);
    }
    void BeginImGuiTextures()
    {
        if (InUIFrame)
            throw std::logic_error("ImGui frame already active");
        InUIFrame = true;
    }
    std::vector<Handle<Texture>> TakeImGuiTextures()
    {
        InUIFrame   = false;
        auto result = std::move(Images);
        Images.clear();
        return result;
    }
    void EndImGuiTextures()
    {
        InUIFrame = false;
        Images.clear();
    }
} // namespace Kans::RenderHardware
