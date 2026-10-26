#pragma once
#include "Kans3D/Timeline/Timeline.h"
#include "ScriptInterop.h"
namespace Kans::ScriptTimeline
{
    void Install(Scripting::NativeApi&);
    void Queue(UUID player, UUID target, const TimelineEvent&, bool finished);
    void BeginFrame();
    void Reset();
} // namespace Kans::ScriptTimeline
