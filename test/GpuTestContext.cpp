#include "GpuTestContext.h"
#include <glad/glad.h>
#include <gtest/gtest.h>
#ifdef KANS_TEST_USE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#include <GLFW/glfw3.h>
#endif
#include "Kans3D/Platform/OpenGL/OpenGLRHI.h"

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
    window = glfwCreateWindow(16, 16, "GPU test", nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    glfwMakeContextCurrent(window);
    ASSERT_TRUE(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)));
    Kans::OpenGLRHI::EnsureDSAFunctionsLoaded(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
#endif
}
void GpuTestContext::TearDown()
{
    if (!m_State)
        return;
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
