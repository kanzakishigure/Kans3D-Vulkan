#include "kspch.h"

#include <glm/glm.hpp>
#include "Kans3D/Core/Math/Math.h"
#include "Components.h"
#include "Entity.h"
#include "Scene.h"

#include "Kans3D/Scene/ScriptableEntity.h"
#include "Kans3D/Script/ScriptEngine.h"

namespace Kans
{

    Scene::Scene(const std::string& name) : m_Name(name) {}

    Scene::~Scene() {}

    void Scene::OnUpdate(TimeStep ts)
    {

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
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag   = name.empty() ? "Entity" : name;

        CORE_ASSERT(m_EntityMap.find(uuid) == m_EntityMap.end(), nullptr);
        m_EntityMap[uuid] = entity;
        m_RootOrder.push_back(uuid);
        return entity;
    }
    void Scene::DestroyEntity(Entity entity)
    {
        // Children go with their parent.
        const auto children = entity.GetComponent<RelationshipComponent>().Children;
        for (UUID child : children)
            if (Entity value = GetEntityByUUID(child))
                DestroyEntity(value);
        Detach(entity);
        auto& id = entity.GetComponent<IDComponent>().ID;
        m_EntityMap.erase(id);
        m_Registry.destroy(entity);
    }

    void Scene::Detach(Entity entity)
    {
        auto&      relationship = entity.GetComponent<RelationshipComponent>();
        const UUID id           = entity.GetUUID();
        Entity       parent     = relationship.Parent ? GetEntityByUUID(relationship.Parent) : Entity {};
        auto&        siblings   = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootOrder;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
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
        if (parent && (parent == entity || IsDescendantOf(parent, entity)))
            return false;
        const glm::mat4 world = GetWorldTransform(entity);
        Detach(entity);
        const UUID id       = entity.GetUUID();
        auto&      siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootOrder;
        auto       at       = before ? std::find(siblings.begin(), siblings.end(), before.GetUUID()) : siblings.end();
        siblings.insert(at, id);
        entity.GetComponent<RelationshipComponent>().Parent = parent ? parent.GetUUID() : UUID(0);
        if (keepWorld)
            SetWorldTransform(entity, world);
        return true;
    }

    bool Scene::SetParent(Entity entity, Entity parent) { return SetParent(entity, parent, {}, true); }

    glm::mat4 Scene::GetWorldTransform(Entity entity) const
    {
        const glm::mat4 local  = entity.GetComponent<TransformComponent>().GetTransform();
        const Entity    parent = GetParent(entity);
        return parent ? GetWorldTransform(parent) * local : local;
    }

    void Scene::SetWorldTransform(Entity entity, const glm::mat4& world)
    {
        const Entity    parent = GetParent(entity);
        const glm::mat4 local  = parent ? glm::inverse(GetWorldTransform(parent)) * world : world;
        auto&           transform = entity.GetComponent<TransformComponent>();
        Math::DecomposeTransform(local, transform.Position, transform.Rotation, transform.Scale);
    }

    namespace
    {
        template<typename T>
        void CopyComponent(Entity source, Entity target)
        {
            if (source.HasComponent<T>())
            {
                if (target.HasComponent<T>())
                    target.GetComponent<T>() = source.GetComponent<T>();
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
        CopyComponent<ProductModelComponent>(source, copy);
        CopyComponent<DirLightComponent>(source, copy);
        CopyComponent<PointLightComponent>(source, copy);
        CopyComponent<ScriptComponent>(source, copy);
        SetParent(copy, parent, before, false);
        for (Entity child : GetChildren(source))
            CopyEntity(child, copy, {});
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
    void Scene::OnComponentAdd<CameraComponent>(Entity entity, CameraComponent& component)
    {
        if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
            component.SceneCamera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }

    template<>
    void Scene::OnComponentAdd<TransformComponent>(Entity entity, TransformComponent& component)
    {}
    template<>
    void Scene::OnComponentAdd<ProductModelComponent>(Entity entity, ProductModelComponent& component)
    {}
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
