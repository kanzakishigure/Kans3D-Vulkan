#pragma once

#include <filesystem>
#include <variant>
#include "Kans3D/Asset/AssetError.h"
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
        void SerializeEntity(YAML::Emitter& out, Entity entity, bool clipboard = false);
        void SerializeRuntime(const std::filesystem::path& path);

        void Deserialize(const std::filesystem::path& path);
        void DeserializeEntity(YAML::Node& entitiesNode, Ref<Scene> scene);
        // Checked persistence: bounded input and atomic save; Load constructs a candidate
        // Scene and leaves the caller's current Scene untouched on any failure.
        std::variant<std::monostate, AssetError>    SerializeChecked(const std::filesystem::path& path);
        static std::variant<Ref<Scene>, AssetError> DeserializeChecked(const std::filesystem::path& path);
        // Shared checked entity/component decoder. Clipboard candidates retain generated
        // prefab entities and animator bindings, which scene files normally regenerate.
        static std::variant<Ref<Scene>, AssetError> DeserializeNodeChecked(const YAML::Node& root,
                                                                        bool clipboard = false);

    private:
        Ref<Scene> m_Scene;
    };
} // namespace Kans
