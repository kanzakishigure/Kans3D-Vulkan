#pragma once
#include <variant>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Audio/AudioEngine.h"

namespace Kans
{
    inline constexpr uint64_t AudioProductMaximumBytes = 512ull * 1024 * 1024;
    enum class AudioEncoding : uint16_t
    {
        Wav,
        Vorbis,
        Mp3,
        Flac
    };

    struct AudioClipData
    {
        AudioEncoding        Encoding = AudioEncoding::Wav;
        AudioSoundInfo       Info {};
        AudioLoadMode        LoadMode  = AudioLoadMode::Decompress;
        float                Volume    = 1;
        uint64_t             LoopBegin = 0, LoopEnd = 0; // Source frames; end is always resolved.
        std::vector<uint8_t> Waveform;
        std::shared_ptr<const std::vector<std::byte>> Encoded;
    };

    // Header-only validation also used before allocating a decoded Product payload.
    [[nodiscard]] std::variant<std::monostate, AssetError> ValidateAudioClipInfo(const AudioClipData& data);
    [[nodiscard]] std::variant<std::monostate, AssetError> ValidateAudioClipData(const AudioClipData& data);

    class AudioClipAsset final : public Asset
    {
    public:
        explicit AudioClipAsset(AudioClipData data) : m_Data(std::move(data)) {}
        static AssetType     GetStaticType() { return AssetType::AudioClip; }
        AssetType            GetAssetType() const override { return GetStaticType(); }
        const AudioClipData& GetData() const { return m_Data; }

    private:
        AudioClipData m_Data;
    };
} // namespace Kans
