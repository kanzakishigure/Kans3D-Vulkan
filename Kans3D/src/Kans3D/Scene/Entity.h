#pragma once
#include <entt.hpp>

#include "Kans3D/Scene/Scene.h"
#include "Kans3D/Scene/Components.h"

namespace Kans
{
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene);
		Entity(const Entity& other) = default;


		//为实现调用组件构造函数而不是直接传入构造好的组件，使用forward将参数转发
		template<typename T, typename ... Args>
		T& AddComponent(Args&&... args)
		{
			CORE_ASSERT(!HasComponent<T>(), "Entity Already Have component ");

			T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdd<T>(*this,component);
			return component;
		}

		template<typename T>
		T& GetComponent()
		{
			CORE_ASSERT(HasComponent<T>(), "Entity don't Have component ");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}
		template<typename T>
		const T& GetComponent() const
		{
			CORE_ASSERT(HasComponent<T>(), "Entity don't Have component ");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}
		template<typename ...T>
		bool HasComponent()
		{
			return m_Scene->m_Registry.all_of<T...>(m_EntityHandle);
		}
		template<typename ...T>
		bool HasComponent() const
		{
			return m_Scene->m_Registry.all_of<T...>(m_EntityHandle);
		}
		template<typename T>
			void RemoveComponent()
		{
			CORE_ASSERT(HasComponent<T>(), "Entity don't Have component ");
			//remove方法只有在组件存在是会进行擦除，若组件不存在，则不会执行
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}
		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }

		operator bool() const { return (m_EntityHandle != entt::null) && m_Scene; }

		operator uint32_t() const { return (uint32_t)m_EntityHandle; }
		operator entt::entity() const { return m_EntityHandle; }

		bool operator== (const Entity& other) const { return other.m_EntityHandle == this->m_EntityHandle; }
		bool operator!= (const Entity& other) const { return !(*this == other); }

	private:
		entt::entity m_EntityHandle{entt::null};
		Scene* m_Scene = nullptr;
	};
}