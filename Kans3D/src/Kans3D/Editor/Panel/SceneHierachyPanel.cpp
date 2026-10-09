#include "kspch.h"
#include "SceneHierachyPanel.h"

#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/ImGui/KansUI.h"
#include "Kans3D/Scene/Components.h"

#include "Kans3D/ImGui/Colors.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"

#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace Kans
{
    void SceneHierachyPanel::onImGuiRender(bool isOpen)
    {

        {
            ImGui::Begin("SceneHierachyPanel:");
            m_Context->m_Registry.each([&](auto entityID) {
                Entity entity = {entityID, m_Context.get()};
                drawEntityNode(entity);
            });

            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
            {
                m_SelectionContext = {};
            }
            // create Entity
            if (ImGui::BeginPopupContextWindow(0, 1, false))
            {
                if (ImGui::MenuItem("Create Entity"))
                    m_Context->CreateEntity("Empty Entity");

                ImGui::EndPopup();
            }
            ImGui::End();
        }

        {
            ImGui::Begin("Properties");
            if (m_SelectionContext)
            {

                drawComponents(m_SelectionContext);
                ImGui::Separator();
                ImGui::SameLine(ImGui::GetWindowWidth() / 3);
                if (ImGui::Button("Add Component", ImVec2(ImGui::GetWindowWidth() / 3, 20)))
                {
                    ImGui::OpenPopup("AddComponent");
                }
                if (ImGui::BeginPopup("AddComponent"))
                {
                    if (ImGui::MenuItem("Sprite Component"))
                    {
                        auto& spritCMP   = m_SelectionContext.AddComponent<SpriteRendererComponent>();
                        spritCMP.Texture = Kans::Renderer::GetWhiteTexture();
                        ImGui::CloseCurrentPopup();
                    }
                    if (!m_SelectionContext.HasComponent<ProductModelComponent>() && ImGui::MenuItem("Model Component"))
                    {
                        m_SelectionContext.AddComponent<ProductModelComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Camera Component"))
                    {
                        m_SelectionContext.AddComponent<CameraComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Script Component"))
                    {
                        auto& ScriptCMP = m_SelectionContext.AddComponent<ScriptComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::End();
        }
    }

    void SceneHierachyPanel::drawEntityNode(Entity entity)
    {
        auto tag = entity.GetComponent<TagComponent>();

        ImGuiTreeNodeFlags flag =
            ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
        // extend selectable area
        flag |= ImGuiTreeNodeFlags_SpanAvailWidth;

        bool opened = ImGui::TreeNodeEx((void*)(uint32_t)(entity), flag, tag.Tag.c_str());

        if (ImGui::IsItemClicked())
        {
            m_SelectionContext = entity;
        }
        bool EntityDelete = false;
        // delete Entity
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete Entity"))
            {
                EntityDelete = true;
            }
            ImGui::EndPopup();
        }
        if (opened)
        {
            ImGuiTreeNodeFlags flags  = ImGuiTreeNodeFlags_OpenOnArrow;
            bool               opened = ImGui::TreeNodeEx((void*)7589654, flags, tag.Tag.c_str());
            if (opened)
            {
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        if (EntityDelete)
        {
            m_Context->DestroyEntity(entity);
            if (m_SelectionContext == entity)
            {
                m_SelectionContext = {};
            }
        }
    }
    const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen;
    void                     SceneHierachyPanel::drawComponents(Entity entity)
    {

        if (entity.HasComponent<IDComponent>())
        {
            auto               id    = (uint64_t)entity.GetComponent<IDComponent>().ID;
            std::string        label = "Entity UUID :";
            std::ostringstream os;
            os << label << id;
            ImGui::Text(os.str().c_str());
            ImGui::Separator();
        }

        if (entity.HasComponent<TagComponent>())
        {

            ImGui::Text("Entity Tag :");
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            char  buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, tag.c_str());
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer)), ImGuiInputTextFlags_AutoSelectAll)
            {
                tag = std::string(buffer);
            }
            ImGui::Separator();
        }

        UI::DrawComponent<TransformComponent>("Transform", entity, [](TransformComponent& component) {
            UI::DrawVec3Control("Position", component.Position);

            glm::vec3 rotation = glm::degrees(component.Rotation);
            UI::DrawVec3Control("Rotation", rotation);
            component.Rotation = glm::radians(rotation);

            UI::DrawVec3Control("Scale", component.Scale, 1.0f);
        });
        UI::DrawComponent<CameraComponent>("Camera", entity, [](CameraComponent& component) {
            auto& camera = component.SceneCamera;
            ImGui::Checkbox("Isprimary", &component.Primary);
            ImGui::Checkbox("FixedAspectRatio", &component.FixedAspectRatio);
            char* projectiontype[]    = {"Perspective", "Orthographic"};
            char* curentprojectstring = projectiontype[(int)camera.GetProjectionType()];
            if (ImGui::BeginCombo("Camera ProjectionType", curentprojectstring))
            {

                for (int i = 0; i < 2; i++)
                {
                    bool isSelected = curentprojectstring == projectiontype[i];
                    if (ImGui::Selectable(projectiontype[i], isSelected))
                    {
                        curentprojectstring = projectiontype[i];
                        camera.SetProjectionType((SceneCamera::ProjectionType)i);
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            // Perspective
            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
            {
                // VerticalFOV
                {
                    float verticalFOV = camera.GetPerspectiveVerticalFOV();
                    if (ImGui::DragFloat("VerticalFOV", &verticalFOV))
                    {
                        camera.SetPerspectiveVerticalFOV(verticalFOV);
                    }
                }
                // Near Clip
                {
                    float nearClip = camera.GetPerspectiveNearClip();
                    if (ImGui::DragFloat("Near Clip", &nearClip))
                    {
                        camera.SetPerspectiveNearClip(nearClip);
                    }
                }
                // Far Clip
                {
                    float farClip = camera.GetPerspectiveFarClip();
                    if (ImGui::DragFloat("Far Clip", &farClip))
                    {
                        camera.SetPerspectiveFarClip(farClip);
                    }
                }
                // Exposure
                {
                    float exposure = camera.GetExposure();
                    if (ImGui::DragFloat("Exposure", &exposure))
                    {
                        camera.SetExposure(exposure);
                    }
                }
            }
            // Orthographic
            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
            {
                // Size
                {
                    float size = camera.GetOrthographicSize();
                    if (ImGui::DragFloat("Size", &size))
                    {
                        camera.SetOrthographicSize(size);
                    }
                }
                // Near Clip
                {
                    float nearClip = camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("Near Clip", &nearClip))
                    {
                        camera.SetOrthographicNearClip(nearClip);
                    }
                }
                // Far Clip
                {
                    float farClip = camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("Far Clip", &farClip))
                    {
                        camera.SetOrthographicFarClip(farClip);
                    }
                }
                // Exposure
                {
                    float exposure = camera.GetExposure();
                    if (ImGui::DragFloat("Exposure", &exposure))
                    {
                        camera.SetExposure(exposure);
                    }
                }
            }
        });
        UI::DrawComponent<SpriteRendererComponent>("SpriteRenderer", entity, [](SpriteRendererComponent& component) {
            auto& color = component.Color;
            ImGui::ColorEdit4("Color:", glm::value_ptr(color));
            ImGui::Separator();
            ImGui::Text("Texture path is : %s", component.Texture->GetPath().c_str());
        });
        UI::DrawComponent<DirLightComponent>("DirLight", entity, [](DirLightComponent& component) {
            UI::DrawVec3Control("Direction", component.Direction);
            ImGui::ColorEdit3("Diffuse_Intensity", glm::value_ptr(component.Diffuse_Intensity));
            ImGui::ColorEdit3("Specular_Intensity", glm::value_ptr(component.Specular_Intensity));
            ImGui::ColorEdit3("Ambient_Intensity", glm::value_ptr(component.Ambient_Intensity));
        });
        UI::DrawComponent<PointLightComponent>("PointLight", entity, [](PointLightComponent& component) {
            ImGui::ColorEdit3("Diffuse_Intensity", glm::value_ptr(component.Diffuse_Intensity));
            ImGui::ColorEdit3("Specular_Intensity", glm::value_ptr(component.Specular_Intensity));
            ImGui::ColorEdit3("Ambient_Intensity", glm::value_ptr(component.Ambient_Intensity));
        });
        drawModelComponent(entity);
        UI::DrawComponent<ScriptComponent>("Script", entity, [](ScriptComponent& component) {
            ImGui::Text("Class :");
            char  buffer[256] = {0};
            auto& name        = component.ClassName;
            strcat(buffer, name.c_str());
            if (ScriptEngine::EntityClassExists(name))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::text));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::red_6));
            }
            if (ImGui::InputText("##Name", buffer, sizeof(buffer)))
            {
                name = std::string(buffer);
            }

            ImGui::PopStyleColor();
        });
    }

    bool SceneHierachyPanel::drawAssetPicker(const char* label, AssetType type, AssetID& id)
    {
        bool        changed = false;
        std::string preview = "(none)";
        for (const auto& choice : m_AssetChoices)
            if (choice.ID == id)
                preview = choice.Name;
        if (id.IsValid() && preview == "(none)")
            preview = "(missing) " + id.ToString();
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            for (const auto& choice : m_AssetChoices)
            {
                if (choice.Type != type)
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
        // Accept a source file dragged from the content browser.
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("asset_payload"))
            {
                std::error_code error;
                const auto      dropped = std::filesystem::weakly_canonical(
                    std::filesystem::path(std::string((const char*)payload->Data, payload->DataSize)), error);
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
        return changed;
    }

    void SceneHierachyPanel::drawModelComponent(Entity entity)
    {
        UI::DrawComponent<ProductModelComponent>("Model", entity, [this](ProductModelComponent& component) {
            if (drawAssetPicker("Model", AssetType::Model, component.Model))
                component.Overrides.clear(); // Overrides refer to the previous model's parts.
            if (!component.Model.IsValid())
            {
                ImGui::TextDisabled("Import a model, then pick it or drop its source file here.");
                return;
            }
            drawModelMaterials(component);
        });
    }

    std::string SceneHierachyPanel::materialName(AssetID material)
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

    bool SceneHierachyPanel::drawMaterialCombo(const char* label, AssetID imported, AssetID current, AssetID& chosen)
    {
        bool picked = false;
        if (ImGui::BeginCombo(label, materialName(current).c_str()))
        {
            if (ImGui::Selectable(("Imported: " + materialName(imported)).c_str(), current == imported))
            {
                chosen = {};
                picked = true;
            }
            ImGui::Separator();
            for (const auto& choice : m_AssetChoices)
            {
                if (choice.Type != AssetType::Material)
                    continue;
                ImGui::PushID(choice.ID.ToString().c_str());
                if (ImGui::Selectable(materialName(choice.ID).c_str(), choice.ID == current))
                {
                    chosen = choice.ID;
                    picked = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return picked;
    }

    void SceneHierachyPanel::drawModelMaterials(ProductModelComponent& component)
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
            ImGui::TextWrapped("Model unavailable: %s", model.Error ? model.Error->Message.c_str() : "");
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

        if (ImGui::TreeNodeEx("Parts", ImGuiTreeNodeFlags_DefaultOpen, "Parts (%zu)", parts.size()))
        {
            for (const auto& part : parts)
            {
                ImGui::PushID(part.Mesh.ToString().c_str());
                AssetID    chosen;
                const auto current = component.ResolveMaterial(part.Mesh, part.Material);
                if (drawMaterialCombo(part.Name.c_str(), part.Material, current, chosen))
                    component.SetOverride(part.Mesh, chosen);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Material slots", 0, "Material slots (%zu)", slots.size()))
        {
            ImGui::TextDisabled("Replaces the material on every part using it.");
            for (AssetID slot : slots)
            {
                ImGui::PushID(slot.ToString().c_str());
                AssetID     chosen;
                const auto* overridden = component.FindOverride(slot);
                const auto  name       = materialName(slot);
                if (drawMaterialCombo(name.c_str(), slot, overridden ? overridden->Replacement : slot, chosen))
                    component.SetOverride(slot, chosen);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        if (!component.Overrides.empty() && ImGui::SmallButton("Restore imported materials"))
            component.Overrides.clear();
    }
} // namespace Kans
