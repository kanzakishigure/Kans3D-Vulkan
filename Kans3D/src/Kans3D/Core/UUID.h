#pragma once
#include "Kans3D/Core/Base.h"

namespace Kans
{
	//uuid通常为128bit，用来唯一标识场景内的资源,目前暂时使用64bit用作uuid
	class UUID
	{
	public:
		UUID();
		UUID(uint64_t uuid);
		UUID(const UUID& other);

		operator uint64_t() { return m_UUID; }
		operator const uint64_t() const { return m_UUID; }

		bool operator ==(const UUID& other) const { return other.m_UUID == m_UUID; }
		bool operator !=(const UUID& other) const { return !(other == *this); }
	private:
		uint64_t m_UUID;
	};


	class UUID32
	{
	public:
		UUID32();
		UUID32(uint32_t uuid);
		UUID32(const UUID32& other);

		operator uint32_t() { return m_UUID; }
		operator const uint32_t() const { return m_UUID; }
		bool operator ==(const UUID32& other) const { return other.m_UUID == m_UUID; }
		bool operator !=(const UUID32& other) const { return !(other == *this); }
	private:
		uint32_t m_UUID;
	};
}
//in order to fix using std::unordered_map<UUID,Entity> we need provide hash template
namespace std {
	template <>
	struct hash<Kans::UUID>
	{
		typedef size_t resualt_type;
		typedef Kans::UUID argyment_type;
		//uuid:id is able and should be use to identify a entity;
		size_t operator()(const Kans::UUID& id) const
		{
			return (uint64_t)id;
		}
	};
	template <>
	struct hash<Kans::UUID32>
	{
		typedef size_t resualt_type;
		typedef Kans::UUID32 argyment_type;
		size_t operator()(const Kans::UUID32& id) const
		{
			return hash<uint32_t>()((uint32_t)id);
		}
	};

}