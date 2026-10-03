#include "kspch.h"
#include <glad/glad.h>
#include <chrono>
#include <iostream>
#include <GLFW/glfw3.h>
#include "Kans3D/Asset/Importer/AssimpMeshImporter.h"
#include "Kans3D/Core/Window.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Renderer/Renderer.h"

using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

int main()
{
    const std::filesystem::path root(KANS_PROJECT_ROOT);
    const auto model = root / "KansEditor/assets/model/new room scene/Come Celebrate Thanksgiving With "
                              "Invrsion!154/gltf/Come Celebrate Thanksgiving With Invrsion!.gltf";
    Kans::Log::Init();
    // Use the editor's deployed shaders, outside the measured loading interval.
    std::filesystem::current_path(root / "build/linux-gcc/KansEditor/Debug");
    Kans::KansFileSystem::Init(root / "test/ModelLoadBenchmark.ini");
    Kans::RendererAPI::SetBackEnd(Kans::RendererAPIType::OPENGL);
    if (!glfwInit())
    {
        std::cerr << "GLFW initialization failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    Kans::WindowSpecification spec;
    spec.Width = spec.Height = 64;
    spec.Title               = "Model loading benchmark";
    spec.VSync               = false;
    auto window              = Kans::Scope<Kans::Window>(Kans::Window::Create(spec));
    window->Init();
    Kans::Renderer::Init(window);
    std::cout << "GPU: " << glGetString(GL_RENDERER) << "\n";
    std::cout << "sample,cpu_import_ms,finalize_cpu_and_gl_ms,gpu_wait_ms,total_ms,vertices,triangles,submeshes\n";
    // Relative path preserves the current importer's path concatenation behavior.
    const auto relative = std::filesystem::relative(model, std::filesystem::current_path());
    for (int sample = 0; sample < 3; ++sample)
    {
        glFinish();
        const auto               start = Clock::now();
        Kans::AssimpMeshImporter importer(relative);
        auto                     mesh   = importer.ImportToMeshSourceCpu();
        const auto               parsed = Clock::now();
        if (!mesh)
        {
            std::cerr << "CPU import failed\n";
            return 1;
        }
        mesh->FinalizeGpuResources();
        const auto submitted = Clock::now();
        glFinish();
        const auto finished = Clock::now();
        if (!mesh->IsGpuReady() || glGetError() != GL_NO_ERROR)
        {
            std::cerr << "GPU finalization failed\n";
            return 1;
        }
        uint64_t vertices = 0, triangles = 0;
        for (const auto& submesh : mesh->GetSubMesh())
        {
            vertices += submesh.VertexCount;
            triangles += submesh.IndexCount;
        }
        std::cout << sample << ',' << Ms(start, parsed) << ',' << Ms(parsed, submitted) << ','
                  << Ms(submitted, finished) << ',' << Ms(start, finished) << ',' << vertices << ',' << triangles << ','
                  << mesh->GetSubMesh().size() << std::endl;
    }
    Kans::Renderer::Shutdown();
    Kans::KansFileSystem::ShutDown();
}
