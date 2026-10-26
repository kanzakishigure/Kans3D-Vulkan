#pragma once
#include <string>
#include <glm/glm.hpp>
namespace Kans
{
    class Scene;
    class Entity;
    // Stable paths; vector fields may select a trailing .x/.y/.z/.w (or .r/.g/.b/.a).
    // Material.<slot>.<parameter> uses per-instance overrides, never edits the asset.
    bool TimelineProperty(Scene&, Entity, const std::string& path, glm::vec4& value, bool write);
    bool TimelineHasComponent(Entity, const std::string& name);
} // namespace Kans
