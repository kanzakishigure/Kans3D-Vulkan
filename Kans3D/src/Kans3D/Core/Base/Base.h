#pragma once
#include<memory>
#include <atomic>
#include "Kans3D/Core/PlatformDetection.h"

#include "Ref.h"

#define BIT(x) (1<<x)
#define BIND_EVENT_FN(fn) std::bind(&fn,this,std::placeholders::_1)


#include "Kans3D/Core/Base/Assert.h"
namespace Kans
{
	using byte = uint8_t;
}
