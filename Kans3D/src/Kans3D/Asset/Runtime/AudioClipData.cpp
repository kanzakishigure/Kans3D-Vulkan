#include "kspch.h"
#include "AudioClipData.h"
#include <cmath>

namespace Kans
{
    std::variant<std::monostate, AssetError> ValidateAudioClipInfo(const AudioClipData& data)
    {
        if (data.Encoding > AudioEncoding::Flac ||
            (data.LoadMode != AudioLoadMode::Decompress && data.LoadMode != AudioLoadMode::Stream) ||
            !data.Info.Channels || data.Info.Channels > 254 || !data.Info.SampleRate || !data.Info.LengthFrames ||
            !std::isfinite(data.Volume) || data.Volume < 0 || data.Volume > 4 || data.LoopBegin >= data.LoopEnd ||
            data.LoopEnd > data.Info.LengthFrames)
            return AssetError {AssetErrorCode::InvalidData, "Invalid AudioClip info/settings"};
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> ValidateAudioClipData(const AudioClipData& data)
    {
        if (auto valid = ValidateAudioClipInfo(data); auto* error = std::get_if<AssetError>(&valid))
            return *error;
        if (data.Waveform.size() != std::min<uint64_t>(1024, data.Info.LengthFrames) || !data.Encoded ||
            data.Encoded->empty() || data.Encoded->size() > AudioProductMaximumBytes - 64 - data.Waveform.size())
            return AssetError {AssetErrorCode::InvalidData, "Invalid AudioClip data"};
        return std::monostate {};
    }
} // namespace Kans
