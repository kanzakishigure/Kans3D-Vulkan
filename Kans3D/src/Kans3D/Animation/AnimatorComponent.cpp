#include "kspch.h"
#include "AnimatorComponent.h"
#include "AnimationProduct.h"
namespace Kans
{
    AnimatorComponent::AnimatorComponent(const AnimatorComponent& c) :
        Skeleton(c.Skeleton), Controller(c.Controller), InlineController(c.InlineController), Enabled(c.Enabled),
        ApplyRootMotion(c.ApplyRootMotion), Speed(c.Speed), Parameters(c.Parameters), SkeletonSource(c.SkeletonSource),
        ControllerSource(c.ControllerSource), ClipSources(c.ClipSources), ActionSources(c.ActionSources)
    {}
    AnimatorComponent& AnimatorComponent::operator=(const AnimatorComponent& c)
    {
        if (this != &c)
        {
            AnimatorComponent copy(c);
            *this = std::move(copy);
        }
        return *this;
    }
    bool operator==(const AnimatorComponent& a, const AnimatorComponent& b)
    {
        if (a.Skeleton != b.Skeleton || a.Controller != b.Controller || a.Enabled != b.Enabled ||
            a.ApplyRootMotion != b.ApplyRootMotion || a.Speed != b.Speed || a.Parameters != b.Parameters ||
            a.InlineController.has_value() != b.InlineController.has_value())
            return false;
        if (!a.InlineController)
            return true;
        auto        x  = EncodeAnimationControllerProduct(*a.InlineController),
                    y  = EncodeAnimationControllerProduct(*b.InlineController);
        const auto* xb = std::get_if<std::vector<std::byte>>(&x);
        const auto* yb = std::get_if<std::vector<std::byte>>(&y);
        return xb && yb && *xb == *yb;
    }
} // namespace Kans
