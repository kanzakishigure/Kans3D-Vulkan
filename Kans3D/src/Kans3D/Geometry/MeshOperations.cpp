#include "kspch.h"
#include "MeshOperations.h"
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <unordered_map>

namespace Kans
{
    namespace
    {
        template<typename T>
        std::vector<T> Unique(std::vector<T> values)
        {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            return values;
        }
        bool IndicesValid(const std::vector<uint32_t>& indices, size_t size)
        {
            return std::all_of(indices.begin(), indices.end(), [&](uint32_t i) { return i < size; });
        }
        MeshOperationResult Failure(std::string error)
        {
            MeshOperationResult r;
            r.Error = std::move(error);
            return r;
        }
        template<typename F>
        MeshOperationResult Edit(EditableMesh& mesh, F&& edit)
        {
            if (auto error = mesh.Validate(); !error.empty())
                return Failure(error);
            EditableMesh next   = mesh;
            auto         result = edit(next);
            if (!result)
                return result;
            if (auto error = next.Validate(); !error.empty())
                return Failure(error);
            mesh = std::move(next);
            return result;
        }
        template<typename F>
        MeshOperationResult EditFaces(EditableMesh& mesh, const std::vector<uint32_t>& faces, F&& edit)
        {
            if (!IndicesValid(faces, mesh.Faces.size()))
                return Failure("Face index out of range");
            return Edit(mesh, [&](EditableMesh& next) {
                for (auto f : Unique(faces))
                    edit(next.Faces[f]);
                return MeshOperationResult {};
            });
        }
        std::vector<std::vector<uint32_t>> FaceGroups(const EditableMesh&          mesh,
                                                      const std::vector<uint32_t>& faces,
                                                      bool                         individual,
                                                      std::set<MeshEdge>&          nonManifold)
        {
            std::vector<uint32_t> parent(mesh.Faces.size(), UINT32_MAX);
            for (auto f : faces)
                parent[f] = f;
            const auto root = [&](uint32_t f) {
                while (parent[f] != f)
                {
                    parent[f] = parent[parent[f]];
                    f         = parent[f];
                }
                return f;
            };
            for (const auto& edge : mesh.Edges())
            {
                if (edge.Faces.size() > 2)
                    nonManifold.insert(edge.Vertices);
                if (individual)
                    continue;
                uint32_t first = UINT32_MAX;
                for (auto f : edge.Faces)
                    if (parent[f] != UINT32_MAX)
                    {
                        if (first == UINT32_MAX)
                            first = f;
                        else
                        {
                            auto a = root(first), b = root(f);
                            parent[std::max(a, b)] = std::min(a, b);
                        }
                    }
            }
            std::map<uint32_t, std::vector<uint32_t>> groups;
            for (auto f : faces)
                groups[root(f)].push_back(f);
            std::vector<std::vector<uint32_t>> result;
            for (auto& [root, group] : groups)
                result.push_back(std::move(group));
            return result;
        }
        struct DirectedBoundary
        {
            uint32_t A, B, Face;
        };
        std::string RegionBoundary(const EditableMesh&            mesh,
                                   const std::vector<uint32_t>&   faces,
                                   std::vector<DirectedBoundary>& boundary,
                                   const std::set<MeshEdge>&      nonManifold)
        {
            std::map<MeshEdge, std::vector<DirectedBoundary>> edges;
            for (auto f : faces)
                for (size_t i = 0; i < mesh.Faces[f].Indices.size(); ++i)
                {
                    auto& loop = mesh.Faces[f].Indices;
                    auto  a = loop[i], b = loop[(i + 1) % loop.size()];
                    edges[SortedEdge(a, b)].push_back({a, b, f});
                }
            for (const auto& [edge, half] : edges)
            {
                if (nonManifold.count(edge))
                    return "Selected region has non-manifold edges";
                if (half.size() == 1)
                    boundary.push_back(half[0]);
                else if (half.size() != 2 || half[0].A != half[1].B || half[0].B != half[1].A)
                    return "Selected faces have inconsistent winding";
            }
            std::set<uint32_t> outgoing, incoming;
            for (const auto& edge : boundary)
                if (!outgoing.insert(edge.A).second || !incoming.insert(edge.B).second)
                    return "Selected region has a branching boundary";
            return {};
        }
        void RemoveDegenerate(EditableMesh& mesh)
        {
            for (auto& face : mesh.Faces)
            {
                std::set<uint32_t> seen;
                face.Indices.erase(std::remove_if(face.Indices.begin(),
                                                  face.Indices.end(),
                                                  [&](auto i) { return !seen.insert(i).second; }),
                                   face.Indices.end());
            }
            mesh.Faces.erase(std::remove_if(mesh.Faces.begin(),
                                            mesh.Faces.end(),
                                            [&](const auto& face) {
                                                return face.Indices.size() < 3 ||
                                                       glm::length(FaceAreaNormal(mesh, face)) == 0;
                                            }),
                             mesh.Faces.end());
        }
    } // namespace
    std::vector<uint32_t> SelectionVertices(const EditableMesh& mesh, const MeshSelection& s)
    {
        std::vector<uint32_t> result;
        if (s.Type == ElementType::Vertex)
            result = s.Vertices;
        else if (s.Type == ElementType::Edge)
            for (auto e : s.Edges)
            {
                result.push_back(e[0]);
                result.push_back(e[1]);
            }
        else
            for (auto f : s.Faces)
                if (f < mesh.Faces.size())
                    result.insert(result.end(), mesh.Faces[f].Indices.begin(), mesh.Faces[f].Indices.end());
        result.erase(std::remove_if(result.begin(), result.end(), [&](auto i) { return i >= mesh.Positions.size(); }),
                     result.end());
        return Unique(std::move(result));
    }
    glm::vec3 SelectionCentroid(const EditableMesh& mesh, const MeshSelection& s)
    {
        auto       vertices = SelectionVertices(mesh, s);
        glm::dvec3 sum(0);
        for (auto i : vertices)
            sum += glm::dvec3(mesh.Positions[i]);
        return vertices.empty() ? glm::vec3(0) : glm::vec3(sum / double(vertices.size()));
    }
    glm::vec3 SelectionNormal(const EditableMesh& mesh, const MeshSelection& s)
    {
        auto               vertices = SelectionVertices(mesh, s);
        std::set<uint32_t> selected(vertices.begin(), vertices.end());
        auto               faces = Unique(s.Faces);
        glm::dvec3         sum(0);
        for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
            if (s.Type == ElementType::Face ? std::binary_search(faces.begin(), faces.end(), f) :
                                              std::any_of(mesh.Faces[f].Indices.begin(),
                                                          mesh.Faces[f].Indices.end(),
                                                          [&](auto v) { return selected.count(v); }))
                sum += FaceAreaNormal(mesh, mesh.Faces[f]);
        return glm::length(sum) == 0 ? glm::vec3(0) : glm::vec3(glm::normalize(sum));
    }
    MeshSelection ConvertSelection(const EditableMesh& mesh, const MeshSelection& s, ElementType type)
    {
        MeshSelection result;
        result.Type                 = type;
        auto               vertices = SelectionVertices(mesh, s);
        std::set<uint32_t> selected(vertices.begin(), vertices.end());
        if (type == ElementType::Vertex)
            result.Vertices = std::move(vertices);
        else if (type == ElementType::Face)
        {
            if (s.Type == ElementType::Face)
            {
                for (auto f : Unique(s.Faces))
                    if (f < mesh.Faces.size())
                        result.Faces.push_back(f);
            }
            else
            {
                std::set<MeshEdge> selectedEdges;
                for (auto e : s.Edges)
                    selectedEdges.insert(SortedEdge(e[0], e[1]));
                for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
                {
                    const auto& loop     = mesh.Faces[f].Indices;
                    bool        complete = true;
                    for (size_t i = 0; i < loop.size(); ++i)
                        complete =
                            complete && (s.Type == ElementType::Edge ?
                                             selectedEdges.count(SortedEdge(loop[i], loop[(i + 1) % loop.size()])) :
                                             selected.count(loop[i]));
                    if (complete)
                        result.Faces.push_back(f);
                }
            }
        }
        else
        {
            std::set<MeshEdge> edges;
            if (s.Type == ElementType::Face)
            {
                for (auto f : s.Faces)
                    if (f < mesh.Faces.size())
                    {
                        auto& loop = mesh.Faces[f].Indices;
                        for (size_t i = 0; i < loop.size(); ++i)
                            edges.insert(SortedEdge(loop[i], loop[(i + 1) % loop.size()]));
                    }
            }
            else if (s.Type == ElementType::Edge)
                for (auto e : s.Edges)
                    edges.insert(SortedEdge(e[0], e[1]));
            else
                for (const auto& edge : mesh.Edges())
                    if (selected.count(edge.Vertices[0]) && selected.count(edge.Vertices[1]))
                        edges.insert(edge.Vertices);
            result.Edges.assign(edges.begin(), edges.end());
        }
        return result;
    }
    MeshOperationResult
    TransformVertices(EditableMesh& mesh, const std::vector<uint32_t>& vertices, const glm::mat4& transform)
    {
        if (!IndicesValid(vertices, mesh.Positions.size()))
            return Failure("Vertex index out of range");
        return Edit(mesh, [&](EditableMesh& next) {
            for (auto i : Unique(vertices))
            {
                auto p = transform * glm::vec4(next.Positions[i], 1);
                if (p.w == 0)
                    return Failure("Transform sends a vertex to infinity");
                next.Positions[i] = glm::vec3(p) / p.w;
            }
            return MeshOperationResult {};
        });
    }
    MeshOperationResult
    ExtrudeFaces(EditableMesh& mesh, const std::vector<uint32_t>& input, float distance, ExtrudeMode mode)
    {
        if (!IndicesValid(input, mesh.Faces.size()))
            return Failure("Face index out of range");
        if (!std::isfinite(distance) || distance == 0)
            return Failure("Extrusion distance must be finite and nonzero");
        auto faces = Unique(input);
        return Edit(mesh, [&](EditableMesh& next) {
            MeshOperationResult result;
            std::set<MeshEdge>  nonManifold;
            auto                groups = FaceGroups(mesh, faces, mode == ExtrudeMode::Individual, nonManifold);
            for (const auto& group : groups)
            {
                std::vector<DirectedBoundary> boundary;
                if (auto error = RegionBoundary(mesh, group, boundary, nonManifold); !error.empty())
                    return Failure(error);
                std::map<uint32_t, glm::dvec3> normals;
                for (auto f : group)
                    for (auto v : mesh.Faces[f].Indices)
                    {
                        if (!normals.count(v))
                            normals[v] = glm::dvec3(0);
                        normals[v] += FaceAreaNormal(mesh, mesh.Faces[f]);
                    }
                std::map<uint32_t, uint32_t> remap;
                for (auto [v, normal] : normals)
                {
                    if (glm::length(normal) == 0)
                        return Failure("Selected face normals cancel at a vertex");
                    remap[v] = uint32_t(next.Positions.size());
                    next.Positions.push_back(mesh.Positions[v] + glm::vec3(glm::normalize(normal)) * distance);
                }
                for (auto f : group)
                {
                    for (auto& v : next.Faces[f].Indices)
                        v = remap.at(v);
                    result.Faces.push_back(f);
                }
                for (auto edge : boundary)
                {
                    Face side {{edge.A, edge.B, remap.at(edge.B), remap.at(edge.A)},
                               mesh.Faces[edge.Face].Material,
                               0,
                               mesh.Faces[edge.Face].UV};
                    next.Faces.push_back(std::move(side));
                }
            }
            return result;
        });
    }
    MeshOperationResult ExtrudeEdges(EditableMesh& mesh, const std::vector<MeshEdge>& input, float distance)
    {
        if (!std::isfinite(distance) || distance <= 0)
            return Failure("Edge extrusion distance must be positive and finite");
        std::vector<MeshEdge> edges;
        for (auto e : input)
            edges.push_back(SortedEdge(e[0], e[1]));
        edges         = Unique(std::move(edges));
        auto topology = mesh.Edges();
        return Edit(mesh, [&](EditableMesh& next) {
            MeshOperationResult result;
            // Shared endpoints use the intersection of offset edge lines (miter).
            std::map<uint32_t, std::vector<glm::vec3>> offsets;
            std::vector<DirectedBoundary>              directed;
            for (auto e : edges)
            {
                auto it = std::lower_bound(
                    topology.begin(), topology.end(), e, [](const auto& t, auto key) { return t.Vertices < key; });
                if (it == topology.end() || it->Vertices != e || !it->IsBoundary())
                    return Failure("Edge extrusion requires existing boundary edges");
                uint32_t    f = it->Faces[0], a = e[0], b = e[1];
                const auto& loop  = mesh.Faces[f].Indices;
                auto        start = std::find(loop.begin(), loop.end(), a) - loop.begin();
                if (loop[(start + 1) % loop.size()] != b)
                    std::swap(a, b);
                auto normal  = glm::vec3(glm::normalize(FaceAreaNormal(mesh, mesh.Faces[f])));
                auto outward = glm::normalize(glm::cross(mesh.Positions[b] - mesh.Positions[a], normal));
                offsets[a].push_back(outward);
                offsets[b].push_back(outward);
                directed.push_back({a, b, f});
            }
            std::map<uint32_t, uint32_t> remap;
            for (const auto& [v, normals] : offsets)
            {
                if (normals.size() > 2)
                    return Failure("Branching edge extrusion is unsupported");
                glm::vec3 offset = normals[0];
                if (normals.size() == 2)
                {
                    float denominator = 1 + glm::dot(normals[0], normals[1]);
                    if (denominator < 1e-5f)
                        return Failure("Edge extrusion has a reversing corner");
                    offset = (normals[0] + normals[1]) / denominator;
                }
                remap[v] = uint32_t(next.Positions.size());
                next.Positions.push_back(mesh.Positions[v] + offset * distance);
            }
            for (auto e : directed)
            {
                next.Faces.push_back(
                    {{e.B, e.A, remap[e.A], remap[e.B]}, mesh.Faces[e.Face].Material, 0, mesh.Faces[e.Face].UV});
                result.Edges.push_back(SortedEdge(remap[e.A], remap[e.B]));
            }
            return result;
        });
    }
    MeshOperationResult InsetFaces(EditableMesh& mesh, const std::vector<uint32_t>& input, float amount)
    {
        if (!IndicesValid(input, mesh.Faces.size()))
            return Failure("Face index out of range");
        if (!std::isfinite(amount) || amount <= 0)
            return Failure("Inset amount must be positive and finite");
        return Edit(mesh, [&](EditableMesh& next) {
            MeshOperationResult result;
            std::set<MeshEdge>  nonManifold;
            for (const auto& faces : FaceGroups(mesh, Unique(input), false, nonManifold))
            {
                std::vector<DirectedBoundary> boundary;
                if (auto error = RegionBoundary(mesh, faces, boundary, nonManifold); !error.empty())
                    return Failure(error);
                if (boundary.empty())
                    return Failure("Inset needs an open planar region boundary");
                auto   normal = glm::normalize(FaceAreaNormal(mesh, mesh.Faces[faces[0]]));
                auto   origin = glm::dvec3(mesh.Positions[mesh.Faces[faces[0]].Indices[0]]);
                double extent = 0;
                for (auto f : faces)
                    for (auto v : mesh.Faces[f].Indices)
                        extent = std::max(extent, glm::length(glm::dvec3(mesh.Positions[v]) - origin));
                for (auto f : faces)
                {
                    if (glm::dot(normal, glm::normalize(FaceAreaNormal(mesh, mesh.Faces[f]))) < 1 - 1e-6)
                        return Failure("Inset requires consistently oriented planar faces");
                    for (auto v : mesh.Faces[f].Indices)
                        if (std::abs(glm::dot(glm::dvec3(mesh.Positions[v]) - origin, normal)) >
                            std::max(1e-7, extent * 1e-6))
                            return Failure("Inset requires a planar region");
                }
                std::map<uint32_t, DirectedBoundary> outgoing, incoming;
                for (auto e : boundary)
                {
                    outgoing.emplace(e.A, e);
                    incoming.emplace(e.B, e);
                }
                std::map<uint32_t, glm::vec3> moved;
                for (auto [v, e] : outgoing)
                {
                    if (!incoming.count(v))
                        return Failure("Inset boundary is not a loop");
                    auto previous = incoming.at(v);
                    auto n1       = glm::cross(
                        normal, glm::normalize(glm::dvec3(mesh.Positions[v]) - glm::dvec3(mesh.Positions[previous.A])));
                    auto n2 = glm::cross(
                        normal, glm::normalize(glm::dvec3(mesh.Positions[e.B]) - glm::dvec3(mesh.Positions[v])));
                    double denominator = 1 + glm::dot(n1, n2);
                    if (denominator < 1e-8)
                        return Failure("Inset has a reversing boundary corner");
                    moved[v] = mesh.Positions[v] + glm::vec3((n1 + n2) * (amount / denominator));
                }
                // Reject collapsed/reversed edges and crossings, including holes.
                for (auto e : boundary)
                    if (glm::dot(moved[e.B] - moved[e.A], mesh.Positions[e.B] - mesh.Positions[e.A]) <= 0)
                        return Failure("Inset amount collapses a boundary edge");
                auto cross = [&](glm::vec3 a, glm::vec3 b) {
                    return glm::dot(glm::cross(glm::dvec3(a), glm::dvec3(b)), normal);
                };
                for (size_t i = 0; i < boundary.size(); ++i)
                    for (size_t j = 0; j < i; ++j)
                    {
                        auto a = boundary[i], b = boundary[j];
                        if (a.A == b.A || a.A == b.B || a.B == b.A || a.B == b.B)
                            continue;
                        auto p = moved[a.A], q = moved[a.B], r = moved[b.A], s = moved[b.B];
                        if (glm::all(glm::lessThanEqual(glm::max(glm::min(p, q), glm::min(r, s)),
                                                        glm::min(glm::max(p, q), glm::max(r, s)))) &&
                            cross(q - p, r - p) * cross(q - p, s - p) <= 0 &&
                            cross(s - r, p - r) * cross(s - r, q - r) <= 0)
                            return Failure("Inset boundaries cross");
                    }
                std::map<uint32_t, uint32_t> remap;
                for (auto f : faces)
                    for (auto v : mesh.Faces[f].Indices)
                        if (!remap.count(v))
                        {
                            remap[v] = uint32_t(next.Positions.size());
                            next.Positions.push_back(moved.count(v) ? moved[v] : mesh.Positions[v]);
                        }
                for (auto f : faces)
                {
                    for (auto& v : next.Faces[f].Indices)
                        v = remap[v];
                    if (glm::dot(FaceAreaNormal(next, next.Faces[f]), normal) <= 0)
                        return Failure("Inset collapses an inner face");
                    result.Faces.push_back(f);
                }
                for (auto e : boundary)
                    next.Faces.push_back(
                        {{e.A, e.B, remap[e.B], remap[e.A]}, mesh.Faces[e.Face].Material, 0, mesh.Faces[e.Face].UV});
            }
            return result;
        });
    }
    MeshOperationResult DeleteFaces(EditableMesh& mesh, const std::vector<uint32_t>& faces)
    {
        if (!IndicesValid(faces, mesh.Faces.size()))
            return Failure("Face index out of range");
        return Edit(mesh, [&](EditableMesh& next) {
            auto selected = Unique(faces);
            for (auto it = selected.rbegin(); it != selected.rend(); ++it)
                next.Faces.erase(next.Faces.begin() + *it);
            MeshOperationResult r;
            r.Remap = next.Compact();
            return r;
        });
    }
    namespace
    {
        MeshOperationResult
        Collapse(EditableMesh& mesh, const std::vector<uint32_t>& input, std::optional<float> distance)
        {
            if (!IndicesValid(input, mesh.Positions.size()))
                return Failure("Vertex index out of range");
            auto vertices = Unique(input);
            return Edit(mesh, [&](EditableMesh& next) {
                std::vector<uint32_t> parent(mesh.Positions.size());
                std::iota(parent.begin(), parent.end(), 0);
                const auto root = [&](uint32_t v) {
                    while (parent[v] != v)
                    {
                        parent[v] = parent[parent[v]];
                        v         = parent[v];
                    }
                    return v;
                };
                const auto join = [&](uint32_t a, uint32_t b) {
                    a                      = root(a);
                    b                      = root(b);
                    parent[std::max(a, b)] = std::min(a, b);
                };
                if (!distance)
                {
                    for (auto v : vertices)
                        join(vertices.front(), v);
                }
                else
                {
                    using Cell = std::array<double, 3>;
                    struct CellHash
                    {
                        size_t operator()(const Cell& cell) const
                        {
                            size_t hash = 0;
                            for (auto value : cell)
                                hash ^= std::hash<double> {}(value) + size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
                            return hash;
                        }
                    };
                    std::unordered_map<Cell, std::vector<uint32_t>, CellHash> grid;
                    grid.reserve(vertices.size());
                    const double squared = double(*distance) * *distance;
                    for (auto v : vertices)
                    {
                        auto p = glm::dvec3(mesh.Positions[v]);
                        Cell cell;
                        for (int axis = 0; axis < 3; ++axis)
                            cell[axis] = *distance == 0 ? p[axis] : std::floor(p[axis] / *distance);
                        if (*distance == 0)
                        {
                            auto& bucket = grid[cell];
                            if (!bucket.empty())
                                join(v, bucket.front());
                            else
                                bucket.push_back(v);
                        }
                        else
                        {
                            for (int x = -1; x <= 1; ++x)
                                for (int y = -1; y <= 1; ++y)
                                    for (int z = -1; z <= 1; ++z)
                                    {
                                        auto it = grid.find({cell[0] + x, cell[1] + y, cell[2] + z});
                                        if (it == grid.end())
                                            continue;
                                        for (auto other : it->second)
                                        {
                                            auto delta = p - glm::dvec3(mesh.Positions[other]);
                                            if (glm::dot(delta, delta) <= squared)
                                                join(v, other);
                                        }
                                    }
                            grid[cell].push_back(v);
                        }
                    }
                }
                std::map<uint32_t, std::pair<glm::dvec3, size_t>> sums;
                for (auto v : vertices)
                {
                    auto key = root(v);
                    if (!sums.count(key))
                        sums[key] = {glm::dvec3(0), 0};
                    sums[key].first += glm::dvec3(mesh.Positions[v]);
                    ++sums[key].second;
                }
                for (auto [v, sum] : sums)
                    next.Positions[v] = glm::vec3(sum.first / double(sum.second));
                for (auto& face : next.Faces)
                    for (auto& v : face.Indices)
                        v = root(v);
                RemoveDegenerate(next);
                auto                compact = next.Compact();
                MeshOperationResult result;
                result.Remap.resize(parent.size());
                for (uint32_t v = 0; v < parent.size(); ++v)
                    result.Remap[v] = compact[root(v)];
                return result;
            });
        }
    } // namespace
    MeshOperationResult MergeVertices(EditableMesh& mesh, const std::vector<uint32_t>& vertices)
    {
        return Collapse(mesh, vertices, {});
    }
    MeshOperationResult WeldVertices(EditableMesh& mesh, const std::vector<uint32_t>& vertices, float distance)
    {
        if (!std::isfinite(distance) || distance < 0)
            return Failure("Weld distance must be nonnegative and finite");
        return Collapse(mesh, vertices, distance);
    }
    MeshOperationResult FlipFaces(EditableMesh& m, const std::vector<uint32_t>& f)
    {
        return EditFaces(m, f, [](Face& face) { std::reverse(face.Indices.begin(), face.Indices.end()); });
    }
    MeshOperationResult SetFaceMaterial(EditableMesh& m, const std::vector<uint32_t>& f, uint32_t value)
    {
        return EditFaces(m, f, [&](Face& face) { face.Material = value; });
    }
    MeshOperationResult SetSmoothingGroup(EditableMesh& m, const std::vector<uint32_t>& f, uint32_t value)
    {
        return EditFaces(m, f, [&](Face& face) { face.SmoothingGroup = value; });
    }
    MeshOperationResult SetFaceUV(EditableMesh& m, const std::vector<uint32_t>& f, const FaceUV& uv)
    {
        return EditFaces(m, f, [&](Face& face) { face.UV = uv; });
    }
} // namespace Kans
