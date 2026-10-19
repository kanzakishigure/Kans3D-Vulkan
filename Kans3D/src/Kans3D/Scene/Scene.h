#pragma once
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <entt.hpp>
#include <glm/glm.hpp>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Core/Math/Bounds.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Core/UUID.h"
#include "EnvironmentSettings.h"

namespace Kans
{
    class AssetManager;
    class AssetChangeObserver;
    class RenderWorld;
    class MeshAsset;
    struct RuntimeMesh;
    struct EditableMesh;
    struct TransformComponent;
    struct PrefabNodeOverride;
    struct Bounds;
    struct MeshMaterialSlot;
    class Entity;
    struct SceneSnapshot;

    using EntityMap = std::unordered_map<UUID, Entity>;

    // the scene to the GamePlayer system
    class Scene : public Asset
    {

    public:
        Scene(const std::string& name = "UntitledScene");
        ~Scene();

        void OnUpdate(TimeStep ts);

        void       OnViewportResize(uint32_t width, uint32_t height);
        glm::uvec2 GetViewportSize() const { return {m_ViewportWidth, m_ViewportHeight}; }

        Entity CreateEntity(const std::string name = std::string());
        Entity CreateEntityWithID(UUID uuid, const std::string& name, bool runtimeMap);
        void   DestroyEntity(Entity entity);
        Entity GetEntityByUUID(UUID uuid) const;

        // ---- hierarchy ----
        // Top-level entities in order.
        std::vector<Entity> GetRootEntities() const;
        std::vector<Entity> GetChildren(Entity entity) const;
        Entity              GetParent(Entity entity) const;
        // True when `ancestor` is a (transitive) parent of `entity`.
        bool IsDescendantOf(Entity entity, Entity ancestor) const;
        // Moves `entity` under `parent` (a null Entity: top level), before the
        // sibling `before` (null or not a sibling: last). With `keepWorld` the
        // local transform changes so the entity stays where it is. Returns false
        // when `parent` is the entity itself or one of its descendants.
        bool SetParent(Entity entity, Entity parent, Entity before, bool keepWorld);
        // Appends `entity` to `parent`'s children, keeping its world transform.
        bool SetParent(Entity entity, Entity parent);
        glm::mat4 GetWorldTransform(Entity entity) const;
        // Sets the local transform so the entity ends up at `world`.
        void SetWorldTransform(Entity entity, const glm::mat4& world);
        // Copies the entity with its children (new IDs), placed right after it.
        Entity DuplicateEntity(Entity entity);

        // Nonblocking; also called by rendering and runtime updates.
        void         SetPrefabAssetManager(AssetManager* assets);
        RenderWorld& GetRenderWorld() const { return *m_RenderWorld; }
        // Resolve CPU metadata, retaining prefab data while replacement assets load.
        Ref<const MeshAsset> ResolveMesh(Entity entity) const;
        Ref<const RuntimeMesh> ResolveEditableMesh(Entity entity) const;
        uint64_t               GetEditableMeshBuildCount() const { return m_EditableMeshBuildCount; }
        void UpdatePrefabs();
        // Nonblocking asset preparation. Pose drives prefab skeleton entities.
        bool PrepareAnimator(Entity root);
        void UpdateAnimations(double seconds);
        bool PreviewAnimation(Entity root, size_t layer, uint32_t state, double time);
        void ApplyAnimatorPose(Entity root, const struct AnimationPose& pose);
        void ResetAnimator(Entity root);
        void UpdatePrefabs(AssetManager& assets)
        {
            SetPrefabAssetManager(&assets);
            UpdatePrefabs();
        }
        Entity              InstantiatePrefab(AssetID prefab, const glm::vec3& position = {}, UUID parent = 0);
        Entity              GetPrefabInstanceRoot(Entity entity) const;
        static UUID         PrefabEntityUUID(UUID instance, const std::string& nodeKey);
        std::vector<Entity> GetMeshEntities(Entity subtree) const;
        std::vector<Entity> GetMeshEntities() const;
        void                CapturePrefabOverrides();
        void   SetPrefabTransformOverride(Entity root, const std::string& key, const TransformComponent& transform);
        void   SetPrefabMaterialOverride(Entity root, const std::string& key, uint32_t slot, AssetID material);
        void   SetPrefabNodeRemoved(Entity root, const std::string& key, bool removed);
        void   ClearPrefabOverride(Entity root, const std::string& key);
        Bounds GetWorldBounds(Entity entity) const;
        // CPU-only pre-placement bounds; empty until the prefab/meshes are ready.
        Bounds                        GetPrefabBounds(AssetID prefab, const glm::mat4& transform = glm::mat4(1));
        std::vector<MeshMaterialSlot> GetMeshSlots(Entity entity) const;
        void                          UpdateBounds();

