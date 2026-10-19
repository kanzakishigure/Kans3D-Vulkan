#include "kspch.h"
#include <glm/gtx/quaternion.hpp>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Renderer/Resource/Skinning.h"
#include "Entity.h"
#include "Scene.h"
namespace Kans
{
    namespace
    {
        const AnimationControllerData& RestController()
        {
            static const auto data = [] {
                AnimationControllerData c;
                AnimationMotion         m;
                m.InlineClip.emplace();
                c.Motions.push_back(m);
                AnimationLayer l;
                l.Name = "Rest";
                l.States.push_back({"Rest", 0, 1, false});
                c.Layers.push_back(l);
                return c;
            }();
            return data;
        }
    } // namespace
    bool Scene::PrepareAnimator(Entity root)
    {
        if (!root || !root.HasComponent<AnimatorComponent>())
            return false;
        auto& a = root.GetComponent<AnimatorComponent>();
        if (a.Player)
            return true;
        AssetID skeleton = a.Skeleton;
        if (!skeleton.IsValid() && root.HasComponent<PrefabInstanceComponent>())
        {
            const auto& prefab = root.GetComponent<PrefabInstanceComponent>();
            if (prefab.Source)
                skeleton = prefab.Source->GetMeshData().Skeleton;
        }
        if (!a.SkeletonSource && m_PrefabAssets && skeleton.IsValid())
        {
            auto loaded = m_PrefabAssets->Request<SkeletonAsset>(skeleton);
            if (!loaded.IsReady())
                return false;
            a.SkeletonSource = loaded.Asset;
        }
        if (!a.SkeletonSource)
            return false;
        if (!a.InlineController && !a.ControllerSource && a.Controller.IsValid() && m_PrefabAssets)
        {
            auto loaded = m_PrefabAssets->Request<AnimationControllerAsset>(a.Controller);
            if (!loaded.IsReady())
                return false;
            a.ControllerSource = loaded.Asset;
        }
        if (!a.InlineController && a.Controller.IsValid() && !a.ControllerSource)
            return false;
        // A prepared player borrows immutable asset data. Retain a copy of inline authoring data so
        // ECS component relocation and editor edits cannot invalidate its pointers.
        if (a.InlineController)
            a.ControllerSource = CreateRef<AnimationControllerAsset>(*a.InlineController);
        const auto& controller = a.ControllerSource ? a.ControllerSource->GetData() : RestController();
        for (const auto& motion : controller.Motions)
            if (motion.Clip.IsValid() && !motion.InlineClip && !a.ClipSources.count(motion.Clip))
            {
                if (!m_PrefabAssets)
                    return false;
                auto loaded = m_PrefabAssets->Request<AnimationClipAsset>(motion.Clip);
                if (!loaded.IsReady())
                    return false;
                a.ClipSources.emplace(motion.Clip, loaded.Asset);
            }
        auto player = CreateRef<Animator>();
        auto valid  = player->Prepare(a.SkeletonSource->GetData(), controller, [&](AssetID id) {
            auto found = a.ClipSources.find(id);
            return found == a.ClipSources.end() ? nullptr : &found->second->GetData();
        });
        if (auto* error = std::get_if<AssetError>(&valid))
        {
            a.LastError = *error;
            return false;
        }
        for (const auto& p : a.Parameters)
        {
            const auto found = std::find_if(controller.Parameters.begin(),
                                            controller.Parameters.end(),
                                            [&](const auto& entry) { return entry.Name == p.first; });
            if (found == controller.Parameters.end())
                continue;
            switch (found->Type)
            {
                case AnimationParameterType::Float:
                    player->SetFloat(p.first, float(p.second));
                    break;
                case AnimationParameterType::Int:
                    if (!std::isfinite(p.second) || p.second < INT_MIN || p.second > INT_MAX ||
                        std::floor(p.second) != p.second)
                    {
                        a.LastError =
                            AssetError {AssetErrorCode::InvalidData, "Animator integer parameter is out of range"};
                        return false;
                    }
                    player->SetInt(p.first, int(p.second));
                    break;
                case AnimationParameterType::Bool:
                    player->SetBool(p.first, p.second != 0);
                    break;
                case AnimationParameterType::Trigger:
                    if (p.second != 0)
                        player->SetTrigger(p.first);
                    break;
            }
        }
        player->ExtractRootMotion = a.ApplyRootMotion;
        player->Speed             = a.Speed;
        player->RebuildPose();
        a.SkinPose = CreateRef<SkinningPose>();
        a.SkinPose->Palette.resize(a.SkeletonSource->GetData().Joints.size());
        a.Player = std::move(player);
        a.Events.reserve(64);
        a.JointEntities.resize(a.SkeletonSource->GetData().Joints.size());
        a.PreviewPose.Resize(a.JointEntities.size());
        const auto& joints = a.SkeletonSource->GetData().Joints;
        for (size_t j = 0; j < joints.size(); ++j)
            a.JointEntities[j] = PrefabEntityUUID(root.GetUUID(), joints[j].NodeKey);
        a.LastError.reset();
        ApplyAnimatorPose(root, a.Player->GetPose());
        return true;
    }
    void Scene::ResetAnimator(Entity root)
    {
        if (root && root.HasComponent<AnimatorComponent>())
        {
            auto& a = root.GetComponent<AnimatorComponent>();
            a.Player.reset();
            a.ControllerSource.reset();
            a.ClipSources.clear();
            if (a.Skeleton.IsValid() && a.SkeletonSource && a.SkeletonSource->assetID != a.Skeleton)
                a.SkeletonSource.reset();
            a.Events.clear();
            a.JointEntities.clear();
        }
    }
    void Scene::ApplyAnimatorPose(Entity root, const AnimationPose& pose)
    {
        auto&      a        = root.GetComponent<AnimatorComponent>();
        const bool old      = m_ReconcilingPrefab;
        m_ReconcilingPrefab = true;
        for (size_t j = 0; j < a.JointEntities.size() && j < pose.Local.size(); ++j)
        {
            auto bone = GetEntityByUUID(a.JointEntities[j]);
            if (!bone)
                continue;
            const auto&        local = pose.Local[j];
            TransformComponent t;
            t.Position                              = local.Translation;
            t.Rotation                              = glm::eulerAngles(local.Rotation);
            t.Scale                                 = local.Scale;
            t.ImportedMatrix                        = local.Matrix();
            t.ImportedPosition                      = t.Position;
            t.ImportedRotation                      = t.Rotation;
            t.ImportedScale                         = t.Scale;
            bone.GetComponent<TransformComponent>() = t;
            if (root.HasComponent<PrefabInstanceComponent>())
            {
                auto& nodes = root.GetComponent<PrefabInstanceComponent>().Nodes;
                auto  found = nodes.find(a.SkeletonSource->GetData().Joints[j].NodeKey);
                if (found != nodes.end())
                    found->second.AppliedTransform = t;
            }
        }
        m_ReconcilingPrefab = old;
        ++a.PoseVersion;
        a.SkinPose->Palette = pose.Palette;
        a.SkinPose->Version = a.PoseVersion;
        // Pose changes preserve render topology. Refresh transforms and conservative bounds in place,
        // avoiding dirty-set nodes and complete proxy reconstruction for every bone every frame.
        const auto rootWorld = GetWorldTransform(root);
        auto refresh = [&](auto&& self, Entity entity, const glm::mat4& relative, const glm::mat4& world) -> Bounds {
            Bounds     bounds;
            const auto record = m_RenderWorld->m_Records.find(entity);
            if (record != m_RenderWorld->m_Records.end())
            {
                auto&      r = record->second;
                const bool skin =
                    r.MeshSource && !r.MeshSource->GetData().Skin.empty() && r.Sort.Root == root.GetUUID();
                const auto geometryWorld = skin ? rootWorld : world;
                const auto animated =
                    skin ? SkinnedBounds(r.MeshSource->GetData().JointBounds, *a.SkinPose) : Bounds {};
                if (r.Mesh)
                {
                    r.Mesh->Transform = geometryWorld;
                    r.OutlineTransform =
                        skin || entity.HasComponent<PrefabOwnedComponent>() ?
                            rootWorld * glm::scale(glm::mat4(1), glm::vec3(1.007f)) * (skin ? glm::mat4(1) : relative) :
                            world * glm::scale(glm::mat4(1), glm::vec3(1.007f));
                }
                for (auto& draw : r.Draws)
                {
                    draw.Transform = geometryWorld;
                    if (skin)
                    {
                        draw.SkinPose    = a.SkinPose;
                        draw.LocalBounds = animated.IsValid() ?
                                               animated :
                                               r.MeshSource->GetData().Sections.at(draw.Section).LocalBounds;
                    }
                    draw.WorldBounds = draw.LocalBounds.Transformed(geometryWorld);
                    bounds.Expand(draw.WorldBounds);
                }
                if (r.Point)
                    r.Point->Position = glm::vec3(world[3]);
                if (r.Sprite)
                    r.Sprite->Transform = world;
                m_RenderWorld->m_PendingStats.Updated += r.Draws.size();
            }
            for (auto id : entity.GetComponent<RelationshipComponent>().Children)
                if (auto child = GetEntityByUUID(id))
                {
                    const auto local         = child.GetComponent<TransformComponent>().GetTransform();
                    const auto childRelative = relative * local;
                    // Owned bones use root * model grouping; ordinary attachments use parentWorld * local,
                    // exactly as GetWorldTransform does (also preserving byte-exact cached validation).
                    const bool owned       = child.HasComponent<PrefabOwnedComponent>() &&
                                             child.GetComponent<PrefabOwnedComponent>().Instance == root.GetUUID();
                    const auto childWorld  = owned ? rootWorld * childRelative : world * local;
                    const auto childBounds = self(self, child, childRelative, childWorld);
                    m_ChildBounds[entt::entity(entity)].Set(child, childBounds);
                    bounds.Expand(childBounds);
                }
            entity.GetComponent<BoundsComponent>().World = bounds;
            ++m_RenderWorld->m_PendingStats.BoundsUpdated;
            return bounds;
        };
        auto bounds = refresh(refresh, root, glm::mat4(1), rootWorld);
        for (auto child = root, parent = GetParent(child); parent; child = parent, parent = GetParent(parent))
        {
            m_ChildBounds[entt::entity(parent)].Set(child, bounds);
            bounds = m_ChildBounds[entt::entity(parent)].Aggregate();
            for (const auto& draw : m_RenderWorld->GetEntityDraws(parent))
                bounds.Expand(draw.WorldBounds);
            parent.GetComponent<BoundsComponent>().World = bounds;
        }
    }
    void Scene::UpdateAnimations(double seconds)
    {
        for (auto handle : m_Registry.view<AnimatorComponent>())
        {
            Entity root {handle, this};
            auto&  a = root.GetComponent<AnimatorComponent>();
            a.Events.clear();
            if (!a.Enabled || !PrepareAnimator(root))
                continue;
            a.Player->ExtractRootMotion = a.ApplyRootMotion;
            a.Player->Speed             = a.Speed;
            a.Player->Advance(seconds, [&](const auto& event) { a.Events.push_back(event); });
            if (a.ApplyRootMotion)
            {
                const auto& delta  = a.Player->GetRootMotion();
                const auto  matrix = root.GetComponent<TransformComponent>().GetTransform() *
                                     glm::translate(glm::mat4(1), delta.Translation) * glm::mat4_cast(delta.Rotation);
                const auto  local  = JointTransform::FromMatrix(matrix);
                auto&       transform      = root.GetComponent<TransformComponent>();
                transform.Position         = local.Translation;
                transform.Rotation         = glm::eulerAngles(local.Rotation);
                transform.Scale            = local.Scale;
                transform.ImportedMatrix   = matrix;
                transform.ImportedPosition = transform.Position;
                transform.ImportedRotation = transform.Rotation;
                transform.ImportedScale    = transform.Scale;
            }
            ApplyAnimatorPose(root, a.Player->GetPose());
        }
        UpdateBounds();
    }
    bool Scene::PreviewAnimation(Entity root, size_t layer, uint32_t state, double time)
    {
        if (!PrepareAnimator(root))
            return false;
        auto& a = root.GetComponent<AnimatorComponent>();
        if (!a.Player->EvaluateState(layer, state, time, a.PreviewPose))
            return false;
        ApplyAnimatorPose(root, a.PreviewPose);
        UpdateBounds();
        return true;
    }
} // namespace Kans
