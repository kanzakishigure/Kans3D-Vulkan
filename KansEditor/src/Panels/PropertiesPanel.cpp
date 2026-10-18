#include "PropertiesPanel.h"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"
#include "UI/BloomSettingsWidget.h"
#include "UI/EditorIcons.h"
#include "UI/PropertyGrid.h"

namespace Kans
{
    namespace
    {
        constexpr ImVec4 ErrorColor {0.95f, 0.38f, 0.35f, 1.0f};

        bool ContainsIgnoreCase(const std::string& text, const char* needle)
        {
            std::string a(text), b(needle);
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return a.find(b) != std::string::npos;
        }

        // Search box at the top of a popup or combo list.
        void FilterBox(std::array<char, 128>& filter)
        {
            if (ImGui::IsWindowAppearing())
            {
                filter.fill(0);
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Search", filter.data(), filter.size());
        }
    } // namespace

    // ---------------------------------------------------------------------- layout

    void PropertiesPanel::Draw()
    {
        Entity entity = m_Hierarchy.GetSelected();
        if (!entity)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Select an entity in the hierarchy to edit its components.");
            return;
        }
        DrawHeader(entity);
        DrawPrefabPart(entity);
        ImGui::Spacing();

        Section<TransformComponent>(
            entity, ICON_FA_ARROWS_MOVE, "Transform", false,
            [](TransformComponent& c) { c = TransformComponent(); },
            [this](TransformComponent& c) { DrawTransform(c); });
        Section<CameraComponent>(
            entity, ICON_FA_VIDEO, "Camera", true,
            [](CameraComponent& c) { c = CameraComponent(); },
            [this](CameraComponent& c) { DrawCamera(c); });
        Section<PrefabInstanceComponent>(
            entity, ICON_FA_CUBES, "Prefab", !entity.HasComponent<PrefabOwnedComponent>(),
            [](PrefabInstanceComponent& c) { c.Overrides.clear(); },
            [this, entity](PrefabInstanceComponent&) { DrawPrefabInstance(entity); });
        Section<MeshComponent>(
            entity, ICON_FA_CUBE, "Mesh", !entity.HasComponent<PrefabOwnedComponent>(), {},
            [this, entity](MeshComponent&) { DrawMesh(entity); });
        Section<SpriteRendererComponent>(
            entity, ICON_FA_IMAGE, "Sprite Renderer", true,
            [](SpriteRendererComponent& c) {
                c.Color        = glm::vec4(1.0f);
                c.TilingFactor = 1.0f;
            },
            [this](SpriteRendererComponent& c) { DrawSprite(c); });
        Section<DirLightComponent>(
            entity, ICON_FA_SUN, "Directional Light", true,
            [](DirLightComponent& c) {
                c.Direction         = {0.0f, -1.0f, 0.0f};
                c.Diffuse_Intensity = c.Specular_Intensity = c.Ambient_Intensity = glm::vec3(1.0f);
            },
            [this](DirLightComponent& c) { DrawDirectionalLight(c); });
        Section<PointLightComponent>(
            entity, ICON_FA_LIGHTBULB, "Point Light", true,
            [](PointLightComponent& c) { c.Diffuse_Intensity = c.Specular_Intensity = c.Ambient_Intensity = glm::vec3(1.0f); },
            [this](PointLightComponent& c) { DrawPointLight(c); });
        Section<ScriptComponent>(
            entity, ICON_FA_CODE, "Script", true, {}, [this](ScriptComponent& c) { DrawScript(c); });

        ImGui::Spacing();
        DrawAddComponent(entity);
    }

