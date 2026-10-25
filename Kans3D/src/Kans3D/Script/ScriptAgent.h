#pragma once
#include <glm/glm.hpp>

namespace Kans
{
    class Entity;
    class ScriptAgent
    {
    public:
        static void SetTranslation(Entity entity, const glm::vec3& value);
        static void SetRotation(Entity entity, const glm::vec3& value);
        static void RegisterFunctions();
        static void RegisterComponents();

    private:
    };

} // namespace Kans