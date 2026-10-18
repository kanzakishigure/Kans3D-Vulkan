#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Asset/Runtime/MeshData.h"

namespace Kans
{
    // Built-in geometry for the renderer (environment capture and skybox).
    class MeshFactory
    {
    public:
        static MeshData         CreateCubeData(const glm::vec3& size);
    };
} // namespace Kans
