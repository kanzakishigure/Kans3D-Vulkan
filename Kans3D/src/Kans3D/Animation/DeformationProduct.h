#pragma once
#include <yaml-cpp/yaml.h>
#include "DeformationComponents.h"
namespace Kans
{
    YAML::Node  EncodeIKComponent(const IKComponent&);
    IKComponent DecodeIKComponent(const YAML::Node&);
} // namespace Kans
