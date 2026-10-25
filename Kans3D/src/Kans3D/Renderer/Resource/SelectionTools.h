#pragma once
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <glm/glm.hpp>
#include "Kans3D/Asset/AssetError.h"
#include "RenderAssetCache.h"

namespace Kans
{
    // One mesh to draw with a model-space transform and an identifier.
    // Borrows its GPU resource. Draw immediately: a result from AppendModelDraws is only
    // valid until the asset cache is next updated (frame eviction, Clear, or a resolving request).
    // Store model descriptions instead when queuing work across cache updates.
    struct MeshDraw
    {
        const GpuMesh* Mesh = nullptr;
        glm::mat4      Transform {1.0f};
        uint32_t       ID = 0; // Non-zero.
    };

    struct PickHit
    {
        uint32_t                 ID = 0;
        std::optional<glm::vec3> Position;
    };

    // Minimal OpenGL program compiled from GLSL source (requires a current context).
    class GlProgram
    {
    public:
        GlProgram() = default;
        ~GlProgram();
        GlProgram(const GlProgram&)            = delete;
        GlProgram& operator=(const GlProgram&) = delete;

        [[nodiscard]] std::variant<std::monostate, AssetError> Build(const std::string& vertex,
                                                                     const std::string& fragment);
        bool     IsValid() const { return m_Program != 0; }
        void     Bind() const;
        int      Location(const char* name) const;
        uint32_t Handle() const { return m_Program; }

    private:
        uint32_t m_Program = 0;
    };

    // Finds what is under a pixel by rendering the draws' IDs into an integer
    // target; only the requested pixel is rasterized.
    class EntityPicker
    {
    public:
        ~EntityPicker();
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
        GlProgram  m_Program;
        uint32_t   m_Framebuffer = 0, m_Color = 0, m_Depth = 0;
        glm::uvec2 m_Size {0, 0};
    };

    // Outlines draws in screen space: renders their silhouette into a mask and
    // composites a border (and a faint fill) over the bound framebuffer.
    class SelectionOutline
    {
    public:
        ~SelectionOutline();
        void Draw(const std::vector<MeshDraw>& draws,
                  const glm::mat4&             viewProjection,
                  glm::uvec2                   viewport,
                  const glm::vec4&             color,
                  float                        width = 2.0f);

    private:
        void       Resize(glm::uvec2 size);
        GlProgram  m_Mask, m_Composite;
        uint32_t   m_Framebuffer = 0, m_Texture = 0, m_EmptyVertexArray = 0;
        glm::uvec2 m_Size {0, 0};
    };
} // namespace Kans
