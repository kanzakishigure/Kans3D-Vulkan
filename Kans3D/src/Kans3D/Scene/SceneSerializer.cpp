#include "kspch.h"
#include "SceneSerializer.h"
#include <cmath>
#include <functional>
#include <set>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Storage/ProductFiles.h"
#include "Entity.h"
namespace Kans
{
    namespace
    {
        constexpr uint64_t SceneBytes = 16ull * 1024 * 1024;
        void               Require(bool valid, const char* message)
        {
            if (!valid)
                throw std::runtime_error(message);
        }
        struct SceneAllocationBudget
        {
            uint64_t Nodes = 0, TextBytes = 0;
            void     Add(size_t textBytes, const ProductModelComponent* model)
            {
                Nodes += 64;
                TextBytes += textBytes;
                if (model)
                {
                    auto valid = ValidateProductModelComponent(*model);
                    if (auto error = std::get_if<AssetError>(&valid))
                        throw std::runtime_error(error->Message);
                    Nodes += model->Overrides.size() * 8;
                }
                Require(Nodes <= 1024 * 1024 && TextBytes <= SceneBytes, "Scene aggregate allocation limit");
            }
        };
        void Keys(const YAML::Node& node, std::initializer_list<const char*> allowed)
        {
            Require(node.IsMap() && node.size() <= allowed.size(), "Invalid Scene mapping");
            std::set<std::string> seen;
            for (const auto& pair : node)
            {
                Require(pair.first.IsScalar(), "Non-scalar Scene key");
                auto key = pair.first.as<std::string>();
                Require(seen.insert(key).second &&
                            std::any_of(allowed.begin(), allowed.end(), [&](const char* item) { return key == item; }),
                        "Duplicate/unknown Scene field");
            }
        }
        std::string Text(const YAML::Node& node)
        {
            Require(node.IsScalar(), "Missing Scene string");
            auto value = node.as<std::string>();
            Require(value.size() <= 4096 && value.find('\0') == std::string::npos, "Invalid Scene string");
            return value;
        }
        float Number(const YAML::Node& node)
        {
            Require(node.IsScalar(), "Missing Scene number");
            auto value = node.as<float>();
            Require(std::isfinite(value), "Non-finite Scene number");
            return value;
        }
        uint64_t UInt(const YAML::Node& node)
        {
            Require(node.IsScalar(), "Missing Scene integer");
            auto value = node.Scalar();
            Require(!value.empty() && value.size() <= 20 &&
                        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }),
                    "Invalid unsigned Scene integer");
            return node.as<uint64_t>();
        }
        uint32_t SceneIndex(const YAML::Node& node)
        {
            auto value = UInt(node);
            Require(value <= UINT32_MAX, "Scene index overflow");
            return uint32_t(value);
        }
        glm::vec3 Vec3(const YAML::Node& node)
        {
            Require(node.IsSequence() && node.size() == 3, "Invalid Scene vec3");
            return {Number(node[0]), Number(node[1]), Number(node[2])};
        }
        glm::vec4 Vec4(const YAML::Node& node)
        {
            Require(node.IsSequence() && node.size() == 4, "Invalid Scene color");
            return {Number(node[0]), Number(node[1]), Number(node[2]), Number(node[3])};
        }
        template<typename T>
        YAML::Node Vector(T value, size_t count)
        {
            YAML::Node node;
            for (size_t i = 0; i < count; ++i)
            {
                Require(std::isfinite(value[i]), "Non-finite Scene vector");
                node.push_back(value[i]);
            }
            return node;
        }
        // An empty string is an unassigned reference.
        AssetID Identity(const YAML::Node& node)
        {
            const auto text = Text(node);
            if (text.empty())
                return {};
            auto value = AssetID::FromString(text);
            Require(value.IsValid(), "Invalid Scene Product identity");
            return value;
        }
        std::string IdentityText(AssetID id) { return id.IsValid() ? id.ToString() : std::string(); }
        YAML::Node EncodeModel(const ProductModelComponent& component)
        {
            auto valid = ValidateProductModelComponent(component);
            if (auto error = std::get_if<AssetError>(&valid))
                throw std::runtime_error(error->Message);
            YAML::Node node;
            node["Model"]     = IdentityText(component.Model);
            node["Overrides"] = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& item : component.Overrides)
            {
                YAML::Node value;
                value["Authored"]    = IdentityText(item.Authored);
                value["Replacement"] = IdentityText(item.Replacement);
                node["Overrides"].push_back(value);
            }
            return node;
        }
        ProductModelComponent DecodeModel(const YAML::Node& node)
        {
            // DrawUniforms was written by an earlier format and is ignored.
            Keys(node, {"Model", "Overrides", "DrawUniforms"});
            ProductModelComponent result;
            result.Model   = Identity(node["Model"]);
            auto overrides = node["Overrides"];
            Require(overrides.IsSequence() && overrides.size() <= 64, "Invalid Scene Material overrides");
            for (const auto& value : overrides)
            {
                Keys(value, {"Authored", "Replacement"});
                result.Overrides.push_back({Identity(value["Authored"]), Identity(value["Replacement"])});
            }
            auto valid = ValidateProductModelComponent(result);
            if (auto error = std::get_if<AssetError>(&valid))
                throw std::runtime_error(error->Message);
            return result;
        }
        void ValidateCamera(SceneCamera& camera)
        {
            Require(std::isfinite(camera.GetAspectRatio()) && camera.GetAspectRatio() > 0 &&
                        std::isfinite(camera.GetPerspectiveVerticalFOV()) && camera.GetPerspectiveVerticalFOV() > 0 &&
                        camera.GetPerspectiveVerticalFOV() < 180 && std::isfinite(camera.GetPerspectiveNearClip()) &&
                        std::isfinite(camera.GetPerspectiveFarClip()) && camera.GetPerspectiveNearClip() > 0 &&
                        camera.GetPerspectiveFarClip() > camera.GetPerspectiveNearClip() &&
                        std::isfinite(camera.GetOrthographicSize()) && camera.GetOrthographicSize() > 0 &&
                        std::isfinite(camera.GetOrthographicNearClip()) &&
                        std::isfinite(camera.GetOrthographicFarClip()) &&
                        camera.GetOrthographicFarClip() > camera.GetOrthographicNearClip() &&
                        (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective ||
                         camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic),
                    "Invalid Scene camera");
        }
        YAML::Node EncodeEntity(Entity entity)
        {
            Require(entity.HasComponent<TagComponent>() && entity.HasComponent<TransformComponent>(),
                    "Incomplete Scene entity");
            YAML::Node node;
            node["Entity"] = uint64_t(entity.GetComponent<IDComponent>().ID);
            // Written in hierarchy order, so children follow their parent in sibling order.
            if (const UUID parent = entity.GetComponent<RelationshipComponent>().Parent)
                node["Parent"] = uint64_t(parent);
            auto name      = entity.GetComponent<TagComponent>().Tag;
            Require(name.size() <= 4096 && name.find('\0') == std::string::npos, "Invalid Scene tag");
            node["TagComponent"]["Tag"] = name;
            const auto& transform       = entity.GetComponent<TransformComponent>();
            auto        value           = node["TransformComponent"];
            value["Position"]           = Vector(transform.Position, 3);
            value["Rotation"]           = Vector(transform.Rotation, 3);
            value["Scale"]              = Vector(transform.Scale, 3);
            if (entity.HasComponent<ProductModelComponent>())
                node["ProductModelComponent"] = EncodeModel(entity.GetComponent<ProductModelComponent>());
            if (entity.HasComponent<CameraComponent>())
            {
                auto& component = entity.GetComponent<CameraComponent>();
                auto& camera    = component.SceneCamera;
                ValidateCamera(camera);
                value.reset(node["CameraComponent"]);
                value["ProjectionType"]       = uint32_t(camera.GetProjectionType());
                value["PerspectiveFOV"]       = camera.GetPerspectiveVerticalFOV();
                value["PerspectiveNearClip"]  = camera.GetPerspectiveNearClip();
                value["PerspectiveFarClip"]   = camera.GetPerspectiveFarClip();
                value["OrthographicSize"]     = camera.GetOrthographicSize();
                value["OrthographicNearClip"] = camera.GetOrthographicNearClip();
                value["OrthographicFarClip"]  = camera.GetOrthographicFarClip();
                value["AspectRatio"]          = camera.GetAspectRatio();
                value["Primary"]              = component.Primary;
                value["FixedAspectRatio"]     = component.FixedAspectRatio;
            }
            if (entity.HasComponent<DirLightComponent>())
            {
                const auto& light = entity.GetComponent<DirLightComponent>();
                value.reset(node["DirLightComponent"]);
                value["Direction"]          = Vector(light.Direction, 3);
                value["Diffuse_Intensity"]  = Vector(light.Diffuse_Intensity, 3);
                value["Specular_Intensity"] = Vector(light.Specular_Intensity, 3);
                value["Ambient_Intensity"]  = Vector(light.Ambient_Intensity, 3);
            }
            if (entity.HasComponent<PointLightComponent>())
            {
                const auto& light = entity.GetComponent<PointLightComponent>();
                value.reset(node["PointLightComponent"]);
                value["Diffuse_Intensity"]  = Vector(light.Diffuse_Intensity, 3);
                value["Specular_Intensity"] = Vector(light.Specular_Intensity, 3);
                value["Ambient_Intensity"]  = Vector(light.Ambient_Intensity, 3);
            }
            if (entity.HasComponent<ScriptComponent>())
            {
                const auto& name = entity.GetComponent<ScriptComponent>().ClassName;
                Require(name.size() <= 4096 && name.find('\0') == std::string::npos, "Invalid Script name");
                node["ScriptComponent"]["ClassName"] = name;
            }
            if (entity.HasComponent<SpriteRendererComponent>())
            {
                const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
                Require(!sprite.Texture, "Legacy Sprite texture has no persistent Product identity");
                value.reset(node["SpriteRendererComponent"]);
                value["Color"] = Vector(sprite.Color, 4);
                Require(std::isfinite(sprite.TilingFactor) && sprite.TilingFactor >= 0, "Invalid Sprite tiling");
                value["TilingFactor"] = sprite.TilingFactor;
            }
            return node;
        }
    } // namespace
    SceneSerializer::SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}
    std::variant<std::monostate, AssetError> SceneSerializer::SerializeChecked(const std::filesystem::path& path)
    {
        if (!m_Scene || path.empty())
            return AssetError {AssetErrorCode::InvalidArgument, "Scene/path absent"};
        std::filesystem::path temporary;
        try
        {
            auto& registry = m_Scene->m_Registry;
            if (!registry.view<NativeScriptComponent>().empty())
                return AssetError {AssetErrorCode::NotImplemented,
                                   "Scenes with native script components cannot be saved"};
            auto name = m_Scene->GetName();
            Require(name.size() <= 4096 && name.find('\0') == std::string::npos, "Invalid Scene name");
            YAML::Node root;
            root["Version"]  = 1;
            root["Scene"]    = name;
            root["Entities"] = YAML::Node(YAML::NodeType::Sequence);
            std::map<uint64_t, Entity> entities;
            SceneAllocationBudget      budget;
            budget.TextBytes = name.size();
            registry.each([&](auto handle) {
                Entity entity {handle, m_Scene.get()};
                Require(entity.HasComponent<IDComponent, TagComponent, TransformComponent>(),
                        "Incomplete Scene entity");
                auto id = uint64_t(entity.GetComponent<IDComponent>().ID);
                Require(id && entities.emplace(id, entity).second && entities.size() <= 65536,
                        "Invalid/duplicate Scene identity");
                auto textBytes = entity.GetComponent<TagComponent>().Tag.size();
                if (entity.HasComponent<ScriptComponent>())
                    textBytes += entity.GetComponent<ScriptComponent>().ClassName.size();
                budget.Add(textBytes,
                           entity.HasComponent<ProductModelComponent>() ?
                               &entity.GetComponent<ProductModelComponent>() :
                               nullptr);
            });
            // Parents before children, siblings in order.
            size_t                      encoded = 0;
            std::function<void(Entity)> write   = [&](Entity entity) {
                root["Entities"].push_back(EncodeEntity(entity));
                ++encoded;
                for (Entity child : m_Scene->GetChildren(entity))
                    write(child);
            };
            for (Entity entity : m_Scene->GetRootEntities())
                write(entity);
            Require(encoded == entities.size(), "Scene hierarchy does not reach every entity");
            YAML::Emitter output;
            output.SetFloatPrecision(std::numeric_limits<float>::max_digits10);
            output << root;
            Require(output.good() && output.size() <= SceneBytes, "Scene serialization exceeds byte limit");
            std::error_code error;
            auto            status = std::filesystem::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
                return AssetError {AssetErrorCode::IoError, error.message()};
            Require(!std::filesystem::is_symlink(status) &&
                        (!std::filesystem::exists(status) || std::filesystem::is_regular_file(status)),
                    "Scene destination not a regular file");
            temporary = path;
            temporary += "." + SourceAssetID::Generate().ToString() + ".tmp";
            auto first   = reinterpret_cast<const std::byte*>(output.c_str());
            auto written = WriteProductFile(temporary, {first, first + output.size()});
            if (auto failure = std::get_if<AssetError>(&written))
            {
                std::filesystem::remove(temporary, error);
                return *failure;
            }
            auto replaced = ReplaceProductCatalog(temporary, path);
            if (auto failure = std::get_if<AssetError>(&replaced))
            {
                std::filesystem::remove(temporary, error);
                return *failure;
            }
            return std::monostate {};
        }
        catch (const std::exception& error)
        {
            if (!temporary.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            }
            return AssetError {AssetErrorCode::InvalidData, std::string(error.what()).substr(0, 2048)};
        }
    }
    std::variant<Ref<Scene>, AssetError> SceneSerializer::DeserializeChecked(const std::filesystem::path& path)
    {
        auto input = ReadProductFile(path, SceneBytes);
        if (auto error = std::get_if<AssetError>(&input))
            return *error;
        try
        {
            auto& bytes = std::get<std::vector<std::byte>>(input);
            Require(!bytes.empty(), "Empty Scene file");
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            Require(text.find('\0') == std::string::npos, "NUL in Scene file");
            auto documents = YAML::LoadAll(text);
            Require(documents.size() == 1, "Scene requires one YAML document");
            auto root = documents.front();
            Keys(root, {"Version", "Scene", "Entities"});
            Require(UInt(root["Version"]) == 1, "Unsupported Scene version");
            auto entities = root["Entities"];
            Require(entities.IsSequence() && entities.size() <= 65536, "Invalid Scene entities");
            auto                  candidate = CreateRef<Scene>(Text(root["Scene"]));
            std::set<uint64_t>    seen;
            SceneAllocationBudget budget;
            budget.TextBytes = candidate->GetName().size();
            // (child, parent) links in file order, applied once every entity exists.
            std::vector<std::pair<uint64_t, uint64_t>> parents;
            for (const auto& node : entities)
            {
                Keys(node,
                     {"Entity",
                      "Parent",
                      "TagComponent",
                      "TransformComponent",
                      "ProductModelComponent",
                      "CameraComponent",
                      "DirLightComponent",
                      "PointLightComponent",
                      "ScriptComponent",
                      "SpriteRendererComponent"});
                auto id = UInt(node["Entity"]);
                Require(id && seen.insert(id).second, "Duplicate/zero Scene identity");
                if (node["Parent"])
                    parents.emplace_back(id, UInt(node["Parent"]));
                Keys(node["TagComponent"], {"Tag"});
                Keys(node["TransformComponent"], {"Position", "Rotation", "Scale"});
                auto tag       = Text(node["TagComponent"]["Tag"]);
                auto textBytes = tag.size();
                if (node["ScriptComponent"])
                    textBytes += Text(node["ScriptComponent"]["ClassName"]).size();
                std::optional<ProductModelComponent> model;
                if (node["ProductModelComponent"])
                    model = DecodeModel(node["ProductModelComponent"]);
                budget.Add(textBytes, model ? &*model : nullptr);
                auto entity = candidate->CreateEntityWithID(UUID(id), tag, true);
                // Entity creation supplies a default display name; persisted tags
                // retain their exact authored value, including an empty string.
                entity.GetComponent<TagComponent>().Tag = std::move(tag);
                auto& transform                         = entity.GetComponent<TransformComponent>();
                auto  value                             = node["TransformComponent"];
                transform.Position                      = Vec3(value["Position"]);
                transform.Rotation                      = Vec3(value["Rotation"]);
                transform.Scale                         = Vec3(value["Scale"]);
                if (model)
                    entity.AddComponent<ProductModelComponent>(std::move(*model));
                if (node["CameraComponent"])
                {
                    value.reset(node["CameraComponent"]);
                    Keys(value,
                         {"ProjectionType",
                          "PerspectiveFOV",
                          "PerspectiveNearClip",
                          "PerspectiveFarClip",
                          "OrthographicSize",
                          "OrthographicNearClip",
                          "OrthographicFarClip",
                          "AspectRatio",
                          "Primary",
                          "FixedAspectRatio"});
                    auto type = SceneIndex(value["ProjectionType"]);
                    Require(type <= 1, "Invalid Scene camera type");
                    auto& component = entity.AddComponent<CameraComponent>();
                    auto& camera    = component.SceneCamera;
                    camera.SetAspectRatio(Number(value["AspectRatio"]));
                    camera.SetPerspectiveVerticalFOV(Number(value["PerspectiveFOV"]));
                    camera.SetPerspectiveNearClip(Number(value["PerspectiveNearClip"]));
                    camera.SetPerspectiveFarClip(Number(value["PerspectiveFarClip"]));
                    camera.SetOrthographicSize(Number(value["OrthographicSize"]));
                    camera.SetOrthographicNearClip(Number(value["OrthographicNearClip"]));
                    camera.SetOrthographicFarClip(Number(value["OrthographicFarClip"]));
                    camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(type));
                    ValidateCamera(camera);
                    component.Primary          = value["Primary"].as<bool>();
                    component.FixedAspectRatio = value["FixedAspectRatio"].as<bool>();
                }
                if (node["DirLightComponent"])
                {
                    value.reset(node["DirLightComponent"]);
                    Keys(value, {"Direction", "Diffuse_Intensity", "Specular_Intensity", "Ambient_Intensity"});
                    auto& light              = entity.AddComponent<DirLightComponent>();
                    light.Direction          = Vec3(value["Direction"]);
                    light.Diffuse_Intensity  = Vec3(value["Diffuse_Intensity"]);
                    light.Specular_Intensity = Vec3(value["Specular_Intensity"]);
                    light.Ambient_Intensity  = Vec3(value["Ambient_Intensity"]);
                }
                if (node["PointLightComponent"])
                {
                    value.reset(node["PointLightComponent"]);
                    Keys(value, {"Diffuse_Intensity", "Specular_Intensity", "Ambient_Intensity"});
                    auto& light              = entity.AddComponent<PointLightComponent>();
                    light.Diffuse_Intensity  = Vec3(value["Diffuse_Intensity"]);
                    light.Specular_Intensity = Vec3(value["Specular_Intensity"]);
                    light.Ambient_Intensity  = Vec3(value["Ambient_Intensity"]);
                }
                if (node["ScriptComponent"])
                {
                    Keys(node["ScriptComponent"], {"ClassName"});
                    entity.AddComponent<ScriptComponent>(Text(node["ScriptComponent"]["ClassName"]));
                }
                if (node["SpriteRendererComponent"])
                {
                    value.reset(node["SpriteRendererComponent"]);
                    Keys(value, {"Color", "TilingFactor"});
                    auto& sprite        = entity.AddComponent<SpriteRendererComponent>();
                    sprite.Color        = Vec4(value["Color"]);
                    sprite.TilingFactor = Number(value["TilingFactor"]);
                    Require(sprite.TilingFactor >= 0, "Invalid Sprite tiling");
                }
            }
            for (const auto& [child, parent] : parents)
            {
                Entity entity = candidate->GetEntityByUUID(UUID(child));
                Entity owner  = candidate->GetEntityByUUID(UUID(parent));
                Require(owner && candidate->SetParent(entity, owner, {}, false), "Invalid Scene entity parent");
            }
            return candidate;
        }
        catch (const std::exception& error)
        {
            return AssetError {AssetErrorCode::InvalidData, std::string(error.what()).substr(0, 2048)};
        }
    }
    void SceneSerializer::Serialize(const std::filesystem::path& path)
    {
        auto value = SerializeChecked(path);
        if (auto error = std::get_if<AssetError>(&value))
            CORE_ERROR("Scene save: {0}", error->Message);
    }
    void SceneSerializer::Deserialize(const std::filesystem::path& path)
    {
        auto value = DeserializeChecked(path);
        if (auto error = std::get_if<AssetError>(&value))
            CORE_ERROR("Scene load: {0}", error->Message);
        else
            m_Scene = std::get<Ref<Scene>>(std::move(value));
    }
    void SceneSerializer::SerializeEntity(YAML::Emitter& output, Entity entity) { output << EncodeEntity(entity); }
    void SceneSerializer::SerializeRuntime(const std::filesystem::path& path) { Serialize(path); }
    void SceneSerializer::DeserializeEntity(YAML::Node&, Ref<Scene>)
    {
        CORE_ERROR("Use checked whole-Scene deserialization");
    }
} // namespace Kans
