#include "kspch.h"
#include "UUID.h"

#include <random>
namespace Kans
{

	//mt19937 
	//梅森旋转算法（Mersenne Twister Algorithm,简称 MT,1997年提出） 为解决伪随机数产生随机数质量不高提出的新随机数生成算法
	static std::random_device s_RandomDevice;
	static std::mt19937_64 re(s_RandomDevice());
	static std::uniform_int_distribution<int64_t> s_UniformDistribution;

	static std::mt19937 re32(s_RandomDevice());
	static std::uniform_int_distribution<int64_t> s_UniformDistribution32;


	//默认构造函数提供一个uuid
	UUID::UUID()
		:m_UUID(s_UniformDistribution(re))
	{

	}

	UUID::UUID(uint64_t uuid)
		:m_UUID(uuid)
	{

	}

	UUID::UUID(const UUID& other)
		:m_UUID(other.m_UUID)
	{

	}

	UUID32::UUID32()
		:m_UUID(s_UniformDistribution32(re32))
	{

	}

	UUID32::UUID32(uint32_t uuid)
		:m_UUID(uuid)
	{

	}

	UUID32::UUID32(const UUID32& other)
		:m_UUID(other.m_UUID)
	{

	}

}