#include "ComponentEditors.h"
#include "Kans3D/Animation/AnimatorComponent.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/Scene.h"
#include "UI/EditorIcons.h"

namespace Kans
{
    namespace
    {
        // The override a prefab part carries, if any.
        const PrefabNodeOverride* PartOverride(Entity entity, Scene& scene)
        {
            if (!entity.HasComponent<PrefabOwnedComponent>())
                return nullptr;
            const auto& owned = entity.GetComponent<PrefabOwnedComponent>();
            Entity      root  = scene.GetEntityByUUID(owned.Instance);
            if (!root || !root.HasComponent<PrefabInstanceComponent>())
                return nullptr;
            const auto& overrides = root.GetComponent<PrefabInstanceComponent>().Overrides;
            const auto  found     = overrides.find(owned.NodeKey);
            return found != overrides.end() ? &found->second : nullptr;
        }

        bool IsPrefabPart(Entity entity) { return entity.HasComponent<PrefabOwnedComponent>(); }

        void DefaultDirectionalLight(DirLightComponent& light)
        {
            light.Direction         = {0.0f, -1.0f, 0.0f};
            light.Diffuse_Intensity = light.Specular_Intensity = light.Ambient_Intensity = glm::vec3(1.0f);
        }
        void DefaultPointLight(PointLightComponent& light)
        {
            light.Diffuse_Intensity = light.Specular_Intensity = light.Ambient_Intensity = glm::vec3(1.0f);
        }
    } // namespace

