#include "kspch.h"
#include "OpenGLImGuiRenderer.h"
#include <glad/glad.h>
#include <imgui.h>
#include <stdexcept>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "Kans3D/Core/Window.h"
namespace Kans::RenderHardware
{
    namespace
    {
        // The bundled renderer establishes its program, inputs, samplers, blend/depth/cull/stencil,
        // viewport/scissor and fill mode. These are its remaining native-state omissions.
        struct ImGuiState
        {
            GLint                       Draw = 0, Read = 0;
            GLboolean                   Mask[4];
            GLboolean                   Dither;
            const std::array<GLenum, 3> Caps {GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD, GL_COLOR_LOGIC_OP};
            std::array<GLboolean, 3>    Enabled;
            std::vector<GLboolean>      Clips;
            explicit ImGuiState(GLint clipCount)
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &Draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &Read);
                glGetBooleani_v(GL_COLOR_WRITEMASK, 0, Mask);
                // Replay disables dithering; the legacy editor UI inherited the context's enabled default.
                Dither = glIsEnabled(GL_DITHER);
                glEnable(GL_DITHER);
                for (size_t i = 0; i < Caps.size(); ++i)
                {
                    Enabled[i] = glIsEnabled(Caps[i]);
                    glDisable(Caps[i]);
                }
                for (GLint i = 0; i < clipCount; ++i)
                {
                    Clips.push_back(glIsEnabled(GL_CLIP_DISTANCE0 + i));
                    glDisable(GL_CLIP_DISTANCE0 + i);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            }
            ~ImGuiState()
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Draw);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, Read);
                glColorMaski(0, Mask[0], Mask[1], Mask[2], Mask[3]);
                Dither ? glEnable(GL_DITHER) : glDisable(GL_DITHER);
                for (size_t i = 0; i < Caps.size(); ++i)
                    Enabled[i] ? glEnable(Caps[i]) : glDisable(Caps[i]);
                for (size_t i = 0; i < Clips.size(); ++i)
                    Clips[i] ? glEnable(GL_CLIP_DISTANCE0 + i) : glDisable(GL_CLIP_DISTANCE0 + i);
            }
        };
        class GLImGuiRenderer final : public ImGuiRenderer
        {
            std::shared_ptr<Device> m_Device;
            bool                    m_Platform;
            GLint                   m_ClipCount           = 0;
            using RenderWindowFn                          = void (*)(ImGuiViewport*, void*);
            RenderWindowFn                 m_RenderWindow = nullptr;
            inline static GLImGuiRenderer* Current        = nullptr;
            static void                    RenderWindow(ImGuiViewport* viewport, void* argument)
            {
                ImGuiState state(Current->m_ClipCount);
                Current->m_RenderWindow(viewport, argument);
            }

        public:
            explicit GLImGuiRenderer(Window* window) : m_Device(GetSharedDevice()), m_Platform(window != nullptr)
            {
                glGetIntegerv(GL_MAX_CLIP_DISTANCES, &m_ClipCount);
                if (m_Platform &&
                    !ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window->GetNativeWindow()), true))
                    throw std::runtime_error("ImGui GLFW initialization failed");
                if (!ImGui_ImplOpenGL3_Init("#version 410"))
                    throw std::runtime_error("ImGui renderer initialization failed");
                if (m_Platform)
                {
                    auto& platform = ImGui::GetPlatformIO();
                    m_RenderWindow = platform.Renderer_RenderWindow;
                    if (m_RenderWindow)
                    {
                        Current                        = this;
                        platform.Renderer_RenderWindow = RenderWindow;
                    }
                }
            }
            ~GLImGuiRenderer() override
            {
                EndImGuiTextures();
                if (Current == this)
                {
                    ImGui::GetPlatformIO().Renderer_RenderWindow = m_RenderWindow;
                    Current                                      = nullptr;
                }
                ImGui_ImplOpenGL3_Shutdown();
                if (m_Platform)
                    ImGui_ImplGlfw_Shutdown();
                m_Device->WaitIdle();
            }
            void NewFrame() override
            {
                BeginImGuiTextures();
                ImGui_ImplOpenGL3_NewFrame();
                if (m_Platform)
                    ImGui_ImplGlfw_NewFrame();
            }
            void Render(ImDrawData* data) override
            {
                ImGuiState state(m_ClipCount);
                ImGui_ImplOpenGL3_RenderDrawData(data);
            }
            void RenderPlatformWindows() override
            {
                if (!m_Platform)
                    return;
                auto* context = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(context);
            }
            void EndFrame() override
            {
                EndImGuiTextures();
                m_Device->WaitIdle();
            }
        };
    } // namespace
    std::unique_ptr<ImGuiRenderer> CreateOpenGLImGuiRenderer(Window* window)
    {
        return std::make_unique<GLImGuiRenderer>(window);
    }
} // namespace Kans::RenderHardware
