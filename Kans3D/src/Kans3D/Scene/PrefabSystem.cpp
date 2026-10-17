#include "kspch.h"
#include <functional>
#include <set>
#include <glm/gtc/type_ptr.hpp>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Core/Math/Math.h"
#include "Entity.h"

namespace Kans
{
    namespace
    {
        bool Same(const TransformComponent& a, const TransformComponent& b)
        {
            return a.GetTransform() == b.GetTransform();
        }
        bool SameOverrides(const std::map<std::string, PrefabNodeOverride>& a,
                           const std::map<std::string, PrefabNodeOverride>& b)
        {
            if (a.size() != b.size())
                return false;
            auto x = a.begin(), y = b.begin();
            for (; x != a.end(); ++x, ++y)
                if (x->first != y->first || x->second.Removed != y->second.Removed ||
                    x->second.Materials != y->second.Materials ||
                    x->second.LocalTransform.has_value() != y->second.LocalTransform.has_value() ||
                    (x->second.LocalTransform && !Same(*x->second.LocalTransform, *y->second.LocalTransform)))
                    return false;
            return true;
        }
        TransformComponent Transform(const std::array<float, 16>& values)
        {
            TransformComponent result;
            const auto         matrix = glm::transpose(glm::make_mat4(values.data()));
            Math::DecomposeTransform(matrix, result.Position, result.Rotation, result.Scale);
            result.ImportedMatrix   = matrix;
            result.ImportedPosition = result.Position;
            result.ImportedRotation = result.Rotation;
            result.ImportedScale    = result.Scale;
            return result;
        }
        AssetID Material(const std::vector<AssetID>& materials, size_t slot)
        {
            return slot < materials.size() ? materials[slot] : AssetID {};
        }
    } // namespace

    UUID Scene::PrefabEntityUUID(UUID instance, const std::string& key)
    {
        // FNV-1a, with explicit little-endian root bytes and a domain separator.
        uint64_t value = 14695981039346656037ull;
        auto     byte  = [&](uint8_t b) { value = (value ^ b) * 1099511628211ull; };
        for (auto c : std::string("Kans.PrefabEntity.v1"))
            byte(uint8_t(c));
        for (unsigned shift = 0; shift < 64; shift += 8)
            byte(uint8_t(uint64_t(instance) >> shift));
        for (unsigned char c : key)
            byte(c);
        return UUID(value ? value : 1);
    }

    Entity Scene::InstantiatePrefab(AssetID prefab, const glm::vec3& position, UUID parent)
    {
        Entity root                                      = CreateEntity("Prefab");
        root.GetComponent<TransformComponent>().Position = position;
        SetParent(root, GetEntityByUUID(parent), {}, false);
        root.AddComponent<PrefabInstanceComponent>().Prefab = prefab;
        return root;
    }

    Entity Scene::GetPrefabInstanceRoot(Entity entity) const
    {
        if (!entity)
            return {};
        if (entity.HasComponent<PrefabOwnedComponent>())
            return GetEntityByUUID(entity.GetComponent<PrefabOwnedComponent>().Instance);
        for (Entity current = entity; current; current = GetParent(current))
            if (current.HasComponent<PrefabInstanceComponent>())
                return current;
        return {};
    }

