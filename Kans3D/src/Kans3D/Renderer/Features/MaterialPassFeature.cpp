#include "kspch.h"
#include "MaterialPassFeature.h"
#include "Kans3D/Renderer/RenderFrame.h"
namespace Kans
{
    MaterialPassFeature::MaterialPassFeature(std::string tag, RenderExtensionPoint point) :
        m_Tag(std::move(tag)), m_Point(point)
    {
        if (m_Tag.empty())
            throw std::invalid_argument("MaterialPassFeature requires a tag");
    }
    void MaterialPassFeature::AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        auto* renderer = context.Frame;
        if (!renderer || !renderer->HasMaterialPass(context.View, m_Tag))
            return;
        auto       uses    = context.Target;
        uses.push_back(
            {context.Graph.ImportTexture("SceneHDR", renderer->Output.GetHDR()), GraphAccess::ColorReadWrite});
        const auto sampled = [&](const RenderHardware::Handle<RenderHardware::Texture>& image) {
            if (!image)
                return;
            auto texture = context.Graph.ImportTexture("MaterialPassTexture", image);
            if (std::none_of(uses.begin(), uses.end(), [&](auto use) { return use.Texture == texture; }))
                uses.push_back({texture, GraphAccess::SampledRead});
        };
        for (const auto& image : renderer->GetMaterialEnvironment())
            sampled(image);
        const auto recordStart = RenderClockMilliseconds();
        auto commands = renderer->GetDevice()->CreateCommandList();
        renderer->DrawShaderMaterials(*commands, sampled, m_Tag, &context.View);
        auto pass = context.Graph.AddRecordedPass(
            "Material passes: " + m_Tag, std::move(uses), std::move(commands), RenderClockMilliseconds() - recordStart);
        if (pass)
            context.Graph.OrderAfter(pass, pass - 1);
    }
} // namespace Kans
