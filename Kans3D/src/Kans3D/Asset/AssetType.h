#pragma once
#include "Kans3D/Core/Base/Base.h"
namespace Kans
{
	enum class AssetFlag : uint16_t
	{
		None = 0,
		Missing = BIT(0),
		Invalid = BIT(1)
	}; 

	enum class AssetType : uint16_t
	{
		None		= 0,
		Scene		= 1,
		Texture		= 2,
		Material	= 3,
		Mesh		= 4,
		MeshSource  = 5,
	  
	};
}