#include "kspch.h"
#include "Texture.h"
#include "Kans3D/Platform/OpenGL/OpenGLTexture.h"
#include "Kans3D/Platform/Vulkan/VulkanTexture.h"
#include "Kans3D/Renderer/Renderer.h"
namespace Kans
{
    std::variant<Ref<Texture2D>, AssetError> Texture2D::CreateFromProduct(const TextureData& data)
    {
        if (RendererAPI::Current() != RendererAPIType::OPENGL)
            return AssetError {AssetErrorCode::NotImplemented,
                               "Texture Product upload is not implemented for this backend"};
        auto uploaded = OpenGLTexture2D::UploadProduct(data);
        if (auto* error = std::get_if<AssetError>(&uploaded))
            return std::move(*error);
        return Ref<Texture2D>(std::get<Ref<OpenGLTexture2D>>(std::move(uploaded)));
    }

    Ref<Kans::Texture2D> Texture2D::Create(const TextureSpecification& specification, Buffer buffer)
    {
        switch (RendererAPI::Current())
        {
            case RendererAPIType::NONE:
                CORE_ASSERT(false, "RendererAPI::NONE is not support");
                return nullptr;
            case RendererAPIType::Vulkan:
                return CreateRef<VulkanTexture2D>(specification, buffer);
            case RendererAPIType::OPENGL:
                return CreateRef<OpenGLTexture2D>(specification, buffer);
        }
        CORE_ASSERT(false, "unknow RendererAPI");
        return nullptr;
    }

    Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification, const std::filesystem::path& filepath)
    {
        switch (RendererAPI::Current())
        {
            case RendererAPIType::NONE:
                CORE_ASSERT(false, "RendererAPI::NONE is not support");
                return nullptr;
            case RendererAPIType::Vulkan:
                return CreateRef<VulkanTexture2D>(specification, filepath);
            case RendererAPIType::OPENGL:
                return CreateRef<OpenGLTexture2D>(specification, filepath);
        }
        CORE_ASSERT(false, "unknow RendererAPI");
        return nullptr;
    }

    Ref<TextureCube> TextureCube::Create(const TextureSpecification& specification, Buffer buffer)
    {
        switch (RendererAPI::Current())
        {
            case RendererAPIType::NONE:
                CORE_ASSERT(false, "RendererAPI::NONE is not support");
                return nullptr;
            case RendererAPIType::OPENGL:
                return CreateRef<OpenGLTextureCube>(specification, buffer);
            case RendererAPIType::Vulkan:
                return nullptr;
        }
        CORE_ASSERT(false, "unknow RendererAPI");
        return nullptr;
    }

} // namespace Kans
