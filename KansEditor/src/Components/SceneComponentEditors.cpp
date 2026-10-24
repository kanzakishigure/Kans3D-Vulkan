#include "ComponentEditors.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Script/ScriptEngine.h"
#include "UI/BloomSettingsWidget.h"
#include "UI/PropertyGrid.h"
#include "UI/ShadowSettingsWidget.h"

namespace Kans::ComponentEditors
{
    void DrawTransform(Entity entity, ComponentUI&)
    {
        if (!PropertyGrid::Begin("##Transform"))
            return;
        // Edits go through PatchComponent so the render world sees them; it only
        // notifies on an actual change.
        entity.PatchComponent<TransformComponent>([](TransformComponent& c) {
            PropertyGrid::Vec3("Position", c.Position);
            glm::vec3 rotation = glm::degrees(c.Rotation);
            if (PropertyGrid::Vec3("Rotation", rotation, 0.0f, 0.5f))
                c.Rotation = glm::radians(rotation);
            PropertyGrid::Vec3("Scale", c.Scale, 1.0f, 0.01f);
        });
        PropertyGrid::End();
    }

    void DrawCamera(Entity entity, ComponentUI& ui)
    {
        if (!PropertyGrid::Begin("##Camera"))
            return;
        entity.PatchComponent<CameraComponent>([&](CameraComponent& c) {
            auto& camera = c.SceneCamera;
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
            auto antiAliasing = camera.GetAntiAliasing();
            if (DrawAntiAliasing(antiAliasing))
                camera.SetAntiAliasing(antiAliasing);
            // Post-processing normally comes from the scene; an override applies to this camera only.
            bool overridden = camera.GetPostProcessOverride().has_value();
            if (PropertyGrid::Checkbox("Override post", overridden))
                camera.SetPostProcessOverride(
                    overridden ? std::optional<PostProcessSettings>(
                                     ui.GetScene() ? ui.GetScene()->GetPostProcessSettings() : PostProcessSettings {}) :
                                 std::nullopt);
            if (camera.GetPostProcessOverride())
            {
                auto settings = *camera.GetPostProcessOverride();
                if (DrawPostProcessSettings(
                        settings,
                        [&](AssetID& dirt) { return ui.AssetPicker("Lens dirt", AssetType::Texture, dirt); },
                        [&](const char* label, AssetID& id) { return ui.AssetPicker(label, AssetType::Material, id); }))
                    camera.SetPostProcessOverride(settings);
            }
        });
        PropertyGrid::End();
    }

    void DrawSprite(Entity entity, ComponentUI&)
    {
        if (!PropertyGrid::Begin("##Sprite"))
            return;
        entity.PatchComponent<SpriteRendererComponent>([](SpriteRendererComponent& c) {
            PropertyGrid::Color4("Color", c.Color);
            PropertyGrid::Float("Tiling", c.TilingFactor, 0.01f, 0.0f, 100.0f);
            const std::string texture = c.Texture && !c.Texture->GetPath().empty() ? c.Texture->GetPath().u8string() : "(white)";
            PropertyGrid::Text("Texture", texture, true);
        });
        PropertyGrid::End();
    }

    void DrawDirectionalLight(Entity entity, ComponentUI&)
    {
        if (!PropertyGrid::Begin("##DirLight"))
            return;
        entity.PatchComponent<DirLightComponent>([](DirLightComponent& c) {
            PropertyGrid::Vec3("Direction", c.Direction, 0.0f, 0.01f);
            PropertyGrid::Color3("Diffuse", c.Diffuse_Intensity);
            PropertyGrid::Color3("Specular", c.Specular_Intensity);
            PropertyGrid::Color3("Ambient", c.Ambient_Intensity);
            auto shadows = c.Shadows;
            if (DrawShadowSettings(shadows, true))
                c.Shadows = shadows.Validated();
        });
        PropertyGrid::End();
    }

    void DrawPointLight(Entity entity, ComponentUI&)
    {
        if (!PropertyGrid::Begin("##PointLight"))
            return;
        entity.PatchComponent<PointLightComponent>([](PointLightComponent& c) {
            PropertyGrid::Color3("Diffuse", c.Diffuse_Intensity);
            PropertyGrid::Color3("Specular", c.Specular_Intensity);
            PropertyGrid::Color3("Ambient", c.Ambient_Intensity);
            PropertyGrid::Text("Position", "From the Transform", true);
            auto shadows = c.Shadows;
            if (DrawShadowSettings(shadows, false))
                c.Shadows = shadows.Validated();
        });
        PropertyGrid::End();
    }

    void DrawScript(Entity entity, ComponentUI&)
    {
        if (!PropertyGrid::Begin("##Script"))
            return;
        entity.PatchComponent<ScriptComponent>([](ScriptComponent& c) {
            const bool exists = ScriptEngine::EntityClassExists(c.ClassName);
            // Classes come from the generated registry of the loaded script assemblies.
            const auto classes = ScriptEngine::GetEntityClasses();
            if (!classes.empty())
            {
                PropertyGrid::Row("Class", "C# entity class (Scripts > Build & Reload Scripts)");
                if (ImGui::BeginCombo("##ClassList", c.ClassName.empty() ? "Select..." : c.ClassName.c_str()))
                {
                    for (const auto& name : classes)
                        if (ImGui::Selectable(name.c_str(), name == c.ClassName))
                            c.ClassName = name;
                    ImGui::EndCombo();
                }
            }
            PropertyGrid::Row(classes.empty() ? "Class" : "Name", "Name of a C# entity class");
            char buffer[256] = {};
            c.ClassName.copy(buffer, sizeof(buffer) - 1);
            if (!exists)
                ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
            if (ImGui::InputText("##Class", buffer, sizeof(buffer)))
                c.ClassName = buffer;
            if (!exists)
                ImGui::PopStyleColor();
            if (!exists)
                PropertyGrid::Text("",
                                   c.ClassName.empty() ? "Enter a class name." : "No entity class with this name.",
                                   true);
        });
        PropertyGrid::End();
    }
} // namespace Kans::ComponentEditors
