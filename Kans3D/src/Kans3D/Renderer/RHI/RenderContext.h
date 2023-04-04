#pragma once	
#include "Kans3D/Core/Window.h"
namespace Kans {

	class RenderContext
	{
	public:
		virtual ~RenderContext() {};
		virtual void Init() = 0;
		virtual void Shutdown() = 0;
		static Ref<RenderContext> Create(const Scope<Window>& window);
		static Ref<RenderContext> Create();
	private:
		
	};

}