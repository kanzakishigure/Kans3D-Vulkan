#include <algorithm>
#include <map>
#include "ComponentEditors.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/Scene.h"
#include "UI/EditorIcons.h"
#include "UI/PropertyGrid.h"

namespace Kans::ComponentEditors
{
    // Mesh and prefab edits notify from their pickers rather than through a
    // per-frame PatchComponent: any prefab notification reconciles the instance.

    void DrawPrefabInstance(Entity root, ComponentUI& ui)
    {
        Scene* scene    = ui.GetScene();
        auto&  instance = root.GetComponent<PrefabInstanceComponent>();
        if (PropertyGrid::Begin("##Prefab"))
        {
            AssetID chosen = instance.Prefab;
            if (ui.AssetPicker("Prefab", AssetType::Model, chosen))
                root.PatchComponent<PrefabInstanceComponent>([&](PrefabInstanceComponent& p) {
                    p.Prefab = chosen;
                    p.Overrides.clear(); // Overrides refer to the previous prefab's nodes.
                });
            PropertyGrid::End();
        }
        if (!instance.Prefab.IsValid())
        {
            ImGui::TextDisabled("Pick an imported model, or drop a model file on the field.");
            return;
        }
        if (instance.LegacyPending || !instance.Source)
        {
            ImGui::TextDisabled("Loading prefab...");
            return;
        }
        if (!scene)
            return;

        // Replace a material on every part of this instance that uses it.
        struct Use
        {
            Entity   Part;
            uint32_t Slot;
        };
        std::vector<std::pair<AssetID, std::vector<Use>>> materials;
        for (Entity part : scene->GetMeshEntities(root))
        {
            if (!part.HasComponent<PrefabOwnedComponent>())
                continue;
            const auto& key   = part.GetComponent<PrefabOwnedComponent>().NodeKey;
            auto        state = instance.Nodes.find(key);
            if (state == instance.Nodes.end())
                continue;
            const auto& base = state->second.BaseMesh.Materials;
            for (uint32_t slot = 0; slot < base.size(); ++slot)
            {
                auto found = std::find_if(materials.begin(), materials.end(),
                                          [&](const auto& item) { return item.first == base[slot]; });
                if (found == materials.end())
                    found = materials.insert(materials.end(), {base[slot], {}});
                found->second.push_back({part, slot});
            }
        }
        if (ImGui::TreeNodeEx("##Materials", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                              "Materials (%zu)", materials.size()))
        {
            ImGui::TextDisabled("Replaces the material on every part using it.");
            if (PropertyGrid::Begin("##MaterialGrid"))
            {
                for (const auto& [imported, uses] : materials)
                {
                    ImGui::PushID(imported.ToString().c_str());
                    // Shown as replaced only when every use carries the same replacement.
                    AssetID current = uses.front().Part.GetComponent<MeshComponent>().Materials.at(uses.front().Slot);
                    for (const auto& use : uses)
                        if (use.Part.GetComponent<MeshComponent>().Materials.at(use.Slot) != current)
                            current = imported;
                    AssetID chosen;
                    if (ui.MaterialPicker(ui.MaterialName(imported).c_str(), imported, current, chosen))
                        for (const auto& use : uses)
                            scene->SetPrefabMaterialOverride(
                                root, use.Part.GetComponent<PrefabOwnedComponent>().NodeKey, use.Slot, chosen);
                    ImGui::PopID();
                }
                PropertyGrid::End();
            }
            ImGui::TreePop();
        }
        size_t removed = 0;
        for (const auto& [key, change] : instance.Overrides)
            removed += change.Removed ? 1 : 0;
        if (removed && ImGui::Button((ICON_FA_ROTATE_LEFT "  Restore " + std::to_string(removed) + " removed part" +
                                      (removed > 1 ? "s" : ""))
                                         .c_str(),
                                     {-FLT_MIN, 0.0f}))
            for (const auto& [key, change] : std::map(instance.Overrides))
                if (change.Removed)
                    scene->SetPrefabNodeRemoved(root, key, false);
        if (!instance.Overrides.empty() &&
            ImGui::Button(ICON_FA_ROTATE_RIGHT "  Revert all overrides", {-FLT_MIN, 0.0f}))
            for (const auto& [key, change] : std::map(instance.Overrides))
                scene->ClearPrefabOverride(root, key);
    }

