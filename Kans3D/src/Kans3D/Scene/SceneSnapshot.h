#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Components.h"

namespace Kans
{
    // In-memory copy of a scene's editable state, for undo/redo. Entities keep
    // their UUIDs, so references and selections survive a restore. Runtime-only
    // state (native scripts) is not captured.
    struct EntitySnapshot
    {
        UUID                                   ID = 0;
        std::string                            Tag;
        TransformComponent                     Transform;
        RelationshipComponent                  Relationship;
        std::optional<CameraComponent>         Camera;
        std::optional<SpriteRendererComponent> Sprite;
        std::optional<MeshComponent>             Model; // Mesh state; name retained for SceneHistory component labels.
        std::optional<EditableMeshComponent>     Editable;
        std::optional<PrefabInstanceComponent>   Prefab;
        std::optional<PrefabAttachmentComponent> PrefabParent;
        std::optional<DirLightComponent>       DirLight;
        std::optional<PointLightComponent>     PointLight;
        std::optional<ScriptComponent>         Script;
    };

    struct SceneSnapshot
    {
        std::string                 Name;
        std::vector<UUID>           RootOrder;
        std::vector<EntitySnapshot> Entities; // Ordered by UUID.

        const EntitySnapshot* Find(UUID id) const;
    };

    bool operator==(const EntitySnapshot& a, const EntitySnapshot& b);
    bool operator==(const SceneSnapshot& a, const SceneSnapshot& b);
    inline bool operator!=(const SceneSnapshot& a, const SceneSnapshot& b) { return !(a == b); }
} // namespace Kans
