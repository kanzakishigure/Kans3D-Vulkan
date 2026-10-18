#pragma once
#include <array>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Kans3D/Asset/Runtime/MeshData.h"

namespace Kans
{
    struct FaceUV
    {
        glm::vec2 Scale {1}, Offset {0};
        float     Rotation = 0; // Degrees; scale, then rotate, then offset. One UV per metre.
    };
    struct Face
    {
        std::vector<uint32_t> Indices;
        uint32_t              Material = 0, SmoothingGroup = 0;
        FaceUV                UV;
    };
    using MeshEdge = std::array<uint32_t, 2>;
    MeshEdge SortedEdge(uint32_t a, uint32_t b);
    struct TopologyEdge
    {
        MeshEdge              Vertices;
        std::vector<uint32_t> Faces;
        bool                  IsBoundary() const { return Faces.size() == 1; }
    };
    struct EditableMesh
    {
        std::vector<glm::vec3>    Positions;
        std::vector<Face>         Faces;
        std::vector<TopologyEdge> Edges() const;
        std::vector<MeshEdge>     BoundaryEdges() const;
        // Empty meshes are valid. A nonempty string describes the first error.
        std::string Validate() const;
        // UINT32_MAX denotes an unused old vertex.
        std::vector<uint32_t> Compact();
    };
    bool       operator==(const FaceUV& a, const FaceUV& b);
    bool       operator==(const Face& a, const Face& b);
    bool       operator==(const EditableMesh& a, const EditableMesh& b);
    glm::dvec3 FaceAreaNormal(const EditableMesh& mesh, const Face& face);
    // Triangles contain shared position indices, in the original winding.
    std::vector<std::array<uint32_t, 3>> TriangulateFace(const EditableMesh& mesh, const Face& face);
    // Invalid input throws invalid_argument. Empty input returns empty MeshData (no draw).
    MeshData Build(const EditableMesh& mesh);
} // namespace Kans
