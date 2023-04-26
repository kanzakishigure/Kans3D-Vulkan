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
			//若要将flag某一位置1，则直接用flag进行或运算
			//若要将flag某一位置0，则将flag取反后进行与运算
			if (Value)
				Flags |= (uint16_t)flag;
			else
				Flags &= ~(uint16_t)flag;
		}
	};
}

