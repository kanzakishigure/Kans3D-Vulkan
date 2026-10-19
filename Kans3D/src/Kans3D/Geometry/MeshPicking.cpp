#include "kspch.h"
#include <cmath>
#include <numeric>
#include "MeshOperations.h"

namespace Kans
{
    namespace
    {
        struct Triangle
        {
            glm::dvec3 A, B, C;
            uint32_t   Face;
            Bounds     Box;
        };
        struct Node
        {
            Bounds Box;
            size_t Begin = 0, End = 0;
            int    Left = -1, Right = -1;
        };
        class FaceTree
        {
            std::vector<Triangle> Triangles;
            std::vector<Node>     Nodes;
            int                   Split(size_t begin, size_t end)
            {
                Node node;
                node.Begin = begin;
                node.End   = end;
                for (size_t i = begin; i < end; ++i)
                    node.Box.Expand(Triangles[i].Box);
                int index = int(Nodes.size());
                Nodes.push_back(node);
                if (end - begin > 8)
                {
                    auto extent = node.Box.Max - node.Box.Min;
                    int  axis   = extent.y > extent.x ? 1 : 0;
                    if (extent.z > extent[axis])
                        axis = 2;
                    size_t middle = (begin + end) / 2;
                    std::nth_element(
                        Triangles.begin() + begin,
                        Triangles.begin() + middle,
                        Triangles.begin() + end,
                        [&](const auto& a, const auto& b) { return a.Box.Center()[axis] < b.Box.Center()[axis]; });
                    int left = Split(begin, middle), right = Split(middle, end);
                    Nodes[index].Left  = left;
                    Nodes[index].Right = right;
                }
                return index;
            }
            bool Intersects(const Bounds& box, glm::dvec3 origin, glm::dvec3 direction, double maximum) const
            {
                double tMin = 0, tMax = maximum;
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (std::abs(direction[axis]) < 1e-30)
                    {
                        if (origin[axis] < box.Min[axis] || origin[axis] > box.Max[axis])
                            return false;
                    }
                    else
                    {
                        double a = (box.Min[axis] - origin[axis]) / direction[axis],
                               b = (box.Max[axis] - origin[axis]) / direction[axis];
                        if (a > b)
                            std::swap(a, b);
                        tMin = std::max(tMin, a);
                        tMax = std::min(tMax, b);
                        if (tMin > tMax)
                            return false;
                    }
                }
                return true;
            }
            void Visit(int                        index,
                       glm::dvec3                 origin,
                       glm::dvec3                 direction,
                       double&                    nearest,
                       std::optional<FaceRayHit>& hit) const
            {
                const auto& node = Nodes[index];
                if (!Intersects(node.Box, origin, direction, nearest))
                    return;
                if (node.Left >= 0)
                {
                    Visit(node.Left, origin, direction, nearest, hit);
                    Visit(node.Right, origin, direction, nearest, hit);
                    return;
                }
                for (size_t i = node.Begin; i < node.End; ++i)
                {
                    const auto& triangle = Triangles[i];
                    auto        e1 = triangle.B - triangle.A, e2 = triangle.C - triangle.A;
                    auto        p   = glm::cross(direction, e2);
                    double      det = glm::dot(e1, p);
                    if (std::abs(det) <= glm::length(e1) * glm::length(e2) * 1e-12)
                        continue;
                    auto   offset = origin - triangle.A;
                    double u      = glm::dot(offset, p) / det;
                    auto   q      = glm::cross(offset, e1);
                    double v = glm::dot(direction, q) / det, t = glm::dot(e2, q) / det;
                    if (u < -1e-9 || v < -1e-9 || u + v > 1 + 1e-9 || t < 0 || t > nearest)
                        continue;
                    if (t == nearest && hit && triangle.Face >= hit->Face)
                        continue;
                    nearest = t;
                    hit     = FaceRayHit {triangle.Face, float(t), glm::vec3(origin + direction * t)};
                }
            }

