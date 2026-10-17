#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Editor/EditorCamera.h"
#include "Kans3D/Renderer/Resource/Environment.h"
#include "Kans3D/Scene/Scene.h"
namespace Kans
{
    class Scene;
    class SceneRenderer;
    struct SceneInfo;
    struct RenderView;
    struct DirLight
    {
        glm::vec3 Dirction {0};
        glm::vec3 Diffuse_Intensity {0};
        glm::vec3 Specular_Intensity {0};
        glm::vec3 Ambient_Intensity {0};
    };
    struct PointLight
    {
        glm::vec3 Position {0};
        glm::vec3 Diffuse_Intensity {0};
        glm::vec3 Specular_Intensity {0};
        glm::vec3 Ambient_Intensity {0};
    };

    struct PipLineState
    {
        bool EnableOutline       = false;
        bool EnableToneShader    = false;
        bool EnableStencil       = false;
        bool EnableDebugNormal   = false;
        bool EnableDefaultShader = true;
        bool EnableGridShader    = true;
    };

    struct RenderResource
    {
        PointLight   pointLight;
        DirLight     dirLight;
        PipLineState Piplinestate;
    };

    class RenderScene : public Asset
    {
    public:
        RenderScene(const Ref<Scene>& logicScene);
        ~RenderScene();

        void PrepareEnvironment(const std::filesystem::path& envMapPath);
        void UpdateRenderable();
        void OnRenderEditor(Ref<SceneRenderer> renderer, TimeStep ts, const EditorCamera& camera);
        void OnRenderRuntime(Ref<SceneRenderer> renderer, TimeStep ts);

        // CPU-only scene extraction. Caller supplies camera, viewport and debug mode.
        void Extract(RenderView& view) const;
        void OnViewportResize(uint32_t width, uint32_t height);
        const Ref<Scene>& GetLogicScene() const { return m_LogicScene; }
        void SetLogicScene(const Ref<Scene>& scene);

        const RenderResource& GetRenderResource() { return m_RenderResource; }
        void                  SetRenderResource(const RenderResource& resource) { m_RenderResource = resource; }

    private:
        void CollectLights(SceneInfo& info) const;
        void Render(SceneRenderer& renderer, const SceneInfo& info);

    private:
        Ref<Scene> m_LogicScene;

        EntityMap          m_2DRenderable;
        EntityMap          m_3DRenderable;
        Ref<SceneRenderer> m_SceneRenderer;

        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

        // temp for create Environment
        RenderResource        m_RenderResource;
        std::filesystem::path m_EnvironmentPath;

        friend class Scene;
        friend class SceneRenderer;
    };

} // namespace Kans