    void DrawMesh(Entity entity, ComponentUI& ui)
    {
        Scene*        scene  = ui.GetScene();
        AssetManager* assets = ui.GetAssets();
        auto&         mesh   = entity.GetComponent<MeshComponent>();
        Entity        root   = scene ? scene->GetPrefabInstanceRoot(entity) : Entity {};
        const bool    owned  = root && entity.HasComponent<PrefabOwnedComponent>();
        if (PropertyGrid::Begin("##Mesh"))
        {
            if (owned)
                PropertyGrid::Text("Mesh", mesh.Section ? "Section " + std::to_string(*mesh.Section + 1) : "All sections");
            else
            {
                AssetID chosen = mesh.Mesh;
                if (ui.AssetPicker("Mesh", AssetType::StaticMesh, chosen))
                    entity.PatchComponent<MeshComponent>([&](MeshComponent& m) {
                        m.Mesh = chosen;
                        m.Materials.clear();
                    });
                // A prefab part's flags come from the prefab and are restored when it
                // re-expands; use the material's receive_shadows for parts instead.
                bool cast = mesh.CastShadows, receive = mesh.ReceiveShadows;
                if (PropertyGrid::Checkbox("Cast shadows", cast) | PropertyGrid::Checkbox("Receive shadows", receive))
                    entity.PatchComponent<MeshComponent>([&](MeshComponent& m) {
                        m.CastShadows    = cast;
                        m.ReceiveShadows = receive;
                    });
            }
            // Read by post-process materials. Unlike the shadow flags, a prefab part keeps
            // these as a prefab override, so they are editable on parts too.
            bool custom  = mesh.RenderCustomDepth;
            int  stencil = mesh.CustomStencil;
            if (PropertyGrid::Checkbox("Custom depth", custom) |
                (custom && PropertyGrid::Int("Custom stencil", stencil, 0, 255)))
                entity.PatchComponent<MeshComponent>([&](MeshComponent& m) {
                    m.RenderCustomDepth = custom;
                    m.CustomStencil     = uint8_t(std::clamp(stencil, 0, 255));
                });
            PropertyGrid::End();
        }
        if (!mesh.Mesh.IsValid() || !scene)
            return;
        const auto slots = scene->GetMeshSlots(entity);
        if (slots.empty())
        {
            ImGui::TextDisabled("Loading mesh...");
            return;
        }
        if (assets)
            if (const auto data = assets->Request<MeshAsset>(mesh.Mesh); data.IsReady())
            {
                const auto& d = data.Asset->GetData();
                ImGui::TextDisabled("%zu vertices, %zu triangles%s",
                                    d.Vertices.size(),
                                    d.Indices.size() / 3,
                                    d.HasSmoothNormals() ? ", smooth normals" : "");
            }
        // A part shows only the slots its sections use.
        std::vector<uint32_t> shown;
        if (mesh.Section && assets)
        {
            if (const auto data = assets->Request<MeshAsset>(mesh.Mesh);
                data.IsReady() && *mesh.Section < data.Asset->GetData().Sections.size())
                shown.push_back(data.Asset->GetData().Sections[*mesh.Section].MaterialSlot);
        }
        if (shown.empty())
            for (uint32_t slot = 0; slot < slots.size(); ++slot)
                shown.push_back(slot);
        const MeshComponent* base = nullptr;
        if (owned)
        {
            const auto& nodes = root.GetComponent<PrefabInstanceComponent>().Nodes;
            auto        state = nodes.find(entity.GetComponent<PrefabOwnedComponent>().NodeKey);
            if (state != nodes.end())
                base = &state->second.BaseMesh;
        }
        if (!PropertyGrid::Begin("##Slots"))
            return;
        for (uint32_t slot : shown)
        {
            ImGui::PushID(int(slot));
            const AssetID imported =
                base && slot < base->Materials.size() ? base->Materials[slot] : slots[slot].DefaultMaterial;
            const AssetID current =
                slot < mesh.Materials.size() && mesh.Materials[slot].IsValid() ? mesh.Materials[slot] : imported;
            AssetID    chosen;
            const auto label = slots[slot].Name.empty() ? "Slot " + std::to_string(slot + 1) : slots[slot].Name;
            if (ui.MaterialPicker(label.c_str(), imported, current, chosen))
            {
                if (owned)
                    scene->SetPrefabMaterialOverride(root, entity.GetComponent<PrefabOwnedComponent>().NodeKey, slot,
                                                     chosen);
                else
                    entity.PatchComponent<MeshComponent>([&](MeshComponent& m) {
                        m.Materials.resize(std::max<size_t>(m.Materials.size(), slot + 1));
                        m.Materials[slot] = chosen;
                    });
            }
            ImGui::PopID();
        }
        PropertyGrid::End();
    }
} // namespace Kans::ComponentEditors
