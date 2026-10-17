#include "kspch.h"
#include "SceneSnapshot.h"
#include <algorithm>
#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Script/ScriptEngine.h"
#include "Entity.h"
#include "Scene.h"

namespace Kans
{
    namespace
    {
        bool Same(const TransformComponent& a, const TransformComponent& b)
        {
            return a.Position == b.Position && a.Rotation == b.Rotation && a.Scale == b.Scale &&
                   a.GetTransform() == b.GetTransform();
        }
        bool Same(const RelationshipComponent& a, const RelationshipComponent& b)
        {
            return a.Parent == b.Parent && a.Children == b.Children;
        }
        bool Same(const CameraComponent& a, const CameraComponent& b)
        {
            auto& x = const_cast<SceneCamera&>(a.SceneCamera);
            auto& y = const_cast<SceneCamera&>(b.SceneCamera);
            return a.Primary == b.Primary && a.FixedAspectRatio == b.FixedAspectRatio &&
                   x.GetProjectionType() == y.GetProjectionType() &&
                   x.GetPerspectiveVerticalFOV() == y.GetPerspectiveVerticalFOV() &&
                   x.GetPerspectiveNearClip() == y.GetPerspectiveNearClip() &&
                   x.GetPerspectiveFarClip() == y.GetPerspectiveFarClip() &&
                   x.GetOrthographicSize() == y.GetOrthographicSize() &&
                   x.GetOrthographicNearClip() == y.GetOrthographicNearClip() &&
                   x.GetOrthographicFarClip() == y.GetOrthographicFarClip() && x.GetExposure() == y.GetExposure() &&
                   // A free aspect ratio follows the viewport; resizing it is not an edit.
                   (!a.FixedAspectRatio || x.GetAspectRatio() == y.GetAspectRatio());
        }
        bool Same(const SpriteRendererComponent& a, const SpriteRendererComponent& b)
        {
            return a.Color == b.Color && a.TilingFactor == b.TilingFactor && a.Texture == b.Texture;
        }
        bool Same(const ProductModelComponent& a, const ProductModelComponent& b)
        {
            return a.Model == b.Model && a.Overrides.size() == b.Overrides.size() &&
                   std::equal(a.Overrides.begin(), a.Overrides.end(), b.Overrides.begin(), [](const auto& x, const auto& y) {
                       return x.Authored == y.Authored && x.Replacement == y.Replacement;
                   });
        }
        bool Same(const MeshComponent& a, const MeshComponent& b)
        {
            return a.Mesh == b.Mesh && a.Materials == b.Materials && a.Section == b.Section;
        }
        bool Same(const PrefabAttachmentComponent& a, const PrefabAttachmentComponent& b)
        {
            return a.Instance == b.Instance && a.NodeKey == b.NodeKey;
        }
        bool Same(const PrefabInstanceComponent& a, const PrefabInstanceComponent& b)
        {
            if (a.Prefab != b.Prefab || a.DrawOrder != b.DrawOrder ||
                a.LegacyPending.has_value() != b.LegacyPending.has_value() ||
                (a.LegacyPending && !Same(*a.LegacyPending, *b.LegacyPending)) ||
                a.Overrides.size() != b.Overrides.size())
                return false;
            auto x = a.Overrides.begin(), y = b.Overrides.begin();
            for (; x != a.Overrides.end(); ++x, ++y)
                if (x->first != y->first || x->second.Removed != y->second.Removed ||
                    x->second.Materials != y->second.Materials ||
                    x->second.LocalTransform.has_value() != y->second.LocalTransform.has_value() ||
                    (x->second.LocalTransform && !Same(*x->second.LocalTransform, *y->second.LocalTransform)))
                    return false;
            return true;
        }
        bool Same(const DirLightComponent& a, const DirLightComponent& b)
        {
            return a.Direction == b.Direction && a.Diffuse_Intensity == b.Diffuse_Intensity &&
                   a.Specular_Intensity == b.Specular_Intensity && a.Ambient_Intensity == b.Ambient_Intensity;
        }
        bool Same(const PointLightComponent& a, const PointLightComponent& b)
        {
            return a.Diffuse_Intensity == b.Diffuse_Intensity && a.Specular_Intensity == b.Specular_Intensity &&
                   a.Ambient_Intensity == b.Ambient_Intensity;
        }
        bool Same(const ScriptComponent& a, const ScriptComponent& b) { return a.ClassName == b.ClassName; }

        template<typename T>
        bool Same(const std::optional<T>& a, const std::optional<T>& b)
        {
            return a.has_value() == b.has_value() && (!a || Same(*a, *b));
        }

        template<typename T>
        void Capture(Entity entity, std::optional<T>& slot)
        {
            if (entity.HasComponent<T>())
                slot = entity.GetComponent<T>();
        }

        template<typename T>
        void Restore(Entity entity, const std::optional<T>& slot)
        {
            if (slot)
                entity.AddComponent<T>(*slot);
        }
    } // namespace

    const EntitySnapshot* SceneSnapshot::Find(UUID id) const
    {
        const auto found = std::lower_bound(
            Entities.begin(), Entities.end(), id, [](const EntitySnapshot& e, UUID value) { return e.ID < value; });
        return found != Entities.end() && found->ID == id ? &*found : nullptr;
    }

