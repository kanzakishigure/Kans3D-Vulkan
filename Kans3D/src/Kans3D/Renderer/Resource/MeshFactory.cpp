#include "kspch.h"
#include "MeshFactory.h"
#include "RenderAssetCache.h"

namespace Kans
{
    MeshData MeshFactory::CreateCubeData(const glm::vec3& size)
    {
        const glm::vec3 h = size * 0.5f;
        MeshData        mesh;
        mesh.Vertices.resize(8);
        const glm::vec3 corners[8] = {
            {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}};
        for (size_t i = 0; i < 8; ++i)
        {
            const glm::vec3 position  = corners[i] * h;
            const glm::vec3 normal    = glm::normalize(corners[i]);
            mesh.Vertices[i].Position = {position.x, position.y, position.z};
            mesh.Vertices[i].Normal   = {normal.x, normal.y, normal.z};
        }
        mesh.Indices = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                        4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
        return mesh;
    }

} // namespace Kans
