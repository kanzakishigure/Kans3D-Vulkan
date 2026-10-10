#pragma once
#include <array>
#include <functional>
#include <vector>
#include "Kans3D/Editor/Panel/SceneHierachyPanel.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/ProductModelComponent.h"

namespace Kans
{
    // Edits the components of the entity selected in the hierarchy: a header
    // with the entity name, one collapsible section per component (with Reset
    // and Remove), and an Add Component menu listing missing components.
    class PropertiesPanel
    {
    public:
        explicit PropertiesPanel(SceneHierachyPanel& hierarchy) : m_Hierarchy(hierarchy) {}

        void Draw();
        void SetAssetChoices(std::vector<EditorAssetChoice> choices) { m_AssetChoices = std::move(choices); }
        // Used to show model parts and material names; may be null.
        void SetAssetManager(AssetManager* assets) { m_Assets = assets; }

    private:
        void DrawHeader(Entity entity);
        void DrawAddComponent(Entity entity);

        // A collapsible component section. `reset` restores defaults (Reset is
        // hidden when empty); Remove is offered when `removable`.
        template<typename T, typename DrawFn>
        void Section(Entity entity, const char* icon, const char* name, bool removable,
                     std::function<void(T&)> reset, DrawFn&& draw);

        void DrawTransform(TransformComponent& component);
        void DrawCamera(CameraComponent& component);
        void DrawSprite(SpriteRendererComponent& component);
        void DrawDirectionalLight(DirLightComponent& component);
        void DrawPointLight(PointLightComponent& component);
        void DrawModel(ProductModelComponent& component);
        void DrawModelMaterials(ProductModelComponent& component);
        void DrawScript(ScriptComponent& component);

        // Picks a published asset of `type` (also by dropping its source file);
        // returns true when `id` changed.
        bool        AssetPicker(const char* label, AssetType type, AssetID& id);
        // Picks a replacement material; returns true with an invalid ID to restore the imported one.
        bool        MaterialPicker(const char* label, AssetID imported, AssetID current, AssetID& chosen);
        std::string MaterialName(AssetID material);

        SceneHierachyPanel&            m_Hierarchy;
        std::vector<EditorAssetChoice> m_AssetChoices;
        AssetManager*                  m_Assets = nullptr;
        std::array<char, 128>          m_ComponentFilter {};
        std::array<char, 128>          m_AssetFilter {};
    };
} // namespace Kans
