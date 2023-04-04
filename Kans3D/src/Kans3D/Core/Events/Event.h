#pragma once
#include"Kans3D/Core/Base/Base.h"
#include"Kans3D/Core/Log/Log.h"
#include <functional>
namespace Kans{

	//�¼��࣬Ŀǰû�����û��壬��ζ������¼�������������ϵͳ������߳�ȥ������¼�
	enum class EventType
	{
		None = 0,
		WindowsClose,WindowResize,WindowFocus,WindowLostFocus,WindowMonved,
		AppTick,AppUpdate,AppRender,
		KeyPressed,KeyReleased, KeyTyped,
		MouseButtonPressed,MouseButtonReleased,MouseMoved,MouseScrolled
	};
	//�¼���λ��ʶ
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
//�궨����#���ַ���������˼���Ὣ֮��Ĳ���ת��Ϊ�ַ���
//##��Ϊ����
//override����дvirtualȨ��
#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override {return category;}
	//�¼��Ļ��࣬�����������ں��������н����ع�
	class  Event
	{
	public:
		virtual ~Event() = default;
		//const��ʾ���޸Ķ���=0��ʾΪ���麯�����޷�ʵ����
		//ͬʱ��ζ�Ŵ��麯����Ҫ����ȥʵ�������ܵ���
		virtual EventType GetEventType() const = 0;
		virtual const char*GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		//ʹ��GetName�ķ���ֵ��ΪĬ�Ϸ���ֵ�������������У�ͨ������д�ú���
		virtual std::string ToString() const { return GetName(); }
		
		inline bool IsInCategory(EventCategory category)
		{
			return GetCategoryFlags() & category;//λ��⣬�Ƿ���ģ�����
		}
		bool Handeled = false;//���ڱ�ʶ�¼��Ƿ��Ѿ�������
		
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