    void PropertiesPanel::DrawHeader(Entity entity)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_FA_CUBE);
        ImGui::SameLine();
        char name[256] = {};
        tag.copy(name, sizeof(name) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##Name", name, sizeof(name)))
            tag = name;
        if (entity.HasComponent<IDComponent>())
        {
            const auto id = std::to_string(static_cast<uint64_t>(entity.GetUUID()));
            ImGui::TextDisabled("ID %s", id.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_COPY "##CopyID"))
                ImGui::SetClipboardText(id.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Copy the entity ID");
        }
    }

    void PropertiesPanel::DrawAddComponent(Entity entity)
    {
        struct Option
        {
            const char*           Icon;
            const char*           Name;
            bool                  Present;
            std::function<void()> Add;
        };
        const Option options[] = {
            {ICON_FA_VIDEO, "Camera", entity.HasComponent<CameraComponent>(),
             [&] { entity.AddComponent<CameraComponent>(); }},
            {ICON_FA_CUBES, "Prefab", entity.HasComponent<PrefabInstanceComponent>(),
             [&] { entity.AddComponent<PrefabInstanceComponent>(); }},
            {ICON_FA_IMAGE, "Sprite Renderer", entity.HasComponent<SpriteRendererComponent>(),
             [&] { entity.AddComponent<SpriteRendererComponent>().Texture = Renderer::GetWhiteTexture(); }},
            {ICON_FA_SUN, "Directional Light", entity.HasComponent<DirLightComponent>(),
             [&] {
                 auto& light             = entity.AddComponent<DirLightComponent>();
                 light.Direction         = {0.0f, -1.0f, 0.0f};
                 light.Diffuse_Intensity = light.Specular_Intensity = light.Ambient_Intensity = glm::vec3(1.0f);
             }},
            {ICON_FA_LIGHTBULB, "Point Light", entity.HasComponent<PointLightComponent>(),
             [&] {
                 auto& light             = entity.AddComponent<PointLightComponent>();
                 light.Diffuse_Intensity = light.Specular_Intensity = light.Ambient_Intensity = glm::vec3(1.0f);
             }},
            {ICON_FA_CODE, "Script", entity.HasComponent<ScriptComponent>(),
             [&] { entity.AddComponent<ScriptComponent>(); }},
        };
        const bool available =
            std::any_of(std::begin(options), std::end(options), [](const Option& option) { return !option.Present; });
        ImGui::BeginDisabled(!available);
        if (ImGui::Button(ICON_FA_PLUS "  Add Component", {-FLT_MIN, 0.0f}))
            ImGui::OpenPopup("##AddComponent");
        ImGui::EndDisabled();
        ImGui::SetNextWindowSizeConstraints({ImGui::GetItemRectSize().x, 0.0f}, {FLT_MAX, 400.0f});
        if (!ImGui::BeginPopup("##AddComponent"))
            return;
        FilterBox(m_ComponentFilter);
        ImGui::Separator();
        for (const auto& option : options)
            if (!option.Present && ContainsIgnoreCase(option.Name, m_ComponentFilter.data()) &&
                ImGui::Selectable((std::string(option.Icon) + "  " + option.Name).c_str()))
            {
                option.Add();
                ImGui::CloseCurrentPopup();
            }
        ImGui::EndPopup();
    }

    template<typename T, typename DrawFn>
    void PropertiesPanel::Section(Entity entity, const char* icon, const char* name, bool removable,
                                  std::function<void(T&)> reset, DrawFn&& draw)
    {
        if (!entity.HasComponent<T>())
            return;
        ImGui::PushID(name);
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                         ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap |
                                         ImGuiTreeNodeFlags_FramePadding;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0f, 4.0f});
        const bool   open   = ImGui::TreeNodeEx("##section", flags, "%s  %s", icon, name);
        const bool   menu   = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const float  height = ImGui::GetFrameHeight();
        ImGui::PopStyleVar();
        // Settings button at the right end of the header.
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - height);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(ICON_FA_GEAR, {height, height}) || menu)
            ImGui::OpenPopup("##SectionMenu");
        ImGui::PopStyleColor();
        bool remove = false;
        if (ImGui::BeginPopup("##SectionMenu"))
        {
            if (reset && ImGui::MenuItem(ICON_FA_ROTATE_RIGHT "  Reset"))
                entity.PatchComponent<T>(reset);
            if (removable && ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                remove = true;
            if (!reset && !removable)
                ImGui::TextDisabled("No actions");
            ImGui::EndPopup();
        }
        if (open)
        {
            ImGui::Spacing();
            // Edits go through PatchComponent so the render world sees them; it only
            // notifies on an actual change. Mesh and prefab sections notify from their
            // pickers instead: any prefab notification reconciles the instance.
            if constexpr (std::is_same_v<T, MeshComponent> || std::is_same_v<T, PrefabInstanceComponent>)
                draw(entity.GetComponent<T>());
            else
                entity.PatchComponent<T>(draw);
            ImGui::Spacing();
            ImGui::TreePop();
        }
        if (remove)
            entity.RemoveComponent<T>();
        ImGui::PopID();
    }

    // ---------------------------------------------------------------------- components

    void PropertiesPanel::DrawTransform(TransformComponent& c)
    {
        if (!PropertyGrid::Begin("##Transform"))
            return;
        PropertyGrid::Vec3("Position", c.Position);
        glm::vec3 rotation = glm::degrees(c.Rotation);
        if (PropertyGrid::Vec3("Rotation", rotation, 0.0f, 0.5f))
            c.Rotation = glm::radians(rotation);
        PropertyGrid::Vec3("Scale", c.Scale, 1.0f, 0.01f);
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawCamera(CameraComponent& c)
    {
        auto& camera = c.SceneCamera;
        if (!PropertyGrid::Begin("##Camera"))
            return;
        PropertyGrid::Checkbox("Primary", c.Primary);
        PropertyGrid::Checkbox("Fixed aspect", c.FixedAspectRatio);
        const char* projections[] = {"Perspective", "Orthographic"};
        int         projection    = int(camera.GetProjectionType());
        if (PropertyGrid::Combo("Projection", projections, 2, projection))
            camera.SetProjectionType(SceneCamera::ProjectionType(projection));
        if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
        {
            float fov = camera.GetPerspectiveVerticalFOV();
            if (PropertyGrid::Float("Vertical FOV", fov, 0.1f, 1.0f, 179.0f, "%.1f deg"))
                camera.SetPerspectiveVerticalFOV(fov);
            float nearClip = camera.GetPerspectiveNearClip();
            if (PropertyGrid::Float("Near clip", nearClip, 0.01f, 0.001f, 1000.0f))
                camera.SetPerspectiveNearClip(nearClip);
            float farClip = camera.GetPerspectiveFarClip();
            if (PropertyGrid::Float("Far clip", farClip, 1.0f, 0.01f, 100000.0f, "%.1f"))
                camera.SetPerspectiveFarClip(farClip);
        }
        else
        {
            float size = camera.GetOrthographicSize();
            if (PropertyGrid::Float("Size", size, 0.1f, 0.01f, 10000.0f))
                camera.SetOrthographicSize(size);
            float nearClip = camera.GetOrthographicNearClip();
            if (PropertyGrid::Float("Near clip", nearClip, 0.1f))
                camera.SetOrthographicNearClip(nearClip);
            float farClip = camera.GetOrthographicFarClip();
            if (PropertyGrid::Float("Far clip", farClip, 0.1f))
                camera.SetOrthographicFarClip(farClip);
        }
        float exposure = camera.GetExposure();
        if (PropertyGrid::Float("Exposure", exposure, 0.01f, 0.0f, 100.0f))
            camera.SetExposure(exposure);
        auto bloom = camera.GetBloomSettings();
        if (DrawBloomSettings(bloom, [this](AssetID& dirt) { return AssetPicker("Lens dirt", AssetType::Texture, dirt); }))
            camera.SetBloomSettings(bloom);
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawSprite(SpriteRendererComponent& c)
    {
        if (!PropertyGrid::Begin("##Sprite"))
            return;
        PropertyGrid::Color4("Color", c.Color);
        PropertyGrid::Float("Tiling", c.TilingFactor, 0.01f, 0.0f, 100.0f);
        const std::string texture = c.Texture && !c.Texture->GetPath().empty() ? c.Texture->GetPath() : "(white)";
        PropertyGrid::Text("Texture", texture, true);
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawDirectionalLight(DirLightComponent& c)
    {
        if (!PropertyGrid::Begin("##DirLight"))
            return;
        PropertyGrid::Vec3("Direction", c.Direction, 0.0f, 0.01f);
        PropertyGrid::Color3("Diffuse", c.Diffuse_Intensity);
        PropertyGrid::Color3("Specular", c.Specular_Intensity);
        PropertyGrid::Color3("Ambient", c.Ambient_Intensity);
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawPointLight(PointLightComponent& c)
    {
        if (!PropertyGrid::Begin("##PointLight"))
            return;
        PropertyGrid::Color3("Diffuse", c.Diffuse_Intensity);
        PropertyGrid::Color3("Specular", c.Specular_Intensity);
        PropertyGrid::Color3("Ambient", c.Ambient_Intensity);
        PropertyGrid::Text("Position", "From the Transform", true);
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawScript(ScriptComponent& c)
    {
        if (!PropertyGrid::Begin("##Script"))
            return;
        const bool exists = ScriptEngine::EntityClassExists(c.ClassName);
        PropertyGrid::Row("Class", "Name of a C# entity class");
        char buffer[256] = {};
        c.ClassName.copy(buffer, sizeof(buffer) - 1);
        if (!exists)
            ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
        if (ImGui::InputText("##Class", buffer, sizeof(buffer)))
            c.ClassName = buffer;
        if (!exists)
            ImGui::PopStyleColor();
        if (!exists)
            PropertyGrid::Text("", c.ClassName.empty() ? "Enter a class name." : "No entity class with this name.", true);
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawPrefabPart(Entity entity)
    {
        // A part of a prefab instance: its structure comes from the prefab; transform and
        // material edits are kept as overrides on the instance.
        if (!entity.HasComponent<PrefabOwnedComponent>())
            return;
        Scene*      scene = m_Hierarchy.GetScene();
        const auto& owned = entity.GetComponent<PrefabOwnedComponent>();
        Entity      root  = scene ? scene->GetEntityByUUID(owned.Instance) : Entity {};
        if (!root)
            return;
        ImGui::Spacing();
        ImGui::TextDisabled(ICON_FA_CUBES "  Part of %s", root.GetComponent<TagComponent>().Tag.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select instance"))
            m_Hierarchy.SetSelected(root);
        const auto& overrides = root.GetComponent<PrefabInstanceComponent>().Overrides;
        if (overrides.count(owned.NodeKey))
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_ROTATE_RIGHT " Revert part"))
                scene->ClearPrefabOverride(root, owned.NodeKey);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore this part's transform and materials from the prefab");
        }
    }

    void PropertiesPanel::DrawPrefabInstance(Entity root)
    {
        Scene* scene    = m_Hierarchy.GetScene();
        auto&  instance = root.GetComponent<PrefabInstanceComponent>();
        if (PropertyGrid::Begin("##Prefab"))
        {
            AssetID chosen = instance.Prefab;
            if (AssetPicker("Prefab", AssetType::Model, chosen))
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
                    if (MaterialPicker(MaterialName(imported).c_str(), imported, current, chosen))
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

    void PropertiesPanel::DrawMesh(Entity entity)
    {
        Scene* scene = m_Hierarchy.GetScene();
        auto&  mesh  = entity.GetComponent<MeshComponent>();
        Entity root  = scene ? scene->GetPrefabInstanceRoot(entity) : Entity {};
        const bool owned = root && entity.HasComponent<PrefabOwnedComponent>();
        if (PropertyGrid::Begin("##Mesh"))
        {
            if (owned)
                PropertyGrid::Text("Mesh", mesh.Section ? "Section " + std::to_string(*mesh.Section + 1) : "All sections");
            else
            {
                AssetID chosen = mesh.Mesh;
                if (AssetPicker("Mesh", AssetType::StaticMesh, chosen))
                    entity.PatchComponent<MeshComponent>([&](MeshComponent& m) {
                        m.Mesh = chosen;
                        m.Materials.clear();
                    });
            }
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
        // A part shows only the slots its sections use.
        std::vector<uint32_t> shown;
        if (mesh.Section && m_Assets)
        {
            if (const auto data = m_Assets->Request<MeshAsset>(mesh.Mesh); data.IsReady() &&
                                                                         *mesh.Section < data.Asset->GetData().Sections.size())
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
            const AssetID imported = base && slot < base->Materials.size() ? base->Materials[slot] :
                                                                             slots[slot].DefaultMaterial;
            const AssetID current  = slot < mesh.Materials.size() && mesh.Materials[slot].IsValid() ?
                                         mesh.Materials[slot] :
                                         imported;
            AssetID       chosen;
            const auto    label = slots[slot].Name.empty() ? "Slot " + std::to_string(slot + 1) : slots[slot].Name;
            if (MaterialPicker(label.c_str(), imported, current, chosen))
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

    // ---------------------------------------------------------------------- pickers

    bool PropertiesPanel::AssetPicker(const char* label, AssetType type, AssetID& id)
    {
        PropertyGrid::Row(label);
        ImGui::PushID(label);
        bool        changed = false;
        std::string preview = id.IsValid() ? "(missing) " + id.ToString() : "(none)";
        for (const auto& choice : m_AssetChoices)
            if (choice.ID == id)
                preview = choice.Name;
        if (ImGui::BeginCombo("##asset", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            FilterBox(m_AssetFilter);
            for (const auto& choice : m_AssetChoices)
            {
                if (choice.Type != type || !ContainsIgnoreCase(choice.Name, m_AssetFilter.data()))
                    continue;
                const bool selected = choice.ID == id;
                if (ImGui::Selectable(choice.Name.c_str(), selected) && !selected)
                {
                    id      = choice.ID;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered() && id.IsValid())
            ImGui::SetTooltip("%s\nDrop a source file here to replace it.", preview.c_str());
        // Accept a source file dragged from the content browser.
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("asset_payload"))
            {
                std::error_code error;
                const auto      dropped = std::filesystem::weakly_canonical(
                    std::filesystem::u8path(std::string(static_cast<const char*>(payload->Data), payload->DataSize)),
                    error);
                for (const auto& choice : m_AssetChoices)
                    if (choice.Type == type && !error && choice.SourcePath == dropped)
                    {
                        changed = choice.ID != id;
                        id      = choice.ID;
                        break;
                    }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
        return changed;
    }

    std::string PropertiesPanel::MaterialName(AssetID material)
    {
        if (!material.IsValid())
            return "(default)";
        std::string name = material.ToString();
        if (m_Assets)
        {
            const auto status = m_Assets->Request<MaterialDataAsset>(material);
            if (status.IsReady())
                name = status.Asset->GetData().Name.empty() ? "(unnamed)" : status.Asset->GetData().Name;
            else if (status.State == AssetLoadState::Loading)
                name = "(loading)";
        }
        for (const auto& choice : m_AssetChoices)
            if (choice.ID == material)
                return name + "  [" + choice.SourcePath.filename().u8string() + "]";
        return name;
    }

    bool PropertiesPanel::MaterialPicker(const char* label, AssetID imported, AssetID current, AssetID& chosen)
    {
        PropertyGrid::Row(label);
        bool       picked     = false;
        const bool overridden = current != imported;
        if (overridden)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.30f, 0.45f, 1.0f));
        const bool open = ImGui::BeginCombo("##material", MaterialName(current).c_str(), ImGuiComboFlags_HeightLarge);
        if (overridden)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(overridden ? "Replaced; imported: %s" : "Imported material: %s",
                              MaterialName(imported).c_str());
        if (!open)
            return false;
        FilterBox(m_AssetFilter);
        if (ImGui::Selectable(("Imported: " + MaterialName(imported)).c_str(), !overridden))
        {
            chosen = {};
            picked = true;
        }
        ImGui::Separator();
        for (const auto& choice : m_AssetChoices)
        {
            if (choice.Type != AssetType::Material)
                continue;
            const auto name = MaterialName(choice.ID);
            if (!ContainsIgnoreCase(name, m_AssetFilter.data()))
                continue;
            ImGui::PushID(choice.ID.ToString().c_str());
            if (ImGui::Selectable(name.c_str(), choice.ID == current))
            {
                chosen = choice.ID;
                picked = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
        return picked;
    }
} // namespace Kans
