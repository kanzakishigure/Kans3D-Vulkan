#pragma once
#include <yaml-cpp/yaml.h>
#include "AnimationData.h"
namespace Kans
{
    YAML::Node                                       EncodeExpressionNode(const ExpressionData&);
    ExpressionData                                   DecodeExpressionNode(const YAML::Node&);
    std::variant<std::vector<std::byte>, AssetError> EncodeExpressionProduct(const ExpressionData&);
    std::variant<ExpressionData, AssetError>         DecodeExpressionProduct(const std::byte*, size_t);
    YAML::Node                                       EncodeAnimationControllerNode(const AnimationControllerData&);
    AnimationControllerData DecodeAnimationControllerNode(const YAML::Node&); // Throws on malformed/invalid data.
    using AnimationProductBytes = std::variant<std::vector<std::byte>, AssetError>;
    AnimationProductBytes EncodeSkeletonProduct(const SkeletonData&);
    AnimationProductBytes EncodeAnimationClipProduct(const AnimationClipData&);
    // Compression is explicitly opt-in; inline controller authoring always uses the original YAML codec.
    std::variant<AnimationCompressionSettings, AssetError>
    ParseAnimationCompressionSettings(const std::map<std::string, std::string>&, const std::string& clipObjectKey = {});
    AnimationProductBytes
    EncodeAnimationClipProduct(const AnimationClipData&, const SkeletonData&, const AnimationCompressionSettings&);
    AnimationProductBytes                             EncodeAnimationControllerProduct(const AnimationControllerData&);
    std::variant<SkeletonData, AssetError>            DecodeSkeletonProduct(const std::byte*, size_t);
    std::variant<AnimationClipData, AssetError>       DecodeAnimationClipProduct(const std::byte*, size_t);
    std::variant<AnimationControllerData, AssetError> DecodeAnimationControllerProduct(const std::byte*, size_t);
} // namespace Kans
