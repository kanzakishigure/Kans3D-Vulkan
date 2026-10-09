#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"

namespace Kans
{
    // Built-in geometry for the renderer (environment capture, skybox, grid).
    class MeshFactory
    {
    public:
        static MeshData         CreateCubeData(const glm::vec3& size);
        static Ref<VertexArray> CreateCube(const glm::vec3& size);
        // Shared 2x2x2 cube; requires a current graphics context on first use.
        static Ref<VertexArray> GetCube();
        // XZ-plane quad with position and texture coordinates.
        static Ref<VertexArray> CreatQuad(const glm::vec2 size);
    };
} // namespace Kans
