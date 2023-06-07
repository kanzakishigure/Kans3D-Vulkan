#pragma once	
#include "Kans3D/Core/Window.h"
namespace Kans {

	class RHI : public RefCounter
	{
	public:
		virtual ~RHI() {};
		virtual void Init() = 0;
		virtual void Shutdown() = 0;
		static Ref<RHI> Create(const Scope<Window>& window);
		static Ref<RHI> Create();
	private:
		
	};

}