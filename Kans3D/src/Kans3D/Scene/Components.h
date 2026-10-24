#pragma once
#include <string.h>
#include <map>
#include <optional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Kans3D/Animation/AnimatorComponent.h"
#include "Kans3D/Audio/AudioComponents.h"
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"

#include "Kans3D/Core/UUID.h"
#include "Kans3D/Geometry/Shapes.h"
#include "Kans3D/Renderer/Resource/ShadowSettings.h"
#include "ProductModelComponent.h"

#include "Kans3D/Renderer/Resource/SceneCamera.h"
#include "Kans3D/Renderer/Resource/Texture.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
namespace Kans
{
    class ScriptableEntity;

    struct IDComponent
    {
        UUID ID                         = 0;
        IDComponent()                   = default;
        IDComponent(const IDComponent&) = default;
    };

    struct TagComponent
    {
        std::string Tag;
        TagComponent()                    = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(std::string tag) : Tag(tag) {}
        operator std::string&() { return Tag; }
    };

    // Parent/child links between entities (by UUID; 0 = no parent). Children are
    // in display order; the Scene keeps the order of top-level entities.
    struct RelationshipComponent
    {
        UUID              Parent = 0;
        std::vector<UUID> Children;
    };

    // Transform relative to the parent entity (or the world for top-level ones).
    struct TransformComponent
    {

        glm::vec3 Position = glm::vec3(0.0f);
        // rotation here is radians
        glm::vec3 Rotation = glm::vec3(0.0f);

        glm::vec3 Scale = glm::vec3(1.0f);

        TransformComponent()                          = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) :
            Position(position), Rotation(rotation), Scale(scale)
        {}

        // Imported matrices remain exact (including shear) until a TRS field is edited.
        std::optional<glm::mat4> ImportedMatrix;
        glm::vec3                ImportedPosition {0}, ImportedRotation {0}, ImportedScale {1};

