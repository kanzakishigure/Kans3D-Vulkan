#include "kspch.h"
#include "RenderFeature.h"
#include <stdexcept>

namespace Kans
{
    void RenderFeatureSet::Register(std::string name, std::unique_ptr<RenderFeature> feature, bool enabled)
    {
        if (name.empty() || !feature)
            throw std::invalid_argument("RenderFeature: invalid registration");
        for (const auto& entry : m_Entries)
            if (entry.Name == name)
                throw std::invalid_argument("RenderFeature: duplicate registration '" + name + "'");
        m_Entries.push_back({std::move(name), std::move(feature), enabled});
    }
    void RenderFeatureSet::SetEnabled(const std::string& name, bool enabled)
    {
        for (auto& entry : m_Entries)
            if (entry.Name == name)
            {
                entry.Enabled = enabled;
                return;
            }
        throw std::invalid_argument("RenderFeature: unknown feature '" + name + "'");
    }
    bool RenderFeatureSet::IsEnabled(const std::string& name) const
    {
        for (const auto& entry : m_Entries)
            if (entry.Name == name)
                return entry.Enabled;
        throw std::invalid_argument("RenderFeature: unknown feature '" + name + "'");
    }
    void RenderFeatureSet::Prepare(RenderView& view, RenderFrame& frame)
    {
        for (auto& entry : m_Entries)
            if (entry.Enabled)
                entry.Feature->Prepare(view, frame);
    }
    void RenderFeatureSet::Inject(RenderExtensionPoint point, const RenderFeatureContext& context)
    {
        for (auto& entry : m_Entries)
            if (entry.Enabled && entry.Feature->RunsAt(point))
            {
                const auto first = context.Graph.PassCount();
                entry.Feature->AddPasses(point, context);
                // Order without retaining unused feature outputs. No temporary callback/list copies.
                for (size_t pass = first; pass < context.Graph.PassCount(); ++pass)
                    if (pass)
                        context.Graph.OrderAfter(GraphPass(pass), GraphPass(pass - 1));
            }
    }
} // namespace Kans
