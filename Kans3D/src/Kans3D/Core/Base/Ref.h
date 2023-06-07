#pragma once

#include<atomic>
#include "Memory.h"
#include <memory>
namespace Kans
{
	class RefCounter
	{
	public:
		void IncRefCount() const
		{
			++m_RefCount;
		}
		void DecRefCount() const
		{
			--m_RefCount;
		}
		uint32_t GetRefCount() const { return m_RefCount.load(); }
	private:
		mutable std::atomic<uint32_t> m_RefCount = 0;

	};

	template <typename T>
	using Scope = std::unique_ptr<T>;
	template <typename T, typename ...Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template <typename T>
	using Ref = std::shared_ptr<T>;
	template <typename T, typename ...Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	

	template <typename T>
	using WeakRef = std::weak_ptr<T>;

}