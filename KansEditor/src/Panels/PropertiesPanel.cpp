#include "PropertiesPanel.h"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"
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
        Entity entity = m_Hierarchy.getSelectEntity();
        if (!entity)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Select an entity in the hierarchy to edit its components.");
            return;
        }
        DrawHeader(entity);
        ImGui::Spacing();

        Section<TransformComponent>(
            entity, ICON_FA_ARROWS_MOVE, "Transform", false,
            [](TransformComponent& c) { c = TransformComponent(); },
            [this](TransformComponent& c) { DrawTransform(c); });
        Section<CameraComponent>(
            entity, ICON_FA_VIDEO, "Camera", true,
            [](CameraComponent& c) { c = CameraComponent(); },
            [this](CameraComponent& c) { DrawCamera(c); });
        Section<ProductModelComponent>(
            entity, ICON_FA_CUBE, "Model", true,
            [](ProductModelComponent& c) { c.Overrides.clear(); },
            [this](ProductModelComponent& c) { DrawModel(c); });
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
            {ICON_FA_CUBE, "Model", entity.HasComponent<ProductModelComponent>(),
             [&] { entity.AddComponent<ProductModelComponent>(); }},
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
                reset(entity.GetComponent<T>());
            if (removable && ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                remove = true;
            if (!reset && !removable)
                ImGui::TextDisabled("No actions");
            ImGui::EndPopup();
        }
        if (open)
        {
            ImGui::Spacing();
            draw(entity.GetComponent<T>());
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

    void PropertiesPanel::DrawModel(ProductModelComponent& c)
    {
        if (PropertyGrid::Begin("##Model"))
        {
            if (AssetPicker("Model", AssetType::Model, c.Model))
                c.Overrides.clear(); // Overrides refer to the previous model's parts.
            PropertyGrid::End();
        }
        if (!c.Model.IsValid())
        {
            ImGui::TextDisabled("Pick an imported model, or drop a model file on the field.");
            return;
        }
        DrawModelMaterials(c);
    }

    void PropertiesPanel::DrawModelMaterials(ProductModelComponent& component)
    {
        if (!m_Assets)
            return;
        const auto model = m_Assets->Request<ModelAsset>(component.Model);
        if (model.State == AssetLoadState::Loading)
        {
            ImGui::TextDisabled("Loading model...");
            return;
        }
        if (!model.IsReady())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ErrorColor, "Model unavailable: %s", model.Error ? model.Error->Message.c_str() : "");
            ImGui::PopTextWrapPos();
            return;
        }

        // A part is one Mesh Product of the model, named after the node using it.
        struct Part
        {
            AssetID     Mesh, Material;
            std::string Name;
        };
        std::vector<Part>    parts;
        std::vector<AssetID> slots;
        for (const auto& node : model.Asset->GetData().Nodes)
            for (size_t i = 0; i < node.Meshes.size(); ++i)
            {
                const AssetID mesh = node.Meshes[i];
                if (std::any_of(parts.begin(), parts.end(), [&](const Part& part) { return part.Mesh == mesh; }))
                    continue;
                Part part {mesh, {}, node.Name.empty() ? "Part " + std::to_string(parts.size()) : node.Name};
                if (node.Meshes.size() > 1)
                    part.Name += " (" + std::to_string(i + 1) + ")";
                const auto loaded = m_Assets->Request<MeshAsset>(mesh);
                if (loaded.IsReady())
                    part.Material = loaded.Asset->GetData().Material;
                if (part.Material.IsValid() && std::find(slots.begin(), slots.end(), part.Material) == slots.end())
                    slots.push_back(part.Material);
                parts.push_back(std::move(part));
            }

        if (ImGui::TreeNodeEx("##Parts", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                              "Parts (%zu)", parts.size()))
        {
            if (PropertyGrid::Begin("##PartGrid"))
            {
                for (const auto& part : parts)
                {
                    ImGui::PushID(part.Mesh.ToString().c_str());
                    AssetID    chosen;
                    const auto current = component.ResolveMaterial(part.Mesh, part.Material);
                    if (MaterialPicker(part.Name.c_str(), part.Material, current, chosen))
                        component.SetOverride(part.Mesh, chosen);
                    ImGui::PopID();
                }
                PropertyGrid::End();
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("##Slots", ImGuiTreeNodeFlags_SpanAvailWidth, "Material slots (%zu)", slots.size()))
        {
            ImGui::TextDisabled("Replaces the material on every part using it.");
            if (PropertyGrid::Begin("##SlotGrid"))
            {
                for (AssetID slot : slots)
                {
                    ImGui::PushID(slot.ToString().c_str());
                    AssetID     chosen;
                    const auto* overridden = component.FindOverride(slot);
                    const auto  name       = MaterialName(slot);
                    if (MaterialPicker(name.c_str(), slot, overridden ? overridden->Replacement : slot, chosen))
                        component.SetOverride(slot, chosen);
                    ImGui::PopID();
                }
                PropertyGrid::End();
            }
            ImGui::TreePop();
        }
        if (!component.Overrides.empty() &&
            ImGui::Button(ICON_FA_ROTATE_RIGHT "  Restore imported materials", {-FLT_MIN, 0.0f}))
            component.Overrides.clear();
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
