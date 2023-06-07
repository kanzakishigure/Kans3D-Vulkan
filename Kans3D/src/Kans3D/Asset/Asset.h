#pragma once
#include "Kans3D/Asset/AssetType.h"
#include "Kans3D/Core/UUID.h"
#include "Kans3D/Core/Base/Ref.h"
namespace Kans 
{
	using AssetHandle = UUID;


	class Asset:public RefCounter
	{
	public:

		AssetHandle Handle = 0;
		uint16_t Flags = (uint16_t)AssetFlag::None;

		virtual ~Asset(){}

		static AssetType GetStaticType() { return AssetType::None; }
		virtual AssetType GetAssetType() const { return AssetType::None; }

		bool IsValid() const {	return ((Flags & (uint16_t)AssetFlag::Missing) | (Flags & (uint16_t)AssetFlag::Invalid)) == 0; }

		virtual bool operator == (const Asset& other )
		{
			return Handle != other.Handle;
		}

		virtual bool operator != (const Asset& other)
		{
			return !(*this == other);
		}

		bool IsFlagSet(AssetFlag flag) const { return Flags & (uint16_t)flag; }
		void SetFlag(AssetFlag flag,bool Value = true)
		{
			//��Ҫ��flagĳһλ��1����ֱ����flag���л�����
			//��Ҫ��flagĳһλ��0����flagȡ�������������
			if (Value)
				Flags |= (uint16_t)flag;
			else
				Flags &= ~(uint16_t)flag;
		}
	};
}

