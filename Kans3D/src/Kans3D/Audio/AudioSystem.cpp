#include "kspch.h"
#include "Kans3D/Audio/AudioSystem.h"

namespace Kans
{
    namespace
    {
        std::unique_ptr<AudioEngine> s_Engine;
    }

    std::optional<std::string> AudioSystem::Init(const AudioEngineConfig& config)
    {
        Shutdown();
        auto engine = std::make_unique<AudioEngine>();
        if (auto error = engine->Init(config))
            return error;
        s_Engine = std::move(engine);
        return std::nullopt;
    }

    void AudioSystem::Shutdown() { s_Engine.reset(); }
    AudioEngine* AudioSystem::Get() { return s_Engine.get(); }
}
