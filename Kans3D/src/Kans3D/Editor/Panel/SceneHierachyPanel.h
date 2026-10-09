#pragma once
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

        virtual void onImGuiRender(bool isOpen) override;
        virtual void setSceneContext(const Ref<Scene>& context) override
        {
            m_SelectionContext = {};
            m_Context          = context;
        }
        Entity getSelectEntity() const { return m_SelectionContext; }
        void   setSelectedEntity(Entity entity) { m_SelectionContext = entity; }
        void   setAssetChoices(std::vector<EditorAssetChoice> choices) { m_AssetChoices = std::move(choices); }

    private:
        void drawEntityNode(Entity entity);
        void drawComponents(Entity entity);
        void drawModelComponent(Entity entity);
        // Picks a published asset of `type`; returns true when `id` changed.
        bool drawAssetPicker(const char* label, AssetType type, AssetID& id);

    private:
        Ref<Scene> m_Context = nullptr;
        Entity     m_SelectionContext;

        std::vector<EditorAssetChoice> m_AssetChoices;
    };
} // namespace Kans
