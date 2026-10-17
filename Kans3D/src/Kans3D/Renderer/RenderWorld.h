#pragma once
#include <map>
#include <unordered_set>
#include "RenderView.h"

namespace Kans
{
    // Persistent CPU data, owned by Scene. No device, camera or frame resources.
    class RenderWorld
    {
    public:
        struct DrawProxy
        {
            AssetID   Mesh, Material;
            uint32_t  Section = 0;
            glm::mat4 Transform {1};
            Bounds    LocalBounds, WorldBounds;
            UUID      EntityID = 0, InstanceRoot = 0;
        };
        struct Stats
        {
            size_t Created = 0, Updated = 0, Removed = 0;
            size_t EntitiesVisited = 0, PrefabsVisited = 0, PrefabNodesVisited = 0, BoundsUpdated = 0,
                   SpriteOrderVisits = 0;
        };
        explicit RenderWorld(Scene& scene) : m_Scene(scene) {}
        void                          Synchronize();
        void                          Extract(RenderView& output, const RenderResource& settings) const;
        void                          CollectLights(SceneInfo& output) const;
        std::vector<DrawProxy>        CollectDraws(UUID subtree = 0) const;
        const Stats&                  GetStats() const { return m_Stats; }
        const std::vector<DrawProxy>& GetEntityDraws(entt::entity entity) const;
        void                          SetValidationEnabled(bool enabled) { m_Validate = enabled; }
        // Debug-only full reference extraction. Throws with the stale UUID.
        void                  Validate() const;
        RenderResource        Settings;
        std::filesystem::path EnvironmentPath;

    private:
        struct Order
        {
            uint64_t Submission = 0;
            UUID     Root = 0, EntityID = 0;
            size_t   Part = SIZE_MAX;
            bool     operator<(const Order& other) const;
        };
        struct Record
        {
            Order                             Sort;
            std::optional<ViewMeshSubmission> Mesh;
            Ref<const MeshAsset>              MeshSource; // Retain the last ready version while reimport loads.
            glm::mat4                         OutlineTransform {1};
            std::vector<DrawProxy>            Draws;
            std::optional<DirLight>           Directional;
            std::optional<PointLight>         Point;
            std::optional<ViewSprite>         Sprite;
            size_t                            SpriteIndex = SIZE_MAX;
            std::vector<UUID>                 Ancestors;
        };
        Record                                   ExtractEntity(Entity entity) const;
        void                                     Dirty(entt::entity entity) { m_Dirty.insert(entity); }
        void                                     AssetChanged(AssetID asset);
        Scene&                                   m_Scene;
        std::unordered_set<entt::entity>         m_Dirty;
        std::unordered_map<entt::entity, Record> m_Records;
        std::map<Order, entt::entity>            m_Order;
        std::map<size_t, entt::entity>           m_SpriteOrder;
        std::unordered_set<entt::entity>         m_Lights;
        std::unordered_map<AssetID, std::unordered_set<entt::entity>> m_MeshUsers;
        Stats                                                         m_Stats, m_PendingStats;
        bool                                                          m_Validate = false;
        friend class Scene;
    };
} // namespace Kans
