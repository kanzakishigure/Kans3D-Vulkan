#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Renderer/RHI/FrameBuffer.h"

#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/RenderScene.h"
#include "Kans3D/Renderer/Resource/SceneCamera.h"
#include "Kans3D/Scene/ProductModelComponent.h"

namespace Kans
{
    struct SceneRendererCamera
    {
        Camera    camera;
        glm::mat4 viewMatrix;
        glm::vec3 Position;
    };
    struct SceneInfo
    {
        DirLight            dirLight;
        PointLight          pointLight;
        SceneRendererCamera sceneCamera;
    };
    struct SceneRendererSpecification
    {
        bool SwapChainTarget = false;
    };

    class Scene;
    class SceneRenderer : public RefCounter
    {
    public:
        SceneRenderer(RenderScene* renderScene, SceneRendererSpecification spec = SceneRendererSpecification());
        ~SceneRenderer();

        // The manager must outlive its use here; pass nullptr before destroying it.
        void              SetAssetManager(AssetManager* assets);
        RenderAssetCache* GetAssetCache() { return m_AssetCache.get(); }

        // Draws a Model asset with the engine PBR shader. Parts that are still
        // loading are skipped; missing textures fall back to material factors.
        void SubmitModel(const ProductModelComponent& model, const glm::mat4& transform);
        // Draws model geometry with a shader from the library that takes
        // U_ViewProjection, U_Transform and U_ViewPos (outline, stencil, debug).
        void SubmitModelGeometry(const ProductModelComponent& model,
                                 const glm::mat4&             transform,
                                 const std::string&           shaderName);

        void                  Init();
        void                  PrepareEnvironment();
        void                  UpdateEnvironment(std::string path);
        void                  BeginScene(SceneInfo info);
        void                  EndScene();
        void                  SetScene(RenderScene* scene);
        void                  SetViewportSize(uint32_t width, uint32_t height);
        std::vector<uint32_t> GetViewportSize() { return {m_ViewportWidth, m_ViewportHeight}; }

        void SubmitGrid(float size, float scale, glm::mat4 transform);
        void SubmitSkybox(Ref<TextureCube> skybox);

        uint32_t               GetOutput(uint32_t index) const;
        const Environment&     GetEnvironment() { return m_Environment; }
        const Ref<TextureCube> GetSkybox() { return m_SkyBox; }

    private:
        void DrawGeometry(const Ref<VertexArray>& geometry);

        // SceneRenderer did't handle the life of Scnene;
        RenderScene*               m_RenderScene;
        SceneInfo                  m_SceneInfo;
        SceneRendererSpecification m_Specification;

        uint32_t         m_ViewportWidth = 0, m_ViewportHeight = 0;
        Ref<FrameBuffer> m_FrameBuffer;

        Environment                     m_Environment;
        Ref<TextureCube>                m_SkyBox;
        Ref<Texture2D>                  m_BrdfLUT;
        bool                            m_Active = false;
        Scope<RenderAssetCache>         m_AssetCache;
    };
} // namespace Kans
