#pragma once
#include "Kans3D/Renderer/RenderFeature.h"
namespace Kans
{
    // Draws tagged passes of the pipeline's prepared, visible material draw list.
    class MaterialPassFeature final : public RenderFeature
    {
    public:
        explicit MaterialPassFeature(std::string          tag,
                                     RenderExtensionPoint point = RenderExtensionPoint::AfterTransparent);
        bool RunsAt(RenderExtensionPoint point) const override { return point == m_Point; }
        void AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context) override;

    private:
        std::string          m_Tag;
        RenderExtensionPoint m_Point;
    };
} // namespace Kans
