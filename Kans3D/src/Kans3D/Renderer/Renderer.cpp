#include "kspch.h"
#include "Renderer.h"
#include "Kans3D/Renderer/RHI/Buffer.h"
#include "RHI/RHI.h"
#include "Renderer2D.h"
#include "Resource/SceneCamera.h"

#include "Kans3D/Platform/OpenGL/OpenGLRendererAPI.h"

// temp
#include "Resource/Camera.h"
#include "Resource/MeshFactory.h"
#include "Resource/RenderScene.h"

namespace Kans
{

    Ref<RHI>                       Renderer::s_RHI            = nullptr;
    Ref<RenderPipeline>            Renderer::s_RenderPipeline = nullptr;
    static RendererAPI*            s_RendererAPI              = nullptr;
    static GlobalRendererResource* s_GlobalRendererResource   = nullptr;

    static RendererAPI* InitRendererAPI()
    {
        switch (RendererAPI::Current())
        {
            case RendererAPIType::Vulkan:
                return nullptr;
            case RendererAPIType::OPENGL:
                return new OpenGLRendererAPI();
        }
        CORE_ASSERT(false, "Unknown RendererAPI");
        return nullptr;
    }
    void Renderer::Init()
    {
        PROFILE_FUCTION();

        s_RHI = RHI::Create();
        s_RHI->Init();

        s_RendererAPI = InitRendererAPI();
        s_RendererAPI->Init();

        Renderer::InitGlobalRendererResource();

        // we need support Vulkan, so need change the architecture, just temp
        if (RendererAPI::Current() == RendererAPIType::OPENGL)
            OpenGLRenderCommand::s_RendererAPI = s_RendererAPI;
    }

    void Renderer::Init(const Scope<Window>& window)
    {

        s_RHI = RHI::Create(window);
        s_RHI->Init();
        {
            // we need support Vulkan, so need change the architecture, just temp for now
            if (RendererAPI::Current() == RendererAPIType::OPENGL)
            {
                s_RendererAPI = InitRendererAPI();
                s_RendererAPI->Init();
                OpenGLRenderCommand::s_RendererAPI = s_RendererAPI;
            }
        }

        s_GlobalRendererResource                  = DEBUG_NEW GlobalRendererResource();
        s_GlobalRendererResource->m_ShaderLibrary = CreateRef<ShaderLibrary>();

        Renderer::InitGlobalRendererResource();
    }

    void Renderer::InitGlobalRendererResource()
    {

        if (!s_GlobalRendererResource)
            s_GlobalRendererResource = DEBUG_NEW GlobalRendererResource();
        // Init renderer asset
        // Texture
        {
            TextureSpecification spec;
            spec.Width  = 1;
            spec.Height = 1;
            spec.Format = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;

            uint32_t Wightdata                     = 0xffffffff;
            s_GlobalRendererResource->WhiteTexture = Texture2D::Create(spec, Buffer(&Wightdata, sizeof(uint32_t)));

            uint32_t Blackdata                     = 0xff000000;
            s_GlobalRendererResource->BlackTexture = Texture2D::Create(spec, Buffer(&Blackdata, sizeof(uint32_t)));
        }

        // QuadsBuff
        {
            s_GlobalRendererResource->Quad = MeshFactory::CreatQuad(glm::vec2(1.0));
        }

        // Kept for editor API compatibility; engine passes use embedded internal shaders.
        s_GlobalRendererResource->m_ShaderLibrary = CreateRef<ShaderLibrary>();
    }

    void Renderer::InitRenderPipline(const RenderPipelineSpecification& spec)
    {
        // Init render pipeline
        // maybe we show move this to editor layer on attach
        // we don't now what we should to Rendering until we have load the Scene From the project
        // maybe the  the pipeline implement by the project
        {
            s_RenderPipeline        = CreateRef<RenderPipeline>();
            s_RenderPipeline->m_RHI = s_RHI;
            s_RenderPipeline->Init(spec);
        }
    }

    void Renderer::Shutdown()
    {
        delete s_GlobalRendererResource;
        s_GlobalRendererResource = nullptr;
        CORE_INFO("delete rendererdata");
        OpenGLRenderCommand::s_RendererAPI = &OpenGLRenderCommand::s_NativeCommands;
        delete s_RendererAPI;
        s_RendererAPI = nullptr;
        CORE_INFO("delete renderer-rhi");

        s_RHI->Shutdown();
        s_RHI.reset();
        s_RenderPipeline->clear();
        s_RenderPipeline.reset();
    }

    void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
    {

        s_GlobalRendererResource->ViewProjectionMatix = camera.GetProjectionMatrix() * glm::inverse(transform);
    }
    void Renderer::EndScene() {}

    void Renderer::OnWindowResize(uint32_t width, uint32_t height) { s_RendererAPI->SetViewPort(0, 0, width, height); }

    Ref<Texture2D> Renderer::GetWhiteTexture() { return s_GlobalRendererResource->WhiteTexture; }

    Ref<Texture2D> Renderer::GetBlackTexture() { return s_GlobalRendererResource->BlackTexture; }

    Kans::Ref<Kans::VertexArray> Renderer::GetQuad() { return s_GlobalRendererResource->Quad; }

    Ref<ShaderLibrary> Renderer::GetShaderLibrary()
    {
        return s_GlobalRendererResource ? s_GlobalRendererResource->m_ShaderLibrary : nullptr;
    }

    const std::unordered_map<std::string, std::string>& Renderer::GetGlobalShaderMacros()
    {
        return s_GlobalRendererResource->GlobalShaderMacros;
    }

} // namespace Kans
