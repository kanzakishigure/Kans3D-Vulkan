#pragma once
#include "ScriptInterop.h"
namespace Kans::ScriptUI
{
    void Install(Scripting::NativeApi&);
    void BeginFrame();
    void Reset();
} // namespace Kans::ScriptUI
