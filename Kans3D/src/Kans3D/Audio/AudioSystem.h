#pragma once

#include "Kans3D/Audio/AudioEngine.h"

namespace Kans
{
    class AudioSystem
    {
    public:
        [[nodiscard]] static std::optional<std::string> Init(const AudioEngineConfig& config);
        static void Shutdown();
        static AudioEngine* Get();
    };
}
