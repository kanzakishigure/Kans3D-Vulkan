#pragma once

#include "Kans3D/Renderer/RHI/RHITypeProxy.h"
#include "Kans3D/Renderer/RHI/RHIStructProxy.h"
namespace Kans
{



	// the abstract of render able entity
	class RenderResourceBase
	{
	public:
		virtual ~RenderResourceBase() {}

		virtual void Clear() = 0;

	};
	struct IBLResource
	{

	};
	struct ColorGradingResource
	{

	};
	struct StorageBuffer
	{
		// limits
		uint32_t _min_uniform_buffer_offset_alignment{ 256 };
		uint32_t _min_storage_buffer_offset_alignment{ 256 };
		uint32_t _max_storage_buffer_range{ 1 << 27 };
		uint32_t _non_coherent_atom_size{ 256 };

		RHIBuffer* _global_upload_ringbuffer;
		RHIDeviceMemory* _global_upload_ringbuffer_memory;
		void* _global_upload_ringbuffer_memory_pointer;
		std::vector<uint32_t> _global_upload_ringbuffers_begin;
		std::vector<uint32_t> _global_upload_ringbuffers_end;
		std::vector<uint32_t> _global_upload_ringbuffers_size;

		RHIBuffer* _global_null_descriptor_storage_buffer;
		RHIDeviceMemory* _global_null_descriptor_storage_buffer_memory;

		// axis
		RHIBuffer* _axis_inefficient_storage_buffer;
		RHIDeviceMemory* _axis_inefficient_storage_buffer_memory;
		void* _axis_inefficient_storage_buffer_memory_pointer;
	};
	struct GlobalRenderResource
	{
		IBLResource          IBLResource;
		ColorGradingResource ColorGradingResource;
		StorageBuffer        Buffer;
	};

}