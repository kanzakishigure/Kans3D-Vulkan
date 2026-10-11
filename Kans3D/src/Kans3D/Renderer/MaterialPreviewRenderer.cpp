#include "kspch.h"
#include "MaterialPreviewRenderer.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "SceneRenderer.h"

namespace Kans
{
    namespace
    {
        // Includes state changed indirectly by mesh/texture/program/UBO uploads.
        struct PreviewState
        {
            GLint     DrawFBO, ReadFBO, Program, Array, ArrayBuffer, UniformBuffer, Renderbuffer, ActiveTexture;
            GLint     Viewport[4], Polygon[2], DepthFunc, CullFace, FrontFace;
            GLint     SrcRGB, DstRGB, SrcAlpha, DstAlpha, EquationRGB, EquationAlpha;
            GLboolean DepthWrite, ColorWrite[4], Blend;
            GLdouble  DepthRange[2];
            const std::array<GLenum, 11> Caps {GL_DEPTH_TEST,
                                               GL_CULL_FACE,
                                               GL_SCISSOR_TEST,
                                               GL_STENCIL_TEST,
                                               GL_FRAMEBUFFER_SRGB,
                                               GL_RASTERIZER_DISCARD,
                                               GL_COLOR_LOGIC_OP,
                                               GL_POLYGON_OFFSET_FILL,
                                               GL_PRIMITIVE_RESTART,
                                               GL_DITHER,
                                               GL_POLYGON_SMOOTH};
            std::array<GLboolean, 11>    Enabled;
            struct Unit
            {
                GLint Texture2D, Cube, Sampler;
            };
            std::array<Unit, 16> Units;
            struct Block
            {
                GLint   Buffer;
                GLint64 Start, Size;
            };
            std::array<Block, 12>        Blocks;
            const std::array<GLenum, 12> Stores {GL_PACK_ALIGNMENT,
                                                 GL_PACK_ROW_LENGTH,
                                                 GL_PACK_SKIP_ROWS,
                                                 GL_PACK_SKIP_PIXELS,
                                                 GL_PACK_SWAP_BYTES,
                                                 GL_PACK_LSB_FIRST,
                                                 GL_UNPACK_ALIGNMENT,
                                                 GL_UNPACK_ROW_LENGTH,
                                                 GL_UNPACK_SKIP_ROWS,
                                                 GL_UNPACK_SKIP_PIXELS,
                                                 GL_UNPACK_SWAP_BYTES,
                                                 GL_UNPACK_LSB_FIRST};
            std::array<GLint, 12>        PixelStore;
            GLint                        PackBuffer, UnpackBuffer, ClipCount;
            std::vector<GLboolean>       Clips;
            inline static thread_local PreviewState* Current = nullptr;
            PreviewState*                            Parent;
            RenderAssetCache*                        Cache;
            explicit PreviewState(RenderAssetCache* cache) : Parent(Current), Cache(cache)
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &DrawFBO);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &ReadFBO);
                glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Array);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &ArrayBuffer);
                glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &UniformBuffer);
                glGetIntegerv(GL_RENDERBUFFER_BINDING, &Renderbuffer);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &ActiveTexture);
                glGetIntegerv(GL_VIEWPORT, Viewport);
                glGetIntegerv(GL_POLYGON_MODE, Polygon);
                glGetIntegerv(GL_DEPTH_FUNC, &DepthFunc);
                glGetIntegerv(GL_CULL_FACE_MODE, &CullFace);
                glGetIntegerv(GL_FRONT_FACE, &FrontFace);
                glGetIntegeri_v(GL_BLEND_SRC_RGB, 0, &SrcRGB);
                glGetIntegeri_v(GL_BLEND_DST_RGB, 0, &DstRGB);
                glGetIntegeri_v(GL_BLEND_SRC_ALPHA, 0, &SrcAlpha);
                glGetIntegeri_v(GL_BLEND_DST_ALPHA, 0, &DstAlpha);
                glGetIntegeri_v(GL_BLEND_EQUATION_RGB, 0, &EquationRGB);
                glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, 0, &EquationAlpha);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthWrite);
                glGetBooleani_v(GL_COLOR_WRITEMASK, 0, ColorWrite);
                glGetDoublev(GL_DEPTH_RANGE, DepthRange);
                Blend = glIsEnabledi(GL_BLEND, 0);
                for (size_t i = 0; i < Caps.size(); ++i)
                    Enabled[i] = glIsEnabled(Caps[i]);
                for (size_t i = 0; i < Units.size(); ++i)
                {
                    glActiveTexture(GL_TEXTURE0 + GLuint(i));
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &Units[i].Texture2D);
                    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &Units[i].Cube);
                    glGetIntegeri_v(GL_SAMPLER_BINDING, GLuint(i), &Units[i].Sampler);
                }
                for (size_t i = 0; i < Blocks.size(); ++i)
                {
                    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, GLuint(i), &Blocks[i].Buffer);
                    glGetInteger64i_v(GL_UNIFORM_BUFFER_START, GLuint(i), &Blocks[i].Start);
                    glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, GLuint(i), &Blocks[i].Size);
                }
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &PackBuffer);
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &UnpackBuffer);
                for (size_t i = 0; i < Stores.size(); ++i)
                    glGetIntegerv(Stores[i], &PixelStore[i]);
                glGetIntegerv(GL_MAX_CLIP_DISTANCES, &ClipCount);
                for (int i = 0; i < ClipCount; ++i)
                    Clips.push_back(glIsEnabled(GL_CLIP_DISTANCE0 + i));
                Current = this;
            }
            void Prepare()
            {
                for (auto cap : Caps)
                    glDisable(cap);
                for (int i = 0; i < ClipCount; ++i)
                    glDisable(GL_CLIP_DISTANCE0 + i);
                for (size_t i = 0; i < Units.size(); ++i)
                    glBindSampler(GLuint(i), 0);
                glActiveTexture(GL_TEXTURE0);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                for (auto store : Stores)
                    glPixelStorei(store, store == GL_PACK_ALIGNMENT || store == GL_UNPACK_ALIGNMENT ? 1 : 0);
                glDisablei(GL_BLEND, 0);
                glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glDepthRange(0, 1);
                glDepthFunc(GL_LESS);
                glCullFace(GL_BACK);
                glFrontFace(GL_CCW);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            ~PreviewState()
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, DrawFBO);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFBO);
                glUseProgram(Program);
                glBindVertexArray(Array);
                glBindBuffer(GL_ARRAY_BUFFER, ArrayBuffer);
                glBindRenderbuffer(GL_RENDERBUFFER, Renderbuffer);
                glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
                glPolygonMode(GL_FRONT_AND_BACK, Polygon[0]);
                glDepthFunc(DepthFunc);
                glDepthMask(DepthWrite);
                glDepthRange(DepthRange[0], DepthRange[1]);
                glCullFace(CullFace);
                glFrontFace(FrontFace);
                glBlendFuncSeparatei(0, SrcRGB, DstRGB, SrcAlpha, DstAlpha);
                glBlendEquationSeparatei(0, EquationRGB, EquationAlpha);
                glColorMaski(0, ColorWrite[0], ColorWrite[1], ColorWrite[2], ColorWrite[3]);
                Blend ? glEnablei(GL_BLEND, 0) : glDisablei(GL_BLEND, 0);
                for (size_t i = 0; i < Caps.size(); ++i)
                    Enabled[i] ? glEnable(Caps[i]) : glDisable(Caps[i]);
                for (int i = 0; i < ClipCount; ++i)
                    Clips[i] ? glEnable(GL_CLIP_DISTANCE0 + i) : glDisable(GL_CLIP_DISTANCE0 + i);
                for (size_t i = 0; i < Units.size(); ++i)
                {
                    glActiveTexture(GL_TEXTURE0 + GLuint(i));
                    glBindTexture(GL_TEXTURE_2D, Units[i].Texture2D);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, Units[i].Cube);
                    glBindSampler(GLuint(i), Units[i].Sampler);
                }
                glActiveTexture(ActiveTexture);
                for (size_t i = 0; i < Blocks.size(); ++i)
                {
                    const auto& b = Blocks[i];
                    if (b.Buffer && b.Size)
                        glBindBufferRange(GL_UNIFORM_BUFFER, GLuint(i), b.Buffer, b.Start, b.Size);
                    else
                        glBindBufferBase(GL_UNIFORM_BUFFER, GLuint(i), b.Buffer);
                }
                glBindBuffer(GL_UNIFORM_BUFFER, UniformBuffer);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, PackBuffer);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, UnpackBuffer);
                for (size_t i = 0; i < Stores.size(); ++i)
                    glPixelStorei(Stores[i], PixelStore[i]);
                Current = Parent;
                // Thumbnail readback and malformed-shader fallbacks nest guards.
                // Only the outer guard has restored the caller bindings completely.
                // RHI-MIGRATION: retire RHI replacements after restoring native preview bindings.
                if (!Parent && Cache)
                    Cache->ReleaseRetiredResources();
            }
        };

        MeshVertexData Vertex(glm::vec3 p, glm::vec3 n, glm::vec2 uv, glm::vec4 t)
        {
            MeshVertexData v;
            v.Position = {p.x, p.y, p.z};
            v.Normal   = {n.x, n.y, n.z};
            v.UV       = {uv.x, uv.y};
            v.Tangent  = {t.x, t.y, t.z, t.w};
            return v;
        }
        MeshData Shape(MaterialPreviewShape shape)
        {
            MeshData mesh;
            if (shape == MaterialPreviewShape::Sphere)
            {
                constexpr uint32_t rings = 32, segments = 64;
                constexpr float    pi = 3.14159265358979323846f;
                for (uint32_t y = 0; y <= rings; ++y)
                    for (uint32_t x = 0; x <= segments; ++x)
                    {
                        float     u = float(x) / segments, v = float(y) / rings;
                        float     a = u * 2 * pi, b = v * pi;
                        glm::vec3 n {std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a)};
                        // +U follows longitude; cross(N,T) points south, along +V.
                        mesh.Vertices.push_back(Vertex(n, n, {u, v}, {-std::sin(a), 0, std::cos(a), 1}));
                    }
                for (uint32_t y = 0; y < rings; ++y)
                    for (uint32_t x = 0; x < segments; ++x)
                    {
                        uint32_t a = y * (segments + 1) + x, b = a + segments + 1;
                        if (y != 0)
                            mesh.Indices.insert(mesh.Indices.end(), {a, a + 1, b});
                        if (y != rings - 1)
                            mesh.Indices.insert(mesh.Indices.end(), {a + 1, b + 1, b});
                    }
            }
            else
            {
                const auto face = [&](glm::vec3 n, glm::vec3 t, glm::vec3 b) {
                    uint32_t  start  = uint32_t(mesh.Vertices.size());
                    glm::vec3 center = shape == MaterialPreviewShape::Plane ? glm::vec3(0) : n * 0.75f;
                    float     half   = shape == MaterialPreviewShape::Plane ? 1.0f : 0.75f;
                    for (auto uv : {glm::vec2 {0, 0}, {1, 0}, {1, 1}, {0, 1}})
                        mesh.Vertices.push_back(
                            Vertex(center + half * ((2 * uv.x - 1) * t + (2 * uv.y - 1) * b), n, uv, glm::vec4(t, 1)));
                    mesh.Indices.insert(mesh.Indices.end(), {start, start + 1, start + 2, start + 2, start + 3, start});
                };
                face({0, 0, 1}, {1, 0, 0}, {0, 1, 0});
                if (shape == MaterialPreviewShape::Cube)
                {
                    face({0, 0, -1}, {-1, 0, 0}, {0, 1, 0});
                    face({1, 0, 0}, {0, 0, -1}, {0, 1, 0});
                    face({-1, 0, 0}, {0, 0, 1}, {0, 1, 0});
                    face({0, 1, 0}, {1, 0, 0}, {0, 0, -1});
                    face({0, -1, 0}, {1, 0, 0}, {0, 0, 1});
                }
            }
            return mesh;
        }
        bool SameShader(const ShaderData& a, const ShaderData& b)
        {
            if (a.TextureFallbacks != b.TextureFallbacks || a.Code.Modules.size() != b.Code.Modules.size())
                return false;
            for (size_t i = 0; i < a.Code.Modules.size(); ++i)
            {
                const auto& x = a.Code.Modules[i];
                const auto& y = b.Code.Modules[i];
                if (x.Stage != y.Stage || x.EntryPoint != y.EntryPoint || x.Words != y.Words)
                    return false;
            }
            return true;
        }
    } // namespace

    MaterialPreviewRenderer::~MaterialPreviewRenderer()
    {
        if (m_Framebuffer)
            glDeleteFramebuffers(1, &m_Framebuffer);
        if (m_Color)
            glDeleteTextures(1, &m_Color);
        if (m_Depth)
            glDeleteRenderbuffers(1, &m_Depth);
        if (m_EmptyArray)
            glDeleteVertexArrays(1, &m_EmptyArray);
    }

    bool MaterialPreviewRenderer::Resize(glm::uvec2 size)
    {
        if (!size.x || !size.y || size.x > 4096 || size.y > 4096)
            return false;
        if (size == m_Size && m_Framebuffer)
            return true;
        if (!m_Framebuffer)
        {
            glGenFramebuffers(1, &m_Framebuffer);
            glGenTextures(1, &m_Color);
            glGenRenderbuffers(1, &m_Depth);
        }
        glBindTexture(GL_TEXTURE_2D, m_Color);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(size.x), GLsizei(size.y), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, m_Depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, GLsizei(size.x), GLsizei(size.y));
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_Depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;
        m_Size = size;
        return true;
    }

    MaterialPreviewResult MaterialPreviewRenderer::Render(const MaterialData&           material,
                                                          const ShaderData*             source,
                                                          const MaterialPreviewOptions& options)
    {
        if (!m_Scene.GetAssetCache())
            return {};
        PreviewState state(m_Scene.GetAssetCache());
        state.Prepare();
        auto&            cache    = *m_Scene.GetAssetCache();
        const GpuShader* shader   = nullptr;
        bool             complete = true;
        if (source && !IsStandardShader(material.Shader))
        {
            if (!m_ShaderSource || !SameShader(*m_ShaderSource, *source))
            {
                m_ShaderSource = *source;
                m_Shader.reset();
                auto built = GpuShader::Create(*source);
                if (auto* value = std::get_if<GpuShader>(&built))
                    m_Shader = CreateScope<GpuShader>(std::move(*value));
            }
            shader = m_Shader.get();
        }
        MaterialData data = material;
        if (!shader)
        {
            complete = IsStandardShader(material.Shader);
            if (!complete)
            {
                data        = MaterialData {};
                data.Render = material.Render;
            }
            auto resolved = ResolveStandardMaterial(data);
            if (std::holds_alternative<AssetError>(resolved))
                return {};
            data   = std::get<MaterialData>(std::move(resolved));
            source = &GetStandardShader();
            shader = cache.GetShader(StandardShaderID());
        }
        if (!shader)
            return {};
        auto packed = MakeGpuMaterialBindings(data, *source);
        if (auto* bindings = std::get_if<GpuMaterialBindings>(&packed))
            return Draw(*shader, *bindings, options, complete);
        // A malformed custom recipe takes the same Standard fallback as the scene.
        if (!IsStandardShader(material.Shader))
        {
            MaterialData fallback;
            fallback.Render = material.Render;
            return Render(fallback, nullptr, options).Texture ? MaterialPreviewResult {m_Color, false} :
                                                                MaterialPreviewResult {};
        }
        return {};
    }

    MaterialPreviewResult MaterialPreviewRenderer::Render(AssetID material, const MaterialPreviewOptions& options)
    {
        if (!m_Scene.GetAssetCache())
            return {};
        PreviewState state(m_Scene.GetAssetCache());
        state.Prepare();
        auto& cache    = *m_Scene.GetAssetCache();
        auto  status   = cache.GetAssetManager().Request<MaterialDataAsset>(material);
        bool  complete = status.IsReady();
        if (status.Asset && !IsStandardShader(status.Asset->GetData().Shader))
            complete &= cache.GetAssetManager().Request<ShaderDataAsset>(status.Asset->GetData().Shader).IsReady();
        const auto* bindings = cache.GetMaterialBindings(material);
        const auto* shader   = bindings ? cache.GetShader(bindings->Shader) : nullptr;
        if (!bindings || !shader)
            return {};
        if (status.Asset && !IsStandardShader(status.Asset->GetData().Shader))
            complete &= bindings->Shader == status.Asset->GetData().Shader;
        return Draw(*shader, *bindings, options, complete);
    }

    MaterialPreviewResult MaterialPreviewRenderer::Draw(const GpuShader&              shader,
                                                        const GpuMaterialBindings&    bindings,
                                                        const MaterialPreviewOptions& options,
                                                        bool                          complete)
    {
        const auto shape = size_t(options.Shape);
        if (shape >= m_Shapes.size() || !std::isfinite(options.Rotation.x) || !std::isfinite(options.Rotation.y) ||
            !Resize(options.Size))
            return {};
        if (!m_Shapes[shape])
        {
            auto uploaded = UploadMesh(Shape(options.Shape));
            if (auto* mesh = std::get_if<GpuMesh>(&uploaded))
                m_Shapes[shape] = std::move(*mesh);
            else
                return {};
        }
        if (!m_Checker.IsValid())
        {
            auto built = m_Checker.Build(R"(#version 430 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2 - 1, 0, 1);
})",
                                         R"(#version 430 core
layout(location=0) out vec4 color;
void main() {
    float c = mod(floor(gl_FragCoord.x / 16) + floor(gl_FragCoord.y / 16), 2);
    color = vec4(vec3(mix(0.22, 0.32, c)), 1);
})");
            if (std::holds_alternative<AssetError>(built))
                return {};
            glGenVertexArrays(1, &m_EmptyArray);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, GLsizei(options.Size.x), GLsizei(options.Size.y));
        const GLfloat depth = 1;
        glClearBufferfv(GL_DEPTH, 0, &depth);
        glBindVertexArray(m_EmptyArray);
        m_Checker.Bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        if (bindings.Render.AlphaMode == MaterialAlphaMode::Blend)
        {
            glEnablei(GL_BLEND, 0);
            glBlendEquationSeparatei(0, GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDepthMask(GL_FALSE);
        }
        if (!bindings.Render.DoubleSided)
            glEnable(GL_CULL_FACE);
        if (!m_Frame)
        {
            m_Frame  = CreateScope<GpuUniformBuffer>(sizeof(EngineFrameUniforms));
            m_Object = CreateScope<GpuUniformBuffer>(sizeof(EngineObjectUniforms));
        }
        EngineFrameUniforms frame;
        frame.CameraPosition = {0, 0, 3.5f, 1};
        frame.ViewProjection =
            glm::perspective(glm::radians(45.0f), float(options.Size.x) / options.Size.y, 0.1f, 20.0f) *
            glm::lookAt(glm::vec3(frame.CameraPosition), glm::vec3(0), glm::vec3(0, 1, 0));
        frame.LightPositions[0] = {-3, 4, 5, 1};
        frame.LightColors[0]    = {40, 40, 40, 1};
        frame.LightDiffuse[0]   = {1, 1, 1, 1};
        frame.Time              = m_Scene.GetElapsedTime();
        m_Frame->Update(&frame, sizeof(frame));
        // RHI-MIGRATION: immediate preview binds the RHI frame buffer through its compatibility shim.
        m_Frame->Bind(GpuShader::FrameBlockPoint);
        EngineObjectUniforms object;
        object.Transform      = glm::rotate(glm::mat4(1), options.Rotation.y, {1, 0, 0}) *
                                glm::rotate(glm::mat4(1), options.Rotation.x, {0, 1, 0});
        object.NormalMatrix   = glm::transpose(glm::inverse(object.Transform));
        object.MaterialRender = {float(bindings.Render.AlphaMode),
                                 bindings.Render.AlphaCutoff,
                                 bindings.Render.DoubleSided ? 1.0f : 0.0f,
                                 0};
        m_Object->Update(&object, sizeof(object));
        // RHI-MIGRATION: immediate preview binds the RHI object buffer through its compatibility shim.
        m_Object->Bind(GpuShader::ObjectBlockPoint);
        const auto& environment = m_Scene.GetEnvironment();
        const auto& brdfLUT     = m_Scene.GetBrdfLUT();
        // Explicitly unbind absent environment maps, never sample an ImGui texture by accident.
        glActiveTexture(GL_TEXTURE0 + GpuShader::IrradianceUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment.IrradianceMap ? environment.IrradianceMap->GetRenererID() : 0);
        glActiveTexture(GL_TEXTURE0 + GpuShader::PrefilterUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment.RadianceMap ? environment.RadianceMap->GetRenererID() : 0);
        glActiveTexture(GL_TEXTURE0 + GpuShader::BrdfLUTUnit);
        glBindTexture(GL_TEXTURE_2D, brdfLUT ? brdfLUT->GetRenererID() : 0);
        // RHI-MIGRATION: bind the RHI program and material buffers for the immediate preview.
        shader.Bind();
        for (const auto& [binding, buffer] : bindings.Blocks)
            buffer.Bind(shader.MaterialBlockPoints().at(binding));
        // RHI-MIGRATION: native material texture bindings from the RHI group 1 resources.
        complete &= m_Scene.GetAssetCache()->BindMaterialTextures(shader, bindings);
        const auto& mesh = *m_Shapes[shape];
        // RHI-MIGRATION: preview geometry uses the RHI mesh VAO compatibility view.
        mesh.VertexArray->Bind();
        glDrawElements(GL_TRIANGLES, GLsizei(mesh.IndexCount), GL_UNSIGNED_INT, nullptr);
        return {m_Color, complete};
    }

    std::optional<MaterialThumbnail> MaterialPreviewRenderer::RenderThumbnail(AssetID                       material,
                                                                              const MaterialPreviewOptions& options)
    {
        PreviewState state(m_Scene.GetAssetCache());
        state.Prepare();
        const auto rendered = Render(material, options);
        if (!rendered.Texture || !rendered.Complete)
            return std::nullopt;
        MaterialThumbnail result;
        result.Size = options.Size;
        result.Pixels.resize(size_t(options.Size.x) * options.Size.y * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(
            0, 0, GLsizei(options.Size.x), GLsizei(options.Size.y), GL_RGBA, GL_UNSIGNED_BYTE, result.Pixels.data());
        const auto row = size_t(options.Size.x) * 4;
        for (size_t y = 0; y < options.Size.y / 2; ++y)
            std::swap_ranges(result.Pixels.begin() + y * row,
                             result.Pixels.begin() + (y + 1) * row,
                             result.Pixels.begin() + (options.Size.y - y - 1) * row);
        return result;
    }
} // namespace Kans
