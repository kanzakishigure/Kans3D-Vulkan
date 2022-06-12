#pragma once
#include"Kans3D/Events/Event.h"
#include<sstream>
namespace Kans{
	
	class  WindowResizeEvent: public Event
	{
	public:
		WindowResizeEvent (unsigned int Width, unsigned int Hight)
			: m_Width(Width), m_Hight(Hight) {};
		 
		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent:" << m_Width <<","<< m_Hight ;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

		unsigned int GetWidth()const { return m_Width; }
		unsigned int GetHeight()const { return m_Hight; }

	private:
		unsigned int m_Width, m_Hight;
	};
	class  WindowCloseEvent:public Event
	{
	public:
		WindowCloseEvent()=default;
		 
		EVENT_CLASS_TYPE(WindowsClose)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:

	};
	class  AppTickEvent : public Event
	{
	public:
		AppTickEvent ()=default;
		EVENT_CLASS_TYPE(AppTick)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:

	};
	class  AppUpdateEvent:public Event
	{
	public:
		AppUpdateEvent () = default;
		EVENT_CLASS_TYPE(AppUpdate)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:

	};
	class  AppRenderEvent : public Event
	{
	public:
		AppRenderEvent() = default;

		EVENT_CLASS_TYPE(AppRender)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
	/*
	类中的宏可以等价替换为
	static EventType GetStaticType(){return  EventType::type;}
	virtual EventType GetEventType() const override { return GetStaticType();}
	virtual const char* GetName() const override {return #type;}
    */
}