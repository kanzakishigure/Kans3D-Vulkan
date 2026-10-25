#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include "Kans3D/Core/Math/Bounds.h"
#include "Kans3D/Editor/EditorCamera.h"

namespace
{
    using namespace Kans;

    bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-3f) { return glm::length(a - b) < tolerance; }

    TEST(EditorCamera, OrbitKeepsFocusAndDistance)
    {
        EditorCamera camera;
        camera.SetViewportSize(800, 600);
        camera.Focus({1, 2, 3}, 1.0f);
        const float distance = glm::length(camera.GetPosition() - camera.GetFocus());
        const auto  before   = camera.GetPosition();
        camera.Orbit({120.0f, 40.0f});
        EXPECT_TRUE(Near(camera.GetFocus(), {1, 2, 3}));
        EXPECT_NEAR(glm::length(camera.GetPosition() - camera.GetFocus()), distance, 1e-3f);
        EXPECT_FALSE(Near(camera.GetPosition(), before));
        // The view matrix looks at the focus.
        const glm::vec4 focus = camera.GetViewMatrix() * glm::vec4(camera.GetFocus(), 1.0f);
        EXPECT_NEAR(focus.x, 0.0f, 1e-3f);
        EXPECT_NEAR(focus.y, 0.0f, 1e-3f);
        EXPECT_LT(focus.z, 0.0f);
    }

    TEST(EditorCamera, PanMovesTheFocusInTheViewPlaneByScreenDistance)
    {
        EditorCamera camera(90.0f);
        camera.SetViewportSize(800, 600);
        camera.Focus({0, 0, 0}, 1.0f);
        const float distance = camera.GetDistance();
        const auto  forward  = camera.Forward();
        camera.Pan({-300.0f, 0.0f}); // Half the viewport height to the left.
        const glm::vec3 moved = camera.GetFocus();
        EXPECT_NEAR(glm::dot(moved, forward), 0.0f, 1e-3f);
        // At 90 degrees the view is 2 * distance high; 300 of 600 pixels is one distance.
        EXPECT_NEAR(glm::length(moved), distance, 1e-3f);
        EXPECT_NEAR(camera.GetDistance(), distance, 1e-5f);
    }

    TEST(EditorCamera, DollyApproachesAndPushesTheFocusWhenTooClose)
    {
        EditorCamera camera;
        camera.SetViewportSize(800, 600);
        camera.Focus({0, 0, 0}, 1.0f);
        const float distance = camera.GetDistance();
        camera.Dolly(1.0f);
        EXPECT_LT(camera.GetDistance(), distance);
        const auto forward = camera.Forward();
        camera.Dolly(100.0f);
        EXPECT_NEAR(camera.GetDistance(), 0.05f, 1e-5f);
        EXPECT_GT(glm::dot(camera.GetFocus(), forward), 0.0f); // Moved past the old focus.
    }

    TEST(EditorCamera, FlyLookAndViewMatrixRoundTrip)
    {
        EditorCamera camera;
        camera.SetViewportSize(800, 600);
        const auto position = camera.GetPosition();
        camera.Look({50.0f, -20.0f});
        EXPECT_TRUE(Near(camera.GetPosition(), position)); // Looking rotates in place.
        camera.SetFlySpeed(2.0f);
        const auto forward = camera.Forward();
        camera.Fly({0, 0, 1}, 1.5f);
        EXPECT_TRUE(Near(camera.GetPosition(), position + forward * 3.0f));

        EditorCamera other;
        other.SetViewportSize(800, 600);
        other.SetViewMatrix(camera.GetViewMatrix());
        EXPECT_TRUE(Near(other.Forward(), camera.Forward()));
        EXPECT_TRUE(Near(other.GetPosition(), camera.GetPosition(), 1e-2f));

        // The ray through the center of the view points forward.
        const auto [origin, direction] = camera.ScreenRay({400.0f, 300.0f});
        EXPECT_TRUE(Near(direction, camera.Forward()));
    }

    TEST(EditorCamera, AxisViewsAreOrthographic)
    {
        EditorCamera camera;
        camera.SetViewportSize(800, 600);
        camera.SetView(EditorCamera::View::Top);
        EXPECT_TRUE(camera.IsOrthographic());
        EXPECT_FLOAT_EQ(camera.GetProjectionMatrix()[3][3], 1.0f);
        EXPECT_LT(camera.Forward().y, -0.99f);
        camera.SetView(EditorCamera::View::Perspective);
        EXPECT_FALSE(camera.IsOrthographic());
        EXPECT_FLOAT_EQ(camera.GetProjectionMatrix()[3][3], 0.0f);
    }

    TEST(Bounds, ExpandAndTransform)
    {
        Bounds bounds;
        EXPECT_FALSE(bounds.IsValid());
        bounds.Expand({-1, -1, -1});
        bounds.Expand({1, 1, 1});
        EXPECT_TRUE(bounds.IsValid());
        EXPECT_NEAR(bounds.Radius(), std::sqrt(3.0f), 1e-5f);
        const auto moved = bounds.Transformed(glm::scale(glm::translate(glm::mat4(1.0f), {10, 0, 0}), {2, 1, 1}));
        EXPECT_TRUE(Near(moved.Min, {8, -1, -1}));
        EXPECT_TRUE(Near(moved.Max, {12, 1, 1}));
        EXPECT_FALSE(Bounds().Transformed(glm::mat4(1.0f)).IsValid());
    }
} // namespace