        const EnvironmentSettings& GetEnvironmentSettings() const { return m_EnvironmentSettings; }
        bool                       HasEnvironmentSettings() const { return m_HasEnvironmentSettings; }
        // Validates and immediately notifies the render world, including map removal.
        void SetEnvironmentSettings(EnvironmentSettings settings);
        template<typename Fn>
        void PatchEnvironmentSettings(Fn&& patch)
        {
            auto settings = m_EnvironmentSettings;
            patch(settings);
            if (settings != m_EnvironmentSettings)
                SetEnvironmentSettings(std::move(settings));
        }
        // Restore legacy inheritance from the view/editor, including default scales.
        void ClearEnvironmentSettings();

        // ---- undo/redo (SceneSnapshot.h) ----
        SceneSnapshot Capture() const;
        // Replaces every entity with the snapshot's; UUIDs are preserved.
        void Restore(const SceneSnapshot& snapshot);

        void OnRuntimeStart();
        void OnRuntimeStop();

        Entity GetCameraEntity();

        const std::string GetName() const { return m_Name; }
        void              SetName(const std::string& name) { m_Name = name; }

    private:
        template<typename T>
        void OnComponentAdd(Entity entity, T& component);
        // Removes the entity from its parent's children or the root order.
        void Detach(Entity entity);
        Entity CopyEntity(Entity source, Entity parent, Entity before);
        void DestroyNativeScriptInstances();

    private:
        void                                 OnRenderComponentChanged(entt::registry&, entt::entity entity);
        void                                 OnEditableMeshChanged(entt::registry&, entt::entity entity);
        mutable std::unordered_map<entt::entity, Ref<const RuntimeMesh>>              m_EditableMeshes;
        mutable std::unordered_map<entt::entity, std::shared_ptr<const EditableMesh>> m_EditableSources;
        mutable uint64_t                                                              m_EditableMeshBuildCount = 0;
        void                                 OnEditableMeshRemoved(entt::registry&, entt::entity entity);
        void                                 OnTransformChanged(entt::registry&, entt::entity entity);
        void                                 OnPrefabChanged(entt::registry&, entt::entity entity);
        void                                 DirtySubtree(Entity entity);
        void                                 DirtyBounds(Entity entity);
        void                                 TrackPrefabAssets(Entity root);
        void                                 PollAssetChanges();
        bool                                 m_SuppressNotifications = false;
        std::unique_ptr<RenderWorld>         m_RenderWorld;
        std::shared_ptr<AssetChangeObserver> m_AssetChanges;
        std::unordered_set<entt::entity>     m_DirtyPrefabs, m_CapturePrefabNodes, m_DirtyBounds;
        std::unordered_map<AssetID, std::unordered_set<entt::entity>> m_PrefabAssetUsers;
        std::unordered_map<entt::entity, std::vector<AssetID>>        m_PrefabDependencies;
        struct ChildBounds
        {
            std::unordered_map<entt::entity, Bounds> Values;
            std::array<std::multiset<float>, 3>      Min, Max;
            void                                     Set(entt::entity child, const Bounds& bounds);
            Bounds                                   Aggregate() const;
        };
        std::unordered_map<entt::entity, ChildBounds> m_ChildBounds;
        AssetManager*     m_PrefabAssets      = nullptr;
        uint64_t          m_NextDrawOrder     = 1;
        bool              m_ReconcilingPrefab = false;
        void              ExpandPrefab(Entity root);
        entt::registry m_Registry;
        uint32_t       m_ViewportWidth = 0, m_ViewportHeight = 0;
        std::string    m_Name;
        EnvironmentSettings                           m_EnvironmentSettings;
        bool                                          m_HasEnvironmentSettings = false;
        EntityMap      m_EntityMap;
        std::vector<UUID> m_RootOrder;

    private:
        friend class Entity;
        friend class RenderWorld;
        friend class SceneSerializer;
    };

} // namespace Kans
