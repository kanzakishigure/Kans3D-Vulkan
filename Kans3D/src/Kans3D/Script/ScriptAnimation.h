#pragma once
#include "Kans3D/Animation/Animation.h"
#include "ScriptInterop.h"
namespace Kans::ScriptAnimation
{
    void Install(Scripting::NativeApi&);
    void Queue(uint64_t entity, const AnimationEventOccurrence&);
    void BeginFrame();
    void Reset();
} // namespace Kans::ScriptAnimation
