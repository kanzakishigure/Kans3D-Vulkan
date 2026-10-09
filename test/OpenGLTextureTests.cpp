#include <glad/glad.h>
#include <gtest/gtest.h>
#ifdef KANS_TEST_USE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Platform/OpenGL/OpenGLRHI.h"
#include "Kans3D/Platform/OpenGL/OpenGLRendererAPI.h"
#include "Kans3D/Platform/OpenGL/OpenGLTexture.h"
#include "Kans3D/Renderer/RHI/RendererAPI.h"

namespace
{
    class OpenGLTextureMipmap : public testing::Test
    {
    protected:
        void SetUp() override
        {
#ifdef KANS_TEST_USE_EGL
            // Surfaceless Mesa works in CI without DISPLAY or a window server.
            display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
            ASSERT_NE(display, EGL_NO_DISPLAY);
            ASSERT_TRUE(eglInitialize(display, nullptr, nullptr));
            ASSERT_TRUE(eglBindAPI(EGL_OPENGL_API));
            const EGLint configAttributes[] = {
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
            EGLConfig config;
            EGLint    count = 0;
            ASSERT_TRUE(eglChooseConfig(display, configAttributes, &config, 1, &count));
            ASSERT_GT(count, 0);
            const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                         4,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         3,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                         EGL_NONE};
            context                   = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
            ASSERT_NE(context, EGL_NO_CONTEXT);
            const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
            surface                          = eglCreatePbufferSurface(display, config, surfaceAttributes);
            ASSERT_NE(surface, EGL_NO_SURFACE);
            ASSERT_TRUE(eglMakeCurrent(display, surface, surface, context));
            ASSERT_TRUE(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(eglGetProcAddress)));
            Kans::OpenGLRHI::EnsureDSAFunctionsLoaded(reinterpret_cast<GLADloadproc>(eglGetProcAddress));
#else
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            window = glfwCreateWindow(16, 16, "Mipmap test", nullptr, nullptr);
            ASSERT_NE(window, nullptr);
            glfwMakeContextCurrent(window);
            ASSERT_TRUE(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)));
            Kans::OpenGLRHI::EnsureDSAFunctionsLoaded(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
#endif
            ASSERT_NE(glGenerateTextureMipmap, nullptr) << "DSA mipmap entry point was not loaded";
            RecordProperty("renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
            RecordProperty("version", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
            ASSERT_EQ(glGetError(), GL_NO_ERROR);
            temp =
                std::filesystem::temp_directory_path() /
                ("kans-mipmap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tga");
        }

        void TearDown() override
        {
            std::error_code ec;
            if (!temp.empty())
                std::filesystem::remove(temp, ec);
#ifdef KANS_TEST_USE_EGL
            if (display != EGL_NO_DISPLAY)
            {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (surface != EGL_NO_SURFACE)
                    eglDestroySurface(display, surface);
                if (context != EGL_NO_CONTEXT)
                    eglDestroyContext(display, context);
                eglTerminate(display);
            }
#else
            if (window)
                glfwDestroyWindow(window);
            glfwTerminate();
#endif
        }

        Kans::TextureSpecification Specification(bool mips = true)
        {
            Kans::TextureSpecification spec;
            spec.Format = Kans::RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;
            spec.Minf = spec.Maxf = Kans::RHIFilter::RHI_FILTER_LINEAR;
            spec.Wrap             = Kans::RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            spec.GenerateMips     = mips;
            return spec;
        }

        void WriteCheckerboard()
        {
            // Uncompressed RGBA TGA: 8x4, every 2x2 block averages to 127.5.
            std::array<unsigned char, 18> header {};
            header[2]  = 2;
            header[12] = 8;
            header[14] = 4;
            header[16] = 32;
            header[17] = 8;
            std::ofstream out(temp, std::ios::binary);
            out.write(reinterpret_cast<const char*>(header.data()), header.size());
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    unsigned char                value = ((x + y) % 2) ? 255 : 0;
                    std::array<unsigned char, 4> pixel {value, value, value, 255};
                    out.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
                }
            ASSERT_TRUE(out.good());
        }

        void CheckChain(GLuint texture, int width, int height)
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            int level = 0;
            for (;;)
            {
                GLint w = 0, h = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &h);
                EXPECT_EQ(w, width) << "level " << level;
                EXPECT_EQ(h, height) << "level " << level;
                ++level;
                if (width == 1 && height == 1)
                    break;
                width  = std::max(1, width / 2);
                height = std::max(1, height / 2);
            }
            GLint levels = 0, minFilter = 0, magFilter = 0;
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
            EXPECT_EQ(levels, level);
            EXPECT_EQ(minFilter, GL_LINEAR_MIPMAP_LINEAR);
            EXPECT_EQ(magFilter, GL_LINEAR);
            EXPECT_EQ(glGetError(), GL_NO_ERROR);
        }

        std::filesystem::path temp;
