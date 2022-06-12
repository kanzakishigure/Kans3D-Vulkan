#pragma once
#include"Kans3D/Core/Window.h"
#include"Kans3D/Renderer/RenderContext.h"
#include "Platform/Vulkan/VulkanSwapChain.h"

#include<GLFW/glfw3.h>

namespace Kans {
	class  WindowsWindow : public Window
	{
	public:
		
		WindowsWindow(const WindowProps& props);
		virtual ~WindowsWindow();
		void OnUpdate() override;
		inline unsigned int GetWidth() const override		{ return m_Data.Width; }
		inline unsigned int GetHeight() const override		{ return m_Data.Height; }

		//结构
		inline void SetEventCallback(const EventCallbackFn& callback) override     { m_Data.EventCallback = callback; }
		void SetVSync(bool enable) override;
		bool IsVSync() const override ;
		virtual inline void* GetNativeWindow() const override { return m_Window; }
	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown ();

	private:
		GLFWwindow* m_Window;
		struct WindowData
		{
			std::string Title;
			unsigned int Width, Height;
			bool VSync;

			EventCallbackFn EventCallback;
		};
		WindowData m_Data;
		Ref<RenderContext> m_Context;
		//通过结构体来传递全部数据
	private:
		static int s_GLFWWindowsCount;
	private:
		VulkanSwapChain m_SwapChain;

	};
}