        glm::mat4 GetTransform() const
        {
            if (ImportedMatrix && Position == ImportedPosition && Rotation == ImportedRotation &&
                Scale == ImportedScale)
                return *ImportedMatrix;
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
            glm::mat4 rotation    = glm::toMat4(glm::quat(Rotation));
            glm::mat4 scale       = glm::scale(glm::mat4(1.0f), Scale);
            return translation * rotation * scale;
        }
    };

    enum class MaterialParameterQuantity : uint32_t
    {
        WorldPosition,
        WorldForward,
        WorldUp,
        WorldRight
    };
    struct MaterialParameterBinding
    {
        std::string               Parameter;
        UUID                      SourceEntity = 0;
        MaterialParameterQuantity Quantity     = MaterialParameterQuantity::WorldPosition;
    };
    struct MaterialParameterDriversComponent
    {
        std::vector<MaterialParameterBinding> Bindings;
        Ref<InstanceMaterialParameters>       Values;
        // Runtime only, ordered by first set. Copies/snapshots retain bindings alone.
        std::vector<std::pair<std::string, std::array<float, 4>>> Overrides;
        MaterialParameterDriversComponent() = default;
        MaterialParameterDriversComponent(const MaterialParameterDriversComponent& other) : Bindings(other.Bindings) {}
        MaterialParameterDriversComponent& operator=(const MaterialParameterDriversComponent& other)
        {
            if (this != &other)
            {
                Bindings = other.Bindings;
                Values.reset();
                Overrides.clear();
            }
            return *this;
        }
        MaterialParameterDriversComponent(MaterialParameterDriversComponent&&) noexcept            = default;
        MaterialParameterDriversComponent& operator=(MaterialParameterDriversComponent&&) noexcept = default;
    };

    struct MeshComponent
    {
        AssetID                 Mesh;
        std::vector<AssetID>    Materials;
        std::optional<uint32_t> Section; // Empty renders all sections, in mesh order.
        bool                    CastShadows = true, ReceiveShadows = true;
        bool                    RenderCustomDepth = false;
        uint8_t                 CustomStencil     = 0;
    };

    inline bool operator==(const MeshComponent& a, const MeshComponent& b)
    {
        return a.Mesh == b.Mesh && a.Materials == b.Materials && a.Section == b.Section &&
               a.CastShadows == b.CastShadows && a.ReceiveShadows == b.ReceiveShadows &&
               a.RenderCustomDepth == b.RenderCustomDepth && a.CustomStencil == b.CustomStencil;
    }

    // Mutually exclusive with MeshComponent. Callers must remove the previous
    // component explicitly; Entity::AddComponent asserts/rejects a conflict.
    struct EditableMeshComponent
    {
        EditableMesh             Mesh;
        std::optional<ShapeDesc> Shape;
        std::vector<AssetID>     Materials;
        bool                     CastShadows = true, ReceiveShadows = true;
        bool                     RenderCustomDepth = false;
        uint8_t                  CustomStencil     = 0;
    };
    bool operator==(const EditableMeshComponent& a, const EditableMeshComponent& b);
    // Explicit regeneration only. PatchComponent never interprets Shape changes.
    void RegenerateShape(EditableMeshComponent& component);

    struct BoundsComponent
    {
        Bounds World;
    };
    struct PrefabOwnedComponent
    {
        UUID        Instance = 0;
        std::string NodeKey;
        size_t      Order = 0;
    };
    // Retains a user child's attachment even while its prefab parent is unavailable.
    struct PrefabAttachmentComponent
    {
        UUID        Instance = 0;
        std::string NodeKey;
    };
    struct PrefabNodeOverride
    {
        std::optional<TransformComponent> LocalTransform;
        std::map<uint32_t, AssetID>       Materials;
        bool                              Removed = false;
        std::optional<bool>               RenderCustomDepth;
        std::optional<uint8_t>            CustomStencil;
        bool                              Empty() const
        {
            return !LocalTransform && Materials.empty() && !Removed && !RenderCustomDepth && !CustomStencil;
        }
    };
    struct PrefabNodeState
    {
        TransformComponent BaseTransform, AppliedTransform;
        MeshComponent      BaseMesh, AppliedMesh;
    };
    struct PrefabInstanceComponent
    {
        AssetID                                   Prefab;
        std::map<std::string, PrefabNodeOverride> Overrides;
        std::optional<ProductModelComponent>      LegacyPending;
        // Runtime state; serializers omit it, snapshots use it to rebuild atomically.
        Ref<const PrefabAsset>                    Source;
        std::map<std::string, PrefabNodeState>    Nodes;
        std::map<AssetID, Ref<const MeshAsset>>   MeshSources;
        Ref<const PrefabAsset>                    ExpandedSource;
        std::map<std::string, PrefabNodeOverride> AppliedOverrides;
        bool                                      Expanded  = false;
        uint64_t                                  DrawOrder = 0;
    };

    struct CameraComponent
    {
        Kans::SceneCamera SceneCamera;
        bool              Primary               = true;
        bool              FixedAspectRatio      = false;
        CameraComponent()                       = default;
        CameraComponent(const CameraComponent&) = default;
    };

    struct SpriteRendererComponent
    {
        glm::vec4      Color = {1.0f, 1.0f, 1.0f, 1.0f};
        Ref<Texture2D> Texture;
        float          TilingFactor = 1.0f;
        // 目前未实现texture
        SpriteRendererComponent()                               = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(glm::vec4 color, Ref<Texture2D> texture = nullptr) : Color(color), Texture(texture)
        {
            // Set The WhiteTexture
            if (texture == nullptr)
            {
                TextureSpecification spec;
                spec.Width    = 1;
                spec.Height   = 1;
                spec.Format   = RenderHardware::Format::RGBA8Unorm;
                uint32_t data = 0xffffffff;
                Texture       = Texture2D::Create(spec, Buffer(&data, sizeof(uint32_t)));
            }
        }
    };

    // can't be serialize
    struct NativeScriptComponent
    {
        ScriptableEntity* Instance = nullptr;

        ScriptableEntity* (*InstantiateFunction)();
        void (*DestoryInstanceFunction)(NativeScriptComponent*);

        template<typename T>
        void Bind()
        {
            InstantiateFunction     = []() { return static_cast<ScriptableEntity*>(new T()); };
            DestoryInstanceFunction = [](NativeScriptComponent* nsc) {
                delete nsc->Instance;
                nsc->Instance = nullptr;
            };
        }
    };

    struct DirLightComponent
    {
        glm::vec3 Direction {0, -1, 0};
        glm::vec3 Diffuse_Intensity {1};
        glm::vec3 Specular_Intensity {1};
        glm::vec3 Ambient_Intensity {0};
        ShadowSettings Shadows;
        DirLightComponent()                         = default;
        DirLightComponent(const DirLightComponent&) = default;
    };

    struct PointLightComponent
    {
        glm::vec3 Diffuse_Intensity {1};
        glm::vec3 Specular_Intensity {1};
        glm::vec3 Ambient_Intensity {0};
        ShadowSettings Shadows                          = ShadowSettings::PointDefaults();
        PointLightComponent()                           = default;
        PointLightComponent(const PointLightComponent&) = default;
    };

    struct ScriptComponent
    {
        ScriptComponent()                       = default;
        ScriptComponent(const ScriptComponent&) = default;
        ScriptComponent(const std::string& name) : ClassName(name) {}

        std::string ClassName;
        bool        IsLoaded = false;
    };

    // Component Type Register
    template<typename... Component>
    struct ComponentGroup
    {};
    using AllComponents = ComponentGroup<IDComponent,
                                         TagComponent,
                                         TransformComponent,
                                         CameraComponent,
                                         SpriteRendererComponent,
                                         NativeScriptComponent,
                                         MeshComponent,
                                         EditableMeshComponent,
                                         PrefabInstanceComponent,
                                         AnimatorComponent,
                                         MaterialParameterDriversComponent,
                                         AudioSourceComponent,
                                         AudioListenerComponent,
                                         PrefabOwnedComponent,
                                         PrefabAttachmentComponent,
                                         BoundsComponent,
                                         DirLightComponent,
                                         PointLightComponent,
                                         ScriptComponent>;
} // namespace Kans
