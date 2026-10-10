#pragma once
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/Editor/EditorResources.h"
#include "Kans3D/Scene/Entity.h"
namespace Kans
{
    // A published Product the editor can reference from components.
    struct EditorAssetChoice
    {
        AssetID               ID;
        AssetType             Type = AssetType::None;
        std::string           Name;
        std::filesystem::path SourcePath; // Absolute path of the source file.
    };

    class SceneHierachyPanel : EditorPanel
    {

    public:
        SceneHierachyPanel() = default;

        // Entity list; components are edited in the editor's properties panel.
        virtual void onImGuiRender() override;
        virtual void setSceneContext(const Ref<Scene>& context) override
        {
            m_SelectionContext = {};
            m_Context          = context;
        }
        Entity getSelectEntity() const { return m_SelectionContext; }
        void   setSelectedEntity(Entity entity) { m_SelectionContext = entity; }

    private:
        void drawEntityNode(Entity entity);

    private:
        Ref<Scene> m_Context = nullptr;
        Entity     m_SelectionContext;

    };
} // namespace Kans
