#include "kspch.h"
#include "UUID.h"

#include <random>
namespace Kans
{

	//mt19937 
	//÷ɭ��ת�㷨��Mersenne Twister Algorithm,��� MT,1997������� Ϊ���α���������������������������������������㷨
	static std::random_device s_RandomDevice;
	static std::mt19937_64 re(s_RandomDevice());
	static std::uniform_int_distribution<int64_t> s_UniformDistribution;

	static std::mt19937 re32(s_RandomDevice());
	static std::uniform_int_distribution<int64_t> s_UniformDistribution32;


	//Ĭ�Ϲ��캯���ṩһ��uuid
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