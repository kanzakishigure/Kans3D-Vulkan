#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    struct JointTransform
    {
        glm::vec3             Translation {0};
        glm::quat             Rotation {1, 0, 0, 0};
        glm::vec3             Scale {1};
        glm::mat4             Matrix() const;
        static JointTransform FromMatrix(const glm::mat4& matrix);
    };
    struct SkeletonJoint
    {
        std::string    Name, Path, NodeKey;
        int32_t        Parent = -1; // Parent precedes child; includes non-deforming ancestors.
        JointTransform RestLocal, BindLocal;
        glm::mat4      InverseBind {1}; // Model space -> joint bind space.
    };
    struct ShapeKey
    {
        std::string Name;
        float       DefaultWeight = 0;
    };
    struct ShapeKeyWeight
    {
        std::string Name;
        float       Weight = 1;
    };
    struct SkeletonData
    {
        std::vector<SkeletonJoint> Joints;
        std::vector<ShapeKey>      ShapeKeys; // Model-wide logical names, shared by every mesh.
        int32_t                    FindShapeKey(const std::string&) const;
        int32_t                    FindJoint(const std::string& nameOrPath) const;
    };
    enum class TrackInterpolation : uint8_t
    {
        Step,
        Linear,
        Spherical
    };
    template<class T>
    struct AnimationKey
    {
        double Time = 0;
        T      Value {};
    };
    template<class T>
    struct AnimationTrack
    {
        TrackInterpolation           Interpolation = TrackInterpolation::Linear;
        std::vector<AnimationKey<T>> Keys;
    };
    struct JointAnimationTrack
    {
        std::string               Joint;
        AnimationTrack<glm::vec3> Translation, Scale;
        AnimationTrack<glm::quat> Rotation {TrackInterpolation::Spherical, {}};
    };
    struct AnimationEvent
    {
        double      Time = 0;
        std::string Name, Payload;
    };
    struct MorphAnimationTrack
    {
        std::string           ShapeKey;
        AnimationTrack<float> Weight;
    };
    struct AnimationClipData
    {
        std::string                      Name, SourceName;
        double                           Duration = 0, FrameRate = 30;
        std::vector<JointAnimationTrack> Tracks;
        std::vector<MorphAnimationTrack> MorphTracks;
        std::vector<AnimationEvent>      Events; // Sorted by time; duplicates are distinct events.
    };
    struct AnimationNameMapping
    {
        std::string                        StripPrefix;
        std::map<std::string, std::string> Names; // Source name -> target name/path; applied after prefix stripping.
        std::string                        Map(const std::string& source) const;
    };
    struct BoundAnimationClip
    {
        const AnimationClipData* Clip = nullptr;
        std::vector<int32_t>     JointTracks, MorphTracks;
        std::vector<std::string> MissingJoints;
    };
    BoundAnimationClip
    BindAnimationClip(const SkeletonData&, const AnimationClipData&, const AnimationNameMapping& = {});

    enum class AnimationParameterType : uint8_t
    {
        Float,
        Int,
        Bool,
        Trigger
    };
    struct AnimationParameter
    {
        std::string            Name;
        AnimationParameterType Type    = AnimationParameterType::Float;
        double                 Default = 0;
    };
    enum class AnimationMotionType : uint8_t
    {
        Clip,
        Blend1D,
        Blend2D
    };
    struct BlendTreeChild
    {
        uint32_t  Motion    = 0;
        float     Threshold = 0;
        glm::vec2 Position {0};
    };
    struct AnimationMotion
    {
        AnimationMotionType              Type = AnimationMotionType::Clip;
        AssetID                          Clip;
        std::optional<AnimationClipData> InlineClip;
        AnimationNameMapping             Mapping;
        std::string                      ParameterX, ParameterY;
        std::vector<BlendTreeChild>      Children;
    };
    struct AnimationState
    {
        std::string Name;
        uint32_t    Motion = 0;
        double      Speed  = 1;
        bool        Loop   = true;
        // Node position in the editor's graph; not used at runtime.
        std::optional<glm::vec2> EditorPosition;
    };
    enum class AnimationConditionOp : uint8_t
    {
        Greater,
        Less,
        Equal,
        NotEqual,
        If,
        IfNot
    };
    struct AnimationCondition
    {
        std::string          Parameter;
        AnimationConditionOp Op    = AnimationConditionOp::If;
        double               Value = 0;
    };
    enum class AnimationInterruption : uint8_t
    {
        None,
        Source,
        Destination,
        Both
    };
    struct AnimationTransition
    {
        int32_t  Source = -1; // -1 = any state. List order is priority; any-state transitions are considered first.
        uint32_t Target = 0;
        std::vector<AnimationCondition> Conditions;
        std::optional<double>           ExitTime; // Normalized source time. <1 repeats each loop; >=1 is absolute.
        double                          Duration            = 0.2; // Seconds, independent of state speed.
        AnimationInterruption           Interruption        = AnimationInterruption::None;
        bool                            CanTransitionToSelf = false;
    };
    enum class AnimationLayerMode : uint8_t
    {
        Override,
        Additive
    };
    struct AvatarMaskEntry
    {
        std::string Joint;
        float       Weight          = 1;
        bool        IncludeChildren = false;
    };
    struct AnimationLayer
    {
        std::string                      Name;
        float                            Weight = 1;
        AnimationLayerMode               Mode   = AnimationLayerMode::Override;
        std::vector<ShapeKeyWeight>
            ShapeKeyMask; // Nonempty masks jointly select channels; a face-only mask selects no joints.
        std::vector<AvatarMaskEntry>
            Mask; // Both masks empty = all channels; otherwise unlisted channels have zero weight.
        uint32_t                         EntryState = 0;
        std::vector<AnimationState>      States;
        std::vector<AnimationTransition> Transitions;
    };
    struct ExpressionPreset
    {
        std::string                 Name;
        std::vector<ShapeKeyWeight> Weights;
    };
    // All points, directions and rotations are world space. Entity=0 uses the explicit value.
    // An unresolved entity disables that constraint for the current evaluation.
    struct IKTarget
    {
        uint64_t  Entity = 0;
        glm::vec3 Position {0};
        glm::quat Rotation {1, 0, 0, 0};
    };
    struct TwoBoneIK
    {
        std::string Name, Root, Middle, End;
        IKTarget    Target;
        uint64_t    PoleEntity = 0;
        glm::vec3   PoleDirection {0, 0, 1};
        float       Weight = 1, RotationWeight = 0;
        bool        Stretch = false;
    };
    struct LookAtBone
    {
        std::string Joint;
        float       Weight = 1;
        glm::vec3   Forward {0, 0, 1}, Up {0, 1, 0};
    };
    struct LookAtIK
    {
        std::string             Name;
        std::vector<LookAtBone> Bones; // Proximal to distal; applied in this order.
        IKTarget                Target;
        glm::vec3               WorldUp {0, 1, 0};
        float                   Weight = 1, AngleLimit = 3.14159265f; // Radians from the incoming pose's forward axis.
    };
    struct AnimationControllerData
    {
        std::vector<AnimationParameter> Parameters;
        std::vector<AnimationMotion>    Motions; // Index-based acyclic graph; nested blend trees allowed.
        std::vector<AnimationLayer>     Layers;
        std::string                     RootMotionJoint;
        std::vector<ExpressionPreset>   Expressions;
        std::vector<TwoBoneIK>          TwoBoneConstraints;
        std::vector<LookAtIK>           LookAtConstraints;
    };
    using AnimationValidation = std::variant<std::monostate, AssetError>;
    AnimationValidation ValidateSkeleton(const SkeletonData&);
    AnimationValidation ValidateAnimationClip(const AnimationClipData&);
    AnimationValidation ValidateAnimationController(const AnimationControllerData&);

    template<class T, AssetType Type>
    class AnimationDataAsset final : public Asset
    {
    public:
        explicit AnimationDataAsset(T data) : m_Data(std::move(data)) {}
        static AssetType GetStaticType() { return Type; }
        AssetType        GetAssetType() const override { return Type; }
        const T&         GetData() const { return m_Data; }

    private:
        T m_Data;
    };
    using SkeletonAsset            = AnimationDataAsset<SkeletonData, AssetType::Skeleton>;
    using AnimationClipAsset       = AnimationDataAsset<AnimationClipData, AssetType::AnimationClip>;
    using AnimationControllerAsset = AnimationDataAsset<AnimationControllerData, AssetType::AnimationController>;
} // namespace Kans