#ifdef KANS_TEST_USE_EGL
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLContext context = EGL_NO_CONTEXT;
        EGLSurface surface = EGL_NO_SURFACE;
#else
        GLFWwindow* window = nullptr;
#endif
    };

    TEST_F(OpenGLTextureMipmap, FileGeneratesCompleteChainAndAveragedPixels)
    {
        ASSERT_NO_FATAL_FAILURE(WriteCheckerboard());
        Kans::OpenGLTexture2D texture(Specification(), temp);
        ASSERT_EQ(glGetError(), GL_NO_ERROR);
        CheckChain(texture.GetRenererID(), 8, 4);
        std::array<unsigned char, 4> pixel {};
        glGetTexImage(GL_TEXTURE_2D, 3, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        ASSERT_EQ(glGetError(), GL_NO_ERROR);
        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(pixel[i], 128, 1);
        EXPECT_EQ(pixel[3], 255);
    }

    TEST_F(OpenGLTextureMipmap, DisabledKeepsOnlyBaseLevel)
    {
        ASSERT_NO_FATAL_FAILURE(WriteCheckerboard());
        Kans::OpenGLTexture2D texture(Specification(false), temp);
        GLint                 levels = 0, filter = 0;
        glBindTexture(GL_TEXTURE_2D, texture.GetRenererID());
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
        EXPECT_EQ(levels, 1);
        EXPECT_EQ(filter, GL_LINEAR);
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    TEST_F(OpenGLTextureMipmap, EditorIconsGenerateNonPowerOfTwoChains)
    {
        for (const char* name : {"FBX",
                                 "Folder",
                                 "Image",
                                 "Model",
                                 "Scene",
                                 "Material",
                                 "Script",
                                 "Shader",
                                 "Audio",
                                 "File",
                                 "OBJ",
                                 "GLTF",
                                 "GLB",
                                 "Blend",
                                 "DAE",
                                 "Mesh"})
        {
            SCOPED_TRACE(name);
            const auto path = std::filesystem::path(KANS_TEST_ICON_DIR) / (std::string(name) + ".png");
            ASSERT_TRUE(std::filesystem::exists(path));
            Kans::OpenGLTexture2D texture(Specification(), path);
            ASSERT_EQ(glGetError(), GL_NO_ERROR);
            CheckChain(texture.GetRenererID(), texture.GetWidth(), texture.GetHeight());
            GLint levels = 0;
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
            std::array<unsigned char, 4> pixel {};
            glGetTexImage(GL_TEXTURE_2D, levels - 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            EXPECT_GT(pixel[3], 0);
            EXPECT_GT(pixel[0] + pixel[1] + pixel[2], 0);
            EXPECT_EQ(glGetError(), GL_NO_ERROR);
        }
    }
    TEST_F(OpenGLTextureMipmap, ProductUploadPreservesAuthoredMipsSrgbAndUnpackState)
    {
        using namespace Kans;
        TextureData data;
        data.Width      = 3;
        data.Height     = 2;
        data.ColorSpace = TextureColorSpace::SRGB;
        data.Mips.resize(2);
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 3; ++x)
                for (unsigned component = 0; component < 4; ++component)
                    data.Mips[0].Pixels.push_back(std::byte(component == 3 ? 255 : y * 30 + x * 4 + component));
        data.Mips[1].Pixels = {std::byte {17}, std::byte {53}, std::byte {91}, std::byte {127}};
        auto encoded        = EncodeTextureProduct(data);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes  = std::get<std::vector<std::byte>>(encoded);
        auto        loaded = Loader<TextureAsset> {}.Load(
            {AssetID {SourceAssetID::Generate(), 1}, AssetType::Texture, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<TextureAsset>>(loaded));
        GLuint pbo = 0;
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 128, nullptr, GL_STATIC_DRAW);
        const GLenum names[]  = {GL_UNPACK_ALIGNMENT,
                                 GL_UNPACK_ROW_LENGTH,
                                 GL_UNPACK_IMAGE_HEIGHT,
                                 GL_UNPACK_SKIP_PIXELS,
                                 GL_UNPACK_SKIP_ROWS,
                                 GL_UNPACK_SKIP_IMAGES,
                                 GL_UNPACK_SWAP_BYTES,
                                 GL_UNPACK_LSB_FIRST};
        const GLint  values[] = {8, 9, 11, 2, 3, 1, GL_TRUE, GL_TRUE};
        for (size_t i = 0; i < 8; ++i)
            glPixelStorei(names[i], values[i]);
        const auto previousBackend = RendererAPI::Current();
        RendererAPI::SetBackEnd(RendererAPIType::OPENGL);
        auto uploaded = Texture2D::CreateFromProduct(std::get<Ref<TextureAsset>>(loaded)->GetData());
        RendererAPI::SetBackEnd(previousBackend);
        for (size_t i = 0; i < 8; ++i)
        {
            GLint restored = 0;
            glGetIntegerv(names[i], &restored);
            EXPECT_EQ(restored, values[i]);
            glPixelStorei(names[i], i == 0 ? 4 : 0);
        }
        GLint bound = 0;
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &bound);
        EXPECT_EQ(bound, GLint(pbo));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glDeleteBuffers(1, &pbo);
        ASSERT_TRUE(std::holds_alternative<Ref<Texture2D>>(uploaded));
        const auto& texture = std::get<Ref<Texture2D>>(uploaded);
        glBindTexture(GL_TEXTURE_2D, texture->GetRenererID());
        GLint internal = 0, levels = 0, maximum = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_LEVELS, &levels);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maximum);
        EXPECT_EQ(internal, GL_SRGB8_ALPHA8);
        EXPECT_EQ(levels, 2);
        EXPECT_EQ(maximum, 1);
        std::vector<std::byte> actual(24);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        EXPECT_TRUE(std::equal(actual.begin(), actual.begin() + 12, data.Mips[0].Pixels.begin() + 12));
        EXPECT_TRUE(std::equal(actual.begin() + 12, actual.end(), data.Mips[0].Pixels.begin()));
        std::array<std::byte, 4> mip {};
        glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip.data());
        EXPECT_TRUE(std::equal(mip.begin(), mip.end(), data.Mips[1].Pixels.begin()));
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }

    TEST_F(OpenGLTextureMipmap, ProductUploadSupportsOddWidthDataAndNativeFloatValues)
    {
        using namespace Kans;
        for (auto format : {TexturePixelFormat::R8,
                            TexturePixelFormat::RG8,
                            TexturePixelFormat::RGBA8,
                            TexturePixelFormat::RGBA32Float})
        {
            TextureData data;
            data.Width     = 3;
            data.Height    = 1;
            data.Format    = format;
            data.Origin    = TextureOrigin::BottomLeft;
            data.Usage     = TextureUsage::Data;
            data.AlphaMode = TextureAlphaMode::Opaque;
            data.Mips.resize(1);
            size_t components = format == TexturePixelFormat::R8 ? 1 : format == TexturePixelFormat::RG8 ? 2 : 4;
            for (size_t i = 0; i < 3 * components; ++i)
            {
                if (format == TexturePixelFormat::RGBA32Float)
                {
                    float    value = float(i) * 0.25f - 1;
                    uint32_t bits  = 0;
                    std::memcpy(&bits, &value, 4);
                    for (unsigned b = 0; b < 4; ++b)
                        data.Mips[0].Pixels.push_back(std::byte((bits >> (b * 8)) & 255));
                }
                else
                    data.Mips[0].Pixels.push_back(std::byte(i * 9 + 1));
            }
            auto uploaded = OpenGLTexture2D::UploadProduct(data);
            ASSERT_TRUE(std::holds_alternative<Ref<OpenGLTexture2D>>(uploaded));
            glBindTexture(GL_TEXTURE_2D, std::get<Ref<OpenGLTexture2D>>(uploaded)->GetRenererID());
            const GLenum external = components == 1 ? GL_RED : components == 2 ? GL_RG : GL_RGBA;
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            if (format == TexturePixelFormat::RGBA32Float)
            {
                std::vector<float> actual(3 * components);
                glGetTexImage(GL_TEXTURE_2D, 0, external, GL_FLOAT, actual.data());
                for (size_t i = 0; i < actual.size(); ++i)
                    EXPECT_FLOAT_EQ(actual[i], float(i) * 0.25f - 1);
            }
            else
            {
                std::vector<std::byte> actual(3 * components);
                glGetTexImage(GL_TEXTURE_2D, 0, external, GL_UNSIGNED_BYTE, actual.data());
                EXPECT_EQ(actual, data.Mips[0].Pixels);
            }
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            EXPECT_EQ(glGetError(), GL_NO_ERROR);
        }
    }

    TEST_F(OpenGLTextureMipmap, ProductUploadRejectsMalformedAndUnsupportedShapes)
    {
        using namespace Kans;
        TextureData data;
        EXPECT_EQ(std::get<AssetError>(OpenGLTexture2D::UploadProduct(data)).Code, AssetErrorCode::InvalidData);
        data.Kind  = TextureKind::Array2D;
        data.Width = data.Height = 1;
        data.Layers              = 2;
        data.Mips                = {{std::vector<std::byte>(8)}};
        EXPECT_EQ(std::get<AssetError>(OpenGLTexture2D::UploadProduct(data)).Code, AssetErrorCode::NotImplemented);
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }
} // namespace