    void RegisterBuiltinComponents(ComponentRegistry& registry)
    {
        using namespace ComponentEditors;

        // Registration order is the Properties panel's section order.
        auto& transform     = registry.Register<TransformComponent>("Transform", ICON_FA_ARROWS_MOVE, "General");
        transform.Add       = {}; // Every entity has one.
        transform.CanRemove = {};
        transform.Draw      = DrawTransform;
        transform.Overridden = [](Entity e, Scene& scene) {
            const auto* change = PartOverride(e, scene);
            return change && change->LocalTransform.has_value();
        };
        transform.Revert = [](Entity e, Scene& scene) {
            // Clear the part's override, then restore its material replacements.
            const auto* change    = PartOverride(e, scene);
            const auto  materials = change ? change->Materials : std::map<uint32_t, AssetID> {};
            const auto& owned     = e.GetComponent<PrefabOwnedComponent>();
            Entity      root      = scene.GetEntityByUUID(owned.Instance);
            scene.ClearPrefabOverride(root, owned.NodeKey);
            for (const auto& [slot, material] : materials)
                scene.SetPrefabMaterialOverride(root, owned.NodeKey, slot, material);
        };

        auto& camera    = registry.Register<CameraComponent>("Camera", ICON_FA_VIDEO, "Rendering");
        camera.IconRank = 1;
        camera.Draw     = DrawCamera;
        camera.CreateName = "Camera";
        camera.CreateOrder = 1;
        // A camera created from the Hierarchy does not take over the primary view.
        camera.Create = [](Entity e) { e.AddComponent<CameraComponent>().Primary = false; };

        auto& prefab      = registry.Register<PrefabInstanceComponent>("Prefab", ICON_FA_CUBES, "Rendering");
        prefab.IconRank   = 4;
        prefab.CanRemove  = [](Entity e) { return !IsPrefabPart(e); };
        prefab.Reset      = [](Entity e) { e.PatchComponent<PrefabInstanceComponent>([](auto& p) { p.Overrides.clear(); }); };
        prefab.Copy       = {};
        prefab.Paste      = {};
        prefab.Draw       = DrawPrefabInstance;
        prefab.CreateName = "Model";
        prefab.CreateOrder = 0;
        prefab.Create     = [](Entity e) { e.AddComponent<PrefabInstanceComponent>(); };

        auto& mesh     = registry.Register<MeshComponent>("Mesh", ICON_FA_CUBE, "Rendering");
        mesh.IconRank  = 5;
        mesh.CanRemove = [](Entity e) { return !IsPrefabPart(e); };
        mesh.Reset     = {};
        mesh.Copy      = {};
        mesh.Paste     = {};
        mesh.Draw      = DrawMesh;
        // Exclusive with an editable mesh, which renders the entity itself.
        mesh.CanAdd = [](Entity e) { return !e.HasComponent<EditableMeshComponent>(); };
        mesh.Overridden = [](Entity e, Scene& scene) {
            const auto* change = PartOverride(e, scene);
            return change && !change->Materials.empty();
        };
        mesh.Revert = [](Entity e, Scene& scene) {
            const auto* change = PartOverride(e, scene);
            if (!change)
                return;
            const auto  slots = change->Materials;
            const auto& owned = e.GetComponent<PrefabOwnedComponent>();
            Entity      root  = scene.GetEntityByUUID(owned.Instance);
            for (const auto& [slot, material] : slots)
                scene.SetPrefabMaterialOverride(root, owned.NodeKey, slot, {});
        };

        auto& editable     = registry.Register<EditableMeshComponent>("Editable Mesh", ICON_FA_SHAPES, "Rendering");
        editable.IconRank  = 5;
        editable.CanAdd    = [](Entity e) { return !e.HasComponent<MeshComponent>() && !IsPrefabPart(e); };
        editable.Add       = [](Entity e) {
            EditableMeshComponent cube;
            cube.Shape = ShapeDesc {};
            RegenerateShape(cube);
            e.AddComponent<EditableMeshComponent>(std::move(cube));
        };
        editable.Reset     = {};
        editable.Draw      = DrawEditableMesh;
        editable.CreateMenu  = "3D Object";
        editable.CreateOrder = 0;
        for (int type = 0; type <= int(ShapeType::Prism); ++type)
            editable.CreateItems.push_back(
                {ShapeName(ShapeType(type)), nullptr, [type](Entity e) { CreateShape(e, ShapeType(type)); }});

        // Added with skinned prefabs (which know their skeleton); not added by hand.
        auto& animator    = registry.Register<AnimatorComponent>("Animator", ICON_FA_PERSON_RUNNING, "Animation");
        animator.IconRank = 4;
        animator.Add      = {};
        animator.Reset    = {};
        animator.Copy     = {};
        animator.Paste    = {};
        animator.Draw     = DrawAnimator;

        auto& drivers = registry.Register<MaterialParameterDriversComponent>(
            "Material Parameter Drivers", ICON_FA_SLIDERS, "Rendering");
        drivers.IconRank = 9;
        drivers.Draw     = DrawMaterialParameterDrivers;

        auto& sprite    = registry.Register<SpriteRendererComponent>("Sprite Renderer", ICON_FA_IMAGE, "Rendering");
        sprite.IconRank = 6;
        sprite.Add = [](Entity e) { e.AddComponent<SpriteRendererComponent>().Texture = Renderer::GetWhiteTexture(); };
        // Reset keeps the texture: it restores colour and tiling only.
        sprite.Reset = [](Entity e) {
            e.PatchComponent<SpriteRendererComponent>([](auto& c) {
                c.Color        = glm::vec4(1.0f);
                c.TilingFactor = 1.0f;
            });
        };
        sprite.Draw       = DrawSprite;
        sprite.CreateName = "Sprite";
        sprite.CreateOrder = 4;
        sprite.Create     = sprite.Add;

        auto& directional    = registry.Register<DirLightComponent>("Directional Light", ICON_FA_SUN, "Lighting");
        directional.IconRank = 2;
        directional.Add      = [](Entity e) { DefaultDirectionalLight(e.AddComponent<DirLightComponent>()); };
        directional.Reset    = [](Entity e) { e.PatchComponent<DirLightComponent>(DefaultDirectionalLight); };
        directional.Draw     = DrawDirectionalLight;
        directional.CreateName = "Directional Light";
        directional.CreateOrder = 2;
        directional.Create     = directional.Add;

        auto& point      = registry.Register<PointLightComponent>("Point Light", ICON_FA_LIGHTBULB, "Lighting");
        point.IconRank   = 3;
        point.Add        = [](Entity e) { DefaultPointLight(e.AddComponent<PointLightComponent>()); };
        point.Reset      = [](Entity e) { e.PatchComponent<PointLightComponent>(DefaultPointLight); };
        point.Draw       = DrawPointLight;
        point.CreateName = "Point Light";
        point.CreateOrder = 3;
        point.Create     = point.Add;

        auto& script    = registry.Register<ScriptComponent>("Script", ICON_FA_CODE, "Scripting");
        script.IconRank = 7;
        script.Reset    = {};
        script.Draw     = DrawScript;
    }
} // namespace Kans