    void Scene::CapturePrefabOverrides()
    {
        // Public component fields are intentionally editable by inspectors and scripts.
        // Compare to the last applied value, so API edits to Overrides aren't overwritten.
        auto instances = m_Registry.view<PrefabInstanceComponent>();
        for (auto handle : instances)
        {
            Entity root {handle, this};
            auto&  instance = root.GetComponent<PrefabInstanceComponent>();
            if (!instance.Expanded)
                continue;
            for (auto& [key, state] : instance.Nodes)
            {
                Entity part = GetEntityByUUID(PrefabEntityUUID(root.GetUUID(), key));
                if (!part)
                    continue;
                auto& transform = part.GetComponent<TransformComponent>();
                auto& change    = instance.Overrides[key];
                if (!Same(transform, state.AppliedTransform))
                {
                    instance.ExpandedSource.reset();
                    if (Same(transform, state.BaseTransform))
                        change.LocalTransform.reset();
                    else
                        change.LocalTransform = transform;
                    state.AppliedTransform = transform;
                }
                if (part.HasComponent<MeshComponent>())
                {
                    const auto& mesh  = part.GetComponent<MeshComponent>();
                    const auto  count = std::max(mesh.Materials.size(), state.AppliedMesh.Materials.size());
                    for (size_t slot = 0; slot < count; ++slot)
                    {
                        const auto value = Material(mesh.Materials, slot);
                        if (value == Material(state.AppliedMesh.Materials, slot))
                            continue;
                        instance.ExpandedSource.reset();
                        if (!value.IsValid() || value == Material(state.BaseMesh.Materials, slot))
                            change.Materials.erase(uint32_t(slot));
                        else
                            change.Materials[uint32_t(slot)] = value;
                    }
                    state.AppliedMesh = mesh;
                }
                if (!change.LocalTransform && change.Materials.empty() && !change.Removed)
                    instance.Overrides.erase(key);
            }
        }
    }

