#include "kspch.h"
#include "SelectionTools.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace Kans
{
    namespace
    {
        const char* const PositionVertex = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
void main() { gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0); }
)";
        const char* const IdFragment = R"(#version 430 core
uniform uint u_ID;
layout(location = 0) out uint o_ID;
void main() { o_ID = u_ID; }
)";
        const char* const MaskFragment = R"(#version 430 core
layout(location = 0) out float o_Mask;
void main() { o_Mask = 1.0; }
)";
        const char* const FullscreenVertex = R"(#version 430 core
out vec2 v_UV;
void main()
{
    const vec2 corners[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    v_UV        = corners[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(corners[gl_VertexID], 0.0, 1.0);
}
)";
        const char* const CompositeFragment = R"(#version 430 core
in vec2 v_UV;
uniform sampler2D u_Mask;
uniform vec4      u_Color;
uniform int       u_Width;
layout(location = 0) out vec4 o_Color;
void main()
{
    vec2 texel = 1.0 / vec2(textureSize(u_Mask, 0));
    if (texture(u_Mask, v_UV).r > 0.5)
    {
        // Faint fill over the selection.
        o_Color = vec4(u_Color.rgb, u_Color.a * 0.12);
        return;
    }
    for (int y = -u_Width; y <= u_Width; ++y)
        for (int x = -u_Width; x <= u_Width; ++x)
            if (texture(u_Mask, v_UV + vec2(x, y) * texel).r > 0.5)
            {
                o_Color = u_Color;
                return;
            }
    discard;
}
)";

        std::string ShaderLog(GLuint shader)
        {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(size_t(std::max(length, 1)), '\0');
            glGetShaderInfoLog(shader, length, nullptr, log.data());
            return log;
        }

        // Draws meshes with `program` (u_ViewProjection, u_Transform, optional u_ID).
        void DrawMeshes(const GlProgram& program, const std::vector<MeshDraw>& draws, const glm::mat4& viewProjection)
        {
            program.Bind();
            glUniformMatrix4fv(program.Location("u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
            const int transform = program.Location("u_Transform"), id = program.Location("u_ID");
            for (const auto& draw : draws)
            {
                if (!draw.Mesh || !draw.Mesh->VertexArray)
                    continue;
                glUniformMatrix4fv(transform, 1, GL_FALSE, glm::value_ptr(draw.Transform));
                if (id >= 0)
                    glUniform1ui(id, draw.ID);
                draw.Mesh->VertexArray->Bind();
                glDrawElements(GL_TRIANGLES, GLsizei(draw.Mesh->IndexCount), GL_UNSIGNED_INT, nullptr);
            }
            glBindVertexArray(0);
        }

        // Saves the state these tools change and restores it on scope exit.
        struct GlStateGuard
        {
            GLint     Framebuffer = 0, ReadFramebuffer = 0, Program = 0, Viewport[4] = {}, Scissor[4] = {};
            GLint     DepthFunction = GL_LESS;
            GLint     PolygonMode[2] {}, BlendSourceRGB, BlendDestinationRGB, BlendSourceAlpha, BlendDestinationAlpha;
            GLint     BlendEquationRGB, BlendEquationAlpha;
            GLboolean DepthMask = GL_TRUE;
            GLdouble  DepthRange[2] = {}, ClearDepth = 1.0;
            GLboolean Depth = GL_FALSE, Blend = GL_FALSE, ScissorTest = GL_FALSE, Cull = GL_FALSE, Stencil = GL_FALSE;
            GlStateGuard()
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &Framebuffer);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &ReadFramebuffer);
                glGetIntegerv(GL_DEPTH_FUNC, &DepthFunction);
                glGetIntegerv(GL_POLYGON_MODE, PolygonMode);
                glGetIntegerv(GL_BLEND_SRC_RGB, &BlendSourceRGB);
                glGetIntegerv(GL_BLEND_DST_RGB, &BlendDestinationRGB);
                glGetIntegerv(GL_BLEND_SRC_ALPHA, &BlendSourceAlpha);
                glGetIntegerv(GL_BLEND_DST_ALPHA, &BlendDestinationAlpha);
                glGetIntegerv(GL_BLEND_EQUATION_RGB, &BlendEquationRGB);
                glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &BlendEquationAlpha);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthMask);
                glGetDoublev(GL_DEPTH_RANGE, DepthRange);
                glGetDoublev(GL_DEPTH_CLEAR_VALUE, &ClearDepth);
                glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
                glGetIntegerv(GL_VIEWPORT, Viewport);
                glGetIntegerv(GL_SCISSOR_BOX, Scissor);
                Depth       = glIsEnabled(GL_DEPTH_TEST);
                Blend       = glIsEnabled(GL_BLEND);
                ScissorTest = glIsEnabled(GL_SCISSOR_TEST);
                Cull        = glIsEnabled(GL_CULL_FACE);
                Stencil     = glIsEnabled(GL_STENCIL_TEST);
            }
            ~GlStateGuard()
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(Framebuffer));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(ReadFramebuffer));
                glDepthFunc(GLenum(DepthFunction));
                glDepthMask(DepthMask);
                glDepthRange(DepthRange[0], DepthRange[1]);
                glClearDepth(ClearDepth);
                glUseProgram(GLuint(Program));
                glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
                glScissor(Scissor[0], Scissor[1], Scissor[2], Scissor[3]);
                glPolygonMode(GL_FRONT_AND_BACK, PolygonMode[0]);
                glBlendFuncSeparate(BlendSourceRGB, BlendDestinationRGB, BlendSourceAlpha, BlendDestinationAlpha);
                glBlendEquationSeparate(BlendEquationRGB, BlendEquationAlpha);
                const auto set = [](GLenum cap, GLboolean on) { on ? glEnable(cap) : glDisable(cap); };
                set(GL_DEPTH_TEST, Depth);
                set(GL_BLEND, Blend);
                set(GL_SCISSOR_TEST, ScissorTest);
                set(GL_CULL_FACE, Cull);
                set(GL_STENCIL_TEST, Stencil);
            }
        };
    } // namespace

    // ---------------------------------------------------------------------- GlProgram

    GlProgram::~GlProgram()
    {
        if (m_Program)
            glDeleteProgram(m_Program);
    }

    std::variant<std::monostate, AssetError> GlProgram::Build(const std::string& vertex, const std::string& fragment)
    {
        const auto compile = [](GLenum stage, const std::string& source, GLuint& shader) -> std::optional<std::string> {
            shader            = glCreateShader(stage);
            const char* text  = source.c_str();
            glShaderSource(shader, 1, &text, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            return compiled == GL_TRUE ? std::nullopt : std::optional<std::string>(ShaderLog(shader));
        };
        GLuint vs = 0, fs = 0;
        auto   error = compile(GL_VERTEX_SHADER, vertex, vs);
        if (!error)
            error = compile(GL_FRAGMENT_SHADER, fragment, fs);
        if (!error)
        {
            const GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked == GL_TRUE)
            {
                if (m_Program)
                    glDeleteProgram(m_Program);
                m_Program = program;
            }
            else
            {
                error = "link failed";
                glDeleteProgram(program);
            }
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (error)
            return AssetError {AssetErrorCode::InvalidData, "Editor shader: " + error->substr(0, 1024)};
        return std::monostate {};
    }

    void GlProgram::Bind() const { glUseProgram(m_Program); }

    int GlProgram::Location(const char* name) const { return glGetUniformLocation(m_Program, name); }

    // ---------------------------------------------------------------------- EntityPicker

    EntityPicker::~EntityPicker()
    {
        if (m_Framebuffer)
        {
            glDeleteFramebuffers(1, &m_Framebuffer);
            glDeleteTextures(1, &m_Color);
            glDeleteRenderbuffers(1, &m_Depth);
        }
    }

    void EntityPicker::Resize(glm::uvec2 size)
    {
        if (size == m_Size && m_Framebuffer)
            return;
        if (!m_Framebuffer)
        {
            glGenFramebuffers(1, &m_Framebuffer);
            glGenTextures(1, &m_Color);
            glGenRenderbuffers(1, &m_Depth);
        }
        m_Size = size;
        glBindTexture(GL_TEXTURE_2D, m_Color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, GLsizei(size.x), GLsizei(size.y), 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindRenderbuffer(GL_RENDERBUFFER, m_Depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, GLsizei(size.x), GLsizei(size.y));
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_Depth);
    }

    uint32_t EntityPicker::Pick(const std::vector<MeshDraw>& draws,
                                const glm::mat4&             viewProjection,
                                glm::uvec2                   viewport,
                                glm::uvec2                   pixel)
    {
        return PickSurface(draws, viewProjection, viewport, pixel).ID;
    }

    PickHit EntityPicker::PickSurface(const std::vector<MeshDraw>& draws,
                                      const glm::mat4&             viewProjection,
                                      glm::uvec2                   viewport,
                                      glm::uvec2                   pixel)
    {
        if (draws.empty() || !viewport.x || !viewport.y || pixel.x >= viewport.x || pixel.y >= viewport.y)
            return {};
        GlStateGuard state;
        if (!m_Program.IsValid() && std::holds_alternative<AssetError>(m_Program.Build(PositionVertex, IdFragment)))
            return {};
        Resize(viewport);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, GLsizei(viewport.x), GLsizei(viewport.y));
        // Only the picked pixel matters.
        glEnable(GL_SCISSOR_TEST);
        glScissor(GLint(pixel.x), GLint(pixel.y), 1, 1);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDepthRange(0.0, 1.0);
        glClearDepth(1.0);
        const GLuint none = 0;
        glClearBufferuiv(GL_COLOR, 0, &none);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        DrawMeshes(m_Program, draws, viewProjection);
        PickHit hit;
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(GLint(pixel.x), GLint(pixel.y), 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &hit.ID);
        if (hit.ID)
        {
            GLfloat depth = 1.0f;
            glReadPixels(GLint(pixel.x), GLint(pixel.y), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            const glm::vec2 ndc = (glm::vec2(pixel) + 0.5f) / glm::vec2(viewport) * 2.0f - 1.0f;
            const glm::vec4 world = glm::inverse(viewProjection) * glm::vec4(ndc, depth * 2.0f - 1.0f, 1.0f);
            hit.Position = glm::vec3(world) / world.w;
        }
        return hit;
    }

    // ---------------------------------------------------------------------- SelectionOutline

    SelectionOutline::~SelectionOutline()
    {
        if (m_Framebuffer)
        {
            glDeleteFramebuffers(1, &m_Framebuffer);
            glDeleteTextures(1, &m_Texture);
        }
        if (m_EmptyVertexArray)
            glDeleteVertexArrays(1, &m_EmptyVertexArray);
    }

    void SelectionOutline::Resize(glm::uvec2 size)
    {
        if (size == m_Size && m_Framebuffer)
            return;
        if (!m_Framebuffer)
        {
            glGenFramebuffers(1, &m_Framebuffer);
            glGenTextures(1, &m_Texture);
        }
        m_Size = size;
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GLsizei(size.x), GLsizei(size.y), 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Texture, 0);
    }

    void SelectionOutline::Draw(const std::vector<MeshDraw>& draws,
                                const glm::mat4&             viewProjection,
                                glm::uvec2                   viewport,
                                const glm::vec4&             color,
                                float                        width)
    {
        if (draws.empty() || !viewport.x || !viewport.y)
            return;
        GlStateGuard state;
        if (!m_Mask.IsValid() && std::holds_alternative<AssetError>(m_Mask.Build(PositionVertex, MaskFragment)))
            return;
        if (!m_Composite.IsValid() &&
            std::holds_alternative<AssetError>(m_Composite.Build(FullscreenVertex, CompositeFragment)))
            return;
        if (!m_EmptyVertexArray)
            glGenVertexArrays(1, &m_EmptyVertexArray);
        Resize(viewport);

        // 1. Silhouette of the selection, ignoring depth so hidden parts show too.
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, GLsizei(viewport.x), GLsizei(viewport.y));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        const GLfloat clear[4] = {0, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, clear);
        DrawMeshes(m_Mask, draws, viewProjection);

        // 2. Border around the silhouette over the original target.
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(state.Framebuffer));
        glViewport(state.Viewport[0], state.Viewport[1], state.Viewport[2], state.Viewport[3]);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        m_Composite.Bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glUniform1i(m_Composite.Location("u_Mask"), 0);
        glUniform4fv(m_Composite.Location("u_Color"), 1, glm::value_ptr(color));
        glUniform1i(m_Composite.Location("u_Width"), std::clamp(int(width + 0.5f), 1, 6));
        glBindVertexArray(m_EmptyVertexArray);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }
} // namespace Kans
