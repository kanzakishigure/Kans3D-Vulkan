#include "kspch.h"
#include "RenderWorld.h"
#include <stdexcept>
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/Scene.h"

namespace Kans
{
    AssetManager* RenderWorld::GetAssetManager() const { return m_Scene ? m_Scene->m_PrefabAssets : nullptr; }
    void          RenderWorld::SetAssetManager(AssetManager* assets)
    {
        if (m_Scene)
            m_Scene->SetPrefabAssetManager(assets);
    }
    bool RenderWorld::Order::operator<(const Order& b) const
    {
        if (Root != b.Root || !Root)
            return Submission != b.Submission ? Submission > b.Submission : EntityID < b.EntityID;
        return Part != b.Part ? Part < b.Part : EntityID < b.EntityID;
    }
    RenderWorld::Record RenderWorld::ExtractEntity(Entity entity) const
    {
        Record     record;
        const auto id   = entity.GetUUID();
        const auto root = m_Scene->GetPrefabInstanceRoot(entity);
        record.Sort = {root ? root.GetComponent<PrefabInstanceComponent>().DrawOrder : uint64_t(uint32_t(entity)),
                       root ? root.GetUUID() : UUID(0),
                       id,
                       entity.HasComponent<PrefabOwnedComponent>() ? entity.GetComponent<PrefabOwnedComponent>().Order :
                                                                     SIZE_MAX};
        const auto world = m_Scene->GetWorldTransform(entity);
        if (entity.HasComponent<MeshComponent>())
        {
            const auto& component   = entity.GetComponent<MeshComponent>();
            record.Mesh             = ViewMeshSubmission {component, world, id, record.Sort.Root};
            record.OutlineTransform = world * glm::scale(glm::mat4(1), glm::vec3(1.007f));
            if (root && entity.HasComponent<PrefabOwnedComponent>())
            {
                std::function<glm::mat4(Entity)> relative = [&](Entity part) {
                    auto parent = m_Scene->GetParent(part);
                    auto local  = part.GetComponent<TransformComponent>().GetTransform();
                    return parent && parent != root ? relative(parent) * local : local;
                };
                record.OutlineTransform =
                    m_Scene->GetWorldTransform(root) * glm::scale(glm::mat4(1), glm::vec3(1.007f)) * relative(entity);
            }
            record.MeshSource = m_Scene->ResolveMesh(entity);
            if (record.MeshSource)
            {
                const auto& data = record.MeshSource->GetData();
                for (size_t i = 0; i < data.Sections.size(); ++i)
                {
                    if (component.Section && *component.Section != i)
                        continue;
                    const auto& section  = data.Sections[i];
                    const auto  slot     = section.MaterialSlot;
                    const auto  material = slot < component.Materials.size() && component.Materials[slot].IsValid() ?
                                               component.Materials[slot] :
                                               data.MaterialSlots.at(slot).DefaultMaterial;
                    record.Draws.push_back({component.Mesh,
                                            material,
                                            uint32_t(i),
                                            world,
                                            section.LocalBounds,
                                            section.LocalBounds.Transformed(world),
                                            id,
                                            record.Sort.Root});
                }
            }
        }
        if (entity.HasComponent<DirLightComponent>())
        {
            const auto& light  = entity.GetComponent<DirLightComponent>();
            record.Directional = {
                light.Direction, light.Diffuse_Intensity, light.Specular_Intensity, light.Ambient_Intensity};
        }
        if (entity.HasComponent<PointLightComponent>())
        {
            const auto& light = entity.GetComponent<PointLightComponent>();
            record.Point      = {
                glm::vec3(world[3]), light.Diffuse_Intensity, light.Specular_Intensity, light.Ambient_Intensity};
        }
        if (entity.HasComponent<SpriteRendererComponent>())
        {
            const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
            record.Sprite      = {world, sprite.Texture, sprite.Color};
        }
        for (auto parent = m_Scene->GetParent(entity); parent; parent = m_Scene->GetParent(parent))
            record.Ancestors.push_back(parent.GetUUID());
        return record;
    }
    const std::vector<RenderWorld::DrawProxy>& RenderWorld::GetEntityDraws(entt::entity entity) const
    {
        static const std::vector<DrawProxy> empty;
        const auto                          found = m_Records.find(entity);
        return found == m_Records.end() ? empty : found->second.Draws;
    }
    std::vector<RenderWorld::DrawProxy> RenderWorld::CollectDraws(UUID subtree) const
    {
        if (!m_Scene)
            return m_ProceduralDraws;
        std::vector<DrawProxy> result;
        for (const auto& [order, handle] : m_Order)
        {
            const auto& record = m_Records.at(handle);
            if (!subtree || order.EntityID == subtree ||
                std::find(record.Ancestors.begin(), record.Ancestors.end(), subtree) != record.Ancestors.end())
                result.insert(result.end(), record.Draws.begin(), record.Draws.end());
        }
        return result;
    }
    void RenderWorld::AssetChanged(AssetID asset)
    {
        const auto found = m_MeshUsers.find(asset);
        if (found != m_MeshUsers.end())
            for (auto handle : found->second)
                if (m_Scene->m_Registry.valid(handle))
                    m_Scene->OnRenderComponentChanged(m_Scene->m_Registry, handle);
    }
    void RenderWorld::Synchronize()
    {
        if (!m_Scene)
            return;
        m_Scene->UpdatePrefabs();
        m_Stats           = m_PendingStats;
        m_PendingStats    = {};
        using SpriteGroup = decltype(m_Scene->m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>));
        // Create the legacy owning group lazily at the first sprite extraction.
        const auto sprites = [&] {
            if (!m_Dirty.empty() &&
                (!m_SpriteOrder.empty() || !m_Scene->m_Registry.view<SpriteRendererComponent>().empty()))
                return m_Scene->m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            return SpriteGroup {};
        }();
        std::unordered_set<size_t> spritePositions;
        for (auto handle : m_Dirty)
        {
            ++m_Stats.EntitiesVisited;
            auto         old      = m_Records.find(handle);
            const size_t oldCount = old == m_Records.end() ? 0 : old->second.Draws.size();
            const bool   valid    = m_Scene->m_Registry.valid(handle) &&
                                    m_Scene->m_Registry.all_of<TransformComponent, IDComponent>(handle);
            Record next;
            if (valid)
                next = ExtractEntity({handle, m_Scene});
            const bool   renderable = next.Mesh || next.Point || next.Directional || next.Sprite;
            const size_t nextCount  = next.Draws.size();
            auto         countProxy = [&](bool before, bool after) {
                m_Stats.Created += !before && after;
                m_Stats.Updated += before && after;
                m_Stats.Removed += before && !after;
            };
            countProxy(old != m_Records.end() && old->second.Point.has_value(), next.Point.has_value());
            countProxy(old != m_Records.end() && old->second.Directional.has_value(), next.Directional.has_value());
            countProxy(old != m_Records.end() && old->second.Sprite.has_value(), next.Sprite.has_value());
            // Section identity survives material/transform/mesh-data changes.
            size_t before = 0, after = 0;
            while (before < oldCount || after < nextCount)
            {
                if (before < oldCount && after < nextCount &&
                    old->second.Draws[before].Section == next.Draws[after].Section)
                {
                    ++m_Stats.Updated;
                    ++before;
                    ++after;
                }
                else if (after == nextCount ||
                         (before < oldCount && old->second.Draws[before].Section < next.Draws[after].Section))
                {
                    ++m_Stats.Removed;
                    ++before;
                }
                else
                {
                    ++m_Stats.Created;
                    ++after;
                }
            }
            const auto oldSpriteIndex = old != m_Records.end() ? old->second.SpriteIndex : SIZE_MAX;
            if (next.Sprite)
                next.SpriteIndex = sprites.storage<TransformComponent>().index(handle);
            if (oldSpriteIndex != next.SpriteIndex)
            {
                if (oldSpriteIndex != SIZE_MAX)
                    spritePositions.insert(oldSpriteIndex);
                if (next.SpriteIndex != SIZE_MAX)
                    spritePositions.insert(next.SpriteIndex);
            }
            if (old != m_Records.end())
            {
                if (old->second.Mesh)
                {
                    m_Order.erase(old->second.Sort);
                    auto users = m_MeshUsers.find(old->second.Mesh->Mesh.Mesh);
                    if (users != m_MeshUsers.end())
                    {
                        users->second.erase(handle);
                        if (users->second.empty())
                            m_MeshUsers.erase(users);
                    }
                }
                m_Records.erase(old);
                m_Lights.erase(handle);
            }
            if (renderable)
            {
                if (next.Mesh)
                {
                    m_Order.emplace(next.Sort, handle);
                    m_MeshUsers[next.Mesh->Mesh.Mesh].insert(handle);
                }
                if (next.Point || next.Directional)
                    m_Lights.insert(handle);
                m_Records.emplace(handle, std::move(next));
            }
        }
        m_Dirty.clear();
        // A group insertion/removal swaps only the affected packed positions.
        // Preserve its reverse-packed execution order without scanning all sprites.
        std::vector<std::pair<size_t, entt::entity>> replacements;
        for (auto index : spritePositions)
        {
            if (index < sprites.size())
            {
                const auto handle = sprites.rbegin()[index];
                auto&      record = m_Records.at(handle);
                m_SpriteOrder.erase(record.SpriteIndex);
                record.SpriteIndex = index;
                replacements.emplace_back(index, handle);
                ++m_Stats.SpriteOrderVisits;
            }
            m_SpriteOrder.erase(index);
        }
        for (auto [index, handle] : replacements)
            m_SpriteOrder[index] = handle;
        if (m_Validate)
            Validate();
    }
    void RenderWorld::CollectLights(SceneInfo& info) const
    {
        if (!m_Scene)
        {
            info.dirLight            = m_ProceduralDirectional.value_or(DirLight {});
            info.HasDirectionalLight = m_ProceduralDirectional.has_value();
            info.pointLights         = m_ProceduralPointLights;
            info.pointLight          = info.pointLights.empty() ? PointLight {} : info.pointLights.front();
            return;
        }
        info.dirLight   = {};
        info.pointLight = {};
        info.pointLights.clear();
        info.HasDirectionalLight = false;
        uint64_t directionalID   = UINT64_MAX;
        struct Candidate
        {
            PointLight Light;
            float      Distance;
            uint64_t   ID;
        };
        std::vector<Candidate> candidates;
        for (auto handle : m_Lights)
        {
            const auto& record = m_Records.at(handle);
            const auto  id     = uint64_t(record.Sort.EntityID);
            if (record.Directional && (!info.HasDirectionalLight || id < directionalID))
            {
                info.dirLight            = *record.Directional;
                info.HasDirectionalLight = true;
                directionalID            = id;
            }
            if (record.Point)
            {
                const auto  delta    = record.Point->Position - info.sceneCamera.Position;
                const float distance = glm::dot(delta, delta);
                candidates.push_back(
                    {*record.Point, std::isfinite(distance) ? distance : std::numeric_limits<float>::max(), id});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.Distance != b.Distance ? a.Distance < b.Distance : a.ID < b.ID;
        });
        for (size_t i = 0; i < std::min(candidates.size(), size_t(EnginePointLightLimit)); ++i)
            info.pointLights.push_back(candidates[i].Light);
        if (!info.pointLights.empty())
            info.pointLight = info.pointLights.front();
    }
    void RenderWorld::Extract(RenderView& output, const RenderResource& settings) const
    {
        CollectLights(output.Scene);
        output.Draws.clear();
        output.Meshes.clear();
        output.Geometry.clear();
        output.Sprites.clear();
        output.Grids.clear();
        output.ImmediateOrder.clear();
        for (const auto& proxy : m_ProceduralDraws)
            output.Draws.push_back({proxy.Mesh,
                                    proxy.Transform,
                                    proxy.Material,
                                    output.DebugMode,
                                    ModelStencilMode::Disabled,
                                    proxy.LocalBounds,
                                    {},
                                    false,
                                    proxy.Section,
                                    proxy.EntityID,
                                    proxy.InstanceRoot,
                                    proxy.WorldBounds,
                                    true});
        const auto& state = settings.Piplinestate;
        for (const auto& [order, handle] : m_Order)
        {
            const auto& record     = m_Records.at(handle);
            auto        submission = *record.Mesh;
            submission.Stencil = state.EnableStencil ? ModelStencilMode::WriteReference : ModelStencilMode::Disabled;
            output.Meshes.push_back(submission);
            for (const auto& proxy : record.Draws)
                output.Draws.push_back({proxy.Mesh,
                                        proxy.Transform,
                                        proxy.Material,
                                        output.DebugMode,
                                        submission.Stencil,
                                        proxy.LocalBounds,
                                        {},
                                        false,
                                        proxy.Section,
                                        proxy.EntityID,
                                        proxy.InstanceRoot,
                                        proxy.WorldBounds,
                                        true});
            const auto& mesh = submission.Mesh;
            if (state.EnableStencil)
                output.Geometry.push_back({record.OutlineTransform,
                                           "StencilShader",
                                           ModelGeometryMode::StencilOutline,
                                           mesh.Mesh,
                                           mesh.Section,
                                           submission.EntityID});
            if (state.EnableOutline)
                output.Geometry.push_back({submission.Transform,
                                           "OutLineShader",
                                           ModelGeometryMode::BackFaces,
                                           mesh.Mesh,
                                           mesh.Section,
                                           submission.EntityID});
            if (state.EnableDebugNormal)
                output.Geometry.push_back({submission.Transform,
                                           "DebugNormalShader",
                                           ModelGeometryMode::Default,
                                           mesh.Mesh,
                                           mesh.Section,
                                           submission.EntityID});
        }
        // Phase 3 groups shell kinds across parts sharing the same submission order.
        std::unordered_map<UUID, uint64_t> submissions;
        for (const auto& [order, handle] : m_Order)
            submissions[order.EntityID] = order.Submission;
        std::stable_sort(output.Geometry.begin(), output.Geometry.end(), [&](const auto& a, const auto& b) {
            const auto ao = submissions.at(a.EntityID), bo = submissions.at(b.EntityID);
            if (ao != bo)
                return ao > bo;
            auto rank = [](const auto& d) {
                return d.Mode == ModelGeometryMode::StencilOutline ? 0 : d.Mode == ModelGeometryMode::BackFaces ? 1 : 2;
            };
            return rank(a) < rank(b);
        });
        for (auto sprite = m_SpriteOrder.rbegin(); sprite != m_SpriteOrder.rend(); ++sprite)
            if (auto found = m_Records.find(sprite->second); found != m_Records.end() && found->second.Sprite)
                output.Sprites.push_back(*found->second.Sprite);
        if (!output.Sprites.empty())
            output.ImmediateOrder.push_back({ViewImmediateKind::Sprites, 0});
        if (state.EnableGridShader)
        {
            output.Grids.push_back({.025f, 16.025f, glm::scale(glm::mat4(1), glm::vec3(10, 1, 10)), {}});
            output.ImmediateOrder.push_back({ViewImmediateKind::Grid, 0});
        }
    }
    void RenderWorld::Validate() const
    {
        if (!m_Scene)
            return;
#ifndef NDEBUG
        auto stale = [&](UUID id) {
            throw std::logic_error("RenderWorld stale entity " + std::to_string(uint64_t(id)));
        };
        size_t count = 0;
        for (auto handle : m_Scene->m_Registry.view<TransformComponent, IDComponent>())
        {
            const auto expected = ExtractEntity({handle, m_Scene});
            if (!expected.Mesh && !expected.Point && !expected.Directional && !expected.Sprite)
                continue;
            ++count;
            const auto found = m_Records.find(handle);
            if (found == m_Records.end())
                stale(expected.Sort.EntityID);
            const auto& actual = found->second;
            if (expected.Mesh.has_value() != actual.Mesh.has_value() ||
                expected.Sprite.has_value() != actual.Sprite.has_value() ||
                expected.Point.has_value() != actual.Point.has_value() ||
                expected.Directional.has_value() != actual.Directional.has_value() || expected.Sort < actual.Sort ||
                actual.Sort < expected.Sort || expected.Ancestors != actual.Ancestors ||
                expected.Draws.size() != actual.Draws.size())
                stale(expected.Sort.EntityID);
            if (expected.Mesh)
            {
                const auto& a = *actual.Mesh;
                const auto& e = *expected.Mesh;
                if (a.Transform != e.Transform || a.Mesh.Mesh != e.Mesh.Mesh || a.Mesh.Materials != e.Mesh.Materials ||
                    a.Mesh.Section != e.Mesh.Section || actual.OutlineTransform != expected.OutlineTransform)
                    stale(e.EntityID);
                for (size_t i = 0; i < expected.Draws.size(); ++i)
                {
                    const auto& x = expected.Draws[i];
                    const auto& y = actual.Draws[i];
                    if (x.Material != y.Material || x.Section != y.Section || x.LocalBounds.Min != y.LocalBounds.Min ||
                        x.LocalBounds.Max != y.LocalBounds.Max || x.WorldBounds.Min != y.WorldBounds.Min ||
                        x.WorldBounds.Max != y.WorldBounds.Max)
                        stale(e.EntityID);
                }
            }
            if (expected.Sprite &&
                (actual.Sprite->Transform != expected.Sprite->Transform ||
                 actual.Sprite->Texture != expected.Sprite->Texture || actual.Sprite->Color != expected.Sprite->Color))
                stale(expected.Sort.EntityID);
            if (expected.Point && (actual.Point->Position != expected.Point->Position ||
                                   actual.Point->Diffuse_Intensity != expected.Point->Diffuse_Intensity ||
                                   actual.Point->Specular_Intensity != expected.Point->Specular_Intensity ||
                                   actual.Point->Ambient_Intensity != expected.Point->Ambient_Intensity))
                stale(expected.Sort.EntityID);
            if (expected.Directional &&
                (actual.Directional->Dirction != expected.Directional->Dirction ||
                 actual.Directional->Diffuse_Intensity != expected.Directional->Diffuse_Intensity ||
                 actual.Directional->Specular_Intensity != expected.Directional->Specular_Intensity ||
                 actual.Directional->Ambient_Intensity != expected.Directional->Ambient_Intensity))
                stale(expected.Sort.EntityID);
        }
        const auto sprites =
            m_Scene->m_Registry.group_if_exists<TransformComponent>(entt::get<SpriteRendererComponent>);
        auto       actualSprite = m_SpriteOrder.rbegin();
        for (auto handle : sprites)
        {
            if (actualSprite == m_SpriteOrder.rend() || actualSprite->second != handle)
                stale(m_Scene->m_Registry.get<IDComponent>(handle).ID);
            ++actualSprite;
        }
        if (actualSprite != m_SpriteOrder.rend())
            stale(m_Records.at(actualSprite->second).Sort.EntityID);
        if (count != m_Records.size())
            for (const auto& [handle, record] : m_Records)
                if (!m_Scene->m_Registry.valid(handle) ||
                    !m_Scene->m_Registry.all_of<TransformComponent, IDComponent>(handle) ||
                    !m_Scene->m_Registry
                         .any_of<MeshComponent, PointLightComponent, DirLightComponent, SpriteRendererComponent>(
                             handle))
                    stale(record.Sort.EntityID);
#endif
    }
} // namespace Kans
