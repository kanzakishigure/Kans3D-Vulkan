#pragma once
#include "Kans3D/Scene/Entity.h"
namespace Kans
{
	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity()=default;
		template <typename T>
		T& GetComponent()
		{
			return m_Entity.GetComponent<T>();
		}
	protected:
		virtual void OnUpdate(TimeStep ts){}
		virtual void OnCreate(){}
		virtual void OnDestory(){}
	private:
		Entity m_Entity;
		friend class Scene;
	};

}