    bool operator==(const EntitySnapshot& a, const EntitySnapshot& b)
    {
        return a.ID == b.ID && a.Tag == b.Tag && Same(a.Transform, b.Transform) &&
               Same(a.Relationship, b.Relationship) && Same(a.Camera, b.Camera) && Same(a.Sprite, b.Sprite) &&
               Same(a.Model, b.Model) && Same(a.Prefab, b.Prefab) && Same(a.PrefabParent, b.PrefabParent) &&
               Same(a.DirLight, b.DirLight) && Same(a.PointLight, b.PointLight) && Same(a.Script, b.Script);
    }

    bool operator==(const SceneSnapshot& a, const SceneSnapshot& b)
    {
        return a.Name == b.Name && a.RootOrder == b.RootOrder && a.Entities == b.Entities;
    }

    SceneSnapshot Scene::Capture() const
    {
        const_cast<Scene*>(this)->UpdatePrefabs();
        SceneSnapshot snapshot;
        snapshot.Name      = m_Name;
        snapshot.RootOrder = m_RootOrder;
        for (const auto& [id, value] : m_EntityMap)
        {
            Entity         entity = value;
            if (entity.HasComponent<PrefabOwnedComponent>())
                continue;
            EntitySnapshot item;
            item.ID           = id;
            item.Tag          = entity.GetComponent<TagComponent>().Tag;
            item.Transform    = entity.GetComponent<TransformComponent>();
            item.Relationship = entity.GetComponent<RelationshipComponent>();
            Kans::Capture(entity, item.Camera);
            Kans::Capture(entity, item.Sprite);
            Kans::Capture(entity, item.Model);
            Kans::Capture(entity, item.Prefab);
            Kans::Capture(entity, item.PrefabParent);
            if (item.Prefab)
            {
                item.Prefab->Expanded = false;
                item.Prefab->Nodes.clear();
            }
            const auto parent = GetParent(entity);
            if (parent && parent.HasComponent<PrefabOwnedComponent>())
            {
                const auto& owned = parent.GetComponent<PrefabOwnedComponent>();
                item.PrefabParent = PrefabAttachmentComponent {owned.Instance, owned.NodeKey};
            }
            auto& children = item.Relationship.Children;
            children.erase(std::remove_if(children.begin(),
                                          children.end(),
                                          [&](UUID child) {
                                              auto e = GetEntityByUUID(child);
                                              return e && e.HasComponent<PrefabOwnedComponent>();
                                          }),
                           children.end());
            Kans::Capture(entity, item.DirLight);
            Kans::Capture(entity, item.PointLight);
            Kans::Capture(entity, item.Script);
            snapshot.Entities.push_back(std::move(item));
        }
        std::sort(snapshot.Entities.begin(), snapshot.Entities.end(), [](const auto& a, const auto& b) {
            return a.ID < b.ID;
        });
        return snapshot;
    }

    void Scene::Restore(const SceneSnapshot& snapshot)
    {
        DestroyNativeScriptInstances();
        if (ScriptEngine::IsInitialized() && ScriptEngine::GetSceneContext() == this)
            ScriptEngine::OnRuntimeEnd();
        for (const auto& [handle, record] : m_RenderWorld->m_Records)
            m_RenderWorld->Dirty(handle);
        m_SuppressNotifications = true;
        m_Registry.clear();
        m_SuppressNotifications = false;
        m_DirtyPrefabs.clear();
        m_CapturePrefabNodes.clear();
        m_DirtyBounds.clear();
        m_EntityMap.clear();
        m_PrefabDependencies.clear();
        m_PrefabAssetUsers.clear();
        m_ChildBounds.clear();
        m_RootOrder.clear();
        m_Name = snapshot.Name;
        for (const auto& item : snapshot.Entities)
        {
            Entity entity                                    = CreateEntityWithID(item.ID, item.Tag, false);
            entity.GetComponent<TagComponent>().Tag          = item.Tag;
            entity.SetComponent<TransformComponent>(item.Transform);
            entity.SetComponent<RelationshipComponent>(item.Relationship);
            Kans::Restore(entity, item.Camera);
            Kans::Restore(entity, item.Sprite);
            Kans::Restore(entity, item.Model);
            Kans::Restore(entity, item.Prefab);
            Kans::Restore(entity, item.PrefabParent);
            Kans::Restore(entity, item.DirLight);
            Kans::Restore(entity, item.PointLight);
            Kans::Restore(entity, item.Script);
        }
        m_RootOrder = snapshot.RootOrder;
        m_ReconcilingPrefab = true;
        for (auto handle : m_Registry.view<PrefabAttachmentComponent>())
        {
            Entity entity {handle, this};
            if (!GetParent(entity))
            {
                const auto& attachment = entity.GetComponent<PrefabAttachmentComponent>();
                SetParent(entity, GetEntityByUUID(attachment.Instance), {}, false);
            }
        }
        m_ReconcilingPrefab = false;
        UpdatePrefabs();
    }
} // namespace Kans
