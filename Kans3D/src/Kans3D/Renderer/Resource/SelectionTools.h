#pragma once
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include "RenderAssetCache.h"

namespace Kans
{
    // One mesh to draw with a model-space transform and an identifier.
    // Borrows its GPU resource. Draw immediately: a result from AppendEntityDraws is only
    // valid until the asset cache is next updated (frame eviction, Clear, or a resolving request).
    // Store model descriptions instead when queuing work across cache updates.
    struct MeshDraw
    {
        const GpuMesh* Mesh = nullptr;
        glm::mat4      Transform {1.0f};
        uint32_t       ID = 0; // Non-zero.
        uint32_t       Section = 0;
        RenderHardware::Handle<RenderHardware::Buffer> Palette;
        bool                                           IgnoreDepth = false, DoubleSided = true;
    };

    struct PickHit
    {
        uint32_t                 ID = 0;
        std::optional<glm::vec3> Position;
        std::optional<uint32_t>  MaterialSlot, Section;
    };

    // Raster IDs are draw indices + 1, independent of caller IDs (which may repeat).
    PickHit ResolveMeshPickHit(const std::vector<MeshDraw>& draws, uint32_t rasterID);

    // Finds what is under a pixel by rendering the draws' IDs into an integer
    // target; only the requested pixel is rasterized.
    class EntityPicker
    {
    public:
        explicit EntityPicker(std::shared_ptr<RenderHardware::Device> device = {}) : m_Device(std::move(device)) {}
        // Inject before first use; otherwise the first mesh provides the device.
        void SetDevice(std::shared_ptr<RenderHardware::Device> device) { m_Device = std::move(device); }
        EntityPicker(const EntityPicker&)            = delete;
        EntityPicker& operator=(const EntityPicker&) = delete;
        // `pixel` has a bottom-left origin. Returns 0 when nothing is there.
        uint32_t Pick(const std::vector<MeshDraw>& draws,
                      const glm::mat4&             viewProjection,
                      glm::uvec2                   viewport,
                      glm::uvec2                   pixel);
        // Same pixel convention as Pick; Position is empty when nothing is hit.
        PickHit PickSurface(const std::vector<MeshDraw>& draws,
                            const glm::mat4&             viewProjection,
                            glm::uvec2                   viewport,
                            glm::uvec2                   pixel);

    private:
        void       Resize(glm::uvec2 size);
        std::shared_ptr<RenderHardware::Device>          m_Device;
        RenderHardware::Handle<RenderHardware::Pipeline> m_Pipeline, m_SkinnedPipeline;
        RenderHardware::Handle<RenderHardware::Texture>  m_Color, m_Depth;
        glm::uvec2 m_Size {0, 0};
    };

    // Outlines draws in screen space: renders their silhouette into a mask and
    // composites a border (and a faint fill) inside the supplied RHI target.
    class SelectionOutline
    {
    public:
        explicit SelectionOutline(std::shared_ptr<RenderHardware::Device> device = {}) : m_Device(std::move(device)) {}
        // Inject before first use; otherwise the first mesh provides the device.
        void SetDevice(std::shared_ptr<RenderHardware::Device> device) { m_Device = std::move(device); }
        SelectionOutline(const SelectionOutline&)            = delete;
        SelectionOutline& operator=(const SelectionOutline&) = delete;
        // Composites onto colour attachment 0, loading its existing pixels.
        void Draw(const RenderHardware::RenderingDesc& target,
                  const std::vector<MeshDraw>&         draws,
                  const glm::mat4&                     viewProjection,
                  glm::uvec2                           viewport,
                  const glm::vec4&                     color,
                  float                                width = 2.0f);

        void RecordMask(RenderHardware::CommandList&                           commands,
                        const RenderHardware::Handle<RenderHardware::Texture>& mask,
                        const std::vector<MeshDraw>&                           draws,
                        const glm::mat4&                                       projection,
                        glm::uvec2                                             viewport,
                        RenderHardware::Handle<RenderHardware::Texture>        depth = {});
        void RecordComposite(RenderHardware::CommandList&                           commands,
                             const RenderHardware::RenderingDesc&                   target,
                             const RenderHardware::Handle<RenderHardware::Texture>& mask,
                             glm::uvec2                                             viewport,
                             const glm::vec4&                                       color,
                             float                                                  width            = 2.0f,
                             bool                                                   surfaceHighlight = false);

    private:
        void       Resize(glm::uvec2 size);
        std::shared_ptr<RenderHardware::Device>                 m_Device;
        RenderHardware::Handle<RenderHardware::Pipeline>        m_Mask, m_Composite, m_SkinnedPipeline, m_VisibleMask,
            m_VisibleSkinnedPipeline;
        RenderHardware::Handle<RenderHardware::BindGroupLayout> m_Layout;
        RenderHardware::Handle<RenderHardware::Sampler>         m_Sampler;
        RenderHardware::Handle<RenderHardware::Texture>         m_Texture;
        glm::uvec2 m_Size {0, 0};
    };
} // namespace Kans
