#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Editor/EditorCamera.h"
#include "Kans3D/Renderer/RenderSettings.h"
#include "Kans3D/Renderer/Resource/Environment.h"
#include "Kans3D/Scene/Scene.h"
namespace Kans
{
    class Scene;
    class SceneRenderer;
    struct SceneInfo;
    struct RenderView;
    class RenderScene : public Asset
    {
    public:
        RenderScene(const Ref<Scene>& logicScene);
        ~RenderScene();

        void PrepareEnvironment(const std::filesystem::path& envMapPath);
        void UpdateRenderable();
        void OnRenderEditor(Ref<SceneRenderer> renderer, TimeStep ts, const EditorCamera& camera);
        void OnRenderRuntime(Ref<SceneRenderer> renderer, TimeStep ts);

        // CPU-only snapshot of the persistent world. Caller supplies camera, viewport and debug mode.
        void Extract(RenderView& view) const;
        void OnViewportResize(uint32_t width, uint32_t height);
        const Ref<Scene>& GetLogicScene() const { return m_LogicScene; }
        void SetLogicScene(const Ref<Scene>& scene);

        const RenderResource&        GetRenderResource();
        void                         SetRenderResource(const RenderResource& resource);
        const std::filesystem::path& GetEnvironmentPath() const;

    private:
        void Render(SceneRenderer& renderer, const SceneInfo& info);

    private:
        Ref<Scene> m_LogicScene;


        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;


        friend class Scene;
        friend class SceneRenderer;
    };

} // namespace Kans
