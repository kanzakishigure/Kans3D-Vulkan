#include "kspch.h"
#include "OpenGLRHI.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
namespace Kans {

void OpenGLRHI::EnsureDSAFunctionsLoaded(GLADloadproc load) {
  // GLAD already loaded DSA via GL 4.5 core — nothing to do
  if (glad_glCreateTextures != nullptr)
    return;

  GLint numExt = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
  for (GLint i = 0; i < numExt; ++i) {
    const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
    if (ext && strcmp(ext, "GL_ARB_direct_state_access") == 0) {
      // Load all DSA (GL_ARB_direct_state_access) entry points.
     
      // -- Buffers --
      glad_glCreateBuffers    = (PFNGLCREATEBUFFERSPROC)   load("glCreateBuffers");
      glad_glNamedBufferData  = (PFNGLNAMEDBUFFERDATAPROC) load("glNamedBufferData");

      // -- Textures --
      glad_glCreateTextures    = (PFNGLCREATETEXTURESPROC)    load("glCreateTextures");
      glad_glTextureStorage2D  = (PFNGLTEXTURESTORAGE2DPROC)  load("glTextureStorage2D");
      glad_glTextureSubImage2D = (PFNGLTEXTURESUBIMAGE2DPROC) load("glTextureSubImage2D");
      glad_glTextureParameteri = (PFNGLTEXTUREPARAMETERIPROC) load("glTextureParameteri");
      glad_glBindTextureUnit   = (PFNGLBINDTEXTUREUNITPROC)   load("glBindTextureUnit");

      // -- Vertex Arrays --
      glad_glCreateVertexArrays = (PFNGLCREATEVERTEXARRAYSPROC)load("glCreateVertexArrays");

      // -- Framebuffers --
      glad_glCreateFramebuffers = (PFNGLCREATEFRAMEBUFFERSPROC)load("glCreateFramebuffers");

      break;
    }
  }
}
        OpenGLRHI::OpenGLRHI(const Scope<Window>& window)
		:m_WindowHandle(window.get())
	{
		CORE_ASSERT(m_WindowHandle,"窗口句柄为空，无法绑定渲染上下文到窗口")
	}

	OpenGLRHI::OpenGLRHI()
		:m_WindowHandle(nullptr)
	{
		CORE_INFO_TAG("Rnederer", "current Context set to offscreen rendering");
	}

	void OpenGLRHI::Init()
	{
		PROFILE_FUCTION();

		if (m_WindowHandle != nullptr)
		{
			glfwMakeContextCurrent((GLFWwindow*)m_WindowHandle->GetNativeWindow());
			m_WindowHandle->SetVSync(m_WindowHandle->IsVSync());
		}
		
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		CORE_ASSERT(status, "无法初始化Gald");
		CORE_INFO("opengl info:");
		CORE_INFO("opengl vendor:{0}", glGetString(GL_VENDOR));
		CORE_INFO("opengl render:{0}", glGetString(GL_RENDERER));
		CORE_INFO("opengl version:{0}", glGetString(GL_VERSION));
		EnsureDSAFunctionsLoaded((GLADloadproc)glfwGetProcAddress);
	}
	
	void OpenGLRHI::Shutdown()
	{

	}

	void OpenGLRHI::CreateSwapChain()
	{

	}

	void OpenGLRHI::RecreateSwapchain()
	{
		
	}

}