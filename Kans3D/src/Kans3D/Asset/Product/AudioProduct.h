#pragma once
#include "Kans3D/Asset/Runtime/AudioClipData.h"

namespace Kans
{
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError> EncodeAudioProduct(const AudioClipData& data);
    [[nodiscard]] std::variant<AudioClipData, AssetError> DecodeAudioProduct(const std::byte* bytes, size_t size);
} // namespace Kans
