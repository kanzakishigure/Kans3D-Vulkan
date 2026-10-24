#pragma once
#include <glm/glm.hpp>
#include "ScriptInterop.h"

namespace Kans
{
    class Entity;
    class ScriptAgent
    {
    public:
        static void SetTranslation(Entity entity, const glm::vec3& value);
        static void SetRotation(Entity entity, const glm::vec3& value);
        static Scripting::NativeApi CreateApi();
    };
} // namespace Kans