    void Scene::ExpandPrefab(Entity root)
    {
        auto& instance = root.GetComponent<PrefabInstanceComponent>();
        if (!instance.Source)
            return;
        const auto& nodes = instance.Source->GetEntities();
        // Require all CPU meshes before migration: a missing material table must not
        // silently discard a pending old-part override.
        bool meshChanged = false;
        auto meshSources = instance.MeshSources;
        for (const auto& node : nodes)
            if (node.Mesh.IsValid())
            {
                if (m_PrefabAssets)
                {
                    auto status = m_PrefabAssets->Request<MeshAsset>(node.Mesh);
                    if (!status.IsReady())
                        return;
                    auto previous = instance.MeshSources.find(node.Mesh);
                    meshChanged |= previous == instance.MeshSources.end() || previous->second != status.Asset;
                    meshSources[node.Mesh] = status.Asset;
                }
                if (!meshSources.count(node.Mesh))
                    return;
            }
        instance.MeshSources = std::move(meshSources);
        for (auto it = instance.Overrides.begin(); it != instance.Overrides.end();)
            if (!it->second.LocalTransform && it->second.Materials.empty() && !it->second.Removed)
                it = instance.Overrides.erase(it);
            else
                ++it;
        if (instance.Expanded && instance.Source == instance.ExpandedSource && !meshChanged &&
            !instance.LegacyPending && SameOverrides(instance.Overrides, instance.AppliedOverrides))
            return;
        std::set<UUID>    desired;
        std::vector<bool> removed(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node   = nodes[i];
            auto        change = instance.Overrides.find(node.Key);
            removed[i]         = (change != instance.Overrides.end() && change->second.Removed) ||
                                 (node.Parent >= 0 && removed.at(node.Parent));
            if (!removed[i])
                desired.insert(PrefabEntityUUID(root.GetUUID(), node.Key));
        }
        // Check collisions before mutating anything. Never adopt an ordinary entity.
        for (auto id : desired)
            if (auto existing = GetEntityByUUID(id))
                if (!existing.HasComponent<PrefabOwnedComponent>() ||
                    existing.GetComponent<PrefabOwnedComponent>().Instance != root.GetUUID())
                {
                    CORE_ERROR("Prefab UUID collides with an existing entity: {0}", uint64_t(id));
                    return;
                }
        m_ReconcilingPrefab = true;
        std::vector<Entity> stale;
        for (auto handle : m_Registry.view<PrefabOwnedComponent>())
        {
            Entity entity {handle, this};
            if (entity.GetComponent<PrefabOwnedComponent>().Instance == root.GetUUID() &&
                !desired.count(entity.GetUUID()))
                stale.push_back(entity);
        }
        // Detach all stale nodes first so destruction cannot take retained nodes or
        // ordinary user additions with it. Attachments retain their original key.
        for (Entity entity : stale)
        {
            for (Entity child : GetChildren(entity))
            {
                if (!child.HasComponent<PrefabOwnedComponent>() && !child.HasComponent<PrefabAttachmentComponent>())
                {
                    const auto& owned = entity.GetComponent<PrefabOwnedComponent>();
                    child.AddComponent<PrefabAttachmentComponent>(owned.Instance, owned.NodeKey);
                }
                SetParent(child, root, {}, false);
            }
            SetParent(entity, root, {}, false);
        }
        for (Entity entity : stale)
            DestroyEntity(entity);
        std::map<std::string, PrefabNodeState> states;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node = nodes[i];
            if (removed[i])
                continue;
            const auto id     = PrefabEntityUUID(root.GetUUID(), node.Key);
            Entity     entity = GetEntityByUUID(id);
            if (!entity)
            {
                entity = CreateEntityWithID(id, node.Name, false);
                entity.AddComponent<PrefabOwnedComponent>(root.GetUUID(), node.Key, i);
            }
            entity.GetComponent<PrefabOwnedComponent>().Order = i;
            Entity parent =
                node.Parent < 0 ? root : GetEntityByUUID(PrefabEntityUUID(root.GetUUID(), nodes.at(node.Parent).Key));
            SetParent(entity, parent, {}, false);
            auto& state         = states[node.Key];
            state.BaseTransform = Transform(node.LocalTransform);
            MeshComponent mesh {node.Mesh, node.Materials, node.Section};
            if (node.Mesh.IsValid())
            {
                const auto& data = instance.MeshSources.at(node.Mesh)->GetData();
                mesh.Materials.resize(std::max(mesh.Materials.size(), data.MaterialSlots.size()));
                for (size_t slot = 0; slot < data.MaterialSlots.size(); ++slot)
                    if (!mesh.Materials[slot].IsValid())
                        mesh.Materials[slot] = data.MaterialSlots[slot].DefaultMaterial;
                // Old precedence: part -> mesh -> authored slot -> bound material.
                if (instance.LegacyPending)
                    for (size_t section = 0; section < data.Sections.size(); ++section)
                    {
                        if (node.Section && *node.Section != section)
                            continue;
                        const auto  slot     = data.Sections[section].MaterialSlot;
                        const auto& material = data.MaterialSlots.at(slot);
                        AssetID     oldPart;
                        for (const auto& identity : instance.Source->GetMeshData().SectionIdentities)
                            if (identity.Mesh == node.Mesh && identity.Section == section)
                                oldPart = identity.OldMesh;
                        const auto chosen = instance.LegacyPending->ResolveMaterial(
                            node.Mesh, material.DefaultMaterial, material.AuthoredMaterial, oldPart);
                        if (chosen != mesh.Materials[slot])
                            instance.Overrides[node.Key].Materials[slot] = chosen;
                        else
                            instance.Overrides[node.Key].Materials.erase(slot);
                    }
            }
            state.BaseMesh = mesh;
            auto transform = state.BaseTransform;
            auto change    = instance.Overrides.find(node.Key);
            if (change != instance.Overrides.end())
            {
                if (change->second.LocalTransform)
                    transform = *change->second.LocalTransform;
                for (const auto& [slot, material] : change->second.Materials)
                {
                    if (slot >= mesh.Materials.size())
                        mesh.Materials.resize(slot + 1);
                    mesh.Materials[slot] = material;
                }
            }
            entity.GetComponent<TransformComponent>() = transform;
            if (node.Mesh.IsValid())
            {
                if (!entity.HasComponent<MeshComponent>())
                    entity.AddComponent<MeshComponent>();
                entity.GetComponent<MeshComponent>() = mesh;
            }
            else if (entity.HasComponent<MeshComponent>())
                entity.RemoveComponent<MeshComponent>();
            state.AppliedTransform = transform;
            state.AppliedMesh      = mesh;
        }
        instance.Nodes = std::move(states);
        instance.LegacyPending.reset();
        for (auto it = instance.Overrides.begin(); it != instance.Overrides.end();)
            if (!it->second.LocalTransform && it->second.Materials.empty() && !it->second.Removed)
                it = instance.Overrides.erase(it);
            else
                ++it;
        instance.AppliedOverrides = instance.Overrides;
        instance.ExpandedSource   = instance.Source;
        instance.Expanded         = true;
        m_ReconcilingPrefab       = false;
    }

    void Scene::UpdatePrefabs()
    {
        CapturePrefabOverrides();
        for (auto handle : m_Registry.view<PrefabInstanceComponent>())
        {
            Entity root {handle, this};
            auto&  instance = root.GetComponent<PrefabInstanceComponent>();
            if (m_PrefabAssets && instance.Prefab.IsValid())
            {
                auto status = m_PrefabAssets->Request<PrefabAsset>(instance.Prefab);
                // Retain the last expanded state while replacement data loads.
                if (status.IsReady())
                    instance.Source = status.Asset;
            }
            ExpandPrefab(root);
        }
        // Resolve ordinary children after every instance has expanded.
        for (auto handle : m_Registry.view<PrefabAttachmentComponent>())
        {
            Entity      entity {handle, this};
            const auto& attachment = entity.GetComponent<PrefabAttachmentComponent>();
            if (Entity parent = GetEntityByUUID(PrefabEntityUUID(attachment.Instance, attachment.NodeKey)))
                SetParent(entity, parent, {}, false);
        }
        UpdateBounds();
    }

    std::vector<Entity> Scene::GetMeshEntities() const { return GetMeshEntities({}); }
    std::vector<Entity> Scene::GetMeshEntities(Entity subtree) const
    {
        std::vector<Entity> result;
        for (auto handle : m_Registry.view<MeshComponent>())
        {
            Entity entity {handle, const_cast<Scene*>(this)};
            if (!subtree || entity == subtree || IsDescendantOf(entity, subtree))
                result.push_back(entity);
        }
        std::sort(result.begin(), result.end(), [&](Entity a, Entity b) {
            const Entity   ar = GetPrefabInstanceRoot(a), br = GetPrefabInstanceRoot(b);
            const uint64_t ao = ar ? ar.GetComponent<PrefabInstanceComponent>().DrawOrder : uint64_t(uint32_t(a));
            const uint64_t bo = br ? br.GetComponent<PrefabInstanceComponent>().DrawOrder : uint64_t(uint32_t(b));
            if (ar != br || !ar)
                return ao != bo ? ao > bo : a.GetUUID() < b.GetUUID();
            const auto ap =
                a.HasComponent<PrefabOwnedComponent>() ? a.GetComponent<PrefabOwnedComponent>().Order : SIZE_MAX;
            const auto bp =
                b.HasComponent<PrefabOwnedComponent>() ? b.GetComponent<PrefabOwnedComponent>().Order : SIZE_MAX;
            return ap != bp ? ap < bp : a.GetUUID() < b.GetUUID();
        });
        return result;
    }

    void Scene::SetPrefabTransformOverride(Entity root, const std::string& key, const TransformComponent& transform)
    {
        CapturePrefabOverrides();
        auto&      instance = root.GetComponent<PrefabInstanceComponent>();
        const auto state    = instance.Nodes.find(key);
        if (state != instance.Nodes.end() && Same(state->second.BaseTransform, transform))
            instance.Overrides[key].LocalTransform.reset();
        else
            instance.Overrides[key].LocalTransform = transform;
        ExpandPrefab(root);
    }
    void Scene::SetPrefabMaterialOverride(Entity root, const std::string& key, uint32_t slot, AssetID material)
    {
        CapturePrefabOverrides();
        auto&      instance = root.GetComponent<PrefabInstanceComponent>();
        const auto state    = instance.Nodes.find(key);
        if (!material.IsValid() ||
            (state != instance.Nodes.end() && material == Material(state->second.BaseMesh.Materials, slot)))
            instance.Overrides[key].Materials.erase(slot);
        else
            instance.Overrides[key].Materials[slot] = material;
        ExpandPrefab(root);
    }
    void Scene::SetPrefabNodeRemoved(Entity root, const std::string& key, bool removed)
    {
        CapturePrefabOverrides();
        root.GetComponent<PrefabInstanceComponent>().Overrides[key].Removed = removed;
        ExpandPrefab(root);
    }
    void Scene::ClearPrefabOverride(Entity root, const std::string& key)
    {
        CapturePrefabOverrides();
        root.GetComponent<PrefabInstanceComponent>().Overrides.erase(key);
        ExpandPrefab(root);
    }

    void Scene::UpdateBounds()
    {
        for (const auto& [id, entity] : m_EntityMap)
        {
            Entity value = entity;
            if (!value.HasComponent<BoundsComponent>())
                value.AddComponent<BoundsComponent>();
            value.GetComponent<BoundsComponent>().World = {};
        }
        for (Entity entity : GetMeshEntities())
        {
            const auto&          component = entity.GetComponent<MeshComponent>();
            Ref<const MeshAsset> mesh;
            if (m_PrefabAssets)
                mesh = m_PrefabAssets->Request<MeshAsset>(component.Mesh).Asset;
            if (!mesh)
                if (Entity root = GetPrefabInstanceRoot(entity))
                {
                    const auto& sources = root.GetComponent<PrefabInstanceComponent>().MeshSources;
                    auto        found   = sources.find(component.Mesh);
                    if (found != sources.end())
                        mesh = found->second;
                }
            if (!mesh)
                continue;
            Bounds      bounds;
            const auto  world    = GetWorldTransform(entity);
            const auto& sections = mesh->GetData().Sections;
            for (size_t i = 0; i < sections.size(); ++i)
                if (!component.Section || *component.Section == i)
                    bounds.Expand(sections[i].LocalBounds.Transformed(world));
            for (Entity owner = entity; owner; owner = GetParent(owner))
                owner.GetComponent<BoundsComponent>().World.Expand(bounds);
        }
    }
    Bounds Scene::GetPrefabBounds(AssetID prefab, const glm::mat4& transform)
    {
        Bounds bounds;
        if (!m_PrefabAssets)
            return bounds;
        const auto source = m_PrefabAssets->Request<PrefabAsset>(prefab);
        if (!source.IsReady())
            return bounds;
        const auto&            nodes = source.Asset->GetEntities();
        std::vector<glm::mat4> world(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node  = nodes[i];
            const auto  local = glm::transpose(glm::make_mat4(node.LocalTransform.data()));
            world[i]          = node.Parent < 0 ? local : world.at(node.Parent) * local;
            if (!node.Mesh.IsValid())
                continue;
            auto mesh = m_PrefabAssets->Request<MeshAsset>(node.Mesh);
            if (!mesh.IsReady())
                return {};
            const auto& sections = mesh.Asset->GetData().Sections;
            for (size_t section = 0; section < sections.size(); ++section)
                if (!node.Section || *node.Section == section)
                    bounds.Expand(sections[section].LocalBounds.Transformed(transform * world[i]));
        }
        return bounds;
    }
    std::vector<MeshMaterialSlot> Scene::GetMeshSlots(Entity entity) const
    {
        if (!entity || !entity.HasComponent<MeshComponent>())
            return {};
        const auto id = entity.GetComponent<MeshComponent>().Mesh;
        if (m_PrefabAssets)
        {
            const auto mesh = m_PrefabAssets->Request<MeshAsset>(id);
            if (mesh.IsReady())
                return mesh.Asset->GetData().MaterialSlots;
        }
        if (auto root = GetPrefabInstanceRoot(entity))
        {
            const auto& sources = root.GetComponent<PrefabInstanceComponent>().MeshSources;
            auto        mesh    = sources.find(id);
            if (mesh != sources.end())
                return mesh->second->GetData().MaterialSlots;
        }
        return {};
    }
    Bounds Scene::GetWorldBounds(Entity entity) const
    {
        return entity && entity.HasComponent<BoundsComponent>() ? entity.GetComponent<BoundsComponent>().World :
                                                                  Bounds {};
    }
} // namespace Kans
