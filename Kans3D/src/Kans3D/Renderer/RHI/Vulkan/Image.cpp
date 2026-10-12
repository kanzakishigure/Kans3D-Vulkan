#include "kspch.h"
#include "Image.h"
#include "Kans3D/Platform/Vulkan/VulkanImage.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/Renderer.h"

namespace Kans
{
    Ref<Image2D> Image2D::Create(ImageSpecification spec)
    {
        switch (RenderHardware::GetBackend())
        {
            case RenderHardware::Backend::Vulkan:
                return CreateScope<VulkanImage2D>(spec);
        }

        CORE_ASSERT(false, "unknow RendererAPI");
        return nullptr;
    }
} // namespace Kans
