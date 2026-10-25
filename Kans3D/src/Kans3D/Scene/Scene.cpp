#include "kspch.h"

#include <glm/glm.hpp>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Core/Math/Math.h"
#include "Kans3D/Renderer/RenderWorld.h"
#include "Components.h"
#include "Entity.h"
#include "Scene.h"

#include "Kans3D/Scene/ScriptableEntity.h"
#include "Kans3D/Script/ScriptEngine.h"

namespace Kans
{

    Scene::Scene(const std::string& name) : m_Name(name)
    {
        m_RenderWorld = std::make_unique<RenderWorld>(*this);
        auto connect  = [&](auto* type) {
            using T = std::remove_pointer_t<decltype(type)>;
            m_Registry.on_construct<T>().template connect<&Scene::OnRenderComponentChanged>(this);
            m_Registry.on_update<T>().template connect<&Scene::OnRenderComponentChanged>(this);
            m_Registry.on_destroy<T>().template connect<&Scene::OnRenderComponentChanged>(this);
        };
        connect(static_cast<MeshComponent*>(nullptr));
        m_Registry.on_construct<EditableMeshComponent>().connect<&Scene::OnEditableMeshChanged>(this);
        m_Registry.on_update<EditableMeshComponent>().connect<&Scene::OnEditableMeshChanged>(this);
        m_Registry.on_destroy<EditableMeshComponent>().connect<&Scene::OnEditableMeshRemoved>(this);
        connect(static_cast<SpriteRendererComponent*>(nullptr));
        connect(static_cast<DirLightComponent*>(nullptr));
        connect(static_cast<PointLightComponent*>(nullptr));
        connect(static_cast<PrefabOwnedComponent*>(nullptr));
        m_Registry.on_construct<TransformComponent>().connect<&Scene::OnTransformChanged>(this);
        m_Registry.on_update<TransformComponent>().connect<&Scene::OnTransformChanged>(this);
        m_Registry.on_destroy<TransformComponent>().connect<&Scene::OnTransformChanged>(this);
        m_Registry.on_update<RelationshipComponent>().connect<&Scene::OnTransformChanged>(this);
        m_Registry.on_construct<PrefabInstanceComponent>().connect<&Scene::OnPrefabChanged>(this);
        m_Registry.on_update<PrefabInstanceComponent>().connect<&Scene::OnPrefabChanged>(this);
        m_Registry.on_destroy<PrefabInstanceComponent>().connect<&Scene::OnPrefabChanged>(this);
    }

    Scene::~Scene()
    {
        m_SuppressNotifications = true;
        m_Registry.clear();
    }

    void Scene::SetEnvironmentSettings(EnvironmentSettings settings)
    {
        settings.Validate();
        m_EnvironmentSettings      = std::move(settings);
        m_HasEnvironmentSettings   = true;
        m_RenderWorld->Environment = m_EnvironmentSettings;
    }

    void Scene::ClearEnvironmentSettings()
    {
        m_EnvironmentSettings    = {};
        m_HasEnvironmentSettings = false;
        m_RenderWorld->Environment.reset();
    }

