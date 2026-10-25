#pragma once
#include "EditableMesh.h"

namespace Kans
{
    enum class ShapeType
    {
        Cube,
        Plane,
        Sphere,
        Cylinder,
        Cone,
        Torus,
        Stairs,
        Arch,
        Pipe,
        Prism
    };
    struct ShapeDesc
    {
        ShapeType  Type = ShapeType::Cube;
        glm::vec3  Size {1};
        glm::ivec3 Subdivisions {1};
        float      Width = 1, Depth = 1, Height = 1, Radius = .5f;
        float      MajorRadius = 1, MinorRadius = .25f, OuterRadius = 1, Thickness = .25f, Angle = 180;
        int        Segments = 32, Rings = 16, Sides = 32, HeightSegments = 1, Rows = 32, Columns = 16, Steps = 8;
        bool       SideWalls = true; // False leaves the stair sides open.
    };
    bool         operator==(const ShapeDesc& a, const ShapeDesc& b);
    void         Sanitize(ShapeDesc& desc);
    EditableMesh GenerateShape(ShapeDesc desc);
} // namespace Kans
