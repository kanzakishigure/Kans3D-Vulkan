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
#include <filesystem>
#include <fstream>
#include "Kans3D/Platform/OpenGL/OpenGLRHI.h"
#include "Kans3D/Platform/OpenGL/OpenGLTexture.h"

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
} // namespace
