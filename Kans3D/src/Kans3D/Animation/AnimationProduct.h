#pragma once
#include <yaml-cpp/yaml.h>
#include "AnimationData.h"
namespace Kans
{
    YAML::Node              EncodeAnimationControllerNode(const AnimationControllerData&);
    AnimationControllerData DecodeAnimationControllerNode(const YAML::Node&); // Throws on malformed/invalid data.
    using AnimationProductBytes = std::variant<std::vector<std::byte>, AssetError>;
    AnimationProductBytes                             EncodeSkeletonProduct(const SkeletonData&);
    AnimationProductBytes                             EncodeAnimationClipProduct(const AnimationClipData&);
    AnimationProductBytes                             EncodeAnimationControllerProduct(const AnimationControllerData&);
    std::variant<SkeletonData, AssetError>            DecodeSkeletonProduct(const std::byte*, size_t);
    std::variant<AnimationClipData, AssetError>       DecodeAnimationClipProduct(const std::byte*, size_t);
    std::variant<AnimationControllerData, AssetError> DecodeAnimationControllerProduct(const std::byte*, size_t);
} // namespace Kans
