#pragma once
#include"Kans3D/Core/Base/Base.h"
#include"Kans3D/Core/Log/Log.h"
#include <functional>
namespace Kans{

	//事件类，目前没有设置缓冲，意味着如果事件发生，则整个系统会进入线程去处理该事件
	enum class EventType
	{
		None = 0,
		WindowsClose,WindowResize,WindowFocus,WindowLostFocus,WindowMonved,
		AppTick,AppUpdate,AppRender,
		KeyPressed,KeyReleased, KeyTyped,
		MouseButtonPressed,MouseButtonReleased,MouseMoved,MouseScrolled
	};
	//事件的位标识
	enum EventCategory{
		None = 0,
		EventCategoryApplication	= BIT(0),
		EventCategoryInput			= BIT(1),
		EventCategoryKeyboard		= BIT(2),
		EventCategoryMouse			= BIT(3),
		EventCategoryMouseButton	= BIT(4)
	};
#define EVENT_CLASS_TYPE(type) static EventType GetStaticType(){return  EventType::##type;}\
							   virtual EventType GetEventType() const override { return GetStaticType();}\
							   virtual const char* GetName() const override {return #type;}
//宏定义中#是字符串化得意思，会将之后的参数转换为字符串
//##后为变量
//override能重写virtual权限
#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override {return category;}
	//事件的基类，析构函数会在后续子类中进行重构
	class  Event
	{
	public:
		virtual ~Event() = default;
		//const表示不修改对象，=0表示为纯虚函数，无法实例化
		//同时意味着纯虚函数需要我们去实现它才能调用
		virtual EventType GetEventType() const = 0;
		virtual const char*GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		//使用GetName的返回值作为默认返回值，但是在子类中，通过宏重写该函数
		virtual std::string ToString() const { return GetName(); }
		
		inline bool IsInCategory(EventCategory category)
		{
			return GetCategoryFlags() & category;//位检测，是否与模板相符
		}
		bool Handeled = false;//用于标识事件是否已经被处理
		
	};
	class  EventDispatcher
	{
		template<typename T>
		using EventFn = std::function <bool(T&)>;
	public:
		EventDispatcher(Event& event)
		:m_Event(event){}

		template<typename T>
		bool Dispatch(EventFn<T> func)
		{
			if (m_Event.GetEventType()==T::GetStaticType())
			{
				m_Event.Handeled = func(*(T*)&m_Event);
				return true;
			}
			return false;
		}
	private:
		Event& m_Event;
	};
	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}

}