#pragma once
#include <algorithm>
#include <cfloat>
#include <glm/glm.hpp>

namespace Kans
{
    // Axis-aligned bounding box; starts empty.
    struct Bounds
    {
        glm::vec3 Min {FLT_MAX};
        glm::vec3 Max {-FLT_MAX};

        bool      IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
        glm::vec3 Center() const { return (Min + Max) * 0.5f; }
        // Radius of the enclosing sphere.
        float     Radius() const { return glm::length(Max - Min) * 0.5f; }

        void Expand(const glm::vec3& point)
        {
            Min = glm::min(Min, point);
            Max = glm::max(Max, point);
        }
        void Expand(const Bounds& other)
        {
            if (other.IsValid())
            {
                Expand(other.Min);
                Expand(other.Max);
            }
        }
        // Bounds of the eight transformed corners.
        Bounds Transformed(const glm::mat4& transform) const
        {
            Bounds result;
            if (!IsValid())
                return result;
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 point((corner & 1) ? Max.x : Min.x, (corner & 2) ? Max.y : Min.y, (corner & 4) ? Max.z : Min.z);
                result.Expand(glm::vec3(transform * glm::vec4(point, 1.0f)));
            }
            return result;
        }
    };
} // namespace Kans