    void Scene::DirtyBounds(Entity entity)
    {
        for (auto current = entity; current; current = GetParent(current))
            m_DirtyBounds.insert(current);
    }
    void Scene::DirtySubtree(Entity entity)
    {
        m_RenderWorld->Dirty(entity);
        if (!entity.HasComponent<RelationshipComponent>())
            return;
        DirtyBounds(entity);
        for (auto child : GetChildren(entity))
            DirtySubtree(child);
    }
    void Scene::OnTransformChanged(entt::registry&, entt::entity handle)
    {
        if (m_SuppressNotifications)
            return;
        Entity entity {handle, this};
        DirtySubtree(entity);
        if (!m_ReconcilingPrefab && entity.HasComponent<PrefabOwnedComponent>())
            m_CapturePrefabNodes.insert(handle);
    }
    void Scene::OnRenderComponentChanged(entt::registry&, entt::entity handle)
    {
        if (m_SuppressNotifications)
            return;
        m_RenderWorld->Dirty(handle);
        Entity entity {handle, this};
        if (entity.HasComponent<RelationshipComponent>())
            DirtyBounds(entity);
        if (!m_ReconcilingPrefab && entity.HasComponent<PrefabOwnedComponent>())
            m_CapturePrefabNodes.insert(handle);
    }
    void Scene::OnEditableMeshChanged(entt::registry& registry, entt::entity handle)
    {
        const auto  source    = m_EditableSources.find(handle);
        const auto* component = registry.try_get<EditableMeshComponent>(handle);
        if (!component || source == m_EditableSources.end() || !(component->Mesh == *source->second))
        {
            m_EditableMeshes.erase(handle);
            m_EditableSources.erase(handle);
        }
        OnRenderComponentChanged(registry, handle);
    }
    void Scene::OnEditableMeshRemoved(entt::registry& registry, entt::entity handle)
    {
        m_EditableMeshes.erase(handle);
        m_EditableSources.erase(handle);
        OnRenderComponentChanged(registry, handle);
    }
    Ref<const RuntimeMesh> Scene::ResolveEditableMesh(Entity entity) const
    {
        if (!entity.HasComponent<EditableMeshComponent>())
            return {};
        auto&       built = m_EditableMeshes[entt::entity(entity)];
        const auto& mesh  = entity.GetComponent<EditableMeshComponent>().Mesh;
        if (!built)
        {
            built                                   = CreateRef<RuntimeMesh>(Build(mesh));
            m_EditableSources[entt::entity(entity)] = std::make_shared<EditableMesh>(mesh);
            ++m_EditableMeshBuildCount;
        }
        return built;
    }
    void Scene::OnPrefabChanged(entt::registry&, entt::entity handle)
    {
        if (m_SuppressNotifications)
            return;
        m_DirtyPrefabs.insert(handle);
        if (m_Registry.all_of<IDComponent, RelationshipComponent>(handle))
            DirtySubtree({handle, this});
    }
    void Scene::SetPrefabAssetManager(AssetManager* assets)
    {
        if (m_PrefabAssets == assets)
            return;
        m_PrefabAssets = assets;
        m_AssetChanges = assets ? assets->ObserveChanges() : nullptr;
        for (auto handle : m_Registry.view<PrefabInstanceComponent>())
            m_DirtyPrefabs.insert(handle);
        for (auto handle : m_Registry.view<MeshComponent>())
            OnRenderComponentChanged(m_Registry, handle);
    }
    void Scene::TrackPrefabAssets(Entity root)
    {
        auto& previous = m_PrefabDependencies[entt::entity(root)];
        for (auto id : previous)
        {
            auto found = m_PrefabAssetUsers.find(id);
            if (found != m_PrefabAssetUsers.end())
            {
                found->second.erase(root);
                if (found->second.empty())
                    m_PrefabAssetUsers.erase(found);
            }
        }
        previous.clear();
        const auto& instance = root.GetComponent<PrefabInstanceComponent>();
        if (instance.Prefab.IsValid())
            previous.push_back(instance.Prefab);
        if (instance.Source)
            for (const auto& node : instance.Source->GetEntities())
                if (node.Mesh.IsValid())
                    previous.push_back(node.Mesh);
        for (auto id : previous)
            m_PrefabAssetUsers[id].insert(root);
    }
    void Scene::PollAssetChanges()
    {
        if (!m_AssetChanges)
            return;
        for (auto id : m_AssetChanges->Drain())
        {
            m_RenderWorld->AssetChanged(id);
            const auto found = m_PrefabAssetUsers.find(id);
            if (found != m_PrefabAssetUsers.end())
                for (auto handle : found->second)
                    m_DirtyPrefabs.insert(handle);
        }
    }
    Ref<const MeshAsset> Scene::ResolveMesh(Entity entity) const
    {
        if (entity.HasComponent<EditableMeshComponent>())
            return ResolveEditableMesh(entity)->Source;
        if (!entity.HasComponent<MeshComponent>())
            return {};
        const auto id = entity.GetComponent<MeshComponent>().Mesh;
        if (m_PrefabAssets)
        {
            auto status = m_PrefabAssets->Request<MeshAsset>(id);
            if (status.IsReady())
                return status.Asset;
        }
        if (auto root = GetPrefabInstanceRoot(entity))
        {
            const auto& sources = root.GetComponent<PrefabInstanceComponent>().MeshSources;
            auto        found   = sources.find(id);
            if (found != sources.end())
                return found->second;
        }
        const auto previous = m_RenderWorld->m_Records.find(entt::entity(entity));
        if (previous != m_RenderWorld->m_Records.end() && previous->second.Mesh &&
            previous->second.Mesh->Mesh.Mesh == id)
            return previous->second.MeshSource;
        return {};
    }

