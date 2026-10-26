#pragma once
#include <yaml-cpp/yaml.h>
#include "Timeline.h"
namespace Kans
{
    YAML::Node                                       EncodeTimelineNode(const TimelineData&);
    TimelineData                                     DecodeTimelineNode(const YAML::Node&);
    std::variant<std::vector<std::byte>, AssetError> EncodeTimelineProduct(const TimelineData&);
    std::variant<TimelineData, AssetError>           DecodeTimelineProduct(const std::byte*, size_t);
} // namespace Kans
