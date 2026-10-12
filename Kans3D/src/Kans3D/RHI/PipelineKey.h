#pragma once
#include <string>
#include "RHI.h"
namespace Kans::RenderHardware::Detail
{
    // Serialize individual fields, never padding. This is also the collision
    // comparison key; a hash collision cannot merge different descriptions.
    inline std::string PipelineKey(const PipelineDesc& d)
    {
        std::string key;
        auto        add = [&](const auto& value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
        auto        shader = d.Program.get();
        add(shader);
        add(d.PushConstantBytes);
        add(d.Primitive);
        add(d.Rasterizer.Cull);
        add(d.Rasterizer.Front);
        add(d.Rasterizer.Polygon);
        add(d.Rasterizer.DepthBiasConstant);
        add(d.Rasterizer.DepthBiasSlope);
        add(d.DepthStencil.DepthTest);
        add(d.DepthStencil.DepthWrite);
        add(d.DepthStencil.DepthCompare);
        add(d.DepthStencil.StencilTest);
        for (auto face : {d.DepthStencil.Front, d.DepthStencil.Back})
        {
            add(face.Compare);
            add(face.Fail);
            add(face.DepthFail);
            add(face.Pass);
            add(face.Reference);
            add(face.ReadMask);
            add(face.WriteMask);
        }
        add(d.HasDepthStencil);
        if (d.HasDepthStencil)
            add(d.DepthStencilFormat);
        add(d.GroupLayouts.size());
        for (const auto& layout : d.GroupLayouts)
        {
            const auto& l = layout->GetDesc();
            add(l.Group);
            add(l.Entries.size());
            for (auto e : l.Entries)
            {
                add(e.Slot);
                add(e.Type);
            }
        }
        add(d.VertexBindings.size());
        for (auto b : d.VertexBindings)
        {
            add(b.Slot);
            add(b.Stride);
            add(b.PerInstance);
        }
        add(d.Attributes.size());
        for (auto a : d.Attributes)
        {
            add(a.Location);
            add(a.BufferSlot);
            add(a.Offset);
            add(a.Type);
        }
        add(d.ColorFormats.size());
        for (auto f : d.ColorFormats)
            add(f);
        add(d.Blends.size());
        for (auto b : d.Blends)
        {
            add(b.Enabled);
            add(b.SourceColor);
            add(b.DestinationColor);
            add(b.ColorOp);
            add(b.SourceAlpha);
            add(b.DestinationAlpha);
            add(b.AlphaOp);
            add(b.WriteMask);
        }
        return key;
    }
} // namespace Kans::RenderHardware::Detail