    void Scene::OnUpdate(TimeStep ts)
    {

        UpdatePrefabs();
        UpdateAnimations(double(ts));

        // update ScripteableEntity
        {

            m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc) {
                if (!nsc.Instance)
                {
                    nsc.Instance           = nsc.InstantiateFunction();
                    nsc.Instance->m_Entity = Entity {entity, this};
                    nsc.Instance->OnCreate();
                }

                nsc.Instance->OnUpdate(ts);
                nsc.Instance->OnDestory();
                nsc.DestoryInstanceFunction(&nsc);
            });
        }
        // update Script
        if (!ScriptEngine::IsInitialized())
            return;
        auto view = m_Registry.view<ScriptComponent>();
        for (auto entity : view)
        {
            Entity e = {entity, this};
            ScriptEngine::OnUpdateEntity(e, ts);
        }
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {

        m_ViewportWidth  = width;
        m_ViewportHeight = height;

        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraCMP = view.get<CameraComponent>(entity);
            if (!cameraCMP.FixedAspectRatio)
            {
                cameraCMP.SceneCamera.SetViewportSize(width, height);
            }
        }
    }

    Entity Scene::CreateEntity(const std::string name)
    {
        UUID id = UUID();
        return CreateEntityWithID(id, name, false);
    }
    Kans::Entity Scene::CreateEntityWithID(UUID uuid, const std::string& name, bool runtimeMap)
    {
        Entity entity = {m_Registry.create(), this};
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<RelationshipComponent>();
        entity.AddComponent<BoundsComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag   = name.empty() ? "Entity" : name;

        CORE_ASSERT(m_EntityMap.find(uuid) == m_EntityMap.end(), nullptr);
        m_EntityMap[uuid] = entity;
        m_RootOrder.push_back(uuid);
        return entity;
    }
    void Scene::DestroyEntity(Entity entity)
    {
        if (!m_ReconcilingPrefab && entity.HasComponent<PrefabOwnedComponent>())
        {
            const auto owned = entity.GetComponent<PrefabOwnedComponent>();
            Entity     root  = GetEntityByUUID(owned.Instance);
            if (root && root.HasComponent<PrefabInstanceComponent>())
            {
                SetPrefabNodeRemoved(root, owned.NodeKey, true);
                UpdateBounds();
                return;
            }
        }
        const bool reconciling = m_ReconcilingPrefab;
        if (entity.HasComponent<PrefabInstanceComponent>())
            m_ReconcilingPrefab = true;
        // Children go with their parent.
        const auto children = entity.GetComponent<RelationshipComponent>().Children;
        for (UUID child : children)
            if (Entity value = GetEntityByUUID(child))
                DestroyEntity(value);
        DirtyBounds(entity);
        m_RenderWorld->Dirty(entity);
        m_DirtyPrefabs.erase(entity);
        m_CapturePrefabNodes.erase(entity);
        if (auto dependencies = m_PrefabDependencies.find(entt::entity(entity));
            dependencies != m_PrefabDependencies.end())
        {
            for (auto asset : dependencies->second)
                m_PrefabAssetUsers[asset].erase(entity);
            m_PrefabDependencies.erase(dependencies);
        }
        Detach(entity);
        auto& id = entity.GetComponent<IDComponent>().ID;
        m_EntityMap.erase(id);
        m_ChildBounds.erase(entt::entity(entity));
        m_Registry.destroy(entity);
        m_ReconcilingPrefab = reconciling;
    }

    void Scene::Detach(Entity entity)
    {
        auto&      relationship = entity.GetComponent<RelationshipComponent>();
        const UUID id           = entity.GetUUID();
        Entity       parent     = relationship.Parent ? GetEntityByUUID(relationship.Parent) : Entity {};
        auto&        siblings   = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootOrder;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
        if (parent)
            m_ChildBounds[entt::entity(parent)].Set(entity, {});
        relationship.Parent = 0;
    }

    std::vector<Entity> Scene::GetRootEntities() const
    {
        std::vector<Entity> result;
        for (UUID id : m_RootOrder)
            if (Entity entity = GetEntityByUUID(id))
                result.push_back(entity);
        return result;
    }

    std::vector<Entity> Scene::GetChildren(Entity entity) const
    {
        std::vector<Entity> result;
        for (UUID id : entity.GetComponent<RelationshipComponent>().Children)
            if (Entity child = GetEntityByUUID(id))
                result.push_back(child);
        return result;
    }

    Entity Scene::GetParent(Entity entity) const
    {
        const UUID parent = entity.GetComponent<RelationshipComponent>().Parent;
        return parent ? GetEntityByUUID(parent) : Entity {};
    }

    bool Scene::IsDescendantOf(Entity entity, Entity ancestor) const
    {
        for (Entity parent = GetParent(entity); parent; parent = GetParent(parent))
            if (parent == ancestor)
                return true;
        return false;
    }

    bool Scene::SetParent(Entity entity, Entity parent, Entity before, bool keepWorld)
    {
        if (!m_ReconcilingPrefab && entity.HasComponent<PrefabOwnedComponent>())
            return false;
        if (parent && (parent == entity || IsDescendantOf(parent, entity)))
            return false;
        const glm::mat4 world = GetWorldTransform(entity);
        DirtyBounds(entity);
        Detach(entity);
        const UUID id       = entity.GetUUID();
        auto&      siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootOrder;
        auto       at       = before ? std::find(siblings.begin(), siblings.end(), before.GetUUID()) : siblings.end();
        siblings.insert(at, id);
        entity.GetComponent<RelationshipComponent>().Parent = parent ? parent.GetUUID() : UUID(0);
        if (!m_ReconcilingPrefab && !entity.HasComponent<PrefabOwnedComponent>())
        {
            if (parent && parent.HasComponent<PrefabOwnedComponent>())
            {
                const auto& owned = parent.GetComponent<PrefabOwnedComponent>();
                if (!entity.HasComponent<PrefabAttachmentComponent>())
                    entity.AddComponent<PrefabAttachmentComponent>();
                entity.GetComponent<PrefabAttachmentComponent>() = {owned.Instance, owned.NodeKey};
            }
            else if (entity.HasComponent<PrefabAttachmentComponent>())
                entity.RemoveComponent<PrefabAttachmentComponent>();
        }
        if (keepWorld)
            SetWorldTransform(entity, world);
        DirtySubtree(entity);
        return true;
    }

    bool Scene::SetParent(Entity entity, Entity parent) { return SetParent(entity, parent, {}, true); }

    glm::mat4 Scene::GetWorldTransform(Entity entity) const
    {
        const glm::mat4 local  = entity.GetComponent<TransformComponent>().GetTransform();
        if (entity.HasComponent<PrefabOwnedComponent>())
        {
            const auto                       root     = GetPrefabInstanceRoot(entity);
            std::function<glm::mat4(Entity)> relative = [&](Entity part) {
                const auto parent = GetParent(part);
                const auto matrix = part.GetComponent<TransformComponent>().GetTransform();
                return parent && parent != root ? relative(parent) * matrix : matrix;
            };
            // Preserve phase-2 multiplication grouping: rootWorld * modelSpace.
            return root ? GetWorldTransform(root) * relative(entity) : local;
        }
        const Entity    parent = GetParent(entity);
        return parent ? GetWorldTransform(parent) * local : local;
    }

    void Scene::SetWorldTransform(Entity entity, const glm::mat4& world)
    {
        const Entity    parent = GetParent(entity);
        const glm::mat4 local  = parent ? glm::inverse(GetWorldTransform(parent)) * world : world;
        entity.PatchComponent<TransformComponent>([&](auto& transform) {
            Math::DecomposeTransform(local, transform.Position, transform.Rotation, transform.Scale);
        });
    }

    namespace
    {
        template<typename T>
        void CopyComponent(Entity source, Entity target)
        {
            if (source.HasComponent<T>())
            {
                if (target.HasComponent<T>())
                    target.SetComponent<T>(source.GetComponent<T>());
                else
                    target.AddComponent<T>(source.GetComponent<T>());
            }
        }
    } // namespace

    Entity Scene::CopyEntity(Entity source, Entity parent, Entity before)
    {
        Entity copy = CreateEntity(source.GetComponent<TagComponent>().Tag);
        CopyComponent<TransformComponent>(source, copy);
        CopyComponent<CameraComponent>(source, copy);
        CopyComponent<SpriteRendererComponent>(source, copy);
        CopyComponent<MeshComponent>(source, copy);
        CopyComponent<EditableMeshComponent>(source, copy);
        CopyComponent<AnimatorComponent>(source, copy);
        if (source.HasComponent<PrefabInstanceComponent>())
        {
            auto instance     = source.GetComponent<PrefabInstanceComponent>();
            instance.Expanded = false;
            instance.Nodes.clear();
            instance.DrawOrder = 0;
            copy.AddComponent<PrefabInstanceComponent>(std::move(instance));
        }
        CopyComponent<DirLightComponent>(source, copy);
        CopyComponent<PointLightComponent>(source, copy);
        CopyComponent<ScriptComponent>(source, copy);
        SetParent(copy, parent, before, false);
        if (copy.HasComponent<PrefabInstanceComponent>())
            ExpandPrefab(copy);
        for (Entity child : GetChildren(source))
            if (!child.HasComponent<PrefabOwnedComponent>())
                CopyEntity(child, copy, {});
        if (source.HasComponent<PrefabInstanceComponent>())
        {
            const auto& instance = source.GetComponent<PrefabInstanceComponent>();
            for (const auto& [key, state] : instance.Nodes)
                if (auto part = GetEntityByUUID(PrefabEntityUUID(source.GetUUID(), key)))
                    for (auto child : GetChildren(part))
                        if (!child.HasComponent<PrefabOwnedComponent>())
                            CopyEntity(child, GetEntityByUUID(PrefabEntityUUID(copy.GetUUID(), key)), {});
        }
        return copy;
    }

    Entity Scene::DuplicateEntity(Entity entity)
    {
        // Insert after the original: before its next sibling.
        const Entity parent   = GetParent(entity);
        const auto&  siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootOrder;
        auto         at       = std::find(siblings.begin(), siblings.end(), entity.GetUUID());
        const Entity next     = at != siblings.end() && at + 1 != siblings.end() ? GetEntityByUUID(*(at + 1)) : Entity {};
        return CopyEntity(entity, parent, next);
    }

    Entity Scene::GetEntityByUUID(UUID uuid) const
    {
        if (m_EntityMap.find(uuid) != m_EntityMap.end())
        {
            return m_EntityMap.at(uuid);
        }
        return {};
    }

    void Scene::OnRuntimeStart()
    {

        // Init
        if (!ScriptEngine::IsInitialized())
        {
            if (!m_Registry.view<ScriptComponent>().empty())
                CORE_ERROR("Scene scripts require an initialized ScriptEngine");
            return;
        }
        ScriptEngine::OnRuntimeStart(this);
        auto view = m_Registry.view<ScriptComponent>();
        for (auto entity : view)
        {
            Entity e = {entity, this};
            ScriptEngine::OnCreateEntity(e);
        }
    }

    void Scene::DestroyNativeScriptInstances()
    {
        m_Registry.view<NativeScriptComponent>().each([](auto& nsc) {
            if (nsc.Instance)
            {
                nsc.Instance->OnDestory();
                nsc.DestoryInstanceFunction(&nsc);
                nsc.Instance = nullptr;
            }
        });
    }

    void Scene::OnRuntimeStop()
    {
        DestroyNativeScriptInstances();
        ScriptEngine::OnRuntimeEnd();
    }

    Kans::Entity Scene::GetCameraEntity()
    {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto [cameraCMP] = view.get(entity);
            if (cameraCMP.Primary)
            {
                return {entity, this};
            }
        }
        return {};
    }

    // To use entt use the call back function we should implement the function advance
    template<typename T>
    void Scene::OnComponentAdd(Entity entity, T& component)
    {
        static_assert(sizeof(T) == 0);
    }

    template<>
    void Scene::OnComponentAdd<AnimatorComponent>(Entity, AnimatorComponent&)
    {}

    template<>
    void Scene::OnComponentAdd<CameraComponent>(Entity entity, CameraComponent& component)
    {
        if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
            component.SceneCamera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }

    template<>
    void Scene::OnComponentAdd<TransformComponent>(Entity entity, TransformComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<MeshComponent>(Entity entity, MeshComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<EditableMeshComponent>(Entity entity, EditableMeshComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<BoundsComponent>(Entity entity, BoundsComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<PrefabOwnedComponent>(Entity entity, PrefabOwnedComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<PrefabAttachmentComponent>(Entity entity, PrefabAttachmentComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<PrefabInstanceComponent>(Entity entity, PrefabInstanceComponent& component)
    {
        if (!component.DrawOrder)
            component.DrawOrder = m_NextDrawOrder++;
        else
            m_NextDrawOrder = std::max(m_NextDrawOrder, component.DrawOrder + 1);
    }
    template<>
    void Scene::OnComponentAdd<TagComponent>(Entity entity, TagComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<NativeScriptComponent>(Entity entity, NativeScriptComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<DirLightComponent>(Entity entity, DirLightComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<PointLightComponent>(Entity entity, PointLightComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<IDComponent>(Entity entity, IDComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<ScriptComponent>(Entity entity, ScriptComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<RelationshipComponent>(Entity entity, RelationshipComponent& component)
    {}
} // namespace Kans
