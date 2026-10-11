#include "kspch.h"
#include "OpenGLDevice.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <spirv-tools/libspirv.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include "Kans3D/Asset/Runtime/ShaderData.h"
#include "OpenGLInterop.h"

namespace Kans::RenderHardware
{
    namespace
    {
        void Require(bool condition, const char* message)
        {
            if (!condition)
                throw std::invalid_argument(message);
        }
        template<class E>
        bool Has(E value, E flag)
        {
            return (uint32_t(value) & uint32_t(flag)) == uint32_t(flag);
        }
        constexpr uint32_t GroupCount = 2, PushPoint = 16;
        uint64_t           Point(uint32_t group, uint32_t slot)
        {
            Require(group < GroupCount, "Binding exceeds backend group capacity");
            return (uint64_t(group) << 32) | slot;
        }
        struct State
        {
            DeviceStats                        Stats;
            bool                               InFrame = false, Alive = true, PendingShaderWrites = false;
            struct Deletion
            {
                std::function<void()> Destroy;
                std::function<bool()> Bound;
            };
            std::vector<Deletion>              Deletions;
            std::vector<std::function<void()>> Retentions;
            void                               Retire(bool preserveBound = false)
            {
                glFinish();
                PendingShaderWrites = false;
                auto retirements    = std::move(Retentions);
                Retentions.clear();
                for (auto& retire : retirements)
                    retire();
                retirements.clear();
                auto work = std::move(Deletions);
                Deletions.clear();
                for (auto& destroy : work)
                {
                    if (preserveBound && destroy.Bound && destroy.Bound())
                    {
                        Deletions.push_back(std::move(destroy));
                        continue;
                    }
                    destroy.Destroy();
                    --Stats.LiveResources;
                    --Stats.PendingDestructions;
                }
            }
        };
        struct Native
        {
            std::shared_ptr<State>      Owner;
            GLuint                      ID = 0;
            std::function<void(GLuint)> Delete;
            std::function<bool(GLuint)> Bound;
            Native(std::shared_ptr<State>      state,
                   GLuint                      id,
                   std::function<void(GLuint)> destroy,
                   std::function<bool(GLuint)> bound = {}) :
                Owner(std::move(state)), ID(id), Delete(std::move(destroy)), Bound(std::move(bound))
            {
                ++Owner->Stats.LiveResources;
            }
            ~Native()
            {
                const auto id      = ID;
                const auto destroy = Delete;
                // Device teardown drains the queue; handles outliving it violate the contract.
                if (Owner->Alive)
                {
                    Owner->Deletions.push_back({[id, destroy] { destroy(id); },
                                                Bound ?
                                                    std::function<bool()>([id, bound = Bound] { return bound(id); }) :
                                                    std::function<bool()>()});
                    ++Owner->Stats.PendingDestructions;
                }
            }
        };
        void Range(size_t capacity, size_t offset, size_t bytes)
        {
            Require(offset <= capacity && bytes <= capacity - offset, "Buffer range exceeds allocation");
        }
        struct FormatInfo
        {
            GLenum   Internal, External, Type;
            uint32_t Bytes;
            bool     Depth = false, Stencil = false;
        };
        FormatInfo Info(Format format)
        {
            switch (format)
            {
                case Format::RGBA8Unorm:
                    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                case Format::RGBA8Srgb:
                    return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                case Format::RGBA16Float:
                    return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
                case Format::RGBA32Float:
                    return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
                case Format::RG8Unorm:
                    return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2};
                case Format::R32Uint:
                    return {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 4};
                case Format::R8Unorm:
                    return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
                case Format::Depth32Float:
                    return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4, true};
                case Format::Depth24Stencil8:
                    return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4, true, true};
            }
            throw std::invalid_argument("Unknown format");
        }
        uint32_t Width(const TextureDesc& d, uint32_t mip) { return std::max(1u, d.Width >> mip); }
        uint32_t Height(const TextureDesc& d, uint32_t mip) { return std::max(1u, d.Height >> mip); }
        size_t   TextureBytes(const TextureDesc& d, TextureSubresource sub)
        {
            Require(sub.Mip < d.MipLevels && sub.Face < (d.Dimension == TextureDimension::Cube ? 6u : 1u),
                    "Invalid texture subresource");
            return size_t(Width(d, sub.Mip)) * Height(d, sub.Mip) * Info(d.PixelFormat).Bytes;
        }
        bool BoundProgram(GLuint id)
        {
            GLint n;
            glGetIntegerv(GL_CURRENT_PROGRAM, &n);
            return GLuint(n) == id;
        }
        bool BoundTexture(GLuint id)
        {
            GLint active, count, n;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &count);
            bool bound = false;
            for (int i = 0; i < count; ++i)
            {
                glActiveTexture(GL_TEXTURE0 + i);
                for (auto target : {GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_CUBE_MAP})
                {
                    glGetIntegerv(target, &n);
                    bound |= GLuint(n) == id;
                }
            }
            glActiveTexture(active);
            return bound;
        }
        bool BoundBuffer(GLuint id)
        {
            GLint n, count;
            for (auto target : {GL_ARRAY_BUFFER_BINDING,
                                GL_ELEMENT_ARRAY_BUFFER_BINDING,
                                GL_UNIFORM_BUFFER_BINDING,
                                GL_PIXEL_PACK_BUFFER_BINDING,
                                GL_PIXEL_UNPACK_BUFFER_BINDING,
                                GL_COPY_READ_BUFFER_BINDING,
                                GL_COPY_WRITE_BUFFER_BINDING,
                                GL_DRAW_INDIRECT_BUFFER_BINDING,
                                GL_DISPATCH_INDIRECT_BUFFER_BINDING,
                                GL_SHADER_STORAGE_BUFFER_BINDING,
                                GL_ATOMIC_COUNTER_BUFFER_BINDING,
                                GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,
                                GL_TEXTURE_BUFFER_BINDING})
            {
                glGetIntegerv(target, &n);
                if (GLuint(n) == id)
                    return true;
            }
            for (auto pair : {std::pair {GL_UNIFORM_BUFFER_BINDING, GL_MAX_UNIFORM_BUFFER_BINDINGS},
                              std::pair {GL_SHADER_STORAGE_BUFFER_BINDING, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS},
                              std::pair {GL_ATOMIC_COUNTER_BUFFER_BINDING, GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS},
                              std::pair {GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, GL_MAX_TRANSFORM_FEEDBACK_BUFFERS}})
            {
                glGetIntegerv(pair.second, &count);
                for (int i = 0; i < count; ++i)
                {
                    glGetIntegeri_v(pair.first, i, &n);
                    if (GLuint(n) == id)
                        return true;
                }
            }
            return false;
        }
        struct BufferBindingGuard
        {
            GLenum Target;
            GLint  Previous;
            explicit BufferBindingGuard(GLenum target, GLenum binding) : Target(target)
            {
                glGetIntegerv(binding, &Previous);
            }
            ~BufferBindingGuard() { glBindBuffer(Target, Previous); }
        };
        struct VertexArrayGuard
        {
            GLint Previous;
            VertexArrayGuard() { glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Previous); }
            ~VertexArrayGuard() { glBindVertexArray(Previous); }
        };
        // Isolate replay from remaining immediate consumers and ImGui. This backend
        // boundary, rather than individual renderers, owns native state preservation.
        struct SubmissionState
        {
            GLint DrawFBO, ReadFBO, Program, Array, ArrayBuffer, UniformBuffer, StorageBuffer, Renderbuffer,
                ActiveTexture;
            GLint                        Viewport[4], Scissor[4], Polygon[2], DepthFunc, CullFace, FrontFace;
            GLboolean                    DepthWrite;
            GLdouble                     DepthRange[2];
            const std::array<GLenum, 12> Caps {GL_DEPTH_TEST,
                                               GL_CULL_FACE,
                                               GL_SCISSOR_TEST,
                                               GL_STENCIL_TEST,
                                               GL_FRAMEBUFFER_SRGB,
                                               GL_RASTERIZER_DISCARD,
                                               GL_COLOR_LOGIC_OP,
                                               GL_POLYGON_OFFSET_FILL,
                                               GL_PRIMITIVE_RESTART,
                                               GL_DITHER,
                                               GL_POLYGON_SMOOTH,
                                               GL_POLYGON_OFFSET_LINE};
            std::array<GLboolean, 12>    Enabled;
            struct Unit
            {
                GLint Texture2D, Cube, Sampler;
            };
            std::vector<Unit> Units;
            struct Block
            {
                GLint   Buffer;
                GLint64 Start, Size;
            };
            std::vector<Block>           Blocks;
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
            struct StencilState
            {
                GLint Function, Reference, ValueMask, WriteMask, Fail, DepthFail, DepthPass;
            };
            StencilState Front, Back;
            struct ColorState
            {
                GLint     SrcRGB, DstRGB, SrcAlpha, DstAlpha, EqRGB, EqAlpha;
                GLboolean Mask[4], Blend;
            };
            std::vector<ColorState> Colors;
            std::vector<Block>      Storage;
            GLfloat                 BiasFactor, BiasUnits;
            SubmissionState()
            {
                GLint count;
                glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &count);
                Units.resize(count);
                glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &count);
                Blocks.resize(count);

                glGetIntegerv(GL_MAX_DRAW_BUFFERS, &count);
                Colors.resize(count);
                for (size_t i = 0; i < Colors.size(); ++i)
                {
                    auto& c = Colors[i];
                    glGetIntegeri_v(GL_BLEND_SRC_RGB, i, &c.SrcRGB);
                    glGetIntegeri_v(GL_BLEND_DST_RGB, i, &c.DstRGB);
                    glGetIntegeri_v(GL_BLEND_SRC_ALPHA, i, &c.SrcAlpha);
                    glGetIntegeri_v(GL_BLEND_DST_ALPHA, i, &c.DstAlpha);
                    glGetIntegeri_v(GL_BLEND_EQUATION_RGB, i, &c.EqRGB);
                    glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, i, &c.EqAlpha);
                    glGetBooleani_v(GL_COLOR_WRITEMASK, i, c.Mask);
                    c.Blend = glIsEnabledi(GL_BLEND, i);
                }
                glGetIntegerv(GL_STENCIL_FUNC, &Front.Function);
                glGetIntegerv(GL_STENCIL_REF, &Front.Reference);
                glGetIntegerv(GL_STENCIL_VALUE_MASK, &Front.ValueMask);
                glGetIntegerv(GL_STENCIL_WRITEMASK, &Front.WriteMask);
                glGetIntegerv(GL_STENCIL_FAIL, &Front.Fail);
                glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &Front.DepthFail);
                glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &Front.DepthPass);
                glGetIntegerv(GL_STENCIL_BACK_FUNC, &Back.Function);
                glGetIntegerv(GL_STENCIL_BACK_REF, &Back.Reference);
                glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &Back.ValueMask);
                glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &Back.WriteMask);
                glGetIntegerv(GL_STENCIL_BACK_FAIL, &Back.Fail);
                glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, &Back.DepthFail);
                glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &Back.DepthPass);
                glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &BiasFactor);
                glGetFloatv(GL_POLYGON_OFFSET_UNITS, &BiasUnits);
                glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &count);
                Storage.resize(count);
                for (size_t i = 0; i < Storage.size(); ++i)
                {
                    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &Storage[i].Buffer);
                    glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &Storage[i].Start);
                    glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &Storage[i].Size);
                }
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &DrawFBO);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &ReadFBO);
                glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Array);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &ArrayBuffer);
                glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &UniformBuffer);
                glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &StorageBuffer);
                glGetIntegerv(GL_RENDERBUFFER_BINDING, &Renderbuffer);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &ActiveTexture);
                glGetIntegerv(GL_VIEWPORT, Viewport);
                glGetIntegerv(GL_SCISSOR_BOX, Scissor);
                glGetIntegerv(GL_POLYGON_MODE, Polygon);
                glGetIntegerv(GL_DEPTH_FUNC, &DepthFunc);
                glGetIntegerv(GL_CULL_FACE_MODE, &CullFace);
                glGetIntegerv(GL_FRONT_FACE, &FrontFace);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &DepthWrite);
                glGetDoublev(GL_DEPTH_RANGE, DepthRange);
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
            ~SubmissionState()
            {
                const auto stencil = [](GLenum face, const StencilState& state) {
                    glStencilFuncSeparate(face, state.Function, state.Reference, state.ValueMask);
                    glStencilMaskSeparate(face, state.WriteMask);
                    glStencilOpSeparate(face, state.Fail, state.DepthFail, state.DepthPass);
                };
                stencil(GL_FRONT, Front);
                stencil(GL_BACK, Back);
                glPolygonOffset(BiasFactor, BiasUnits);
                for (size_t i = 0; i < Colors.size(); ++i)
                {
                    const auto& c = Colors[i];
                    glBlendFuncSeparatei(i, c.SrcRGB, c.DstRGB, c.SrcAlpha, c.DstAlpha);
                    glBlendEquationSeparatei(i, c.EqRGB, c.EqAlpha);
                    glColorMaski(i, c.Mask[0], c.Mask[1], c.Mask[2], c.Mask[3]);
                    c.Blend ? glEnablei(GL_BLEND, i) : glDisablei(GL_BLEND, i);
                }
                for (size_t i = 0; i < Storage.size(); ++i)
                {
                    const auto& b = Storage[i];
                    if (b.Buffer && b.Size)
                        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, b.Buffer, b.Start, b.Size);
                    else
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, b.Buffer);
                }

                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, DrawFBO);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFBO);
                glUseProgram(Program);
                glBindVertexArray(Array);
                glBindBuffer(GL_ARRAY_BUFFER, ArrayBuffer);
                glBindRenderbuffer(GL_RENDERBUFFER, Renderbuffer);
                glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
                glScissor(Scissor[0], Scissor[1], Scissor[2], Scissor[3]);
                glPolygonMode(GL_FRONT_AND_BACK, Polygon[0]);
                glDepthFunc(DepthFunc);
                glDepthMask(DepthWrite);
                glDepthRange(DepthRange[0], DepthRange[1]);
                glCullFace(CullFace);
                glFrontFace(FrontFace);
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
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, StorageBuffer);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, PackBuffer);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, UnpackBuffer);
                for (size_t i = 0; i < Stores.size(); ++i)
                    glPixelStorei(Stores[i], PixelStore[i]);
            }
        };

        struct TransferState
        {
            GLenum                Target, Binding;
            GLint                 Texture = 0, Buffer = 0;
            std::array<GLenum, 8> Stores;
            std::array<GLint, 8>  Values;
            TransferState(GLenum target, bool pack) :
                Target(target), Binding(pack ? GL_PIXEL_PACK_BUFFER : GL_PIXEL_UNPACK_BUFFER)
            {
                glGetIntegerv(target == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_CUBE_MAP, &Texture);
                glGetIntegerv(pack ? GL_PIXEL_PACK_BUFFER_BINDING : GL_PIXEL_UNPACK_BUFFER_BINDING, &Buffer);
                Stores = pack ? std::array<GLenum, 8> {GL_PACK_ALIGNMENT,
                                                       GL_PACK_ROW_LENGTH,
                                                       GL_PACK_IMAGE_HEIGHT,
                                                       GL_PACK_SKIP_ROWS,
                                                       GL_PACK_SKIP_PIXELS,
                                                       GL_PACK_SKIP_IMAGES,
                                                       GL_PACK_SWAP_BYTES,
                                                       GL_PACK_LSB_FIRST} :
                                std::array<GLenum, 8> {GL_UNPACK_ALIGNMENT,
                                                       GL_UNPACK_ROW_LENGTH,
                                                       GL_UNPACK_IMAGE_HEIGHT,
                                                       GL_UNPACK_SKIP_ROWS,
                                                       GL_UNPACK_SKIP_PIXELS,
                                                       GL_UNPACK_SKIP_IMAGES,
                                                       GL_UNPACK_SWAP_BYTES,
                                                       GL_UNPACK_LSB_FIRST};
                for (size_t i = 0; i < Stores.size(); ++i)
                {
                    glGetIntegerv(Stores[i], &Values[i]);
                    glPixelStorei(Stores[i], i == 0 ? 1 : 0);
                }
            }
            ~TransferState()
            {
                for (size_t i = 0; i < Stores.size(); ++i)
                    glPixelStorei(Stores[i], Values[i]);
                glBindBuffer(Binding, Buffer);
                glBindTexture(Target, Texture);
            }
        };
        struct GLBuffer final : Buffer
        {
            BufferDesc Desc;
            Native     Name;
            size_t     PendingWrites = 0;
            GLBuffer(const std::shared_ptr<State>& state, BufferDesc desc, GLuint id) :
                Desc(desc), Name(state, id, [](GLuint n) { glDeleteBuffers(1, &n); }, BoundBuffer)
            {}
            const BufferDesc& GetDesc() const override { return Desc; }
            void              Write(size_t offset, const void* data, size_t bytes) override
            {
                Require(Desc.Memory == MemoryUsage::Upload && (data || !bytes), "Write requires upload memory");
                Range(Desc.Bytes, offset, bytes);
                BufferBindingGuard binding(GL_COPY_WRITE_BUFFER, GL_COPY_WRITE_BUFFER_BINDING);
                glBindBuffer(GL_COPY_WRITE_BUFFER, Name.ID);
                glBufferSubData(GL_COPY_WRITE_BUFFER, offset, bytes, data);
            }
            std::vector<uint8_t> Read(size_t offset, size_t bytes) const override
            {
                Require(Desc.Memory == MemoryUsage::Readback && PendingWrites == 0,
                        "Readback requires frame retirement or WaitIdle");
                Range(Desc.Bytes, offset, bytes);
                std::vector<uint8_t> result(bytes);
                BufferBindingGuard   binding(GL_COPY_READ_BUFFER, GL_COPY_READ_BUFFER_BINDING);
                glBindBuffer(GL_COPY_READ_BUFFER, Name.ID);
                if (bytes)
                {
                    const void* mapping = glMapBufferRange(GL_COPY_READ_BUFFER, offset, bytes, GL_MAP_READ_BIT);
                    if (!mapping)
                        throw std::runtime_error("Readback mapping failed");
                    std::memcpy(result.data(), mapping, bytes);
                    if (!glUnmapBuffer(GL_COPY_READ_BUFFER))
                        throw std::runtime_error("Readback mapping became invalid");
                }
                return result;
            }
        };
        struct GLTexture final : Texture
        {
            std::shared_ptr<void> BorrowedOwner;
            TextureDesc Desc;
            Native      Name;
            GLTexture(const std::shared_ptr<State>& state, TextureDesc desc, GLuint id) :
                Desc(desc), Name(state, id, [](GLuint n) { glDeleteTextures(1, &n); }, BoundTexture)
            {}
            const TextureDesc& GetDesc() const override { return Desc; }
            GLenum             Target() const
            {
                return Desc.Dimension == TextureDimension::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            }
            GLenum Face(uint32_t face) const
            {
                return Desc.Dimension == TextureDimension::Cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D;
            }
        };
        struct GLSampler final : Sampler
        {
            Native Name;
            GLSampler(const std::shared_ptr<State>& state, GLuint id) :
                Name(state, id, [](GLuint n) { glDeleteSamplers(1, &n); })
            {}
        };
        struct ShaderBinding
        {
            uint32_t         Group, Slot;
            BindingType      Type;
            size_t           Bytes;
            TextureDimension Dimension = TextureDimension::Texture2D;
        };
        struct TextureUnit
        {
            uint32_t Unit;
            uint64_t ImagePoint, SamplerPoint;
        };
        class ShaderCompiler final : public spirv_cross::CompilerGLSL
        {
        public:
            using spirv_cross::CompilerGLSL::CompilerGLSL;
            void EmulatePushBuffer(uint32_t id)
            {
                // Code generation must see uniform storage as well as a uniform
                // declaration, otherwise row-major push loads are transposed twice.
                get<spirv_cross::SPIRVariable>(id).storage = spv::StorageClassUniform;
                ir.for_each_typed_id<spirv_cross::SPIRType>([&](uint32_t typeID, const spirv_cross::SPIRType& type) {
                    if (type.storage == spv::StorageClassPushConstant)
                        get<spirv_cross::SPIRType>(typeID).storage = spv::StorageClassUniform;
                });
            }
        };
        struct PushCopy
        {
            uint32_t Source, Destination, Bytes;
        };
        struct PushShape
        {
            uint32_t              Alignment, Bytes;
            std::vector<PushCopy> Copies;
        };
        struct PushPackingCache
        {
            std::map<std::tuple<uint32_t, uint32_t, bool>, PushShape> Shapes;
            std::map<uint32_t, uint32_t>                              SourceArrayStrides;
        };
        uint32_t Aligned(uint32_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }
        // Retain byte offsets from the module, then give the cross-compiler a
        // uniform-compatible layout. Arrays/matrices in push data need repacking:
        // their source strides need not equal uniform-buffer strides.
        PushShape PackPushType(spirv_cross::CompilerGLSL& compiler,
                               uint32_t                   id,
                               uint32_t                   matrixStride,
                               bool                       rowMajor,
                               PushPackingCache&          cache)
        {
            const auto key = std::make_tuple(id, matrixStride, rowMajor);
            if (auto found = cache.Shapes.find(key); found != cache.Shapes.end())
                return found->second;
            const auto type = compiler.get_type(id);
            if (!type.array.empty())
            {
                Require(type.array.size() == 1 && type.array_size_literal[0] && type.array[0],
                        "Unsupported push constant array");
                const auto sourceStride =
                    cache.SourceArrayStrides.emplace(id, compiler.get_decoration(id, spv::DecorationArrayStride));
                const auto oldStride = sourceStride.first->second;
                auto       element   = PackPushType(compiler, type.parent_type, matrixStride, rowMajor, cache);
                const auto alignment = Aligned(element.Alignment, 16), stride = Aligned(element.Bytes, alignment);
                compiler.set_decoration(id, spv::DecorationArrayStride, stride);
                PushShape result {alignment, stride * type.array[0], {}};
                for (uint32_t i = 0; i < type.array[0]; ++i)
                    for (auto copy : element.Copies)
                        result.Copies.push_back(
                            {copy.Source + i * oldStride, copy.Destination + i * stride, copy.Bytes});
                cache.Shapes.emplace(key, result);
                return result;
            }
            if (type.basetype == spirv_cross::SPIRType::Struct)
            {
                PushShape result {16, 0, {}};
                for (uint32_t i = 0; i < type.member_types.size(); ++i)
                {
                    const auto source      = compiler.type_struct_member_offset(type, i);
                    const auto stride      = compiler.has_member_decoration(id, i, spv::DecorationMatrixStride) ?
                                                 compiler.get_member_decoration(id, i, spv::DecorationMatrixStride) :
                                                 0;
                    const bool row         = compiler.has_member_decoration(id, i, spv::DecorationRowMajor);
                    auto       member      = PackPushType(compiler, type.member_types[i], stride, row, cache);
                    result.Alignment       = std::max(result.Alignment, member.Alignment);
                    const auto destination = Aligned(result.Bytes, member.Alignment);
                    compiler.set_member_decoration(id, i, spv::DecorationOffset, destination);
                    if (stride)
                        compiler.set_member_decoration(id, i, spv::DecorationMatrixStride, 16);
                    for (auto copy : member.Copies)
                        result.Copies.push_back({copy.Source + source, copy.Destination + destination, copy.Bytes});
                    result.Bytes = destination + member.Bytes;
                }
                result.Bytes = Aligned(result.Bytes, result.Alignment);
                cache.Shapes.emplace(key, result);
                return result;
            }
            Require(type.width == 32 &&
                        (type.basetype == spirv_cross::SPIRType::Float || type.basetype == spirv_cross::SPIRType::Int ||
                         type.basetype == spirv_cross::SPIRType::UInt),
                    "Unsupported push constant scalar");
            if (type.columns > 1)
            {
                Require(matrixStride != 0, "Push constant matrix lacks stride");
                const auto count      = rowMajor ? type.vecsize : type.columns;
                const auto components = rowMajor ? type.columns : type.vecsize;
                PushShape  result {16, count * 16, {}};
                for (uint32_t i = 0; i < count; ++i)
                    result.Copies.push_back({i * matrixStride, i * 16, components * 4});
                cache.Shapes.emplace(key, result);
                return result;
            }
            const auto alignment = type.vecsize == 1 ? 4u : type.vecsize == 2 ? 8u : 16u;
            PushShape  result {alignment, type.vecsize * 4, {{0, 0, type.vecsize * 4}}};
            cache.Shapes.emplace(key, result);
            return result;
        }
        struct GLShader final : Shader
        {
            Native                     Name;
            uint32_t                   PushBytes = 0, PushUploadBytes = 0;
            std::vector<PushCopy>      PushCopies;
            std::vector<ShaderBinding> Bindings;
            std::vector<TextureUnit>   Units;
            std::map<uint64_t, uint32_t> UniformPoints, StoragePoints, CombinedUnits;
            GLShader(const std::shared_ptr<State>& state, GLuint id) :
                Name(state, id, [](GLuint n) { glDeleteProgram(n); }, BoundProgram)
            {}
            uint32_t GetPushConstantBytes() const override { return PushBytes; }
        };
        struct GLLayout final : BindGroupLayout
        {
            std::shared_ptr<State>     Owner;
            BindGroupLayoutDesc        Desc;
            const BindGroupLayoutDesc& GetDesc() const override { return Desc; }
        };
        struct GLGroup final : BindGroup
        {
            std::shared_ptr<State> Owner;
            BindGroupDesc          Desc;
        };
        struct GLPipeline final : Pipeline
        {
            PipelineDesc Desc;
            Native       Array;
            GLPipeline(const std::shared_ptr<State>& state, PipelineDesc desc, GLuint vao) :
                Desc(std::move(desc)), Array(state, vao, [](GLuint n) { glDeleteVertexArrays(1, &n); })
            {}
            const PipelineDesc& GetDesc() const override { return Desc; }
        };
        template<class T, class B>
        std::shared_ptr<T> Cast(const std::shared_ptr<B>& resource, const std::shared_ptr<State>& owner)
        {
            auto result = std::dynamic_pointer_cast<T>(resource);
            Require(bool(result), "Resource belongs to another backend or is null");
            if constexpr (std::is_same_v<T, GLPipeline>)
                Require(result->Array.Owner == owner, "Resource belongs to another device");
            else if constexpr (std::is_same_v<T, GLLayout> || std::is_same_v<T, GLGroup>)
                Require(result->Owner == owner, "Resource belongs to another device");
            else
                Require(result->Name.Owner == owner, "Resource belongs to another device");
            return result;
        }
        GLenum Compare(CompareOp op)
        {
            static const GLenum values[] = {
                GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
            return values[uint32_t(op)];
        }
        GLenum Stencil(StencilOp op)
        {
            static const GLenum values[] = {
                GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
            return values[uint32_t(op)];
        }
        GLenum Factor(BlendFactor factor)
        {
            static const GLenum values[] = {GL_ZERO,
                                            GL_ONE,
                                            GL_SRC_COLOR,
                                            GL_ONE_MINUS_SRC_COLOR,
                                            GL_SRC_ALPHA,
                                            GL_ONE_MINUS_SRC_ALPHA,
                                            GL_DST_COLOR,
                                            GL_ONE_MINUS_DST_COLOR,
                                            GL_DST_ALPHA,
                                            GL_ONE_MINUS_DST_ALPHA};
            return values[uint32_t(factor)];
        }
        GLenum Equation(BlendOp op)
        {
            static const GLenum values[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
            return values[uint32_t(op)];
        }
        GLenum Primitive(Topology t)
        {
            static const GLenum values[] = {GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_LINES, GL_POINTS};
            return values[uint32_t(t)];
        }
        struct AttributeInfo
        {
            GLint    Count;
            GLenum   Type;
            bool     Integer, Normalized;
            uint32_t Bytes;
        };
        AttributeInfo Attribute(VertexFormat f)
        {
            const auto i = uint32_t(f);
            if (i < 4)
                return {GLint(i + 1), GL_FLOAT, false, false, (i + 1) * 4};
            if (i < 8)
                return {GLint(i - 3), GL_UNSIGNED_INT, true, false, (i - 3) * 4};
            return {4, GL_UNSIGNED_BYTE, false, true, 4};
        }
        // Serialize individual fields, never padding. This is also the collision
        // comparison key; a hash collision cannot merge different descriptions.
        std::string PipelineKey(const PipelineDesc& d)
        {
            std::string key;
            auto add    = [&](const auto& value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
            auto shader = d.Program.get();
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
        void Enable(GLenum capability, bool enabled)
        {
            if (enabled)
                glEnable(capability);
            else
                glDisable(capability);
        }
        void Apply(const GLPipeline& p)
        {
            const auto& d = p.Desc;
            glUseProgram(std::static_pointer_cast<GLShader>(d.Program)->Name.ID);
            glBindVertexArray(p.Array.ID);
            Enable(GL_CULL_FACE, d.Rasterizer.Cull != CullMode::None);
            glCullFace(d.Rasterizer.Cull == CullMode::Front ? GL_FRONT : GL_BACK);
            glFrontFace(d.Rasterizer.Front == FrontFace::CounterClockwise ? GL_CCW : GL_CW);
            glPolygonMode(GL_FRONT_AND_BACK, d.Rasterizer.Polygon == PolygonMode::Fill ? GL_FILL : GL_LINE);
            const bool bias = d.Rasterizer.DepthBiasConstant != 0 || d.Rasterizer.DepthBiasSlope != 0;
            Enable(GL_POLYGON_OFFSET_FILL, bias && d.Rasterizer.Polygon == PolygonMode::Fill);
            Enable(GL_POLYGON_OFFSET_LINE, bias && d.Rasterizer.Polygon == PolygonMode::Line);
            glPolygonOffset(d.Rasterizer.DepthBiasSlope, d.Rasterizer.DepthBiasConstant);
            Enable(GL_DEPTH_TEST, d.DepthStencil.DepthTest);
            glDepthMask(d.DepthStencil.DepthWrite);
            glDepthFunc(Compare(d.DepthStencil.DepthCompare));
            Enable(GL_STENCIL_TEST, d.DepthStencil.StencilTest);
            auto stencil = [](GLenum side, const StencilFaceState& s) {
                glStencilFuncSeparate(side, Compare(s.Compare), s.Reference, s.ReadMask);
                glStencilOpSeparate(side, Stencil(s.Fail), Stencil(s.DepthFail), Stencil(s.Pass));
                glStencilMaskSeparate(side, s.WriteMask);
            };
            stencil(GL_FRONT, d.DepthStencil.Front);
            stencil(GL_BACK, d.DepthStencil.Back);
            for (size_t i = 0; i < d.Blends.size(); ++i)
            {
                const auto& b = d.Blends[i];
                if (b.Enabled)
                    glEnablei(GL_BLEND, i);
                else
                    glDisablei(GL_BLEND, i);
                glBlendFuncSeparatei(i,
                                     Factor(b.SourceColor),
                                     Factor(b.DestinationColor),
                                     Factor(b.SourceAlpha),
                                     Factor(b.DestinationAlpha));
                glBlendEquationSeparatei(i, Equation(b.ColorOp), Equation(b.AlphaOp));
                glColorMaski(i, b.WriteMask & 1, b.WriteMask & 2, b.WriteMask & 4, b.WriteMask & 8);
            }
            Enable(GL_FRAMEBUFFER_SRGB, std::any_of(d.ColorFormats.begin(), d.ColorFormats.end(), [](Format f) {
                       return f == Format::RGBA8Srgb;
                   }));
            glDisable(GL_RASTERIZER_DISCARD);
            glDisable(GL_PRIMITIVE_RESTART);
            glDisable(GL_COLOR_LOGIC_OP);
            glDisable(GL_DITHER); // Deterministic normalized conversion for offscreen targets.
        }
        struct Execution
        {
            std::shared_ptr<State>                       Owner;
            std::shared_ptr<GLPipeline>                  Pipeline;
            RenderingDesc                                Rendering;
            std::map<uint32_t, std::shared_ptr<GLGroup>> Groups;
            std::map<uint32_t, VertexBufferView>         Vertices;
            std::shared_ptr<GLBuffer>                    Index;
            IndexType                                    IndexFormat = IndexType::Uint32;
            size_t                                       IndexOffset = 0;
            GLuint                                       Framebuffer = 0, PushBuffer = 0;
            std::array<uint8_t, 128>                     PushData {};
            ~Execution()
            {
                if (Framebuffer)
                    glDeleteFramebuffers(1, &Framebuffer);
                if (PushBuffer)
                    glDeleteBuffers(1, &PushBuffer);
            }
            void Prepare()
            {
                Require(bool(Pipeline), "Draw needs a pipeline");
                if (Owner->PendingShaderWrites)
                {
                    // Graphics storage writes can feed any subsequent binding type.
                    // This backend conservatively synchronizes once before that use.
                    glMemoryBarrier(GL_ALL_BARRIER_BITS);
                    Owner->PendingShaderWrites = false;
                }
                Apply(*Pipeline); // Rendering clears temporarily override pipeline masks.
                const auto& d = Pipeline->Desc;
                Require(d.ColorFormats.size() == Rendering.Colors.size(), "Pipeline target count mismatch");
                for (size_t i = 0; i < d.ColorFormats.size(); ++i)
                    Require(d.ColorFormats[i] == Rendering.Colors[i].Target->GetDesc().PixelFormat,
                            "Pipeline target format mismatch");
                Require(d.HasDepthStencil == bool(Rendering.Depth.Target), "Pipeline depth target mismatch");
                if (d.HasDepthStencil)
                    Require(d.DepthStencilFormat == Rendering.Depth.Target->GetDesc().PixelFormat,
                            "Pipeline depth format mismatch");
                auto program = std::static_pointer_cast<GLShader>(d.Program);
                for (const auto& layout : d.GroupLayouts)
                {
                    const auto& expected = layout->GetDesc();
                    auto        it       = Groups.find(expected.Group);
                    Require(it != Groups.end(), "Missing bind group");
                    const auto& actual = it->second->Desc.Layout->GetDesc();
                    Require(actual.Entries.size() == expected.Entries.size(), "Bind group layout mismatch");
                    for (size_t i = 0; i < expected.Entries.size(); ++i)
                        Require(actual.Entries[i].Slot == expected.Entries[i].Slot &&
                                    actual.Entries[i].Type == expected.Entries[i].Type,
                                "Bind group layout mismatch");
                }
                for (const auto& binding : program->Bindings)
                {
                    auto group = Groups.at(binding.Group);
                    auto entry = std::find_if(group->Desc.Entries.begin(),
                                              group->Desc.Entries.end(),
                                              [&](const auto& e) { return e.Slot == binding.Slot; });
                    Require(entry != group->Desc.Entries.end(), "Missing shader binding");
                    if (entry->BufferResource)
                    {
                        const auto bytes =
                            entry->Bytes ? entry->Bytes : entry->BufferResource->GetDesc().Bytes - entry->Offset;
                        Require(bytes >= binding.Bytes, "Binding range is smaller than the shader block");
                    }
                    if (entry->TextureResource)
                        Require(entry->TextureResource->GetDesc().Dimension == binding.Dimension,
                                "Texture dimension disagrees with shader binding");
                }
                std::map<uint64_t, GLuint> images, samplers;
                for (const auto& [groupIndex, group] : Groups)
                {
                    const auto& l = group->Desc.Layout->GetDesc();
                    for (size_t i = 0; i < l.Entries.size(); ++i)
                    {
                        const auto& e     = group->Desc.Entries[i];
                        auto        type  = l.Entries[i].Type;
                        const auto  point = Point(groupIndex, e.Slot);
                        if (e.BufferResource)
                        {
                            auto b = std::static_pointer_cast<GLBuffer>(e.BufferResource);
                            const auto& points =
                                type == BindingType::StorageBuffer ? program->StoragePoints : program->UniformPoints;
                            if (!points.count(point))
                                continue;
                            glBindBufferRange(
                                type == BindingType::StorageBuffer ? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER,
                                (type == BindingType::StorageBuffer ? program->StoragePoints : program->UniformPoints)
                                    .at(point),
                                b->Name.ID,
                                e.Offset,
                                e.Bytes ? e.Bytes : b->Desc.Bytes - e.Offset);
                        }
                        if (e.TextureResource)
                            images[point] = std::static_pointer_cast<GLTexture>(e.TextureResource)->Name.ID;
                        if (e.SamplerResource)
                            samplers[point] = std::static_pointer_cast<GLSampler>(e.SamplerResource)->Name.ID;
                    }
                }
                for (auto unit : program->Units)
                {
                    glActiveTexture(GL_TEXTURE0 + unit.Unit);
                    // Only one image dimension is active for this shader resource.
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                    Require(images.count(unit.ImagePoint) && samplers.count(unit.SamplerPoint),
                            "Missing texture/sampler binding");
                    std::shared_ptr<GLTexture> texture;
                    for (const auto& [g, group] : Groups)
                        for (const auto& e : group->Desc.Entries)
                            if (Point(g, e.Slot) == unit.ImagePoint)
                                texture = std::static_pointer_cast<GLTexture>(e.TextureResource);
                    glBindTexture(texture->Target(), images.at(unit.ImagePoint));
                    glBindSampler(unit.Unit, samplers.at(unit.SamplerPoint));
                }
                if (d.PushConstantBytes)
                {
                    if (!PushBuffer)
                        glGenBuffers(1, &PushBuffer);
                    glBindBuffer(GL_UNIFORM_BUFFER, PushBuffer);
                    // Orphaning prevents later draws from overwriting earlier constants.
                    std::vector<uint8_t> packed(program->PushUploadBytes, 0);
                    for (auto copy : program->PushCopies)
                        std::memcpy(packed.data() + copy.Destination, PushData.data() + copy.Source, copy.Bytes);
                    glBufferData(GL_UNIFORM_BUFFER, packed.size(), packed.data(), GL_STREAM_DRAW);
                    glBindBufferBase(GL_UNIFORM_BUFFER, PushPoint, PushBuffer);
                }
                for (auto b : d.VertexBindings)
                {
                    auto it = Vertices.find(b.Slot);
                    Require(it != Vertices.end(), "Missing vertex buffer");
                    auto buffer = std::static_pointer_cast<GLBuffer>(it->second.Resource);
                    glBindVertexBuffer(b.Slot, buffer->Name.ID, it->second.Offset, b.Stride);
                }
                if (Index)
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Index->Name.ID);
            }
            void MarkShaderWrites()
            {
                const auto shader = std::static_pointer_cast<GLShader>(Pipeline->Desc.Program);
                if (std::any_of(shader->Bindings.begin(), shader->Bindings.end(), [](const auto& b) {
                        return b.Type == BindingType::StorageBuffer;
                    }))
                    Owner->PendingShaderWrites = true;
            }
        };
        struct GLCommands final : CommandList
        {
            std::shared_ptr<State>                       Owner;
            std::vector<std::function<void(Execution&)>> Commands;
            std::vector<std::shared_ptr<GLBuffer>>       Readbacks;
            bool                                         Rendering = false, Sealed = false;
            std::shared_ptr<GLPipeline>                  BoundPipeline;
            std::shared_ptr<GLBuffer>                    BoundIndex;
            explicit GLCommands(std::shared_ptr<State> owner) : Owner(std::move(owner)) {}
            ~GLCommands() override
            {
                // Cancel unsubmitted writes. Submitted lists are retained until
                // retirement, which clears Readbacks after decrementing them.
                for (const auto& buffer : Readbacks)
                    --buffer->PendingWrites;
            }
            void Record(std::function<void(Execution&)> command)
            {
                Require(!Sealed, "Command list is already submitted");
                Commands.push_back(std::move(command));
            }
            void Outside() { Require(!Rendering, "Copy must be outside rendering"); }
            void Inside() { Require(Rendering, "Command requires rendering"); }
            void BeginRendering(const RenderingDesc& desc) override
            {
                Require(!Rendering && (!desc.Colors.empty() || desc.Depth.Target), "Invalid rendering scope");
                Require(desc.Colors.size() <= 8, "Too many color attachments");
                uint32_t width = 0, height = 0;
                auto     validate = [&](const Handle<Texture>& t, TextureSubresource sub, bool depth) {
                    auto texture = Cast<GLTexture>(t, Owner);
                    TextureBytes(texture->Desc, sub);
                    Require(Has(texture->Desc.Usage, depth ? TextureUsage::DepthStencil : TextureUsage::RenderTarget),
                            "Attachment usage mismatch");
                    Require(Info(texture->Desc.PixelFormat).Depth == depth, "Attachment format mismatch");
                    auto w = Width(texture->Desc, sub.Mip), h = Height(texture->Desc, sub.Mip);
                    if (!width)
                    {
                        width  = w;
                        height = h;
                    }
                    Require(width == w && height == h, "Attachment dimensions mismatch");
                };
                for (const auto& c : desc.Colors)
                    validate(c.Target, c.Subresource, false);
                if (desc.Depth.Target)
                    validate(desc.Depth.Target, desc.Depth.Subresource, true);
                Record([desc](Execution& e) {
                    e.Rendering = desc;
                    glGenFramebuffers(1, &e.Framebuffer);
                    glBindFramebuffer(GL_FRAMEBUFFER, e.Framebuffer);
                    std::vector<GLenum> drawBuffers;
                    auto attach = [](GLenum attachment, const Handle<Texture>& t, TextureSubresource sub) {
                        auto texture = std::static_pointer_cast<GLTexture>(t);
                        glFramebufferTexture2D(
                            GL_FRAMEBUFFER, attachment, texture->Face(sub.Face), texture->Name.ID, sub.Mip);
                    };
                    for (size_t i = 0; i < desc.Colors.size(); ++i)
                    {
                        attach(GL_COLOR_ATTACHMENT0 + i, desc.Colors[i].Target, desc.Colors[i].Subresource);
                        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
                    }
                    glDrawBuffers(drawBuffers.size(), drawBuffers.data());
                    glReadBuffer(drawBuffers.empty() ? GL_NONE : GL_COLOR_ATTACHMENT0);
                    if (desc.Depth.Target)
                        attach(Info(desc.Depth.Target->GetDesc().PixelFormat).Stencil ? GL_DEPTH_STENCIL_ATTACHMENT :
                                                                                        GL_DEPTH_ATTACHMENT,
                               desc.Depth.Target,
                               desc.Depth.Subresource);
                    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                        throw std::runtime_error("Incomplete rendering target");
                    // Clears must ignore masks/scissor left by previous pipelines/scopes.
                    glDisable(GL_SCISSOR_TEST);
                    glDepthMask(GL_TRUE);
                    glStencilMask(255);
                    Enable(GL_FRAMEBUFFER_SRGB, std::any_of(desc.Colors.begin(), desc.Colors.end(), [](const auto& c) {
                               return c.Target->GetDesc().PixelFormat == Format::RGBA8Srgb;
                           }));
                    for (size_t i = 0; i < desc.Colors.size(); ++i)
                    {
                        const auto& c = desc.Colors[i];
                        glColorMaski(i, true, true, true, true);
                        if (c.Load == LoadOp::Clear)
                        {
                            if (c.Target->GetDesc().PixelFormat == Format::R32Uint)
                            {
                                const GLuint values[] = {c.ClearUint, 0, 0, 0};
                                glClearBufferuiv(GL_COLOR, i, values);
                            }
                            else
                                glClearBufferfv(GL_COLOR, i, c.ClearColor.data());
                        }
                        else if (c.Load == LoadOp::Discard)
                        {
                            const GLenum a = GL_COLOR_ATTACHMENT0 + i;
                            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &a);
                        }
                    }
                    if (desc.Depth.Target && desc.Depth.Load == LoadOp::Clear)
                    {
                        if (Info(desc.Depth.Target->GetDesc().PixelFormat).Stencil)
                            glClearBufferfi(GL_DEPTH_STENCIL, 0, desc.Depth.ClearDepth, desc.Depth.ClearStencil);
                        else
                            glClearBufferfv(GL_DEPTH, 0, &desc.Depth.ClearDepth);
                    }
                    else if (desc.Depth.Target && desc.Depth.Load == LoadOp::Discard)
                    {
                        const GLenum aspects[] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
                        glInvalidateFramebuffer(
                            GL_FRAMEBUFFER, Info(desc.Depth.Target->GetDesc().PixelFormat).Stencil ? 2 : 1, aspects);
                    }
                });
                Rendering = true;
            }
            void EndRendering() override
            {
                Inside();
                Record([](Execution& e) {
                    std::vector<GLenum> discard;
                    for (size_t i = 0; i < e.Rendering.Colors.size(); ++i)
                        if (e.Rendering.Colors[i].Store == StoreOp::Discard)
                            discard.push_back(GL_COLOR_ATTACHMENT0 + i);
                    if (e.Rendering.Depth.Target && e.Rendering.Depth.Store == StoreOp::Discard)
                    {
                        discard.push_back(GL_DEPTH_ATTACHMENT);
                        if (Info(e.Rendering.Depth.Target->GetDesc().PixelFormat).Stencil)
                            discard.push_back(GL_STENCIL_ATTACHMENT);
                    }
                    if (!discard.empty())
                        glInvalidateFramebuffer(GL_FRAMEBUFFER, discard.size(), discard.data());
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glDeleteFramebuffers(1, &e.Framebuffer);
                    e.Framebuffer = 0;
                    e.Rendering   = {};
                });
                Rendering = false;
            }
            void SetViewport(const Viewport& v) override
            {
                Inside();
                Require(v.Width > 0 && v.Height > 0 && v.MinDepth >= 0 && v.MaxDepth <= 1 && v.MinDepth <= v.MaxDepth,
                        "Invalid viewport");
                Record([v](Execution&) {
                    glViewport(v.X, v.Y, v.Width, v.Height);
                    glDepthRange(v.MinDepth, v.MaxDepth);
                });
            }
            void SetScissor(const Scissor& s) override
            {
                Inside();
                Record([s](Execution&) {
                    glEnable(GL_SCISSOR_TEST);
                    glScissor(s.X, s.Y, s.Width, s.Height);
                });
            }
            void BindPipeline(const Handle<Pipeline>& pipeline) override
            {
                Inside();
                auto p = Cast<GLPipeline>(pipeline, Owner);
                Record([p](Execution& e) {
                    e.Pipeline = p;
                    Apply(*p);
                });
                BoundPipeline = p;
            }
            void BindGroup(const Handle<RenderHardware::BindGroup>& group) override
            {
                Inside();
                auto g = Cast<GLGroup>(group, Owner);
                Record([g](Execution& e) { e.Groups[g->Desc.Layout->GetDesc().Group] = g; });
            }
            void PushConstants(uint32_t offset, const void* data, uint32_t bytes) override
            {
                Inside();
                Require(BoundPipeline && data && offset % 4 == 0 && bytes % 4 == 0, "Invalid push constant write");
                Range(BoundPipeline->Desc.PushConstantBytes, offset, bytes);
                std::vector<uint8_t> copy(bytes);
                std::memcpy(copy.data(), data, bytes);
                Record([offset, copy](Execution& e) {
                    std::memcpy(e.PushData.data() + offset, copy.data(), copy.size());
                });
            }
            void BindVertexBuffers(const std::vector<VertexBufferView>& buffers) override
            {
                Inside();
                for (const auto& view : buffers)
                {
                    auto b = Cast<GLBuffer>(view.Resource, Owner);
                    Require(Has(b->Desc.Usage, BufferUsage::Vertex) && view.Offset < b->Desc.Bytes,
                            "Invalid vertex buffer view");
                }
                Record([buffers](Execution& e) {
                    for (auto v : buffers)
                        e.Vertices[v.Slot] = v;
                });
            }
            void BindIndexBuffer(const Handle<Buffer>& buffer, IndexType type, size_t offset) override
            {
                Inside();
                auto b = Cast<GLBuffer>(buffer, Owner);
                Require(Has(b->Desc.Usage, BufferUsage::Index) && offset < b->Desc.Bytes &&
                            offset % (type == IndexType::Uint16 ? 2 : 4) == 0,
                        "Invalid index buffer view");
                Record([b, type, offset](Execution& e) {
                    e.Index       = b;
                    e.IndexFormat = type;
                    e.IndexOffset = offset;
                });
                BoundIndex = b;
            }
            void Draw(uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) override
            {
                Inside();
                Require(bool(BoundPipeline), "Draw requires pipeline");
                Record([vertices, instances, firstVertex, firstInstance](Execution& e) {
                    e.Prepare();
                    glDrawArraysInstancedBaseInstance(
                        Primitive(e.Pipeline->Desc.Primitive), firstVertex, vertices, instances, firstInstance);
                    e.MarkShaderWrites();
                });
            }
            void DrawIndexed(uint32_t indices,
                             uint32_t instances,
                             uint32_t firstIndex,
                             int32_t  baseVertex,
                             uint32_t firstInstance) override
            {
                Inside();
                Require(BoundPipeline && BoundIndex, "Indexed draw requires pipeline and index buffer");
                Record([indices, instances, firstIndex, baseVertex, firstInstance](Execution& e) {
                    e.Prepare();
                    const auto bytes  = e.IndexFormat == IndexType::Uint16 ? 2u : 4u;
                    const auto offset = e.IndexOffset + size_t(firstIndex) * bytes;
                    Range(e.Index->Desc.Bytes, offset, size_t(indices) * bytes);
                    glDrawElementsInstancedBaseVertexBaseInstance(Primitive(e.Pipeline->Desc.Primitive),
                                                                  indices,
                                                                  bytes == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                                                  reinterpret_cast<void*>(offset),
                                                                  instances,
                                                                  baseVertex,
                                                                  firstInstance);
                    e.MarkShaderWrites();
                });
            }
            void MarkReadback(const std::shared_ptr<GLBuffer>& buffer)
            {
                if (buffer->Desc.Memory == MemoryUsage::Readback)
                {
                    Readbacks.push_back(buffer);
                    // Mark at recording time too: an immediate backend must give the same result.
                    ++buffer->PendingWrites;
                }
            }
            void CopyBuffer(const Handle<Buffer>& source,
                            size_t                sourceOffset,
                            const Handle<Buffer>& destination,
                            size_t                destinationOffset,
                            size_t                bytes) override
            {
                Outside();
                auto s = Cast<GLBuffer>(source, Owner), d = Cast<GLBuffer>(destination, Owner);
                Require(Has(s->Desc.Usage, BufferUsage::CopySource) && Has(d->Desc.Usage, BufferUsage::CopyDestination),
                        "Buffer copy usage mismatch");
                Range(s->Desc.Bytes, sourceOffset, bytes);
                Range(d->Desc.Bytes, destinationOffset, bytes);
                Require(s != d || sourceOffset + bytes <= destinationOffset ||
                            destinationOffset + bytes <= sourceOffset,
                        "Overlapping buffer copy");
                Record([s, d, sourceOffset, destinationOffset, bytes](Execution&) {
                    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                    glBindBuffer(GL_COPY_READ_BUFFER, s->Name.ID);
                    glBindBuffer(GL_COPY_WRITE_BUFFER, d->Name.ID);
                    glCopyBufferSubData(
                        GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, sourceOffset, destinationOffset, bytes);
                    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                });
                MarkReadback(d);
            }
            void CopyBufferToTexture(const Handle<Buffer>&  source,
                                     size_t                 offset,
                                     const Handle<Texture>& destination,
                                     TextureSubresource     sub) override
            {
                Outside();
                auto s = Cast<GLBuffer>(source, Owner);
                auto d = Cast<GLTexture>(destination, Owner);
                Require(Has(s->Desc.Usage, BufferUsage::CopySource) &&
                            Has(d->Desc.Usage, TextureUsage::CopyDestination),
                        "Texture upload usage mismatch");
                Require(offset % Info(d->Desc.PixelFormat).Bytes == 0, "Texture copy offset must be texel aligned");
                Range(s->Desc.Bytes, offset, TextureBytes(d->Desc, sub));
                Record([s, d, offset, sub](Execution&) {
                    TransferState state(d->Target(), false);
                    glMemoryBarrier(GL_PIXEL_BUFFER_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s->Name.ID);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
                    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
                    glBindTexture(d->Target(), d->Name.ID);
                    const auto f = Info(d->Desc.PixelFormat);
                    glTexSubImage2D(d->Face(sub.Face),
                                    sub.Mip,
                                    0,
                                    0,
                                    Width(d->Desc, sub.Mip),
                                    Height(d->Desc, sub.Mip),
                                    f.External,
                                    f.Type,
                                    reinterpret_cast<void*>(offset));
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                });
            }
            void CopyTextureToBuffer(const Handle<Texture>& source,
                                     TextureSubresource     sub,
                                     const Handle<Buffer>&  destination,
                                     size_t                 offset) override
            {
                Outside();
                auto s = Cast<GLTexture>(source, Owner);
                auto d = Cast<GLBuffer>(destination, Owner);
                Require(Has(s->Desc.Usage, TextureUsage::CopySource) &&
                            Has(d->Desc.Usage, BufferUsage::CopyDestination),
                        "Texture readback usage mismatch");
                Require(offset % Info(s->Desc.PixelFormat).Bytes == 0, "Texture copy offset must be texel aligned");
                Range(d->Desc.Bytes, offset, TextureBytes(s->Desc, sub));
                Record([s, d, offset, sub](Execution&) {
                    TransferState state(s->Target(), true);
                    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, d->Name.ID);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
                    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
                    glBindTexture(s->Target(), s->Name.ID);
                    const auto f = Info(s->Desc.PixelFormat);
                    glGetTexImage(s->Face(sub.Face), sub.Mip, f.External, f.Type, reinterpret_cast<void*>(offset));
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                });
                MarkReadback(d);
            }
            void GenerateMips(const Handle<Texture>& texture) override
            {
                Outside();
                auto t = Cast<GLTexture>(texture, Owner);
                Require(Has(t->Desc.Usage, TextureUsage::CopySource) &&
                            Has(t->Desc.Usage, TextureUsage::CopyDestination) && t->Desc.MipLevels > 1 &&
                            !Info(t->Desc.PixelFormat).Depth && t->Desc.PixelFormat != Format::R32Uint,
                        "Texture cannot generate mips");
                Record([t](Execution&) {
                    glBindTexture(t->Target(), t->Name.ID);
                    glGenerateMipmap(t->Target());
                });
            }
        };
        class GLDevice final : public Device
        {
            DeviceDesc             m_Desc;
            std::shared_ptr<State> m_State = std::make_shared<State>();
            std::unordered_map<size_t, std::vector<std::pair<std::string, std::weak_ptr<GLPipeline>>>> m_Cache;

        public:
            explicit GLDevice(DeviceDesc desc) : m_Desc(desc)
            {
                Require(desc.FramesInFlight > 0, "FramesInFlight must be positive");
                Require(glGetString && glGetString(GL_VERSION), "Device needs a current context");
            }
            ~GLDevice() override
            {
                m_State->Retire();
                m_State->Alive = false;
            }
            const DeviceDesc&     GetDesc() const override { return m_Desc; }
            CoordinateConventions GetCoordinateConventions() const override
            {
                return {ClipYDirection::Up, DepthRange::MinusOneToOne, FramebufferOrigin::LowerLeft};
            }
            DeviceStats GetStats() const override { return m_State->Stats; }
            size_t      GetBufferBindingAlignment(BindingType type) const override
            {
                Require(type == BindingType::UniformBuffer || type == BindingType::StorageBuffer,
                        "Alignment query requires a buffer binding type");
                GLint alignment = 0;
                glGetIntegerv(type == BindingType::UniformBuffer ? GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT :
                                                                   GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,
                              &alignment);
                return size_t(alignment);
            }
            Handle<Buffer> CreateBuffer(const BufferDesc& desc) override
            {
                Require(desc.Bytes && uint32_t(desc.Usage), "Invalid buffer description");
                Require(desc.Bytes <= size_t(std::numeric_limits<GLsizeiptr>::max()), "Buffer too large");
                GLuint id = 0;
                glGenBuffers(1, &id);
                auto b = std::make_shared<GLBuffer>(m_State, desc, id);
                BufferBindingGuard binding(GL_COPY_WRITE_BUFFER, GL_COPY_WRITE_BUFFER_BINDING);
                glBindBuffer(GL_COPY_WRITE_BUFFER, id);
                glBufferData(GL_COPY_WRITE_BUFFER,
                             desc.Bytes,
                             nullptr,
                             desc.Memory == MemoryUsage::Readback ? GL_STREAM_READ :
                             desc.Memory == MemoryUsage::Upload   ? GL_STREAM_DRAW :
                                                                    GL_STATIC_DRAW);
                return b;
            }
            Handle<Texture> CreateTexture(const TextureDesc& desc) override
            {
                Require(desc.Width && desc.Height && desc.MipLevels && uint32_t(desc.Usage),
                        "Invalid texture description");
                Require(desc.Dimension != TextureDimension::Cube || desc.Width == desc.Height,
                        "Cube faces must be square");
                uint32_t maxMips = 1;
                for (auto size = std::max(desc.Width, desc.Height); size > 1; size >>= 1)
                    ++maxMips;
                Require(desc.MipLevels <= maxMips, "Too many mip levels");
                const auto info = Info(desc.PixelFormat);
                Require(!(Has(desc.Usage, TextureUsage::RenderTarget) && info.Depth) &&
                            !(Has(desc.Usage, TextureUsage::DepthStencil) && !info.Depth),
                        "Texture format/usage mismatch");
                GLuint id = 0;
                glGenTextures(1, &id);
                auto t = std::make_shared<GLTexture>(m_State, desc, id);
                GLint previous;
                glGetIntegerv(desc.Dimension == TextureDimension::Cube ? GL_TEXTURE_BINDING_CUBE_MAP :
                                                                         GL_TEXTURE_BINDING_2D,
                              &previous);
                glBindTexture(t->Target(), id);
                glTexStorage2D(t->Target(), desc.MipLevels, info.Internal, desc.Width, desc.Height);
                glTexParameteri(t->Target(), GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(t->Target(), GL_TEXTURE_MAX_LEVEL, desc.MipLevels - 1);
                glBindTexture(t->Target(), previous);
                return t;
            }
            Handle<Sampler> CreateSampler(const SamplerDesc& desc) override
            {
                Require(desc.MinLod <= desc.MaxLod, "Invalid sampler LOD range");
                GLuint id = 0;
                glGenSamplers(1, &id);
                auto       sampler = std::make_shared<GLSampler>(m_State, id);
                const auto address = [](AddressMode m) {
                    return m == AddressMode::Repeat      ? GL_REPEAT :
                           m == AddressMode::ClampToEdge ? GL_CLAMP_TO_EDGE :
                                                           GL_MIRRORED_REPEAT;
                };
                const auto min =
                    desc.Min == Filter::Nearest ?
                        (desc.Mip == Filter::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR) :
                        (desc.Mip == Filter::Nearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, min);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, desc.Mag == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, address(desc.U));
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, address(desc.V));
                glSamplerParameteri(id, GL_TEXTURE_WRAP_R, address(desc.W));
                glSamplerParameterf(id, GL_TEXTURE_MIN_LOD, desc.MinLod);
                glSamplerParameterf(id, GL_TEXTURE_MAX_LOD, desc.MaxLod);
                return sampler;
            }
            Handle<Shader> CreateShader(const Kans::ShaderData& data) override
            {
                Require(data.Code.Modules.size() == 2, "Graphics shader needs vertex and fragment modules");
                auto                         shader = std::make_shared<GLShader>(m_State, glCreateProgram());
                std::set<ShaderProductStage> kinds;
                std::vector<GLuint>          stages;
                try
                {
                    std::set<uint32_t> reservedUnits;
                    std::set<uint64_t> uniformSlots, storageSlots, combinedSlots;
                    for (const auto& module : data.Code.Modules)
                    {
                        Require((module.Stage == ShaderProductStage::Vertex ||
                                 module.Stage == ShaderProductStage::Fragment) &&
                                    kinds.insert(module.Stage).second,
                                "Unsupported or duplicate shader stage");
                        spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_2);
                        std::string          diagnostic;
                        validator.SetMessageConsumer(
                            [&](spv_message_level_t, const char*, const spv_position_t&, const char* text) {
                                diagnostic = text;
                            });
                        if (!validator.Validate(module.Words))
                            throw std::invalid_argument("Invalid shader bytecode: " + diagnostic);
                        spirv_cross::Compiler reflection(module.Words);
                        const auto            resources = reflection.get_shader_resources();
                        auto                  collect   = [&](const auto& list, auto& slots) {
                            for (const auto& resource : list)
                                slots.insert(Point(reflection.get_decoration(resource.id, spv::DecorationDescriptorSet),
                                                   reflection.get_decoration(resource.id, spv::DecorationBinding)));
                        };
                        collect(resources.uniform_buffers, uniformSlots);
                        collect(resources.storage_buffers, storageSlots);
                        collect(resources.sampled_images, combinedSlots);
                    }
                    uint32_t nextUniform = 2, nextStorage = 0, nextTexture = 3;
                    for (auto point : uniformSlots)
                    {
                        if (nextUniform == PushPoint)
                            ++nextUniform;
                        shader->UniformPoints[point] = point < 2 ? uint32_t(point) : nextUniform++;
                    }
                    for (auto point : storageSlots)
                        shader->StoragePoints[point] = nextStorage++;
                    for (auto point : combinedSlots)
                    {
                        uint32_t unit                = point >= 2 && point <= 4 ? point - 2 : nextTexture++;
                        shader->CombinedUnits[point] = unit;
                        reservedUnits.insert(unit);
                    }
                    for (const auto& module : data.Code.Modules)
                    {
                        ShaderCompiler compiler(module.Words);
                        compiler.set_entry_point(module.EntryPoint, static_cast<spv::ExecutionModel>(module.Stage));
                        auto options                                 = compiler.get_common_options();
                        options.version                              = 430;
                        options.es                                   = false;
                        options.vulkan_semantics                     = false;
                        options.emit_push_constant_as_uniform_buffer = true;
                        compiler.set_common_options(options);
                        const auto resources = compiler.get_shader_resources();
                        Require(resources.storage_images.empty() && resources.subpass_inputs.empty() &&
                                    resources.atomic_counters.empty(),
                                "Unsupported shader resource");
                        std::map<uint32_t, uint64_t> separatePoints;
                        auto remap = [&](const auto& list, BindingType type) {
                            for (const auto& r : list)
                            {
                                const auto group = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                                const auto slot  = compiler.get_decoration(r.id, spv::DecorationBinding);
                                Require(compiler.get_type(r.type_id).array.empty(), "Binding arrays are unsupported");
                                const auto       point     = Point(group, slot);
                                size_t           bytes     = 0;
                                TextureDimension dimension = TextureDimension::Texture2D;
                                if (type == BindingType::SampledTexture || type == BindingType::CombinedTextureSampler)
                                {
                                    const auto image = compiler.get_type(r.type_id).image;
                                    Require((image.dim == spv::Dim2D || image.dim == spv::DimCube) && !image.arrayed &&
                                                !image.ms && !image.depth,
                                            "Unsupported shader image dimension or comparison image");
                                    dimension = image.dim == spv::DimCube ? TextureDimension::Cube :
                                                                            TextureDimension::Texture2D;
                                }
                                if (type == BindingType::UniformBuffer || type == BindingType::StorageBuffer)
                                    bytes = compiler.get_declared_struct_size(compiler.get_type(r.base_type_id));
                                auto old =
                                    std::find_if(shader->Bindings.begin(), shader->Bindings.end(), [&](const auto& b) {
                                        return b.Group == group && b.Slot == slot;
                                    });
                                if (old == shader->Bindings.end())
                                    shader->Bindings.push_back({group, slot, type, bytes, dimension});
                                else
                                    Require(old->Type == type && old->Bytes == bytes && old->Dimension == dimension,
                                            "Shader stages disagree about a binding");
                                const auto nativePoint =
                                    type == BindingType::UniformBuffer          ? shader->UniformPoints.at(point) :
                                    type == BindingType::StorageBuffer          ? shader->StoragePoints.at(point) :
                                    type == BindingType::CombinedTextureSampler ? shader->CombinedUnits.at(point) :
                                                                                  0u;
                                if (type == BindingType::SampledTexture || type == BindingType::Sampler)
                                    separatePoints[r.id] = point;
                                compiler.set_decoration(r.id, spv::DecorationBinding, nativePoint);
                                compiler.unset_decoration(r.id, spv::DecorationDescriptorSet);
                                if (type == BindingType::CombinedTextureSampler)
                                {
                                    auto unit =
                                        std::find_if(shader->Units.begin(), shader->Units.end(), [point](auto u) {
                                            return u.ImagePoint == point && u.SamplerPoint == point;
                                        });
                                    if (unit == shader->Units.end())
                                        shader->Units.push_back({shader->CombinedUnits.at(point), point, point});
                                }
                            }
                        };
                        remap(resources.uniform_buffers, BindingType::UniformBuffer);
                        remap(resources.storage_buffers, BindingType::StorageBuffer);
                        remap(resources.sampled_images, BindingType::CombinedTextureSampler);
                        remap(resources.separate_images, BindingType::SampledTexture);
                        remap(resources.separate_samplers, BindingType::Sampler);
                        if (!resources.separate_images.empty())
                        {
                            compiler.build_combined_image_samplers();
                            for (auto pair : compiler.get_combined_image_samplers())
                            {
                                const auto image   = separatePoints.at(pair.image_id);
                                const auto sampler = separatePoints.at(pair.sampler_id);
                                auto       unit =
                                    std::find_if(shader->Units.begin(), shader->Units.end(), [image, sampler](auto u) {
                                        return u.ImagePoint == image && u.SamplerPoint == sampler;
                                    });
                                uint32_t index;
                                if (unit != shader->Units.end())
                                    index = unit->Unit;
                                else
                                {
                                    index = 0;
                                    while (reservedUnits.count(index) ||
                                           std::any_of(shader->Units.begin(),
                                                       shader->Units.end(),
                                                       [index](const auto& u) { return u.Unit == index; }))
                                        ++index;
                                    GLint limit = 0;
                                    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &limit);
                                    Require(index < uint32_t(limit), "Too many separate texture/sampler pairs");
                                    shader->Units.push_back({index, image, sampler});
                                }
                                compiler.set_decoration(pair.combined_id, spv::DecorationBinding, index);
                            }
                        }
                        Require(resources.push_constant_buffers.size() <= 1,
                                "Only one push constant block is supported");
                        for (const auto& r : resources.push_constant_buffers)
                        {
                            const auto bytes = compiler.get_declared_struct_size(compiler.get_type(r.base_type_id));
                            Require(bytes <= 128, "Push constant block exceeds 128 bytes");
                            PushPackingCache packingCache;
                            auto             packed = PackPushType(compiler, r.base_type_id, 0, false, packingCache);
                            if (shader->PushBytes)
                            {
                                Require(shader->PushBytes == bytes && shader->PushUploadBytes == packed.Bytes &&
                                            shader->PushCopies.size() == packed.Copies.size(),
                                        "Shader stages disagree about push data");
                                for (size_t i = 0; i < packed.Copies.size(); ++i)
                                {
                                    const auto a = shader->PushCopies[i], b = packed.Copies[i];
                                    Require(a.Source == b.Source && a.Destination == b.Destination &&
                                                a.Bytes == b.Bytes,
                                            "Shader stages disagree about push layout");
                                }
                            }
                            shader->PushBytes       = uint32_t(bytes);
                            shader->PushUploadBytes = packed.Bytes;
                            shader->PushCopies      = std::move(packed.Copies);
                            compiler.set_decoration(r.id, spv::DecorationBinding, PushPoint);
                            compiler.EmulatePushBuffer(r.id);
                        }
                        const auto   source = compiler.compile();
                        const char*  text   = source.c_str();
                        const GLuint stage  = glCreateShader(
                            module.Stage == ShaderProductStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
                        stages.push_back(stage);
                        glShaderSource(stage, 1, &text, nullptr);
                        glCompileShader(stage);
                        GLint ok = 0;
                        glGetShaderiv(stage, GL_COMPILE_STATUS, &ok);
                        if (!ok)
                        {
                            GLint length = 0;
                            glGetShaderiv(stage, GL_INFO_LOG_LENGTH, &length);
                            std::string log(std::max(length, 1), '\0');
                            glGetShaderInfoLog(stage, length, nullptr, log.data());
                            throw std::runtime_error("Shader compilation failed: " + log);
                        }
                        glAttachShader(shader->Name.ID, stage);
                    }
                    glLinkProgram(shader->Name.ID);
                    GLint ok = 0;
                    glGetProgramiv(shader->Name.ID, GL_LINK_STATUS, &ok);
                    if (!ok)
                    {
                        GLint length = 0;
                        glGetProgramiv(shader->Name.ID, GL_INFO_LOG_LENGTH, &length);
                        std::string log(std::max(length, 1), '\0');
                        glGetProgramInfoLog(shader->Name.ID, length, nullptr, log.data());
                        throw std::runtime_error("Shader link failed: " + log);
                    }
                    // Reflection supplied by assets is metadata, not authority. Check it
                    // against the bytecode, and also accept fresh modules with no metadata.
                    for (const auto& stage : data.Reflection)
                        for (const auto& b : stage.Bindings)
                        {
                            auto reflected =
                                std::find_if(shader->Bindings.begin(), shader->Bindings.end(), [&](const auto& r) {
                                    return r.Group == b.Set && r.Slot == b.Binding;
                                });
                            Require(reflected != shader->Bindings.end() && b.Count == 1 &&
                                        reflected->Type == (b.Kind == ShaderBindingKind::UniformBuffer ?
                                                                BindingType::UniformBuffer :
                                                                BindingType::CombinedTextureSampler) &&
                                        reflected->Bytes == b.BlockBytes,
                                    "Asset reflection disagrees with bytecode");
                        }
                }
                catch (...)
                {
                    for (auto stage : stages)
                        glDeleteShader(stage);
                    throw;
                }
                for (auto stage : stages)
                {
                    glDetachShader(shader->Name.ID, stage);
                    glDeleteShader(stage);
                }
                return shader;
            }
            Handle<BindGroupLayout> CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override
            {
                Require(desc.Group < GroupCount, "Group exceeds backend capacity");
                auto layout   = std::make_shared<GLLayout>();
                layout->Owner = m_State;
                layout->Desc  = desc;
                auto& entries = layout->Desc.Entries;
                std::sort(entries.begin(), entries.end(), [](auto a, auto b) { return a.Slot < b.Slot; });
                for (size_t i = 0; i < entries.size(); ++i)
                {
                    Point(desc.Group, entries[i].Slot);
                    Require(!i || entries[i - 1].Slot != entries[i].Slot, "Duplicate group slot");
                }
                return layout;
            }
            Handle<BindGroup> CreateBindGroup(const BindGroupDesc& desc) override
            {
                auto layout   = Cast<GLLayout>(desc.Layout, m_State);
                auto group    = std::make_shared<GLGroup>();
                group->Owner  = m_State;
                group->Desc   = desc;
                auto& entries = group->Desc.Entries;
                std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.Slot < b.Slot; });
                Require(entries.size() == layout->Desc.Entries.size(), "Bind group entries do not match layout");
                for (size_t i = 0; i < entries.size(); ++i)
                {
                    const auto& e = entries[i];
                    const auto& l = layout->Desc.Entries[i];
                    Require(e.Slot == l.Slot, "Bind group slot mismatch");
                    const bool buffer = l.Type == BindingType::UniformBuffer || l.Type == BindingType::StorageBuffer;
                    const bool texture =
                        l.Type == BindingType::SampledTexture || l.Type == BindingType::CombinedTextureSampler;
                    const bool sampler =
                        l.Type == BindingType::Sampler || l.Type == BindingType::CombinedTextureSampler;
                    Require(bool(e.BufferResource) == buffer && bool(e.TextureResource) == texture &&
                                bool(e.SamplerResource) == sampler,
                            "Bind group resource type mismatch");
                    if (buffer)
                    {
                        auto b = Cast<GLBuffer>(e.BufferResource, m_State);
                        Require(Has(b->Desc.Usage,
                                    l.Type == BindingType::UniformBuffer ? BufferUsage::Uniform : BufferUsage::Storage),
                                "Binding buffer usage mismatch");
                        const auto bytes = e.Bytes ? e.Bytes : b->Desc.Bytes - std::min(e.Offset, b->Desc.Bytes);
                        Range(b->Desc.Bytes, e.Offset, bytes);
                        Require(bytes > 0, "Empty buffer binding");
                        const auto alignment = GetBufferBindingAlignment(l.Type);
                        Require(e.Offset % alignment == 0, "Buffer binding offset is not aligned");
                    }
                    if (texture)
                        Require(Has(Cast<GLTexture>(e.TextureResource, m_State)->Desc.Usage, TextureUsage::Sampled),
                                "Binding texture usage mismatch");
                    if (sampler)
                        Cast<GLSampler>(e.SamplerResource, m_State);
                }
                return group;
            }
            Handle<Pipeline> CreatePipeline(const PipelineDesc& desc) override
            {
                auto shader = Cast<GLShader>(desc.Program, m_State);
                Require(desc.PushConstantBytes >= shader->PushBytes && desc.PushConstantBytes <= 128 &&
                            desc.PushConstantBytes % 4 == 0,
                        "Pipeline push constant range mismatch");
                Require(desc.ColorFormats.size() <= 8 && desc.Blends.size() == desc.ColorFormats.size(),
                        "Pipeline blend/target count mismatch");
                std::set<uint32_t> groups;
                for (const auto& l : desc.GroupLayouts)
                    Require(groups.insert(Cast<GLLayout>(l, m_State)->Desc.Group).second, "Duplicate pipeline group");
                for (auto b : shader->Bindings)
                {
                    bool found = false;
                    for (const auto& l : desc.GroupLayouts)
                        if (l->GetDesc().Group == b.Group)
                            for (auto e : l->GetDesc().Entries)
                                if (e.Slot == b.Slot && e.Type == b.Type)
                                    found = true;
                    Require(found, "Pipeline layout omits shader binding");
                }
                for (size_t i = 0; i < desc.ColorFormats.size(); ++i)
                {
                    Require(!Info(desc.ColorFormats[i]).Depth, "Invalid color target format");
                    Require(desc.ColorFormats[i] != Format::R32Uint || !desc.Blends[i].Enabled,
                            "Integer targets cannot blend");
                }
                if (desc.HasDepthStencil)
                    Require(Info(desc.DepthStencilFormat).Depth, "Invalid depth target format");
                Require(!desc.DepthStencil.StencilTest ||
                            (desc.HasDepthStencil && Info(desc.DepthStencilFormat).Stencil),
                        "Stencil needs a stencil format");
                auto  key    = PipelineKey(desc);
                auto& bucket = m_Cache[std::hash<std::string> {}(key)];
                for (auto it = bucket.begin(); it != bucket.end();)
                {
                    if (auto p = it->second.lock())
                    {
                        if (it->first == key)
                            return p;
                        ++it;
                    }
                    else
                        it = bucket.erase(it);
                }
                std::set<uint32_t> slots, locations;
                for (auto b : desc.VertexBindings)
                    Require(b.Stride && b.Slot < 16 && slots.insert(b.Slot).second, "Invalid vertex binding");
                for (auto a : desc.Attributes)
                {
                    Require(a.Location < 16 && locations.insert(a.Location).second && slots.count(a.BufferSlot),
                            "Invalid vertex attribute");
                    auto b = std::find_if(desc.VertexBindings.begin(), desc.VertexBindings.end(), [&](auto binding) {
                        return binding.Slot == a.BufferSlot;
                    });
                    Range(b->Stride, a.Offset, Attribute(a.Type).Bytes);
                }
                GLuint vao = 0;
                glGenVertexArrays(1, &vao);
                auto pipeline = std::make_shared<GLPipeline>(m_State, desc, vao);
                VertexArrayGuard binding;
                glBindVertexArray(vao);
                for (auto b : desc.VertexBindings)
                    glVertexBindingDivisor(b.Slot, b.PerInstance ? 1 : 0);
                for (auto a : desc.Attributes)
                {
                    const auto f = Attribute(a.Type);
                    glEnableVertexAttribArray(a.Location);
                    if (f.Integer)
                        glVertexAttribIFormat(a.Location, f.Count, f.Type, a.Offset);
                    else
                        glVertexAttribFormat(a.Location, f.Count, f.Type, f.Normalized, a.Offset);
                    glVertexAttribBinding(a.Location, a.BufferSlot);
                }
                bucket.emplace_back(std::move(key), pipeline);
                return pipeline;
            }
            Handle<CommandList> CreateCommandList() override { return std::make_shared<GLCommands>(m_State); }
            void                BeginFrame() override
            {
                Require(!m_State->InFrame, "Frame is already active");
                m_State->InFrame = true;
            }
            void Submit(const Handle<CommandList>& commands) override
            {
                auto list = std::dynamic_pointer_cast<GLCommands>(commands);
                Require(list && list->Owner == m_State && !list->Sealed && !list->Rendering,
                        "Invalid command submission");
                SubmissionState state;
                state.Prepare();
                list->Sealed = true;
                Execution execution;
                execution.Owner = m_State;
                // Retain even if replay throws: earlier commands may already be on the GPU.
                m_State->Retentions.push_back([list] {
                    for (const auto& b : list->Readbacks)
                        --b->PendingWrites;
                    list->Commands.clear();
                    list->Readbacks.clear();
                    list->BoundPipeline.reset();
                    list->BoundIndex.reset();
                });
                for (const auto& command : list->Commands)
                    command(execution);
            }
            void EndFrame() override
            {
                Require(m_State->InFrame, "No active frame");
                m_State->Retire();
                m_State->InFrame = false;
            }
            void            WaitIdle() override { m_State->Retire(true); }
            Handle<Texture> Borrow(GLuint id, const TextureDesc& desc)
            {
                auto texture         = std::make_shared<GLTexture>(m_State, desc, id);
                texture->Name.Delete = [](GLuint) {};
                return texture;
            }
            uintptr_t GetImGuiTextureID(const Handle<Texture>& texture) const override
            {
                return Cast<GLTexture>(texture, m_State)->Name.ID;
            }
        };
    } // namespace
    std::unique_ptr<Device> CreateOpenGLDevice(const DeviceDesc& desc) { return std::make_unique<GLDevice>(desc); }
    namespace OpenGLInterop
    {
        std::shared_ptr<Device> AcquireDevice()
        {
            static std::weak_ptr<Device> current;
            auto                         device = current.lock();
            if (!device)
            {
                device  = CreateOpenGLDevice();
                current = device;
            }
            return device;
        }
        uint32_t BufferName(const Handle<Buffer>& b) { return std::dynamic_pointer_cast<GLBuffer>(b)->Name.ID; }
        uint32_t TextureName(const Handle<Texture>& t) { return std::dynamic_pointer_cast<GLTexture>(t)->Name.ID; }
        uint32_t ProgramName(const Handle<Shader>& s) { return std::dynamic_pointer_cast<GLShader>(s)->Name.ID; }
        uint32_t UniformPoint(const Handle<Shader>& s, uint32_t group, uint32_t slot)
        {
            return std::dynamic_pointer_cast<GLShader>(s)->UniformPoints.at(Point(group, slot));
        }
        uint32_t TextureUnit(const Handle<Shader>& s, uint32_t group, uint32_t slot)
        {
            return std::dynamic_pointer_cast<GLShader>(s)->CombinedUnits.at(Point(group, slot));
        }
        void BindTexture(const Handle<Texture>& t, uint32_t unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            auto native = std::dynamic_pointer_cast<GLTexture>(t);
            glBindSampler(unit, 0);
            glBindTexture(native->Target(), native->Name.ID);
        }
        Handle<Texture>
        BorrowTexture(Device& device, uint32_t name, TextureDimension dimension, std::shared_ptr<void> owner)
        {
            TextureDesc desc;
            desc.Dimension   = dimension;
            desc.Usage       = TextureUsage::Sampled;
            desc.PixelFormat = Format::RGBA16Float;
            GLenum target    = dimension == TextureDimension::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            GLint  previous, width, height;
            glGetIntegerv(dimension == TextureDimension::Cube ? GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D,
                          &previous);
            glBindTexture(target, name);
            GLenum face = dimension == TextureDimension::Cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : GL_TEXTURE_2D;
            glGetTexLevelParameteriv(face, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(face, 0, GL_TEXTURE_HEIGHT, &height);
            desc.Width     = width;
            desc.Height    = height;
            desc.MipLevels = 0;
            for (int mip = 0; mip < 32; ++mip)
            {
                GLint w;
                glGetTexLevelParameteriv(face, mip, GL_TEXTURE_WIDTH, &w);
                if (!w)
                    break;
                ++desc.MipLevels;
            }
            glBindTexture(target, previous);
            auto texture = dynamic_cast<GLDevice&>(device).Borrow(name, desc);
            std::dynamic_pointer_cast<GLTexture>(texture)->BorrowedOwner = std::move(owner);
            return texture;
        }
        class TextureAdapter final : public Kans::Texture2D
        {
        public:
            std::shared_ptr<Device>         GPU;
            Handle<RenderHardware::Texture> Image;
            std::filesystem::path           Path;
            TextureAdapter(std::shared_ptr<Device> gpu, Handle<RenderHardware::Texture> image) :
                GPU(std::move(gpu)), Image(std::move(image))
            {
                auto  native = std::dynamic_pointer_cast<GLTexture>(Image);
                GLint previous;
                glGetIntegerv(native->Desc.Dimension == TextureDimension::Cube ? GL_TEXTURE_BINDING_CUBE_MAP :
                                                                                 GL_TEXTURE_BINDING_2D,
                              &previous);
                glBindTexture(native->Target(), native->Name.ID);
                glTexParameteri(native->Target(), GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(native->Target(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(native->Target(), previous);
            }
            uint32_t GetWidth() const override { return Image->GetDesc().Width; }
            uint32_t GetHeight() const override { return Image->GetDesc().Height; }
            uint32_t GetRenererID() const override { return uint32_t(GPU->GetImGuiTextureID(Image)); }
            void     Bind(uint32_t unit) const override { BindTexture(Image, unit); }
            const std::filesystem::path& GetPath() const override { return Path; }
        };
        Ref<Kans::Texture2D> TextureView(std::shared_ptr<Device> gpu, Handle<Texture> texture)
        {
            return CreateRef<TextureAdapter>(std::move(gpu), std::move(texture));
        }
        Handle<Texture> TextureResource(const Ref<Kans::Texture2D>& texture)
        {
            return std::dynamic_pointer_cast<TextureAdapter>(texture)->Image;
        }
        class IndexAdapter final : public Kans::IndexBuffer
        {
            std::shared_ptr<Device> GPU;
            Handle<Buffer>          BufferResource;
            uint32_t                Count;

        public:
            IndexAdapter(std::shared_ptr<Device> gpu, Handle<Buffer> b, uint32_t count) :
                GPU(std::move(gpu)), BufferResource(std::move(b)), Count(count)
            {}
            void     Bind() const override { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, BufferName(BufferResource)); }
            void     Unbind() const override { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
            uint32_t GetCount() const override { return Count; }
        };
        class VertexAdapter final : public Kans::VertexBuffer
        {
            std::shared_ptr<Device> GPU;
            Handle<Buffer>          BufferResource;
            BufferLayout            Layout {{ShaderDataType::Float3, "a_Position"},
                                            {ShaderDataType::Float3, "a_Normal"},
                                            {ShaderDataType::Float2, "a_UV"},
                                            {ShaderDataType::Float4, "a_Color"},
                                            {ShaderDataType::Float4, "a_Tangent"}};

        public:
            VertexAdapter(std::shared_ptr<Device> gpu, Handle<Buffer> b) :
                GPU(std::move(gpu)), BufferResource(std::move(b))
            {}
            void Bind() const override { glBindBuffer(GL_ARRAY_BUFFER, BufferName(BufferResource)); }
            void Unbind() const override { glBindBuffer(GL_ARRAY_BUFFER, 0); }
            void SetData(const void*, uint32_t) override { throw std::logic_error("Immutable RHI mesh interop view"); }
            void SetBufferLayout(const BufferLayout&) override
            {
                throw std::logic_error("Immutable RHI mesh interop view");
            }
            const BufferLayout& GetLayout() const override { return Layout; }
        };
        class ArrayAdapter final : public Kans::VertexArray
        {
            std::shared_ptr<Device>              GPU;
            Handle<Buffer>                       Vertices, Indices;
            std::vector<Ref<Kans::VertexBuffer>> LegacyVertices;
            Ref<Kans::IndexBuffer>               LegacyIndices;
            Native                               Array;

        public:
            ArrayAdapter(std::shared_ptr<Device> gpu,
                         Handle<Buffer>          vertices,
                         Handle<Buffer>          indices,
                         uint32_t                count,
                         GLuint                  vao) :
                GPU(std::move(gpu)), Vertices(std::move(vertices)), Indices(std::move(indices)),
                LegacyVertices {CreateRef<VertexAdapter>(GPU, Vertices)},
                LegacyIndices(CreateRef<IndexAdapter>(GPU, Indices, count)),
                Array(std::dynamic_pointer_cast<GLBuffer>(Vertices)->Name.Owner, vao, [](GLuint n) {
                    glDeleteVertexArrays(1, &n);
                })
            {
                Bind();
                glBindBuffer(GL_ARRAY_BUFFER, BufferName(Vertices));
                const uint32_t sizes[] = {3, 3, 2, 4, 4}, offsets[] = {0, 12, 24, 32, 48};
                for (uint32_t i = 0; i < 5; ++i)
                {
                    glEnableVertexAttribArray(i);
                    glVertexAttribPointer(
                        i, sizes[i], GL_FLOAT, GL_FALSE, 64, reinterpret_cast<void*>(uintptr_t(offsets[i])));
                }
                LegacyIndices->Bind();
                Unbind();
            }
            void Bind() const override { glBindVertexArray(Array.ID); }
            void Unbind() const override { glBindVertexArray(0); }
            void AddVertexBuffer(const Ref<Kans::VertexBuffer>&) override
            {
                throw std::logic_error("Immutable RHI mesh interop view");
            }
            void SetIndexBuffer(const Ref<Kans::IndexBuffer>&) override
            {
                throw std::logic_error("Immutable RHI mesh interop view");
            }
            const std::vector<Ref<Kans::VertexBuffer>>& GetVertexBuffer() const override { return LegacyVertices; }
            const Ref<Kans::IndexBuffer>&               GetIndexBuffer() const override { return LegacyIndices; }
        };
        Ref<Kans::VertexArray>
        MeshVertexArray(std::shared_ptr<Device> gpu, Handle<Buffer> vertices, Handle<Buffer> indices, uint32_t count)
        {
            GLuint vao;
            glGenVertexArrays(1, &vao);
            return CreateRef<ArrayAdapter>(std::move(gpu), std::move(vertices), std::move(indices), count, vao);
        }
        uint32_t CreateSceneFramebuffer(const std::vector<Handle<Texture>>& colors, const Handle<Texture>& depth)
        {
            GLuint fbo;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            std::vector<GLenum> attachments;
            for (size_t i = 0; i < colors.size(); ++i)
            {
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, TextureName(colors[i]), 0);
                attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
            }
            glDrawBuffers(attachments.size(), attachments.data());
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, TextureName(depth), 0);
            Require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                    "Incomplete scene interop target");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return fbo;
        }
        void BindSceneFramebuffer(uint32_t fbo, uint32_t width, uint32_t height)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, width, height);
        }
        void DeleteSceneFramebuffer(uint32_t fbo)
        {
            if (fbo)
                glDeleteFramebuffers(1, &fbo);
        }
    } // namespace OpenGLInterop
} // namespace Kans::RenderHardware
