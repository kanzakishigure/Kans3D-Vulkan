#pragma once
#include "Kans3D/Renderer/RenderFeature.h"

namespace Kans
{
    class EditorGridFeature final : public RenderFeature
    {
    public:
        bool RunsAt(RenderExtensionPoint point) const override { return point == RenderExtensionPoint::Immediate; }
        void AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context) override;
    };
    class DebugGeometryFeature final : public RenderFeature
    {
    public:
        bool RunsAt(RenderExtensionPoint point) const override
        {
            return point == RenderExtensionPoint::AfterTransparent;
        }
        void AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context) override;
    };
    class EditorOutlineFeature : public RenderFeature
    {
    public:
        bool RunsAt(RenderExtensionPoint point) const override { return point == RenderExtensionPoint::Overlay; }

    protected:
        static void AddOutline(const RenderFeatureContext& context, std::string name, bool preview, glm::vec4 color);
    };
    class EditorSelectionFeature final : public EditorOutlineFeature
    {
    public:
        void AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context) override;
    };
    class EditorPreviewFeature final : public EditorOutlineFeature
    {
    public:
        void Prepare(RenderView& view, SceneRenderer& renderer) override;
        void AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context) override;
    };
    // Temporary compatibility policy. Move this one call from SceneRenderer's constructor
    // to editor view setup when the editor takes ownership of feature registration.
    void RegisterDefaultEditorFeatures(RenderFeatureSet& features);
} // namespace Kans
