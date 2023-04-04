#pragma once

#include<atomic>
#include "Memory.h"
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
}