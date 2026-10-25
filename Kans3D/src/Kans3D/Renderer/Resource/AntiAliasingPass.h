#pragma once
#include <map>
#include "Kans3D/RHI/RHI.h"
namespace Kans
{
    // Device-owned shader/pipeline cache; all images are supplied by the frame graph.
    class AntiAliasingPass
    {
    public:
        void Resolve(RenderHardware::Device&                                device,
                     RenderHardware::CommandList&                           commands,
                     const RenderHardware::Handle<RenderHardware::Texture>& source,
                     const RenderHardware::Handle<RenderHardware::Texture>& target,
                     bool                                                   hdr = true);
        void FXAA(RenderHardware::Device&                                device,
                  RenderHardware::CommandList&                           commands,
                  const RenderHardware::Handle<RenderHardware::Texture>& source,
                  const RenderHardware::Handle<RenderHardware::Texture>& target);

    private:
        void Record(RenderHardware::Device&,
                    RenderHardware::CommandList&,
                    const RenderHardware::Handle<RenderHardware::Texture>&,
                    const RenderHardware::Handle<RenderHardware::Texture>&,
                    unsigned kind);
        RenderHardware::Handle<RenderHardware::BindGroupLayout> m_Layout;
        RenderHardware::Handle<RenderHardware::Sampler>         m_Sampler;
        std::map<std::pair<unsigned, RenderHardware::Format>, RenderHardware::Handle<RenderHardware::Pipeline>>
            m_Pipelines;
    };
} // namespace Kans
