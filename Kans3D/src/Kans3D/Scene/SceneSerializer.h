#pragma once

#include <filesystem>
#include "Scene.h"

namespace YAML
{
    class Emitter;
    class Node;
} // namespace YAML
namespace Kans
{

    class SceneSerializer
    {
    public:
        SceneSerializer(const Ref<Scene>& Scene);

        void Serialize(const std::filesystem::path& path);
        void SerializeEntity(YAML::Emitter& out, Entity entity);
        void SerializeRuntime(const std::filesystem::path& path);

        void Deserialize(const std::filesystem::path& path);
        void DeserializeEntity(YAML::Node& entitiesNode, Ref<Scene> scene);

    private:
        Ref<Scene> m_Scene;
    };
} // namespace Kans