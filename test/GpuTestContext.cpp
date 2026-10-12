#include "GpuTestContext.h"
#include <glad/glad.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <gtest/gtest.h>
#ifdef KANS_TEST_USE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#include <GLFW/glfw3.h>
#endif
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/OpenGL/OpenGLDevice.h"
#include "Kans3D/RHI/Vulkan/VulkanDevice.h"

struct GpuTestContext::State
{
#ifdef KANS_TEST_USE_EGL
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
#else
    GLFWwindow* window = nullptr;
#endif
};
GpuTestContext::GpuTestContext(GpuTestBackend backend) : m_State(std::make_unique<State>()), m_Backend(backend) {}
GpuTestContext::~GpuTestContext() { TearDown(); }
void GpuTestContext::SetUp()
{
    if (m_Backend == GpuTestBackend::Vulkan)
    {
        // Before even constructing an instance, refuse an unpinned test process.
        const char* driver = std::getenv("VK_DRIVER_FILES");
        const char* legacy = std::getenv("VK_ICD_FILENAMES");
        ASSERT_NE(driver, nullptr);
        ASSERT_NE(legacy, nullptr);
        ASSERT_STREQ(driver, "/usr/share/vulkan/icd.d/lvp_icd.json");
        ASSERT_STREQ(legacy, "/usr/share/vulkan/icd.d/lvp_icd.json");
        Kans::RenderHardware::InitializeHeadlessDevice(Kans::RenderHardware::Backend::Vulkan, CreateDevice());
        return;
    }
    ASSERT_EQ(m_Backend, GpuTestBackend::OpenGL);
    if (!m_State)
        m_State = std::make_unique<State>();
#ifdef KANS_TEST_USE_EGL
    auto& display = m_State->display;
    auto& context = m_State->context;
    auto& surface = m_State->surface;
#else
    auto& window = m_State->window;
#endif
#ifdef KANS_TEST_USE_EGL
    // Surfaceless Mesa works in CI without DISPLAY or a window server.
    display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    ASSERT_NE(display, EGL_NO_DISPLAY);
    ASSERT_TRUE(eglInitialize(display, nullptr, nullptr));
    ASSERT_TRUE(eglBindAPI(EGL_OPENGL_API));
    // Real ImGui pixel checks require RGBA8; unconstrained EGL often selects RGB565.
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE,
                                       EGL_PBUFFER_BIT,
                                       EGL_RENDERABLE_TYPE,
                                       EGL_OPENGL_BIT,
                                       EGL_RED_SIZE,
                                       8,
                                       EGL_GREEN_SIZE,
                                       8,
                                       EGL_BLUE_SIZE,
                                       8,
                                       EGL_ALPHA_SIZE,
                                       8,
                                       EGL_NONE};
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
    Kans::RenderHardware::LoadOpenGLEntryPoints(
        reinterpret_cast<Kans::RenderHardware::OpenGLLoadProc>(eglGetProcAddress));
#else
    ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(16, 16, "GPU test", nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    glfwMakeContextCurrent(window);
    Kans::RenderHardware::LoadOpenGLEntryPoints(
        reinterpret_cast<Kans::RenderHardware::OpenGLLoadProc>(glfwGetProcAddress));
#endif
    Kans::RenderHardware::InitializeDevice(Kans::RenderHardware::Backend::OpenGL);
}
void GpuTestContext::TearDown()
{
    if (!m_State)
        return;
    if (m_Backend == GpuTestBackend::Vulkan)
    {
        Kans::RenderHardware::ShutdownDevice();
        m_State.reset();
        return;
    }
    Kans::RenderHardware::ShutdownDevice();
#ifdef KANS_TEST_USE_EGL
    auto& display = m_State->display;
    auto& context = m_State->context;
    auto& surface = m_State->surface;
#else
    auto& window = m_State->window;
#endif
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
    m_State.reset();
}

std::unique_ptr<Kans::RenderHardware::Device> GpuTestContext::CreateDevice(const Kans::RenderHardware::DeviceDesc& desc)
{
    switch (m_Backend)
    {
        case GpuTestBackend::OpenGL:
            return Kans::RenderHardware::CreateOpenGLDevice(desc);
        case GpuTestBackend::Vulkan: {
            Kans::RenderHardware::VulkanDeviceOptions options;
            options.RequireCpuDevice                        = true;
            options.Validation                              = true;
            options.SynchronizationValidation               = true;
            // gtest records failures from driver callback threads too. This sink
            // remains valid through resource/device/instance destruction.
            options.ValidationError = [](const std::string& message) { ADD_FAILURE() << message; };
            return Kans::RenderHardware::CreateVulkanDevice(desc, options);
        }
    }
    throw std::invalid_argument("Unsupported GPU test backend");
}
