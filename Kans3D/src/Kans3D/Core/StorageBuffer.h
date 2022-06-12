#pragma once
#include "kans3D/Core/Base.h"
#include "kans3D/Core/Log.h"
#include <glm/glm.hpp>
namespace Kans
{
	struct StorageBuffer
	{
		void* Data;
		uint32_t Size;

		StorageBuffer(void* data ,uint32_t size)
		:Data(data),Size(size)
		{

			
		}
		StorageBuffer()
			:Data(nullptr), Size(0)
		{

		}
		void Allocate(uint32_t size)
		{
			delete[](byte*)Data;
			Data = nullptr;

			if (size == 0)
				return;

			Data = (void*)new byte[size];
			std::memset(Data, 0, size);
			Size = size;
		}
		void Release()
		{
			delete[] (byte*)Data;
			Size = 0;
			Data = nullptr;
		}
		void Write(void* value, uint32_t offset, uint32_t size)
		{
			HZ_ASSERT(offset + size <= Size, "Buffer overflow !");
			std::memcpy((byte*)Data + offset, value, size);

		}
		template <typename T>
		T& Read(uint32_t offset)
		{
			return *(T*)((byte*)Data + offset);
		}
	};
}