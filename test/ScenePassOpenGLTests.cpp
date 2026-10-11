#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/Scene.h"
#include "GpuTestContext.h"

namespace
{
    using namespace Kans;
    using namespace Kans::RenderHardware;
    using Image             = RenderHardware::Texture;
    constexpr uint32_t Size = 64;
    class ScenePassGL : public testing::Test
    {
    protected:
        std::unique_ptr<GpuTestContext> Context;
        Ref<Scene>                      Logic;
        Scope<RenderScene>              Scene;
        Ref<SceneRenderer>              Renderer;
        SceneInfo                       Info {};
        RendererAPIType                 PreviousAPI = RendererAPIType::NONE;
        void                            SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Context = std::make_unique<GpuTestContext>(GpuTestBackend::OpenGL);
            ASSERT_NO_FATAL_FAILURE(Context->SetUp());
            PreviousAPI = RendererAPI::Current();
            RendererAPI::SetBackEnd(RendererAPIType::OPENGL);
            Logic = CreateRef<Kans::Scene>();
            Scene = CreateScope<RenderScene>(Logic);
            Scene->OnViewportResize(Size, Size);
            Renderer                    = CreateRef<SceneRenderer>(Scene.get());
            Info.sceneCamera.camera     = Camera(glm::mat4(1));
            Info.sceneCamera.viewMatrix = glm::mat4(1);
        }
        void TearDown() override
        {
            Renderer2D::Shutdown();
            Renderer.reset();
            Scene.reset();
            Logic.reset();
            Context.reset();
            RendererAPI::SetBackEnd(PreviousAPI);
        }
        std::vector<uint8_t> Read(const Handle<Image>& image, TextureSubresource sub = {})
        {
            size_t bytes    = size_t(std::max(1u, image->GetDesc().Width >> sub.Mip)) *
                              std::max(1u, image->GetDesc().Height >> sub.Mip) *
                              (image->GetDesc().PixelFormat == Format::RGBA16Float ? 8 : 4);
            auto   device   = Renderer->GetDevice();
            auto   buffer   = device->CreateBuffer({bytes, BufferUsage::CopyDestination, MemoryUsage::Readback});
            auto   commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(image, sub, buffer);
            device->Submit(commands);
            device->WaitIdle();
            return buffer->Read(0, bytes);
        }
        std::array<uint8_t, 4> Pixel(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y)
        {
            y             = DeviceRow(y, Size, Renderer->GetDevice()->GetCoordinateConventions());
            size_t offset = (y * Size + x) * 4;
            return {pixels.at(offset), pixels.at(offset + 1), pixels.at(offset + 2), pixels.at(offset + 3)};
        }
        glm::vec4 HalfPixel(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t x, uint32_t y)
        {
            uint16_t words[4];
            std::memcpy(words, pixels.data() + (y * width + x) * 8, 8);
            return {glm::unpackHalf1x16(words[0]),
                    glm::unpackHalf1x16(words[1]),
                    glm::unpackHalf1x16(words[2]),
                    glm::unpackHalf1x16(words[3])};
        }
        Handle<Image> Solid(glm::vec4 color, TextureDimension dimension = TextureDimension::Texture2D)
        {
            auto        device = Renderer->GetDevice();
            TextureDesc desc;
            desc.Dimension   = dimension;
            desc.PixelFormat = Format::RGBA32Float;
            desc.Usage       = RenderHardware::TextureUsage::Sampled | RenderHardware::TextureUsage::CopyDestination;
            auto image       = device->CreateTexture(desc);
            auto upload      = device->CreateBuffer({sizeof(color), BufferUsage::CopySource, MemoryUsage::Upload});
            upload->Write(0, &color, sizeof(color));
            auto commands = device->CreateCommandList();
            for (uint32_t face = 0; face < (dimension == TextureDimension::Cube ? 6u : 1u); ++face)
                commands->CopyBufferToTexture(upload, 0, image, {0, face});
            device->Submit(commands);
            device->WaitIdle();
            return image;
        }
        void Environment()
        {
            // Direction-coded Radiance HDR: swapping a face or mirroring either
            // face axis changes independently checked R/G/B values.
            auto path = std::filesystem::temp_directory_path() / "kans-ibl-direction-1c3.hdr";
            {
                std::ofstream output(path, std::ios::binary);
                output << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
                for (uint32_t y = 0; y < 32; ++y)
                    for (uint32_t x = 0; x < 64; ++x)
                    {
                        double        longitude = ((x + 0.5) / 64.0 - 0.5) * 2 * glm::pi<double>();
                        double        latitude  = (0.5 - (y + 0.5) / 32.0) * glm::pi<double>();
                        glm::dvec3    direction {std::cos(longitude) * std::cos(latitude),
                                                 std::sin(latitude),
                                                 std::sin(longitude) * std::cos(latitude)};
                        unsigned char rgbe[4] {uint8_t(128 * (1 + .75 * direction.x)),
                                               uint8_t(128 * (1 + .75 * direction.y)),
                                               uint8_t(128 * (1 + .75 * direction.z)),
                                               129};
                        output.write(reinterpret_cast<char*>(rgbe), 4);
                    }
            }
            Scene->PrepareEnvironment(path);
            Renderer->PrepareEnvironment({32, 8, 32, 32});
            std::filesystem::remove(path);
        }
    };

    TEST_F(ScenePassGL, EquirectangularCaptureKeepsEveryFaceAndBothAxes)
    {
        Environment();
        auto cube = Renderer->GetSkybox();
        ASSERT_TRUE(cube);
        for (uint32_t face = 0; face < 6; ++face)
        {
            auto pixels = Read(cube, {0, face});
            for (auto xy : {glm::uvec2(16, 16), glm::uvec2(5, 10), glm::uvec2(25, 23)})
            {
                float     u = 2 * (xy.x + .5f) / 32 - 1, v = 2 * (xy.y + .5f) / 32 - 1;
                glm::vec3 directions[] = {{1, -v, -u}, {-1, -v, u}, {u, 1, v}, {u, -1, -v}, {u, -v, 1}, {-u, -v, -1}};
                glm::vec3 expected     = glm::vec3(1) + .75f * glm::normalize(directions[face]);
                auto      color        = HalfPixel(pixels, 32, xy.x, xy.y);
                // .02 covers RGBE truncation (<1/128), spherical bilinear
                // interpolation and half rounding; a mirrored axis differs by >.2.
                for (uint32_t c = 0; c < 3; ++c)
                    EXPECT_NEAR(color[c], expected[c], .02) << face << ":" << xy.x << "," << xy.y << " channel " << c;
                EXPECT_EQ(color.a, 1);
            }
        }
    }

    TEST_F(ScenePassGL, PrefilterRoughnessMipsActuallyBroadenTheDirectionalLobe)
    {
        Environment();
        auto image = Renderer->GetEnvironment().RadianceMap;
        ASSERT_TRUE(image);
        // Same direction-coded HDR has a bright +X red lobe. Increasing GGX
        // roughness must reduce its +X face mean, not copy mip zero to every level.
        std::array<double, 5> means {};
        for (uint32_t mip = 0; mip < 5; ++mip)
        {
            auto     pixels = Read(image, {mip, 0});
            uint32_t width  = 32 >> mip;
            for (uint32_t y = 0; y < width; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    means[mip] += HalfPixel(pixels, width, x, y).r / (width * width);
        }
        EXPECT_GT(means[0], 1.55);
        EXPECT_GT(means[0] - means[4], .15);
        EXPECT_GT(means[1] - means[3], .05);
        EXPECT_GT(means[3] - means[4], .03);
    }

    glm::dvec2 IntegrateBrdf(double ndv, double roughness)
    {
        glm::dvec3 view {std::sqrt(1 - ndv * ndv), 0, ndv};
        glm::dvec2 sum {0};
        for (uint32_t i = 0; i < 1024; ++i)
        {
            // Arithmetic digit reversal is independent of the shader's bit-mask implementation.
            uint32_t bits    = i;
            double   radical = 0, weight = .5;
            while (bits)
            {
                radical += (bits % 2) * weight;
                bits /= 2;
                weight *= .5;
            }
            double phi = 2 * glm::pi<double>() * i / 1024, a = roughness * roughness;
            double cosTheta = std::sqrt((1 - radical) / (1 + (a * a - 1) * radical));
            double sinTheta = std::sqrt(1 - cosTheta * cosTheta);
            // Shader's tangent basis at N=(0,0,1) maps H.x to -Y and H.y to X.
            glm::dvec3 h {std::sin(phi) * sinTheta, -std::cos(phi) * sinTheta, cosTheta};
            double     vdh = std::max(glm::dot(view, h), 0.0);
            auto       l   = glm::normalize(2 * vdh * h - view);
            double     ndl = std::max(l.z, 0.0);
            if (ndl <= 0)
                continue;
            double k          = roughness * roughness / 2;
            double g          = (ndv / (ndv * (1 - k) + k)) * (ndl / (ndl * (1 - k) + k));
            double visibility = g * vdh / (h.z * ndv), fresnel = std::pow(1 - vdh, 5);
            sum += glm::dvec2((1 - fresnel) * visibility, fresnel * visibility);
        }
        return sum / 1024.0;
    }
    TEST_F(ScenePassGL, BrdfLutMatchesIntegrationAtAsymmetricCoordinates)
    {
        // Catches a V flip, swapped NdotV/roughness or a lost integration channel.
        Environment();
        auto lut = Renderer->GetBrdfLUT();
        ASSERT_TRUE(lut);
        auto pixels = Read(lut);
        for (auto xy : {glm::uvec2(3, 5), glm::uvec2(23, 6), glm::uvec2(6, 25), glm::uvec2(26, 23)})
        {
            auto expected = IntegrateBrdf((xy.x + .5) / 32, (xy.y + .5) / 32);
            auto actual   = HalfPixel(pixels, 32, xy.x, xy.y);
            // Float shader accumulation plus half output rounding; CPU oracle uses doubles.
            EXPECT_NEAR(actual.r, expected.x, .001);
            EXPECT_NEAR(actual.g, expected.y, .001);
        }
    }

    TEST_F(ScenePassGL, ExplicitGridOverlayLeavesDepthAndBlendsAlphaWhileSkyboxStaysBehindSprites)
    {
        Renderer2D::Init();
        // Projection maps the XZ grid to screen XY, with constant depth 0.
        Info.sceneCamera.camera = Camera(
            glm::mat4(glm::vec4(1, 0, 0, 0), glm::vec4(0, 0, 1, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 0, 1)));
        Renderer->BeginScene(Info);
        Renderer->SubmitGrid(.5f, 4, glm::scale(glm::mat4(1), glm::vec3(2)), {true, false});
        Renderer->EndScene();
        auto target = Renderer->GetRenderTarget();
        auto depth = Read(target.Depth.Target), color = Read(target.Colors[0].Target);
        // Exact packed depth24: a grid depth write would replace 0xffffff by 0x800000.
        for (uint32_t y = 0; y < Size; ++y)
            for (uint32_t x = 0; x < Size; ++x)
            {
                uint32_t word;
                std::memcpy(&word, depth.data() + (y * Size + x) * 4, 4);
                ASSERT_EQ(word >> 8, 0xffffffu) << x << "," << y;
                const uint8_t red = color[(y * Size + x) * 4];
                ASSERT_EQ(color[(y * Size + x) * 4 + 3], red == 5 ? 255 : 191); // Legacy straight-alpha grid blend.
                ASSERT_TRUE(red == 5 || red == 117);
            }
        EXPECT_NE(Pixel(color, 4, 4), (std::array<uint8_t, 4> {5, 5, 5, 255}));
        Info.sceneCamera.camera = Camera(glm::mat4(1));
        auto sky                = Solid({0, 4, 0, 1}, TextureDimension::Cube);
        Renderer->BeginScene(Info);
        Renderer2D::BeginScene(Info.sceneCamera.camera, glm::mat4(1), {true});
        Renderer2D::DrawQuad(glm::vec3(0), glm::vec2(1), glm::vec4(1, 0, 0, 1));
        Renderer2D::EndScene();
        Renderer->SubmitSkybox(sky);
        Renderer->EndScene();
        color = Read(target.Colors[0].Target);
        EXPECT_EQ(Pixel(color, 32, 32), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        EXPECT_EQ(Pixel(color, 4, 4), (std::array<uint8_t, 4> {0, 230, 0, 255}));
    }

    TEST_F(ScenePassGL, SpriteTextureChangesBeyond32SlotsPreserveBindingsAndBlendOrder)
    {
        Renderer2D::Init();
        Renderer->BeginScene(Info);
        Renderer2D::ResetStats();
        Renderer2D::BeginScene(Info.sceneCamera.camera, glm::mat4(1), {true});
        std::vector<Ref<Texture2D>> images;
        for (uint32_t i = 0; i < 40; ++i)
        {
            TextureSpecification desc;
            desc.Width = desc.Height = 1;
            desc.Format              = RHI_FORMAT_R8G8B8A8_SRGB;
            std::array<uint8_t, 4> color {uint8_t(17 + i * 5), uint8_t(239 - i * 5), uint8_t(31 + i * 3), 255};
            images.push_back(Texture2D::Create(desc, Kans::Buffer(color.data(), 4)));
            float x = -1 + (i % 8 + .5f) * .25f, y = -1 + (i / 8 + .5f) * .4f;
            Renderer2D::DrawQuad(glm::vec3(x, y, 0), glm::vec2(.24f, .38f), images.back());
        }
        Renderer2D::EndScene();
        Renderer->EndScene();
        auto color = Read(Renderer->GetRenderTarget().Colors[0].Target);
        for (uint32_t i = 0; i < 40; ++i)
            EXPECT_EQ(Pixel(color, (i % 8) * 8 + 4, uint32_t((i / 8 + .5f) * Size / 5)),
                      (std::array<uint8_t, 4> {uint8_t(17 + i * 5), uint8_t(239 - i * 5), uint8_t(31 + i * 3), 255}))
                << i;
        EXPECT_EQ(Renderer2D::GetStats().QuadCount, 40u);
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 40u);
        Renderer->BeginScene(Info);
        Renderer2D::ResetStats();
        Renderer2D::BeginScene(Info.sceneCamera.camera, glm::mat4(1), {true});
        // Separate textures and decreasing depth force A/B/A draws; grouping all A's
        // together changes the straight-alpha blend result at the centre.
        Renderer2D::DrawQuad(glm::vec3(0, 0, .3f), glm::vec2(1), images[0], glm::vec4(1, 1, 1, .5f));
        Renderer2D::DrawQuad(glm::vec3(0, 0, .2f), glm::vec2(1), images[39], glm::vec4(1, 1, 1, .5f));
        Renderer2D::DrawQuad(glm::vec3(0, 0, .1f), glm::vec2(1), images[0], glm::vec4(1, 1, 1, .5f));
        Renderer2D::EndScene();
        Renderer->EndScene();
        color                        = Read(Renderer->GetRenderTarget().Colors[0].Target);
        auto                  actual = Pixel(color, 32, 32);
        std::array<double, 3> a {17, 239, 31}, b {212, 44, 148};
        for (uint32_t c = 0; c < 3; ++c)
            EXPECT_NEAR(actual[c], a[c] * .625 + b[c] * .25 + 5 * .125, 1); // RGBA8 rounding at three blends.
        EXPECT_NEAR(actual[3], 143, 1);
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 3u);
    }
    TEST_F(ScenePassGL, DefaultSpritesKeepLegacyAlphaDepthWritesAndEqualDepthOrder)
    {
        // Catches accidental straight-alpha blending, LESS instead of LEQUAL,
        // or disabled depth writes in the runtime/editor sprite defaults.
        Renderer2D::Init();
        Renderer->BeginScene(Info);
        Renderer2D::BeginScene(Info.sceneCamera.camera, glm::mat4(1));
        Renderer2D::DrawQuad(glm::vec3(0), glm::vec2(1), glm::vec4(1, 0, 0, .5f));
        Renderer2D::DrawQuad(glm::vec3(0), glm::vec2(1), glm::vec4(0, 1, 0, .5f));
        Renderer2D::EndScene();
        Renderer->SubmitSkybox(Solid({4, 0, 0, 1}, TextureDimension::Cube));
        Renderer->EndScene();
        auto target = Renderer->GetRenderTarget();
        EXPECT_EQ(Pixel(Read(target.Colors[0].Target), 32, 32), (std::array<uint8_t, 4> {0, 255, 0, 128}));
        auto     depth = Read(target.Depth.Target);
        uint32_t word;
        std::memcpy(&word, depth.data() + (32 * Size + 32) * 4, 4);
        EXPECT_EQ(word >> 8, 0x800000u);
    }

    TEST_F(ScenePassGL, SpriteCapacityFlushKeepsTheLastQuadAndStatistics)
    {
        // Catches an overflowing vertex/index batch, lost final quad, or reuse of
        // an already submitted upload range when the 10000-quad capacity is hit.
        Renderer2D::Init();
        Renderer->BeginScene(Info);
        Renderer2D::ResetStats();
        Renderer2D::BeginScene(Info.sceneCamera.camera, glm::mat4(1), {true});
        for (uint32_t i = 0; i < 9999; ++i)
            Renderer2D::DrawQuad(glm::vec3(5, 5, 0), glm::vec2(1), glm::vec4(0, 1, 0, 1));
        Renderer2D::DrawQuad(glm::vec3(0, 0, 0), glm::vec2(1), glm::vec4(0, 0, 1, 1));
        Renderer2D::DrawQuad(glm::vec3(0, 0, -.1f), glm::vec2(1), glm::vec4(1, 0, 0, 1));
        Renderer2D::EndScene();
        Renderer->EndScene();
        auto color = Read(Renderer->GetRenderTarget().Colors[0].Target);
        EXPECT_EQ(Pixel(color, 32, 32), (std::array<uint8_t, 4> {255, 0, 0, 255}));
        EXPECT_EQ(Pixel(color, 4, 4), (std::array<uint8_t, 4> {5, 5, 5, 255}));
        EXPECT_EQ(Renderer2D::GetStats().QuadCount, 10001u);
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 2u);
        EXPECT_EQ(Renderer2D::GetStats().GetTotalVertexCount(), 40004u);
        EXPECT_EQ(Renderer2D::GetStats().GetTotalIndexCount(), 60006u);
    }

} // namespace
