#pragma once
#include "Kans3D/Events/Event.h"
#include "Kans3D/Core/Base.h"

///本文件是对window类的抽象描述
///函数均为纯虚函数，且无任何依赖
namespace Kans
{
	//窗口的抽象化属性
	struct WindowSpecification
	{
		std::string Title = "Kans Engine";
		unsigned int Width = 1920;
		unsigned int Height = 1080;
		bool Fullscreen ;
		bool HideTitlebar ;
	};
	class  Window
	{
	public:
		//使用function容器存储void（Event&）的函数
		using EventCallbackFn = std::function<void(Event&)>;
		virtual ~Window() {}

		virtual void Init() = 0;
		virtual void ProcessEvents() = 0;
		virtual void SwapBuffers() = 0;

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;
		//window 结构
		virtual void SetEventCallback(const EventCallbackFn& callback)=0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual inline void* GetNativeWindow() const = 0;
		//这些部分需要在实际的平台上实现，这里应该将依赖隔离，只提供接口
		static Scope<Window> Create(const WindowSpecification& props = WindowSpecification());
		//使用结构体构造函数对结构体引用赋值
	};
}