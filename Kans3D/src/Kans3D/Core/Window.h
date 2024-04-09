#pragma once
#include "Kans3D/Core/Events/Event.h"
#include "Kans3D/Core/Base/Base.h"


namespace Kans
{
	
	struct WindowSpecification
	{
		std::string Title	= "Kans Engine";
		uint32_t Width		= 1920;
		uint32_t Height		= 1080;
		bool Fullscreen		= false;
		bool HideTitlebar	= true;
		bool VSync			= true;
	};
	class  Window
	{
	public:
		
		using EventCallbackFn = std::function<void(Event&)>;
		virtual ~Window() {}

		virtual void Init() = 0;
		virtual void ProcessEvents() = 0;
		virtual void SwapBuffers() = 0;

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;
		virtual const WindowSpecification& GetWindowSpecification()  const = 0;
		virtual const void SetWindowSpecification(const WindowSpecification& spec) = 0;

		
		virtual void SetEventCallback(const EventCallbackFn& callback)=0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual inline void* GetNativeWindow() const = 0;

		// return a windows pointer of the platform we support
		static Window* Create(const WindowSpecification& props = WindowSpecification());
		
	};
}