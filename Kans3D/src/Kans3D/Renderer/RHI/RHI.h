#pragma once	
#include "Kans3D/Core/Window.h"
namespace Kans {

	class RHI : public RefCounter
	{
	public:
		static Ref<RHI> Create();
		static Ref<RHI> Create(const Scope<Window>& window);
		
		virtual ~RHI() {};
		virtual void Init() = 0;
		virtual void Shutdown() = 0;
		virtual void CreateSwapChain() = 0;
		virtual void RecreateSwapchain() = 0;
	private:
		
	};

}