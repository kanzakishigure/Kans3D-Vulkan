#include <glad/glad.h>
#include <imgui.h>
#include <png.h>
#include <stb_image.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Editor/EditorResources.h"
#include "Kans3D/ImGui/EditorFonts.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/ImGuiRenderer.h"
#include "../KansEditor/src/Panels/ContentBrowser/ThumbnailCache.h"
#include "GpuTestContext.h"

using namespace Kans;
namespace RH = Kans::RenderHardware;
namespace fs = std::filesystem;
namespace
{
    class EditorStartupGL : public testing::Test
    {
    protected:
        GpuTestContext Context {GpuTestBackend::OpenGL};
        fs::path       PreviousCwd, Temp;
        bool           FileSystemReady = false;
        void           SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            ASSERT_NO_FATAL_FAILURE(Context.SetUp());
            PreviousCwd = fs::current_path();
            Temp =
                fs::temp_directory_path() /
                ("kans-editor-startup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(Temp);
            // Read the user's actual resources through a symlink; never write into that directory.
            fs::create_directory_symlink(fs::path(KANS_TEST_ICON_DIR).parent_path().parent_path(), Temp / "Resources");
            fs::current_path(Temp);
            Configure("Resources/");
            ImGui::CreateContext();
            auto& io       = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {16, 16};
            io.DeltaTime   = 1.0f / 60;
        }
        void TearDown() override
        {
            EditorResources::ShutDown();
            if (ImGui::GetCurrentContext())
                ImGui::DestroyContext();
            if (FileSystemReady)
                KansFileSystem::ShutDown();
            if (!PreviousCwd.empty())
                fs::current_path(PreviousCwd);
            std::error_code error;
            if (!Temp.empty())
                fs::remove_all(Temp, error);
            Context.TearDown();
        }
        void Configure(const char* resources)
        {
            if (FileSystemReady)
                KansFileSystem::ShutDown();
            {
                std::ofstream ini("Startup.ini");
                ini << "ResoucesDir=" << resources << "\nFontsDir=" << resources << "Fonts/\n";
            }
            KansFileSystem::Init("Startup.ini");
            FileSystemReady = true;
        }
        std::vector<uint8_t> Read(const Ref<Texture2D>& texture)
        {
            const size_t size   = size_t(texture->GetWidth()) * texture->GetHeight() * 4;
            auto         device = RH::GetSharedDevice();
            auto readback = device->CreateBuffer({size, RH::BufferUsage::CopyDestination, RH::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(texture->GetResource(), {}, readback, 0);
            device->Submit(commands);
            device->WaitIdle();
            return readback->Read(0, size);
        }
        void CheckActualFile(const Ref<Texture2D>& texture, const fs::path& path)
        {
            ASSERT_TRUE(texture);
            int w = 0, h = 0, channels = 0;
            // Independent legacy stb oracle, including row flip. A fallback must not make this test pass.
            stbi_set_flip_vertically_on_load(0);
            auto* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
            ASSERT_NE(pixels, nullptr);
            std::vector<uint8_t> expected(size_t(w) * h * 4);
            for (int y = 0; y < h; ++y)
                std::copy_n(pixels + size_t(h - 1 - y) * w * 4, size_t(w) * 4, expected.data() + size_t(y) * w * 4);
            stbi_image_free(pixels);
            ASSERT_EQ(texture->GetWidth(), uint32_t(w));
            ASSERT_EQ(texture->GetHeight(), uint32_t(h));
            EXPECT_EQ(texture->GetPath(), path);
            EXPECT_EQ(texture->GetDevice(), RH::GetSharedDevice());
            EXPECT_EQ(texture->GetResource()->GetDesc().PixelFormat, RH::Format::RGBA8Unorm);
            EXPECT_EQ(Read(texture), expected);
            const auto id = texture->GetImGuiTextureID();
            EXPECT_EQ(id, texture->GetDevice()->GetImGuiTextureID(texture->GetResource(), &texture->GetSampling()));
            EXPECT_EQ(glIsTexture(GLuint(id)), GL_TRUE);
            // Startup icons retain the old complete chain, trilinear minification and clamped edges.
            uint32_t levels = 1;
            for (auto extent = std::max(w, h); extent > 1; extent >>= 1)
                ++levels;
            EXPECT_EQ(texture->GetResource()->GetDesc().MipLevels, levels);
            GLint min = 0, mag = 0, wrap = 0;
            glBindTexture(GL_TEXTURE_2D, GLuint(id));
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
            EXPECT_EQ(min, GL_LINEAR_MIPMAP_LINEAR);
            EXPECT_EQ(mag, GL_LINEAR);
            EXPECT_EQ(wrap, GL_CLAMP_TO_EDGE);
        }
        void DrawFont()
        {
            auto renderer = RH::CreateImGuiRenderer(RH::Backend::OpenGL, nullptr);
            renderer->NewFrame();
            ImGui::NewFrame();
            ImGui::GetBackgroundDrawList()->AddText({0, 0}, IM_COL32_WHITE, "A");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui::Render();
            renderer->Render(ImGui::GetDrawData());
            std::array<uint8_t, 16 * 16 * 4> pixels {};
            glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            // A missing/default font or an unuploaded font atlas must still produce visible text.
            size_t visible = 0;
            for (size_t p = 0; p < pixels.size(); p += 4)
                if (pixels[p] != 0)
                    ++visible;
            EXPECT_GT(visible, 10u); // 'A' at 16 px has substantially more than ten covered pixels.
            renderer->EndFrame();
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
    };

    TEST_F(EditorStartupGL, RealStartupLoadsRelativeResourcesFromChangedCwd)
    {
        // Bug: passing Resources/Editor/... to the absolute-only asset importer aborts every editor startup.
        ASSERT_NE(fs::current_path(), PreviousCwd);
        ASSERT_EQ(KansFileSystem::GetResoucesFolder(), fs::path("Resources/"));
        ASSERT_FALSE(KansFileSystem::GetResoucesFolder().is_absolute());
        ASSERT_NO_THROW(EditorResources::Init()); // Exact call made by EditorLayer::OnAttach.
        const std::pair<const Ref<Texture2D>*, const char*> icons[] = {{&EditorResources::FbxFileIcon, "FBX"},
                                                                       {&EditorResources::ModelFileIcon, "Model"},
                                                                       {&EditorResources::ObjFileIcon, "OBJ"},
                                                                       {&EditorResources::GltfFileIcon, "GLTF"},
                                                                       {&EditorResources::GlbFileIcon, "GLB"},
                                                                       {&EditorResources::BlendFileIcon, "Blend"},
                                                                       {&EditorResources::DaeFileIcon, "DAE"},
                                                                       {&EditorResources::MeshFileIcon, "Mesh"},
                                                                       {&EditorResources::TextureFileIcon, "Image"},
                                                                       {&EditorResources::SceneFileIcon, "Scene"},
                                                                       {&EditorResources::MaterialFileIcon, "Material"},
                                                                       {&EditorResources::ScriptFileIcon, "Script"},
                                                                       {&EditorResources::ShaderFileIcon, "Shader"},
                                                                       {&EditorResources::AudioFileIcon, "Audio"},
                                                                       {&EditorResources::FileIcon, "File"},
                                                                       {&EditorResources::FolderIcon, "Folder"}};
        for (const auto& icon : icons)
        {
            SCOPED_TRACE(icon.second);
            ASSERT_NO_FATAL_FAILURE(CheckActualFile(
                *icon.first, fs::path("Resources/Editor/ContentBrowser") / (std::string(icon.second) + ".png")));
        }
        ASSERT_NO_FATAL_FAILURE(
            CheckActualFile(EditorResources::BorderShadow, "Resources/Editor/Border/Translucency.png"));
        LoadEditorFonts(); // Exact helper used by ImGuiLayer::OnAttach, with the same relative FontsDir.
        auto& io = ImGui::GetIO();
        ASSERT_TRUE(io.Fonts->Build());
        ASSERT_NE(io.FontDefault, nullptr);
        ASSERT_EQ(io.Fonts->Fonts.Size, 2);
        EXPECT_STREQ(io.Fonts->Fonts[0]->GetDebugName(), "Roboto-Regular.ttf, 16px");
        EXPECT_STREQ(io.Fonts->Fonts[1]->GetDebugName(), "Roboto-Bold.ttf, 16px");
        for (auto* font : io.Fonts->Fonts)
        {
            EXPECT_NE(font->FindGlyphNoFallback('A'), nullptr);
            EXPECT_NE(font->FindGlyphNoFallback(0xf07b), nullptr); // FontAwesome folder, merged into both fonts.
        }
        ASSERT_NO_FATAL_FAILURE(DrawFont());
    }

    TEST_F(EditorStartupGL, MissingResourcesLogAndStillRenderWithFallbacks)
    {
        // Bug: a missing icon asserts/throws, or a missing Roboto file reaches ImGui's file-loader assertion.
        Configure("MissingResources/");
        std::ostringstream errors;
        auto               sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(errors);
        sink->set_level(spdlog::level::err);
        struct Capture
        {
            spdlog::sink_ptr Sink;
            explicit Capture(spdlog::sink_ptr sink) : Sink(std::move(sink))
            {
                Log::GetCoreLogger()->sinks().push_back(Sink);
            }
            ~Capture()
            {
                auto& sinks = Log::GetCoreLogger()->sinks();
                sinks.erase(std::remove(sinks.begin(), sinks.end(), Sink), sinks.end());
            }
        } capture(sink);
        ASSERT_NO_THROW(EditorResources::Init());
        ASSERT_TRUE(EditorResources::FileIcon);
        EXPECT_EQ(Read(EditorResources::FileIcon),
                  (std::vector<uint8_t> {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255}));
        EXPECT_EQ(EditorResources::GetFileIcon("missing.png"), EditorResources::TextureFileIcon);
        EXPECT_NE(errors.str().find("MissingResources/Editor/ContentBrowser/FBX.png"), std::string::npos);
        EXPECT_NE(errors.str().find("magenta checker fallback"), std::string::npos);
        ASSERT_NO_THROW(LoadEditorFonts());
        EXPECT_NE(errors.str().find("Roboto-Regular.ttf"), std::string::npos);
        EXPECT_NE(errors.str().find("Roboto-Bold.ttf"), std::string::npos);
        EXPECT_NE(errors.str().find("fa-solid-900.ttf"), std::string::npos);
        ASSERT_TRUE(ImGui::GetIO().Fonts->Build());
        ASSERT_NE(ImGui::GetIO().FontDefault->FindGlyphNoFallback('A'), nullptr);
        ASSERT_NO_FATAL_FAILURE(DrawFont());
    }

    TEST_F(EditorStartupGL, ThreeChannelOddWidthFilePreservesBytesAndOrientation)
    {
        // Bugs: RGB is rejected/padded incorrectly, tight odd-width rows use GL's default unpack alignment,
        // or migration restricts the legacy decoder to the importer's smaller set of formats (PNM here).
        const std::vector<uint8_t> top {11, 22, 33, 44, 55, 66, 77, 88, 99};
        const std::vector<uint8_t> bottom {101, 102, 103, 111, 112, 113, 121, 122, 123};
        {
            std::ofstream file("rgb.ppm", std::ios::binary);
            file << "P6\n3 2\n255\n";
            file.write(reinterpret_cast<const char*>(top.data()), top.size());
            file.write(reinterpret_cast<const char*>(bottom.data()), bottom.size());
        }
        auto texture = Texture2D::Create({}, fs::path("rgb.ppm"));
        ASSERT_EQ(texture->GetWidth(), 3u);
        ASSERT_EQ(texture->GetHeight(), 2u);
        EXPECT_EQ(texture->GetPath(), fs::path("rgb.ppm"));
        std::vector<uint8_t> expected;
        for (const auto& row : {bottom, top})
            for (size_t p = 0; p < row.size(); p += 3)
                expected.insert(expected.end(), {row[p], row[p + 1], row[p + 2], 255});
        EXPECT_EQ(Read(texture), expected);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(EditorStartupGL, SixteenBitLdrIsTruncatedToLegacyBytesWithoutMetadataTransforms)
    {
        // Bugs: 16-bit PNG is mistaken for HDR and stored at half precision, rounded instead of truncated,
        // or a nonstandard gAMA chunk makes the runtime display loader reject/transform legacy stored values.
        FILE* file = std::fopen("wide.png", "wb");
        ASSERT_NE(file, nullptr);
        auto* png  = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        auto* info = png_create_info_struct(png);
        ASSERT_EQ(setjmp(png_jmpbuf(png)), 0);
        png_init_io(png, file);
        png_set_IHDR(png,
                     info,
                     2,
                     1,
                     16,
                     PNG_COLOR_TYPE_RGB,
                     PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);
        png_set_gAMA(png, info, 0.6);
        png_write_info(png, info);
        unsigned char row[] = {0x01, 0xff, 0x80, 0x01, 0xff, 0x00, 0x7f, 0xff, 0x00, 0xff, 0x40, 0x01};
        png_write_row(png, row);
        png_write_end(png, info);
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        auto texture = Texture2D::Create({}, fs::path("wide.png"));
        ASSERT_EQ(texture->GetResource()->GetDesc().PixelFormat, RH::Format::RGBA8Unorm);
        EXPECT_EQ(Read(texture), (std::vector<uint8_t> {1, 128, 255, 255, 127, 0, 64, 255}));
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(EditorStartupGL, HdrStaysLinearHalfPrecisionWithOpaqueAlphaAndFlippedRows)
    {
        // Bugs: HDR inherits an sRGB request, clips values above one, loses RGB's implicit opaque alpha,
        // stores full-float instead of legacy half, or fails to flip/generate the complete mip chain.
        {
            std::ofstream file("light.hdr", std::ios::binary);
            file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 3\n";
            for (const auto& pixel :
                 {std::array<uint8_t, 4> {128, 64, 32, 130}, std::array<uint8_t, 4> {64, 128, 192, 130}})
                for (int x = 0; x < 3; ++x)
                    file.write(reinterpret_cast<const char*>(pixel.data()), 4);
        }
        TextureSpecification spec;
        spec.SRGB         = true;
        spec.GenerateMips = true;
        spec.Minf = spec.Maxf = RH::Filter::Linear;
        auto texture          = Texture2D::Create(spec, fs::path("light.hdr"));
        ASSERT_EQ(texture->GetResource()->GetDesc().PixelFormat, RH::Format::RGBA16Float);
        ASSERT_EQ(texture->GetResource()->GetDesc().MipLevels, 2u);
        glBindTexture(GL_TEXTURE_2D, GLuint(texture->GetImGuiTextureID()));
        std::array<float, 24> pixels {};
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
            {
                const std::array<float, 4> expected =
                    y == 0 ? std::array<float, 4> {1, 2, 3, 1} : std::array<float, 4> {2, 1, 0.5f, 1};
                for (int c = 0; c < 4; ++c)
                    EXPECT_EQ(pixels[(y * 3 + x) * 4 + c],
                              expected[c]); // All values are exactly representable binary16.
            }
        std::array<float, 4> mip {};
        glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_FLOAT, mip.data());
        EXPECT_EQ(mip, (std::array<float, 4> {1.5f, 1.5f, 1.75f, 1}));
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }

    TEST_F(EditorStartupGL, MalformedAndEmptyFilesReturnAUsableChecker)
    {
        // Bug: only NotFound is handled, leaving corrupt/empty resources to terminate startup.
        std::ofstream("empty.png");
        {
            std::ofstream corrupt("corrupt.png");
            corrupt << "not an image";
        }
        for (const char* path : {"empty.png", "corrupt.png", "absent.png"})
        {
            SCOPED_TRACE(path);
            auto texture = Texture2D::Create({}, fs::path(path));
            ASSERT_TRUE(texture);
            EXPECT_EQ(texture->GetPath(), fs::path(path));
            EXPECT_EQ(Read(texture),
                      (std::vector<uint8_t> {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255}));
            EXPECT_EQ(glIsTexture(GLuint(texture->GetImGuiTextureID())), GL_TRUE);
        }
    }

    TEST_F(EditorStartupGL, RealThumbnailWorkerUploadsPixelsFromAbsoluteBrowserPaths)
    {
        // Bug: the replacement Product upload changes browser thumbnail format/row origin or fails with a new CWD.
        // Browser paths remain absolute (Project canonicalizes AssetDirectory); reuse the unedited real worker.
        const auto              path     = fs::absolute("Resources/Editor/ContentBrowser/Image.png");
        const auto              modified = fs::last_write_time(path);
        Browser::ThumbnailCache cache;
        Ref<Texture2D>          thumbnail;
        const auto              deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!thumbnail && std::chrono::steady_clock::now() < deadline)
        {
            thumbnail = cache.Get(path, modified, 4096); // Larger than this icon: no thumbnail resampling.
            cache.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        ASSERT_TRUE(thumbnail);
        int w = 0, h = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        auto* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        ASSERT_NE(pixels, nullptr);
        std::vector<uint8_t> expected(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
            std::copy_n(pixels + size_t(h - 1 - y) * w * 4, size_t(w) * 4, expected.data() + size_t(y) * w * 4);
        stbi_image_free(pixels);
        EXPECT_EQ(thumbnail->GetResource()->GetDesc().PixelFormat, RH::Format::RGBA8Unorm);
        EXPECT_EQ(Read(thumbnail), expected);
        EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
} // namespace
