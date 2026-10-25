#include "kspch.h"
#include "AudioClipRegistry.h"

namespace Kans
{
    std::optional<AudioSoundID> AudioClipRegistry::Acquire(AudioEngine& engine, const Ref<const AudioClipAsset>& clip)
    {
        if (!engine.IsInitialized() || (m_Engine && m_Engine != &engine) || !clip || !clip->assetID.IsValid() ||
            !clip->IsValid())
            return std::nullopt;
        auto found = m_Clips.find(clip->assetID);
        if (found != m_Clips.end())
        {
            if (found->second.Users == UINT64_MAX)
                return std::nullopt;
            ++found->second.Users;
            return found->second.Sound;
        }
        if (auto valid = ValidateAudioClipData(clip->GetData()); std::holds_alternative<AssetError>(valid))
            return std::nullopt;
        while (m_NextSound && engine.IsSoundRegistered(m_NextSound))
            ++m_NextSound;
        if (!m_NextSound)
            return std::nullopt; // Exhausted; never wrap to an old ID.
        const auto sound = m_NextSound++;
        if (engine.RegisterSound(sound, clip->GetData().Encoded))
            return std::nullopt;
        if (!m_Engine)
        {
            m_Engine = &engine;
            engine.AttachClipRegistry(this);
        }
        m_Clips.emplace(clip->assetID, Entry {sound, 1});
        return sound;
    }

    void AudioClipRegistry::Release(AssetID id)
    {
        auto found = m_Clips.find(id);
        if (found == m_Clips.end())
            return;
        if (--found->second.Users)
            return;
        m_Engine->UnregisterSound(found->second.Sound);
        m_Clips.erase(found);
        if (m_Clips.empty())
        {
            m_Engine->DetachClipRegistry(this);
            m_Engine = nullptr;
        }
    }

    void AudioClipRegistry::Clear()
    {
        if (!m_Engine)
            return;
        for (const auto& [id, entry] : m_Clips)
            m_Engine->UnregisterSound(entry.Sound);
        m_Clips.clear();
        m_Engine->DetachClipRegistry(this);
        m_Engine = nullptr;
    }

    AudioVoiceHandle
    AudioClipRegistry::CreateVoice(AudioEngine& engine, const Ref<const AudioClipAsset>& clip, AudioVoiceDesc overrides)
    {
        if (m_Engine != &engine || !engine.IsInitialized() || !clip || !clip->IsValid())
            return {};
        const auto found = m_Clips.find(clip->assetID);
        if (found == m_Clips.end())
            return {};
        overrides.Sound    = found->second.Sound;
        overrides.LoadMode = clip->GetData().LoadMode;
        overrides.Volume *= clip->GetData().Volume;
        auto voice = engine.CreateVoice(overrides);
        if (engine.IsVoiceValid(voice))
            engine.SetLoopRegion(voice, clip->GetData().LoopBegin, clip->GetData().LoopEnd);
        return voice;
    }
} // namespace Kans
