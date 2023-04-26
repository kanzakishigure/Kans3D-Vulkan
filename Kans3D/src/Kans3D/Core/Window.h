#pragma once
#include "Kans3D/Core/Events/Event.h"
#include "Kans3D/Core/Base/Base.h"

///���ļ��Ƕ�window��ĳ�������
///������Ϊ���麯���������κ�����
namespace Kans
{
	//���ڵĳ�������
	struct WindowSpecification
	{
		std::string Title = "Kans Engine";
		uint32_t Width = 1920;
		uint32_t Height = 1080;
		bool Fullscreen ;
		bool HideTitlebar ;
		bool VSync = true;
	};
	class  Window
	{
	public:
		//ʹ��function�����洢void��Event&���ĺ���
		using EventCallbackFn = std::function<void(Event&)>;
		virtual ~Window() {}

		virtual void Init() = 0;
		virtual void ProcessEvents() = 0;
		virtual void SwapBuffers() = 0;

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;
		virtual const WindowSpecification& GetWindowSpecification()  const = 0;
		virtual const void SetWindowSpecification(const WindowSpecification& spec) = 0;

		//window �ṹ
		virtual void SetEventCallback(const EventCallbackFn& callback)=0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual inline void* GetNativeWindow() const = 0;

		//��Щ������Ҫ��ʵ�ʵ�ƽ̨��ʵ�֣�����Ӧ�ý��������룬ֻ�ṩ�ӿ�
		static Window* Create(const WindowSpecification& props = WindowSpecification());
		//ʹ�ýṹ�幹�캯���Խṹ�����ø�ֵ
	};
}