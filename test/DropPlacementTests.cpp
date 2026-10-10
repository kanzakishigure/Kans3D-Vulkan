#include <gtest/gtest.h>
#include "Kans3D/Editor/DropPlacement.h"

namespace
{
    using namespace Kans;

    void ExpectPosition(const glm::vec3& actual, const glm::vec3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, 1e-6f);
        EXPECT_NEAR(actual.y, expected.y, 1e-6f);
        EXPECT_NEAR(actual.z, expected.z, 1e-6f);
    }

    TEST(DropPlacement, SurfaceWinsOverGroundAndSeatsAsymmetricScaledBounds)
    {
        // Using min x/z, centre y, or adding the offset gives a different origin.
        const Bounds bounds {{2, -3, -8}, {10, 5, 2}};
        ExpectPosition(ComputeDropPosition({1, 6, 2}, {0, -1, 0}, glm::vec3(20, 7, 30), bounds, 4),
                       {14, 10, 33});
        // Empty bounds must not subtract their sentinel values.
        ExpectPosition(ComputeDropPosition({1, 6, 2}, {0, -1, 0}, glm::vec3(20, 7, 30), {}, 4),
                       {20, 7, 30});
    }

    TEST(DropPlacement, ForwardGroundHitUsesRayIntersectionThenBoundsOffset)
    {
        // The unit ray reaches y=0 after 10 units, rather than fallbackDistance=2.
        const Bounds bounds {{-4, -2, 2}, {2, 4, 8}};
        ExpectPosition(ComputeDropPosition({1, 6, 3}, {0, -0.6f, -0.8f}, std::nullopt, bounds, 2),
                       {2, 2, -10});
    }

    TEST(DropPlacement, GroundBehindCameraFallsBackAboveAndBelowPlane)
    {
        ExpectPosition(ComputeDropPosition({1, 6, 3}, {0, 0.6f, -0.8f}, std::nullopt, {}, 5), {1, 9, -1});
        ExpectPosition(ComputeDropPosition({1, -6, 3}, {0, -0.6f, -0.8f}, std::nullopt, {}, 5), {1, -9, -1});
    }

    TEST(DropPlacement, ParallelRayUsesFallbackAndStillSeatsBounds)
    {
        const Bounds bounds {{2, -3, -8}, {10, 5, 2}};
        ExpectPosition(ComputeDropPosition({1, 6, 3}, {1, 0, 0}, std::nullopt, bounds, 4), {-1, 9, 6});
    }

    TEST(DropPlacement, GroundDistanceLimitIsInclusiveAndMeasuredAlongRay)
    {
        // y=6 is below maxDistance=9, but the intersection is 10 units down the ray.
        ExpectPosition(ComputeDropPosition({1, 6, 3}, {0, -0.6f, -0.8f}, std::nullopt, {}, 5, 9), {1, 3, -1});
        ExpectPosition(ComputeDropPosition({1, 6, 3}, {0, -0.6f, -0.8f}, std::nullopt, {}, 5, 10), {1, 0, -5});
    }
} // namespace