        public:
            explicit FaceTree(const EditableMesh& mesh, bool build = true)
            {
                if (!build || !mesh.Validate().empty())
                    return;
                for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
                    for (auto indices : TriangulateFace(mesh, mesh.Faces[f]))
                    {
                        Triangle triangle {glm::dvec3(mesh.Positions[indices[0]]),
                                           glm::dvec3(mesh.Positions[indices[1]]),
                                           glm::dvec3(mesh.Positions[indices[2]]),
                                           f,
                                           {}};
                        for (auto i : indices)
                            triangle.Box.Expand(mesh.Positions[i]);
                        Triangles.push_back(triangle);
                    }
                if (!Triangles.empty())
                    Split(0, Triangles.size());
            }
            std::optional<FaceRayHit> Cast(const MeshRay& ray) const
            {
                glm::dvec3 origin(ray.Origin), direction(ray.Direction);
                for (int i = 0; i < 3; ++i)
                    if (!std::isfinite(origin[i]) || !std::isfinite(direction[i]))
                        return {};
                if (Nodes.empty() || glm::length(direction) == 0)
                    return {};
                direction                         = glm::normalize(direction);
                double                    nearest = DBL_MAX;
                std::optional<FaceRayHit> hit;
                Visit(0, origin, direction, nearest, hit);
                return hit;
            }
        };
        struct Projected
        {
            glm::vec2 Pixel;
            float     Depth, W;
        };
        struct Picker
        {
            const EditableMesh& Mesh;
            glm::mat4           MVP, Inverse;
            glm::vec2           Viewport;
            bool                Occlusion, Valid;
            FaceTree            Tree;
            Picker(const EditableMesh& mesh, const glm::mat4& mvp, glm::vec2 viewport, bool occlusion) :
                Mesh(mesh), MVP(mvp), Inverse(glm::inverse(mvp)), Viewport(viewport), Occlusion(occlusion),
                Valid(viewport.x > 0 && viewport.y > 0 && std::isfinite(viewport.x) && std::isfinite(viewport.y)),
                Tree(mesh, occlusion)
            {
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        Valid = Valid && std::isfinite(Inverse[c][r]);
            }
            std::optional<Projected> Project(glm::vec3 point, bool clipXY = true) const
            {
                if (!Valid)
                    return {};
                auto clip = MVP * glm::vec4(point, 1);
                if (clip.w <= 0)
                    return {};
                auto ndc = glm::vec3(clip) / clip.w;
                if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z) || ndc.z < 0 || ndc.z > 1 ||
                    (clipXY && (ndc.x < -1 || ndc.x > 1 || ndc.y < -1 || ndc.y > 1)))
                    return {};
                return Projected {{(ndc.x + 1) * .5f * Viewport.x, (1 - ndc.y) * .5f * Viewport.y}, ndc.z, clip.w};
            }
            bool Visible(glm::vec3 point) const
            {
                if (!Occlusion)
                    return true;
                auto projected = Project(point);
                if (!projected)
                    return false;
                glm::vec2 ndc(projected->Pixel.x / Viewport.x * 2 - 1, 1 - projected->Pixel.y / Viewport.y * 2);
                auto      near4 = Inverse * glm::vec4(ndc, 0, 1);
                if (near4.w == 0)
                    return false;
                glm::vec3 eye   = glm::vec3(near4) / near4.w;
                auto      delta = point - eye;
                float     depth = glm::length(delta);
                auto      hit   = Tree.Cast({eye, delta});
                return !hit || hit->Distance >= depth - 1e-4f * std::max(1.0f, depth);
            }
            bool Segment(MeshEdge edge, glm::vec3& a, glm::vec3& b, Projected& pa, Projected& pb) const
            {
                if (!Valid || edge[0] >= Mesh.Positions.size() || edge[1] >= Mesh.Positions.size())
                    return false;
                a        = Mesh.Positions[edge[0]];
                b        = Mesh.Positions[edge[1]];
                auto  ca = MVP * glm::vec4(a, 1), cb = MVP * glm::vec4(b, 1);
                float lo = 0, hi = 1;
                for (int plane = 0; plane < 7; ++plane)
                {
                    auto value = [&](glm::vec4 p) {
                        switch (plane)
                        {
                            case 0:
                                return p.x + p.w;
                            case 1:
                                return p.w - p.x;
                            case 2:
                                return p.y + p.w;
                            case 3:
                                return p.w - p.y;
                            case 4:
                                return p.z;
                            case 5:
                                return p.w - p.z;
                            default:
                                return p.w - 1e-7f;
                        }
                    };
                    float x = value(ca), y = value(cb);
                    if (x < 0 && y < 0)
                        return false;
                    if (x < 0)
                        lo = std::max(lo, x / (x - y));
                    if (y < 0)
                        hi = std::min(hi, x / (x - y));
                }
                if (lo > hi)
                    return false;
                auto original = a;
                a             = glm::mix(original, b, lo);
                b             = glm::mix(original, b, hi);
                auto x = Project(a, false), y = Project(b, false);
                if (!x || !y)
                    return false;
                pa = *x;
                pb = *y;
                return true;
            }
        };
        float SegmentParameter(glm::vec2 a, glm::vec2 b, glm::vec2 p)
        {
            float length2 = glm::dot(b - a, b - a);
            return length2 == 0 ? 0 : std::clamp(glm::dot(p - a, b - a) / length2, 0.0f, 1.0f);
        }
        glm::vec3 PerspectivePoint(glm::vec3 a, glm::vec3 b, const Projected& pa, const Projected& pb, float t)
        {
            float u = t / pb.W / ((1 - t) / pa.W + t / pb.W);
            return glm::mix(a, b, u);
        }
        bool InRect(glm::vec2 p, MeshRect rect)
        {
            return p.x >= rect.Min.x && p.x <= rect.Max.x && p.y >= rect.Min.y && p.y <= rect.Max.y;
        }
        bool RectSegment(glm::vec2 a, glm::vec2 b, MeshRect rect, float& parameter)
        {
            float lo = 0, hi = 1;
            for (int axis = 0; axis < 2; ++axis)
            {
                float d = b[axis] - a[axis];
                if (d == 0)
                {
                    if (a[axis] < rect.Min[axis] || a[axis] > rect.Max[axis])
                        return false;
                }
                else
                {
                    float x = (rect.Min[axis] - a[axis]) / d, y = (rect.Max[axis] - a[axis]) / d;
                    if (x > y)
                        std::swap(x, y);
                    lo = std::max(lo, x);
                    hi = std::min(hi, y);
                    if (lo > hi)
                        return false;
                }
            }
            parameter = (lo + hi) * .5f;
            return true;
        }
    } // namespace
    std::optional<FaceRayHit> RaycastFaces(const EditableMesh& mesh, const MeshRay& ray)
    {
        return FaceTree(mesh).Cast(ray);
    }
    std::optional<VertexPickHit> PickVertex(const EditableMesh& mesh,
                                            const glm::mat4&    mvp,
                                            glm::vec2           viewport,
                                            glm::vec2           pixel,
                                            float               radius,
                                            bool                occlusion)
    {
        Picker                       picker(mesh, mvp, viewport, occlusion);
        std::optional<VertexPickHit> hit;
        float                        depth = FLT_MAX;
        if (!std::isfinite(radius) || radius < 0)
            return {};
        for (uint32_t v = 0; v < mesh.Positions.size(); ++v)
        {
            auto p = picker.Project(mesh.Positions[v]);
            if (!p)
                continue;
            float distance = glm::length(p->Pixel - pixel);
            if (distance > radius ||
                (hit && (distance > hit->DistancePixels || (distance == hit->DistancePixels && p->Depth >= depth))) ||
                !picker.Visible(mesh.Positions[v]))
                continue;
            hit   = VertexPickHit {v, distance};
            depth = p->Depth;
        }
        return hit;
    }
    std::optional<EdgePickHit> PickEdge(const EditableMesh& mesh,
                                        const glm::mat4&    mvp,
                                        glm::vec2           viewport,
                                        glm::vec2           pixel,
                                        float               radius,
                                        bool                occlusion)
    {
        Picker                     picker(mesh, mvp, viewport, occlusion);
        std::optional<EdgePickHit> hit;
        float                      depth = FLT_MAX;
        if (!std::isfinite(radius) || radius < 0)
            return {};
        for (const auto& edge : mesh.Edges())
        {
            glm::vec3 a, b;
            Projected pa, pb;
            if (!picker.Segment(edge.Vertices, a, b, pa, pb))
                continue;
            float t        = SegmentParameter(pa.Pixel, pb.Pixel, pixel);
            float distance = glm::length(glm::mix(pa.Pixel, pb.Pixel, t) - pixel), z = glm::mix(pa.Depth, pb.Depth, t);
            if (distance > radius ||
                (hit && (distance > hit->DistancePixels || (distance == hit->DistancePixels && z >= depth))) ||
                !picker.Visible(PerspectivePoint(a, b, pa, pb, t)))
                continue;
            hit   = EdgePickHit {edge.Vertices, distance};
            depth = z;
        }
        return hit;
    }
    MeshSelection SelectInRect(const EditableMesh& mesh,
                               const glm::mat4&    mvp,
                               glm::vec2           viewport,
                               MeshRect            rect,
                               ElementType         type,
                               bool                occlusion)
    {
        MeshSelection result;
        result.Type  = type;
        auto minimum = glm::min(rect.Min, rect.Max);
        rect.Max     = glm::max(rect.Min, rect.Max);
        rect.Min     = minimum;
        Picker picker(mesh, mvp, viewport, occlusion);
        if (type == ElementType::Vertex)
        {
            for (uint32_t v = 0; v < mesh.Positions.size(); ++v)
                if (auto p = picker.Project(mesh.Positions[v]);
                    p && InRect(p->Pixel, rect) && picker.Visible(mesh.Positions[v]))
                    result.Vertices.push_back(v);
        }
        else if (type == ElementType::Face)
        {
            for (uint32_t f = 0; f < mesh.Faces.size(); ++f)
            {
                glm::dvec3 sum(0);
                for (auto v : mesh.Faces[f].Indices)
                    sum += glm::dvec3(mesh.Positions[v]);
                if (mesh.Faces[f].Indices.empty())
                    continue;
                auto centre = glm::vec3(sum / double(mesh.Faces[f].Indices.size()));
                if (auto p = picker.Project(centre); p && InRect(p->Pixel, rect) && picker.Visible(centre))
                    result.Faces.push_back(f);
            }
        }
        else
            for (const auto& edge : mesh.Edges())
            {
                glm::vec3 a, b;
                Projected pa, pb;
                float     t;
                if (picker.Segment(edge.Vertices, a, b, pa, pb) && RectSegment(pa.Pixel, pb.Pixel, rect, t) &&
                    picker.Visible(PerspectivePoint(a, b, pa, pb, t)))
                    result.Edges.push_back(edge.Vertices);
            }
        return result;
    }
} // namespace Kans
