#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/RHI/Backend.h"
using namespace Kans::RenderHardware;
TEST(BackendSelection, DefaultsToOpenGLAndCommandLineOverridesTheIni)
{
    auto path = std::filesystem::temp_directory_path() / "kans-backend-selection.ini";
    std::filesystem::remove(path);
    EXPECT_EQ(ResolveBackend(path.string()), Backend::OpenGL);
    {
        std::ofstream out(path);
        out << "WorkSpace=x\nRenderBackend=Vulkan\n";
    }
    EXPECT_EQ(ResolveBackend(path.string()), Backend::Vulkan);
    EXPECT_EQ(ResolveBackend(path.string(), {"--render-backend=opengl"}), Backend::OpenGL);
    {
        std::ofstream out(path);
        out << "RenderBackend=OpenGL\n";
    }
    EXPECT_EQ(ResolveBackend(path.string(), {"--render-backend=vulkan"}), Backend::Vulkan);
    std::filesystem::remove(path);
}
TEST(BackendSelection, RejectsInvalidEmptyAndDuplicateSelections)
{
    auto path = std::filesystem::temp_directory_path() / "kans-backend-invalid.ini";
    for (const char* value : {"", "Vulkna", "None", "DirectX"})
    {
        {
            std::ofstream out(path);
            out << "RenderBackend=" << value << '\n';
        }
        EXPECT_THROW(ResolveBackend(path.string()), std::invalid_argument);
    }
    std::filesystem::remove(path);
    for (const char* option : {"--render-backend=", "--render-backend=none", "--render-backend"})
        EXPECT_THROW(ResolveBackend(path.string(), {option}), std::invalid_argument);
    EXPECT_THROW(ResolveBackend(path.string(), {"--render-backend=vulkan", "--render-backend=opengl"}),
                 std::invalid_argument);
}
TEST(BackendSelection, CaptureArgumentsReachApplicationStartupAndRejectMalformedSchedules)
{
    char  app[] = "KansEditor", backend[] = "--render-backend=vulkan", capture[] = "--frame-capture=/tmp/editor.ppm",
          count[] = "--frame-capture-after=90";
    char* args[]  = {app, backend, capture, count};
    SetStartupArguments(4, args);
    EXPECT_EQ(ResolveStartupBackend(""), Backend::Vulkan);
    EXPECT_EQ(GetStartupFrameCapture().Path, "/tmp/editor.ppm");
    EXPECT_EQ(GetStartupFrameCapture().AfterFrames, 90u);
    SetStartupArguments(1, args);
    EXPECT_EQ(ResolveStartupBackend(""), Backend::OpenGL);
    EXPECT_TRUE(GetStartupFrameCapture().Path.empty());
    EXPECT_EQ(ResolveFrameCaptureArguments({"--frame-capture=x.ppm"}).AfterFrames, 60u);
    for (const char* invalid : {"0", "-1", "", "30x", "4294967296"})
        EXPECT_THROW(
            ResolveFrameCaptureArguments({"--frame-capture=x.ppm", std::string("--frame-capture-after=") + invalid}),
            std::invalid_argument);
    EXPECT_THROW(ResolveFrameCaptureArguments({"--frame-capture="}), std::invalid_argument);
    EXPECT_THROW(ResolveFrameCaptureArguments({"--frame-capture-after=10"}), std::invalid_argument);
}

TEST(BackendSelection, FilesystemStartupRewritePreservesTheBackendSetting)
{
    // Concrete bug: first startup computes the root hash and truncates the ini,
    // discarding RenderBackend so the following startup silently changes backend.
    const auto original = std::filesystem::current_path();
    auto root = std::filesystem::temp_directory_path() /
                ("kans-phase4-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto project = root / "project";
    for (const char* name : {"assets", "mono", "Resources"})
        std::filesystem::create_directories(project / name);
    {
        std::ofstream out(project / "Project.ini");
    }
    auto config = project / "KansEditor.ini";
    {
        std::ofstream out(config);
        out << "\xef\xbb\xbfRenderBackend = Vulkan\nWorkSpace=project\nResoucesDir=Resources\n";
    }
    EXPECT_EQ(ResolveBackend(config.string()), Backend::Vulkan);
    std::filesystem::current_path(project);
    Kans::Log::Init();
    Kans::KansFileSystem::Init(config);
    Kans::KansFileSystem::ShutDown();
    std::filesystem::current_path(original);
    EXPECT_EQ(ResolveBackend(config.string()), Backend::Vulkan);
    std::ifstream input(config);
    std::string   contents((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(contents.find("RootPathHashValue="), std::string::npos);
    Kans::Log::ShutDown();
    std::filesystem::remove_all(root);
}
