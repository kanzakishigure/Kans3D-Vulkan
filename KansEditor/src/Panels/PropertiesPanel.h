#pragma once
#include <any>
#include <array>
#include <filesystem>
#include <typeindex>
#include <vector>
#include "Components/ComponentRegistry.h"
#include "HierarchyPanel.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Editor/EditorAssetChoice.h"
#include "Kans3D/Scene/Components.h"

namespace Kans
{
    // Edits the components of the entity selected in the hierarchy: a header
    // with the entity name, one collapsible section per component (generated from
    // the ComponentRegistry, with Reset, Copy/Paste, Revert and Remove) and an
    // Add Component menu grouped by category.
    class PropertiesPanel final : public ComponentUI
    {
    public:
        explicit PropertiesPanel(HierarchyPanel& hierarchy) : m_Hierarchy(hierarchy) {}

        void Draw();
        // Project-relative map the editor lights scenes without environment settings with.
        void SetDefaultEnvironment(std::filesystem::path map) { m_DefaultEnvironment = std::move(map); }
        void SetAssetChoices(std::vector<EditorAssetChoice> choices) { m_AssetChoices = std::move(choices); }
        // Used to show model parts and material names; may be null.
        void SetAssetManager(AssetManager* assets) { m_Assets = assets; }

        // ComponentUI
        Scene*        GetScene() override { return m_Hierarchy.GetScene(); }
        AssetManager* GetAssets() override { return m_Assets; }
        void          Select(Entity entity) override { m_Hierarchy.SetSelected(entity); }
        bool          AssetPicker(const char* label, AssetType type, AssetID& id) override;
        bool        MaterialPicker(const char* label, AssetID imported, AssetID current, AssetID& chosen) override;
        std::string MaterialName(AssetID material) override;

    private:
        // Scene-wide settings, shown when no entity is selected.
        void DrawSceneSettings(Scene& scene);
        void DrawPostProcess(Scene& scene);
        void DrawHeader(Entity entity);
        void DrawPrefabPart(Entity entity);
        void DrawSection(Entity entity, const ComponentEntry& component);
        void DrawAddComponent(Entity entity);

        HierarchyPanel&                m_Hierarchy;
        std::vector<EditorAssetChoice> m_AssetChoices;
        AssetManager*                  m_Assets = nullptr;
        std::array<char, 128>          m_ComponentFilter {};
        std::array<char, 128>          m_AssetFilter {};
        // Copied component values (Copy/Paste in a section's menu).
        std::type_index m_ClipboardType = typeid(void);
        std::any        m_Clipboard;
        std::vector<std::filesystem::path> m_EnvironmentMaps;
        std::filesystem::path              m_DefaultEnvironment;
    };
} // namespace Kans
