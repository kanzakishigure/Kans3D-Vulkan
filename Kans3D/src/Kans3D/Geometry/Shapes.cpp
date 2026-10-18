#include "kspch.h"
#include "Shapes.h"
#include <cmath>
#include <map>
#include <tuple>

namespace Kans
{
    bool operator==(const ShapeDesc& a, const ShapeDesc& b)
    {
        return a.Type == b.Type && a.Size == b.Size && a.Subdivisions == b.Subdivisions && a.Width == b.Width &&
               a.Depth == b.Depth && a.Height == b.Height && a.Radius == b.Radius && a.MajorRadius == b.MajorRadius &&
               a.MinorRadius == b.MinorRadius && a.OuterRadius == b.OuterRadius && a.Thickness == b.Thickness &&
               a.Angle == b.Angle && a.Segments == b.Segments && a.Rings == b.Rings && a.Sides == b.Sides &&
               a.HeightSegments == b.HeightSegments && a.Rows == b.Rows && a.Columns == b.Columns &&
               a.Steps == b.Steps && a.SideWalls == b.SideWalls;
    }
    void Sanitize(ShapeDesc& d)
    {
        const auto positive = [](float value, float fallback) {
            return std::isfinite(value) ? std::clamp(value, .0001f, 100000.0f) : fallback;
        };
        if (int(d.Type) < 0 || int(d.Type) > int(ShapeType::Prism))
            d.Type = ShapeType::Cube;
        for (int i = 0; i < 3; ++i)
        {
            d.Size[i]         = positive(d.Size[i], 1);
            d.Subdivisions[i] = std::clamp(d.Subdivisions[i], 1, 64);
        }
        d.Width          = positive(d.Width, 1);
        d.Depth          = positive(d.Depth, 1);
        d.Height         = positive(d.Height, 1);
        d.Radius         = positive(d.Radius, .5f);
        d.OuterRadius    = positive(d.OuterRadius, 1);
        d.MajorRadius    = positive(d.MajorRadius, 1);
        d.MinorRadius    = std::clamp(positive(d.MinorRadius, .25f), .00001f, d.MajorRadius * .99f);
        float radius     = d.Type == ShapeType::Pipe ? d.Radius : d.OuterRadius;
        d.Thickness      = std::clamp(positive(d.Thickness, .25f), .00001f, radius * .99f);
        d.Angle          = std::isfinite(d.Angle) ? std::clamp(d.Angle, .1f, 360.0f) : 180;
        d.Segments       = std::clamp(d.Segments, 3, 128);
        d.Rings          = std::clamp(d.Rings, 2, 64);
        d.Sides          = std::clamp(d.Sides, 3, 128);
        d.HeightSegments = std::clamp(d.HeightSegments, 1, 64);
        d.Rows           = std::clamp(d.Rows, 3, 128);
        d.Columns        = std::clamp(d.Columns, 3, 128);
        d.Steps          = std::clamp(d.Steps, 1, 128);
    }
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        struct Writer
        {
            EditableMesh                                        Mesh;
            std::map<std::tuple<float, float, float>, uint32_t> Points;
            uint32_t                                            Point(glm::vec3 p)
            {
                auto [it, added] = Points.emplace(std::make_tuple(p.x, p.y, p.z), uint32_t(Mesh.Positions.size()));
                if (added)
                    Mesh.Positions.push_back(p);
                return it->second;
            }
            void Polygon(std::vector<uint32_t> loop, glm::vec3 outward, uint32_t group = 0)
            {
                Face face {std::move(loop), 0, group, {}};
                if (glm::dot(FaceAreaNormal(Mesh, face), glm::dvec3(outward)) < 0)
                    std::reverse(face.Indices.begin(), face.Indices.end());
                Mesh.Faces.push_back(std::move(face));
            }
            void Centre()
            {
                Bounds bounds;
                for (auto p : Mesh.Positions)
                    bounds.Expand(p);
                auto centre = bounds.Center();
                for (auto& p : Mesh.Positions)
                    p -= centre;
            }
        };
        glm::vec3 Radial(double angle, float radius, float y)
        {
            return {float(std::cos(angle) * radius), y, float(std::sin(angle) * radius)};
        }
        void ExtrudedProfile(Writer& w, const std::vector<glm::vec2>& profile, float width, bool walls)
        {
            std::vector<uint32_t> left, right;
            for (auto p : profile)
            {
                left.push_back(w.Point({-width / 2, p.y, p.x}));
                right.push_back(w.Point({width / 2, p.y, p.x}));
            }
            if (walls)
            {
                w.Polygon(left, {-1, 0, 0});
                w.Polygon(right, {1, 0, 0});
            }
            // The profile is CCW in (depth,height), so its outward 2D normal is (dy,-dx).
            for (size_t i = 0; i < profile.size(); ++i)
            {
                auto j     = (i + 1) % profile.size();
                auto delta = profile[j] - profile[i];
                w.Polygon({left[i], right[i], right[j], left[j]}, {0, -delta.x, delta.y});
            }
        }
    } // namespace
    EditableMesh GenerateShape(ShapeDesc d)
    {
        Sanitize(d);
        Writer w;
        if (d.Type == ShapeType::Cube || d.Type == ShapeType::Plane)
        {
            auto grid = [&](int axis, int sign, int a, int b, int na, int nb, glm::vec3 size) {
                for (int j = 0; j < nb; ++j)
                    for (int i = 0; i < na; ++i)
                    {
                        std::vector<uint32_t> loop;
                        for (auto corner :
                             {glm::ivec2(i, j), glm::ivec2(i + 1, j), glm::ivec2(i + 1, j + 1), glm::ivec2(i, j + 1)})
                        {
                            glm::vec3 p(0);
                            p[axis] = sign * size[axis] * .5f;
                            p[a]    = size[a] * (float(corner.x) / na - .5f);
                            p[b]    = size[b] * (float(corner.y) / nb - .5f);
                            loop.push_back(w.Point(p));
                        }
                        glm::vec3 n(0);
                        n[axis] = float(sign);
                        w.Polygon(std::move(loop), n);
                    }
            };
            if (d.Type == ShapeType::Plane)
                grid(1, 1, 0, 2, d.Subdivisions.x, d.Subdivisions.z, {d.Width, 0, d.Depth});
            else
                for (int axis = 0; axis < 3; ++axis)
                    for (int sign : {-1, 1})
                    {
                        int a = (axis + 1) % 3, b = (axis + 2) % 3;
                        grid(axis, sign, a, b, d.Subdivisions[a], d.Subdivisions[b], d.Size);
                    }
        }
        else if (d.Type == ShapeType::Sphere)
        {
            auto                               top = w.Point({0, d.Radius, 0}), bottom = w.Point({0, -d.Radius, 0});
            std::vector<std::vector<uint32_t>> rings;
            for (int r = 1; r < d.Rings; ++r)
            {
                double                latitude = Pi * r / d.Rings;
                std::vector<uint32_t> ring;
                for (int s = 0; s < d.Segments; ++s)
                    ring.push_back(w.Point(Radial(2 * Pi * s / d.Segments,
                                                  float(d.Radius * std::sin(latitude)),
                                                  float(d.Radius * std::cos(latitude)))));
                rings.push_back(std::move(ring));
            }
            for (int s = 0; s < d.Segments; ++s)
            {
                int next = (s + 1) % d.Segments;
                w.Polygon({top, rings.front()[s], rings.front()[next]}, {0, 1, 0}, 1);
                w.Polygon({bottom, rings.back()[s], rings.back()[next]}, {0, -1, 0}, 1);
                for (size_t r = 1; r < rings.size(); ++r)
                {
                    auto n = w.Mesh.Positions[rings[r][s]] + w.Mesh.Positions[rings[r][next]];
                    w.Polygon({rings[r - 1][s], rings[r][s], rings[r][next], rings[r - 1][next]}, n, 1);
                }
            }
        }
        else if (d.Type == ShapeType::Cylinder || d.Type == ShapeType::Cone || d.Type == ShapeType::Pipe)
        {
            bool                               pipe = d.Type == ShapeType::Pipe, cone = d.Type == ShapeType::Cone;
            int                                heights = cone ? 1 : d.HeightSegments;
            std::vector<std::vector<uint32_t>> outer, inner;
            for (int h = 0; h <= heights; ++h)
            {
                float                 y = d.Height * (float(h) / heights - .5f);
                std::vector<uint32_t> ring, hole;
                for (int s = 0; s < d.Sides; ++s)
                {
                    ring.push_back(w.Point(Radial(2 * Pi * s / d.Sides, cone && h == heights ? 0 : d.Radius, y)));
                    if (pipe)
                        hole.push_back(w.Point(Radial(2 * Pi * s / d.Sides, d.Radius - d.Thickness, y)));
                }
                outer.push_back(std::move(ring));
                inner.push_back(std::move(hole));
            }
            for (int s = 0; s < d.Sides; ++s)
            {
                int  next = (s + 1) % d.Sides;
                auto n    = Radial(2 * Pi * (s + .5) / d.Sides, 1, 0);
                for (int h = 0; h < heights; ++h)
                {
                    if (cone)
                        w.Polygon({outer[0][s], outer[0][next], outer[1][s]}, n, 1);
                    else
                        w.Polygon({outer[h][s], outer[h][next], outer[h + 1][next], outer[h + 1][s]}, n, 1);
                    if (pipe)
                        w.Polygon({inner[h][s], inner[h][next], inner[h + 1][next], inner[h + 1][s]}, -n, 2);
                }
                if (pipe)
                    for (int h : {0, heights})
                        w.Polygon({outer[h][s], outer[h][next], inner[h][next], inner[h][s]}, {0, h == 0 ? -1 : 1, 0});
            }
            if (!pipe)
            {
                w.Polygon(outer.front(), {0, -1, 0});
                if (!cone)
                    w.Polygon(outer.back(), {0, 1, 0});
            }
        }
        else if (d.Type == ShapeType::Torus)
        {
            std::vector<std::vector<uint32_t>> rings(d.Rows, std::vector<uint32_t>(d.Columns));
            for (int r = 0; r < d.Rows; ++r)
                for (int c = 0; c < d.Columns; ++c)
                {
                    double minor = 2 * Pi * c / d.Columns;
                    rings[r][c]  = w.Point(Radial(2 * Pi * r / d.Rows,
                                                  float(d.MajorRadius + d.MinorRadius * std::cos(minor)),
                                                  float(d.MinorRadius * std::sin(minor))));
                }
            for (int r = 0; r < d.Rows; ++r)
                for (int c = 0; c < d.Columns; ++c)
                {
                    int    nr = (r + 1) % d.Rows, nc = (c + 1) % d.Columns;
                    double minor = 2 * Pi * (c + .5) / d.Columns;
                    auto   n     = Radial(2 * Pi * (r + .5) / d.Rows, float(std::cos(minor)), float(std::sin(minor)));
                    w.Polygon({rings[r][c], rings[nr][c], rings[nr][nc], rings[r][nc]}, n, 1);
                }
        }
        else if (d.Type == ShapeType::Stairs)
        {
            std::vector<glm::vec2> profile {
                {-d.Depth / 2, -d.Height / 2}, {d.Depth / 2, -d.Height / 2}, {d.Depth / 2, d.Height / 2}};
            for (int i = d.Steps - 1; i >= 0; --i)
            {
                float z = d.Depth * (float(i) / d.Steps - .5f);
                profile.push_back({z, d.Height * (float(i + 1) / d.Steps - .5f)});
                if (i > 0)
                    profile.push_back({z, d.Height * (float(i) / d.Steps - .5f)});
            }
            ExtrudedProfile(w, profile, d.Width, d.SideWalls);
        }
        else if (d.Type == ShapeType::Prism)
        {
            ExtrudedProfile(
                w, {{-d.Size.z / 2, -d.Size.y / 2}, {d.Size.z / 2, -d.Size.y / 2}, {0, d.Size.y / 2}}, d.Size.x, true);
        }
        else if (d.Type == ShapeType::Arch)
        {
            bool                                 full   = d.Angle == 360;
            int                                  points = d.Sides + (full ? 0 : 1);
            std::vector<std::array<uint32_t, 4>> rings(points);
            for (int s = 0; s < points; ++s)
            {
                double angle = Pi / 2 + glm::radians(double(d.Angle)) * (double(s) / d.Sides - .5);
                int    k     = 0;
                for (float z : {-d.Depth / 2, d.Depth / 2})
                    for (float radius : {d.OuterRadius, d.OuterRadius - d.Thickness})
                        rings[s][k++] = w.Point({float(radius * std::cos(angle)), float(radius * std::sin(angle)), z});
            }
            for (int s = 0; s < d.Sides; ++s)
            {
                auto      a = rings[s], b = rings[(s + 1) % points];
                double    angle = Pi / 2 + glm::radians(double(d.Angle)) * ((s + .5) / d.Sides - .5);
                glm::vec3 radial(float(std::cos(angle)), float(std::sin(angle)), 0);
                w.Polygon({a[0], b[0], b[2], a[2]}, radial, 1);
                w.Polygon({a[1], b[1], b[3], a[3]}, -radial, 2);
                w.Polygon({a[0], a[1], b[1], b[0]}, {0, 0, -1});
                w.Polygon({a[2], a[3], b[3], b[2]}, {0, 0, 1});
            }
            if (!full)
                for (int s : {0, points - 1})
                {
                    double    angle = Pi / 2 + glm::radians(double(d.Angle)) * (double(s) / d.Sides - .5);
                    glm::vec3 tangent(-float(std::sin(angle)), float(std::cos(angle)), 0);
                    auto      a = rings[s];
                    w.Polygon({a[0], a[1], a[3], a[2]}, s == 0 ? -tangent : tangent);
                }
            w.Centre();
        }
        return std::move(w.Mesh);
    }
} // namespace Kans
