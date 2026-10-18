#pragma once
#include <optional>
#include "EditableMesh.h"

namespace Kans
{
    enum class ElementType
    {
        Vertex,
        Edge,
        Face
    };
    struct MeshSelection
    {
        ElementType           Type = ElementType::Vertex;
        std::vector<uint32_t> Vertices, Faces;
        std::vector<MeshEdge> Edges;
    };
    std::vector<uint32_t> SelectionVertices(const EditableMesh& mesh, const MeshSelection& selection);
    glm::vec3             SelectionCentroid(const EditableMesh& mesh, const MeshSelection& selection);
    glm::vec3             SelectionNormal(const EditableMesh& mesh, const MeshSelection& selection);
    // Vertex->face/edge requires all vertices; edge->face requires all boundary
    // edges. Face->edge returns only actual edges of the selected faces.
    MeshSelection ConvertSelection(const EditableMesh& mesh, const MeshSelection& selection, ElementType type);
    struct MeshOperationResult
    {
        std::string           Error;
        std::vector<uint32_t> Faces;
        std::vector<MeshEdge> Edges;
        std::vector<uint32_t> Remap;
        explicit              operator bool() const { return Error.empty(); }
    };
    enum class ExtrudeMode
    {
        Group,
        Individual
    };
    // All mutations are atomic on failure. Topology edits do not alter component Shape.
    MeshOperationResult
    TransformVertices(EditableMesh& mesh, const std::vector<uint32_t>& vertices, const glm::mat4& transform);
    MeshOperationResult ExtrudeFaces(EditableMesh&                mesh,
                                     const std::vector<uint32_t>& faces,
                                     float                        distance,
                                     ExtrudeMode                  mode = ExtrudeMode::Group);
    MeshOperationResult ExtrudeEdges(EditableMesh& mesh, const std::vector<MeshEdge>& edges, float distance);
    // Planar manifold regions only; rejects offsets that collapse or cross a boundary.
    MeshOperationResult InsetFaces(EditableMesh& mesh, const std::vector<uint32_t>& faces, float amount);
    MeshOperationResult DeleteFaces(EditableMesh& mesh, const std::vector<uint32_t>& faces);
    MeshOperationResult MergeVertices(EditableMesh& mesh, const std::vector<uint32_t>& vertices);
    // Transitive distance clusters collapse to their centroids. Unselected vertices stay separate.
    MeshOperationResult WeldVertices(EditableMesh& mesh, const std::vector<uint32_t>& vertices, float distance);
    MeshOperationResult FlipFaces(EditableMesh& mesh, const std::vector<uint32_t>& faces);
    MeshOperationResult SetFaceMaterial(EditableMesh& mesh, const std::vector<uint32_t>& faces, uint32_t material);
    MeshOperationResult SetSmoothingGroup(EditableMesh& mesh, const std::vector<uint32_t>& faces, uint32_t group);
    MeshOperationResult SetFaceUV(EditableMesh& mesh, const std::vector<uint32_t>& faces, const FaceUV& uv);

    struct MeshRay
    {
        glm::vec3 Origin {0}, Direction {0, 0, -1};
    };
    struct FaceRayHit
    {
        uint32_t  Face;
        float     Distance;
        glm::vec3 Point;
    };
    // Distance is in local metres, independent of ray direction length; double-sided.
    std::optional<FaceRayHit> RaycastFaces(const EditableMesh& mesh, const MeshRay& ray);
    struct VertexPickHit
    {
        uint32_t Vertex;
        float    DistancePixels;
    };
    struct EdgePickHit
    {
        MeshEdge Edge;
        float    DistancePixels;
    };
    struct MeshRect
    {
        glm::vec2 Min, Max;
    };
    // Pixel origin is upper-left, clip +Y is up, Vulkan Z is 0..1. Occlusion
    // uses this mesh only, with a local-distance tolerance of 1e-4 * max(1, depth).
    std::optional<VertexPickHit> PickVertex(const EditableMesh& mesh,
                                            const glm::mat4&    mvp,
                                            glm::vec2           viewportSize,
                                            glm::vec2           pixel,
                                            float               radiusPixels,
                                            bool                occlusion = true);
    std::optional<EdgePickHit>   PickEdge(const EditableMesh& mesh,
                                          const glm::mat4&    mvp,
                                          glm::vec2           viewportSize,
                                          glm::vec2           pixel,
                                          float               radiusPixels,
                                          bool                occlusion = true);
    // Vertices/face centres inside the rectangle; edges intersecting it.
    MeshSelection SelectInRect(const EditableMesh& mesh,
                               const glm::mat4&    mvp,
                               glm::vec2           viewportSize,
                               MeshRect            rect,
                               ElementType         type,
                               bool                occlusion = true);
} // namespace Kans
