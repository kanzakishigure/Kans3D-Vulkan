#pragma once
#include "Kans3D/Core/Math/Math.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Scene/Entity.h"

namespace Kans::Test
{
    // Legacy fixture data is kept only as pending migration input, never as an ECS component.
    inline ProductModelComponent& SetLegacyPending(Entity entity, ProductModelComponent legacy = {})
    {
        if (!entity.HasComponent<PrefabInstanceComponent>())
            entity.AddComponent<PrefabInstanceComponent>();
        auto& prefab = entity.GetComponent<PrefabInstanceComponent>();
        if (prefab.Prefab != legacy.Model)
        {
            prefab.Source.reset();
            prefab.MeshSources.clear();
        }
        prefab.Prefab        = legacy.Model;
        prefab.LegacyPending = std::move(legacy);
        return *prefab.LegacyPending;
    }
    inline void ExactTransform(Entity entity, const glm::mat4& matrix)
    {
        auto& transform = entity.GetComponent<TransformComponent>();
        Math::DecomposeTransform(matrix, transform.Position, transform.Rotation, transform.Scale);
        transform.ImportedMatrix   = matrix;
        transform.ImportedPosition = transform.Position;
        transform.ImportedRotation = transform.Rotation;
        transform.ImportedScale    = transform.Scale;
    }
    // Independent phase-2 oracle: flatten the product and resolve the legacy
    // precedence here, then exercise the engine's mesh submission API. Deliberately
    // does not use Scene::UpdatePrefabs to construct the expected frame.
    template<class Callback>
    void LegacySections(RenderAssetCache& cache, const ProductModelComponent& model, Callback callback)
    {
        if (const auto* instances = cache.GetModel(model.Model))
            for (const auto& instance : instances->Instances)
                if (const auto* mesh = cache.GetMesh(instance.Mesh))
                {
                    const auto sections = mesh->Sections;
                    const auto slots    = mesh->MaterialSlots;
                    for (size_t section = instance.AllSections ? 0 : instance.Section,
                                end     = instance.AllSections ? sections.size() : instance.Section + 1;
                         section < end && section < sections.size();
                         ++section)
                    {
                        const auto    slot    = sections[section].MaterialSlot;
                        const auto&   binding = slots[slot];
                        MeshComponent component {instance.Mesh, {}, uint32_t(section)};
                        component.Materials.resize(slots.size());
                        component.Materials[slot] = model.ResolveMaterial(
                            instance.Mesh, binding.DefaultMaterial, binding.AuthoredMaterial, instance.Part);
                        callback(component, instance.Transform);
                    }
                }
    }
    inline void SubmitLegacyOracle(SceneRenderer&               renderer,
                                   const ProductModelComponent& model,
                                   const glm::mat4&             transform,
                                   ModelStencilMode             stencil = ModelStencilMode::Disabled)
    {
        if (auto* cache = renderer.GetAssetCache())
            LegacySections(*cache, model, [&](const MeshComponent& mesh, const glm::mat4& local) {
                renderer.SubmitMesh({mesh, transform * local, 0, 0, stencil});
            });
    }
    inline void SubmitLegacyGeometryOracle(SceneRenderer&               renderer,
                                           const ProductModelComponent& model,
                                           const glm::mat4&             transform,
                                           const std::string&           shader,
                                           ModelGeometryMode            mode = ModelGeometryMode::Default)
    {
        if (auto* cache = renderer.GetAssetCache())
            LegacySections(*cache, model, [&](const MeshComponent& mesh, const glm::mat4& local) {
                renderer.SubmitMeshGeometry({transform * local, shader, mode, mesh.Mesh, mesh.Section});
            });
    }
    struct LegacyOracleDraw
    {
        ProductModelComponent Model;
        glm::mat4             Transform {1};
    };
    inline Entity LegacyOracleEntities(SceneRenderer&               renderer,
                                       Scene&                       scene,
                                       const ProductModelComponent& model,
                                       const glm::mat4&             transform,
                                       UUID                         id = UUID())
    {
        auto root = scene.CreateEntityWithID(id, "Phase-2 oracle", false);
        if (auto* cache = renderer.GetAssetCache())
            LegacySections(*cache, model, [&](const MeshComponent& mesh, const glm::mat4& local) {
                auto part = scene.CreateEntity("Oracle section");
                scene.SetParent(part, root, {}, false);
                ExactTransform(part, transform * local);
                part.AddComponent<MeshComponent>(mesh);
            });
        return root;
    }
    inline std::vector<UUID>
    HighlightLegacyOracle(SceneRenderer& renderer, Scene& scene, const std::vector<LegacyOracleDraw>& draws)
    {
        std::vector<UUID> roots;
        for (const auto& draw : draws)
            roots.push_back(LegacyOracleEntities(renderer, scene, draw.Model, draw.Transform).GetUUID());
        renderer.SetEntityHighlight(roots);
        return roots;
    }
    inline void SubmitSceneMeshes(SceneRenderer& renderer, Scene& scene)
    {
        if (auto* cache = renderer.GetAssetCache())
            scene.UpdatePrefabs(cache->GetAssetManager());
        for (auto entity : scene.GetMeshEntities())
        {
            const auto root = scene.GetPrefabInstanceRoot(entity);
            renderer.SubmitMesh({entity.GetComponent<MeshComponent>(),
                                 scene.GetWorldTransform(entity),
                                 entity.GetUUID(),
                                 root ? root.GetUUID() : UUID(0)});
        }
    }
} // namespace Kans::Test
