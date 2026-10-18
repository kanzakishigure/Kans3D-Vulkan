#include "kspch.h"
#include "EditableMesh.h"
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace Kans
{
    MeshEdge SortedEdge(uint32_t a, uint32_t b) { return {std::min(a, b), std::max(a, b)}; }
    bool     operator==(const FaceUV& a, const FaceUV& b)
    {
        return a.Scale == b.Scale && a.Offset == b.Offset && a.Rotation == b.Rotation;
    }
    bool operator==(const Face& a, const Face& b)
    {
        return a.Indices == b.Indices && a.Material == b.Material && a.SmoothingGroup == b.SmoothingGroup &&
               a.UV == b.UV;
    }
    bool operator==(const EditableMesh& a, const EditableMesh& b)
    {
        return a.Positions == b.Positions && a.Faces == b.Faces;
    }
    glm::dvec3 FaceAreaNormal(const EditableMesh& mesh, const Face& face)
    {
        glm::dvec3 sum(0);
        if (face.Indices.empty())
            return sum;
        // Translate first to retain precision for small polygons far from the origin.
        const glm::dvec3 origin(mesh.Positions.at(face.Indices[0]));
        for (size_t i = 0; i < face.Indices.size(); ++i)
            sum += glm::cross(glm::dvec3(mesh.Positions.at(face.Indices[i])) - origin,
                              glm::dvec3(mesh.Positions.at(face.Indices[(i + 1) % face.Indices.size()])) - origin);
        return sum * .5;
    }
    std::string EditableMesh::Validate() const
    {
        for (const auto& p : Positions)
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(p[axis]))
                    return "Non-finite position";
        for (size_t f = 0; f < Faces.size(); ++f)
        {
            const auto& face  = Faces[f];
            const auto  error = [&](const char* message) { return "Face " + std::to_string(f) + ": " + message; };
            if (face.Indices.size() < 3)
                return error("fewer than three indices");
            for (size_t i = 0; i < face.Indices.size(); ++i)
            {
                if (face.Indices[i] >= Positions.size())
                    return error("index out of range");
                if (face.Indices[i] == face.Indices[(i + 1) % face.Indices.size()])
                    return error("repeated consecutive index");
            }
            if (glm::length(FaceAreaNormal(*this, face)) == 0)
                return error("zero-area face");
            if (!std::isfinite(face.UV.Rotation) || !std::isfinite(face.UV.Scale.x) ||
                !std::isfinite(face.UV.Scale.y) || !std::isfinite(face.UV.Offset.x) || !std::isfinite(face.UV.Offset.y))
                return error("non-finite UV settings");
            if (face.Material >= 1000000)
                return error("material slot exceeds MeshData limit");
        }
        return {};
    }
    std::vector<TopologyEdge> EditableMesh::Edges() const
    {
        std::map<MeshEdge, std::vector<uint32_t>> edges;
        for (uint32_t f = 0; f < Faces.size(); ++f)
            for (size_t i = 0; i < Faces[f].Indices.size(); ++i)
                edges[SortedEdge(Faces[f].Indices[i], Faces[f].Indices[(i + 1) % Faces[f].Indices.size()])].push_back(
                    f);
        std::vector<TopologyEdge> result;
        result.reserve(edges.size());
        for (auto& [edge, faces] : edges)
            result.push_back({edge, std::move(faces)});
        return result;
    }
    std::vector<MeshEdge> EditableMesh::BoundaryEdges() const
    {
        std::vector<MeshEdge> result;
        for (const auto& edge : Edges())
            if (edge.IsBoundary())
                result.push_back(edge.Vertices);
        return result;
    }
    std::vector<uint32_t> EditableMesh::Compact()
    {
        std::vector<uint32_t> remap(Positions.size(), UINT32_MAX);
        for (const auto& face : Faces)
            for (auto i : face.Indices)
                remap.at(i) = 0;
        std::vector<glm::vec3> positions;
        for (uint32_t i = 0; i < Positions.size(); ++i)
            if (remap[i] != UINT32_MAX)
            {
                remap[i] = uint32_t(positions.size());
                positions.push_back(Positions[i]);
            }
        for (auto& face : Faces)
            for (auto& i : face.Indices)
                i = remap[i];
        Positions = std::move(positions);
        return remap;
    }
    namespace
    {
        double Cross(glm::dvec2 a, glm::dvec2 b) { return a.x * b.y - a.y * b.x; }
        void   Projection(glm::dvec3 n, glm::dvec3& u, glm::dvec3& v)
        {
            n = glm::normalize(n);
            u = std::abs(n.y) < .999 ? glm::normalize(glm::cross(glm::dvec3(0, 1, 0), n)) : glm::dvec3(1, 0, 0);
            // Reproject the horizontal-face axis for slightly tilted faces.
            u = glm::normalize(u - n * glm::dot(u, n));
            v = glm::cross(n, u);
        }
        struct UnionFind
        {
            std::vector<uint32_t> Parent;
            explicit UnionFind(size_t count) : Parent(count) { std::iota(Parent.begin(), Parent.end(), 0); }
            uint32_t Find(uint32_t i) { return Parent[i] == i ? i : Parent[i] = Find(Parent[i]); }
            void     Join(uint32_t a, uint32_t b)
            {
                a = Find(a);
                b = Find(b);
                if (a != b)
                    Parent[std::max(a, b)] = std::min(a, b);
            }
        };
        uint32_t Bits(float f)
        {
            uint32_t value;
            if (f == 0)
                f = 0; // Canonicalise negative zero.
            std::memcpy(&value, &f, sizeof(value));
            return value;
        }
    } // namespace
    std::vector<std::array<uint32_t, 3>> TriangulateFace(const EditableMesh& mesh, const Face& face)
    {
        std::vector<std::array<uint32_t, 3>> result;
        if (face.Indices.size() < 3)
            return result;
        if (face.Indices.size() == 3)
            return {{face.Indices[0], face.Indices[1], face.Indices[2]}};
        auto normal = FaceAreaNormal(mesh, face);
        if (glm::length(normal) == 0)
            normal = {0, 1, 0};
        if (face.Indices.size() == 4)
        {
            bool         convex    = true;
            const double tolerance = glm::dot(normal, normal) * 1e-12;
            for (size_t i = 0; i < 4; ++i)
            {
                glm::dvec3 a(mesh.Positions.at(face.Indices[i]));
                glm::dvec3 b(mesh.Positions.at(face.Indices[(i + 1) % 4]));
                glm::dvec3 c(mesh.Positions.at(face.Indices[(i + 2) % 4]));
                convex = convex && glm::dot(glm::cross(b - a, c - b), normal) > tolerance;
            }
            if (convex)
                return {{face.Indices[0], face.Indices[1], face.Indices[2]},
                        {face.Indices[0], face.Indices[2], face.Indices[3]}};
        }
        glm::dvec3 u, v;
        Projection(normal, u, v);
        const glm::dvec3        origin(mesh.Positions.at(face.Indices[0]));
        std::vector<glm::dvec2> points;
        double                  extent = 0;
        for (auto index : face.Indices)
        {
            auto p = glm::dvec3(mesh.Positions.at(index)) - origin;
            points.push_back({glm::dot(p, u), glm::dot(p, v)});
            extent = std::max(extent, glm::length(points.back()));
        }
        const double          epsilon = std::max(extent * extent * 1e-12, 1e-30);
        std::vector<uint32_t> loop(points.size());
        std::iota(loop.begin(), loop.end(), 0);
        while (loop.size() > 3)
        {
            bool clipped = false;
            for (size_t i = 0; i < loop.size(); ++i)
            {
                auto a = loop[(i + loop.size() - 1) % loop.size()], b = loop[i], c = loop[(i + 1) % loop.size()];
                if (Cross(points[b] - points[a], points[c] - points[a]) <= epsilon)
                    continue;
                bool inside = false;
                for (auto p : loop)
                    if (p != a && p != b && p != c && Cross(points[b] - points[a], points[p] - points[a]) >= -epsilon &&
                        Cross(points[c] - points[b], points[p] - points[b]) >= -epsilon &&
                        Cross(points[a] - points[c], points[p] - points[c]) >= -epsilon)
                    {
                        inside = true;
                        break;
                    }
                if (inside)
                    continue;
                result.push_back({face.Indices[a], face.Indices[b], face.Indices[c]});
                loop.erase(loop.begin() + i);
                clipped = true;
                break;
            }
            if (!clipped)
            {
                // Deterministic fallback: discard the least-area corner. Collinear
                // boundary points do not obstruct the remaining ears.
                size_t least = 0;
                double area  = DBL_MAX;
                for (size_t i = 0; i < loop.size(); ++i)
                {
                    auto   a = loop[(i + loop.size() - 1) % loop.size()], b = loop[i], c = loop[(i + 1) % loop.size()];
                    double candidate = std::abs(Cross(points[b] - points[a], points[c] - points[a]));
                    if (candidate < area)
                    {
                        least = i;
                        area  = candidate;
                    }
                }
                auto a = loop[(least + loop.size() - 1) % loop.size()], b = loop[least],
                     c = loop[(least + 1) % loop.size()];
                if (area > epsilon)
                    result.push_back({face.Indices[a], face.Indices[b], face.Indices[c]});
                loop.erase(loop.begin() + least);
            }
        }
        if (loop.size() == 3)
            result.push_back({face.Indices[loop[0]], face.Indices[loop[1]], face.Indices[loop[2]]});
        return result;
    }
    MeshData Build(const EditableMesh& mesh)
    {
        if (auto error = mesh.Validate(); !error.empty())
            throw std::invalid_argument(error);
        MeshData result;
        if (mesh.Faces.empty())
            return result;
        std::vector<uint32_t> starts(mesh.Faces.size() + 1);
        for (size_t f = 0; f < mesh.Faces.size(); ++f)
            starts[f + 1] = starts[f] + uint32_t(mesh.Faces[f].Indices.size());
        std::vector<glm::dvec3> areas;
        areas.reserve(mesh.Faces.size());
        for (const auto& face : mesh.Faces)
            areas.push_back(FaceAreaNormal(mesh, face));
        UnionFind islands(starts.back());
        struct HalfEdge
        {
            uint32_t Face, A, B;
        };
        std::unordered_map<uint64_t, std::vector<HalfEdge>> edges;
        edges.reserve(starts.back());
        for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            if (!mesh.Faces[f].SmoothingGroup)
                continue;
            const auto& loop = mesh.Faces[f].Indices;
            for (uint32_t i = 0; i < loop.size(); ++i)
            {
                uint32_t j    = (i + 1) % uint32_t(loop.size());
                auto     edge = SortedEdge(loop[i], loop[j]);
                edges[(uint64_t(edge[0]) << 32) | edge[1]].push_back({f, starts[f] + i, starts[f] + j});
            }
        }
        for (const auto& [key, adjacent] : edges)
            for (size_t i = 1; i < adjacent.size(); ++i)
                for (size_t j = 0; j < i; ++j)
                {
                    auto a = adjacent[i], b = adjacent[j];
                    auto group = mesh.Faces[a.Face].SmoothingGroup;
                    if (!group || group != mesh.Faces[b.Face].SmoothingGroup)
                        continue;
                    const bool same = mesh.Faces[a.Face].Indices[a.A - starts[a.Face]] ==
                                      mesh.Faces[b.Face].Indices[b.A - starts[b.Face]];
                    islands.Join(a.A, same ? b.A : b.B);
                    islands.Join(a.B, same ? b.B : b.A);
                }
        std::vector<glm::dvec3> normals(starts.back(), glm::dvec3(0));
        for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
            for (uint32_t i = starts[f]; i < starts[f + 1]; ++i)
                normals[islands.Find(i)] += areas[f];
        using Key = std::array<uint32_t, 5>;
        struct KeyHash
        {
            size_t operator()(const Key& key) const
            {
                size_t hash = 0;
                for (auto word : key)
                    hash ^= word + size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
                return hash;
            }
        };
        std::unordered_map<Key, uint32_t, KeyHash> vertices;
        std::map<uint32_t, std::vector<uint32_t>>  sections;
        result.Vertices.reserve(starts.back());
        std::vector<uint32_t> renderIndices;
        for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const auto& face       = mesh.Faces[f];
            auto        areaNormal = areas[f];
            const auto  flatNormal = glm::normalize(areaNormal);
            glm::dvec3  u, v;
            Projection(areaNormal, u, v);
            float angle = glm::radians(face.UV.Rotation), cosine = std::cos(angle), sine = std::sin(angle);
            renderIndices.resize(face.Indices.size());
            for (uint32_t i = 0; i < face.Indices.size(); ++i)
            {
                uint32_t position = face.Indices[i], island = islands.Find(starts[f] + i);
                // Flat faces use one normal identity for all their corners.
                uint32_t  identity = face.SmoothingGroup ? island : starts[f];
                glm::vec2 uv(glm::dot(glm::dvec3(mesh.Positions[position]), u),
                             glm::dot(glm::dvec3(mesh.Positions[position]), v));
                uv *= face.UV.Scale;
                uv = glm::vec2(cosine * uv.x - sine * uv.y, sine * uv.x + cosine * uv.y) + face.UV.Offset;
                Key key {position, identity, Bits(uv.x), Bits(uv.y), uint32_t(face.UV.Scale.x * face.UV.Scale.y < 0)};
                uint32_t renderIndex = uint32_t(result.Vertices.size());
                bool     inserted    = true;
                if (face.SmoothingGroup)
                {
                    auto found  = vertices.emplace(key, renderIndex);
                    renderIndex = found.first->second;
                    inserted    = found.second;
                }
                if (inserted)
                {
                    MeshVertexData vertex;
                    const auto     p = mesh.Positions[position];
                    auto           n = flatNormal;
                    if (face.SmoothingGroup && glm::length(normals[island]) != 0)
                        n = glm::normalize(normals[island]);
                    vertex.Position = {p.x, p.y, p.z};
                    vertex.Normal   = {float(n.x), float(n.y), float(n.z)};
                    vertex.UV       = {uv.x, uv.y};
                    result.Vertices.push_back(vertex);
                    result.LocalBounds.Expand(p);
                }
                renderIndices[i] = renderIndex;
            }
            for (const auto& triangle : TriangulateFace(mesh, face))
                for (auto p : triangle)
                    sections[face.Material].push_back(
                        renderIndices[std::find(face.Indices.begin(), face.Indices.end(), p) - face.Indices.begin()]);
        }
        result.MaterialSlots.resize(sections.rbegin()->first + 1);
        for (const auto& [slot, indices] : sections)
        {
            MeshSection section {uint32_t(result.Indices.size()), uint32_t(indices.size()), slot, {}};
            for (auto i : indices)
            {
                const auto& p = result.Vertices[i].Position;
                section.LocalBounds.Expand({p[0], p[1], p[2]});
            }
            result.Indices.insert(result.Indices.end(), indices.begin(), indices.end());
            result.Sections.push_back(section);
        }
        std::vector<glm::vec3> tangents(result.Vertices.size(), glm::vec3(0)), bitangents(tangents);
        for (size_t i = 0; i < result.Indices.size(); i += 3)
        {
            uint32_t   a = result.Indices[i], b = result.Indices[i + 1], c = result.Indices[i + 2];
            const auto pos = [&](uint32_t k) {
                const auto& p = result.Vertices[k].Position;
                return glm::vec3(p[0], p[1], p[2]);
            };
            const auto uv = [&](uint32_t k) {
                const auto& t = result.Vertices[k].UV;
                return glm::vec2(t[0], t[1]);
            };
            auto  e1 = pos(b) - pos(a), e2 = pos(c) - pos(a);
            auto  t1 = uv(b) - uv(a), t2 = uv(c) - uv(a);
            float det = t1.x * t2.y - t1.y * t2.x;
            if (std::abs(det) < 1e-20f)
                continue;
            auto tangent   = (e1 * t2.y - e2 * t1.y) / det;
            auto bitangent = (e2 * t1.x - e1 * t2.x) / det;
            for (auto k : {a, b, c})
            {
                tangents[k] += tangent;
                bitangents[k] += bitangent;
            }
        }
        for (size_t i = 0; i < result.Vertices.size(); ++i)
        {
            auto&     vertex = result.Vertices[i];
            glm::vec3 n(vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]);
            auto      t = tangents[i] - n * glm::dot(n, tangents[i]);
            if (glm::length(t) < 1e-12f)
                t = glm::cross(std::abs(n.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), n);
            t              = glm::normalize(t);
            vertex.Tangent = {t.x, t.y, t.z, glm::dot(glm::cross(n, t), bitangents[i]) < 0 ? -1.0f : 1.0f};
        }
        if (auto valid = ValidateMeshData(result); std::holds_alternative<AssetError>(valid))
            throw std::invalid_argument(std::get<AssetError>(valid).Message);
        return result;
    }
} // namespace Kans
