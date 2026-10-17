#pragma once
#include <entt.hpp>
#include <glm/glm.hpp>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Core/UUID.h"

namespace Kans
{
    class AssetManager;
    struct TransformComponent;
    struct PrefabNodeOverride;
    struct Bounds;
    struct MeshMaterialSlot;
    class RenderScene;
    class SceneRenderer;
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

        void OnViewportResize(uint32_t width, uint32_t height);

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

        // Nonblocking; also called by RenderScene and runtime updates.
        void SetPrefabAssetManager(AssetManager* assets) { m_PrefabAssets = assets; }
        void UpdatePrefabs();
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
        AssetManager*     m_PrefabAssets      = nullptr;
        uint64_t          m_NextDrawOrder     = 1;
        bool              m_ReconcilingPrefab = false;
        void              ExpandPrefab(Entity root);
        entt::registry m_Registry;
        uint32_t       m_ViewportWidth = 0, m_ViewportHeight = 0;
        std::string    m_Name;
        EntityMap      m_EntityMap;
        std::vector<UUID> m_RootOrder;

    private:
        friend class Entity;
        friend class RenderScene;
        friend class SceneSerializer;
    };

} // namespace Kans
