#pragma once
#include "Kans3D/Core/Window.h"
namespace Kans
{

    class RHI : public RefCounter
    {
    public:
        virtual ~RHI() {};
        virtual void Init()              = 0;
        virtual void Shutdown()          = 0;
        virtual void CreateSwapChain()   = 0;
        virtual void RecreateSwapchain() = 0;

    private:
    };

} // namespace Kans