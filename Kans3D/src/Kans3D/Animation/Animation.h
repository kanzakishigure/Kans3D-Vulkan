#pragma once
#include "AnimationData.h"

namespace Kans
{
    struct AnimationPose
    {
        std::vector<JointTransform> Local;
        std::vector<glm::mat4>      Model, Palette;
        std::vector<float>          MorphWeights; // Logical shape-key order from SkeletonData.
        void                        Resize(size_t joints);
    };
    double AnimationSampleTime(double time, double duration, bool loop);
    void   RestPose(const SkeletonData&, AnimationPose&);
    void   SampleAnimation(const SkeletonData&, const BoundAnimationClip&, double time, bool loop, AnimationPose&);
    void   BuildModelPose(const SkeletonData&, AnimationPose&);
    JointTransform BlendJoint(const JointTransform&, const JointTransform&, float weight);
    void           BlendAnimationPoses(const AnimationPose&,
                                       const AnimationPose&,
                                       float weight,
                                       AnimationPose&,
                                       const std::vector<float>& mask      = {},
                                       const std::vector<float>& morphMask = {});
    void           AddAnimationPose(AnimationPose&,
                                    const AnimationPose&,
                                    const AnimationPose&      reference,
                                    float                     weight,
                                    const std::vector<float>& mask      = {},
                                    const std::vector<float>& morphMask = {});
    // Caller-owned output has child count entries. Cartesian gradient-band interpolation, normalized to unity.
    void AnimationBlendWeights(const AnimationMotion&, glm::vec2 parameter, std::vector<float>& output);
    struct AnimationEventOccurrence
    {
        const AnimationEvent* Event        = nullptr;
        double                CrossingTime = 0;
        int64_t               Cycle        = 0;
        uint32_t              Layer        = 0;
        bool                  Backwards    = false;
    };
    using AnimationEventSink = std::function<void(const AnimationEventOccurrence&)>;
    // Forward (from,to], backward [to,from). Equal endpoints produce no events.
    void CrossAnimationEvents(const AnimationClipData&,
                              double from,
                              double to,
                              bool   loop,
                              const AnimationEventSink&,
                              uint32_t layer = 0);
    struct RootMotionDelta
    {
        glm::vec3 Translation {0};
        glm::quat Rotation {1, 0, 0, 0};
    };
    struct AnimatorLayerStatus
    {
        uint32_t              State = 0, Target = 0;
        double                Time = 0, TargetTime = 0, FadeElapsed = 0, FadeDuration = 0;
        bool                  Transitioning = false;
        AnimationInterruption Interruption  = AnimationInterruption::None;
        float                 CrossfadeWeight() const;
    };

    // CPU evaluator. Prepare allocates/binds once. Advance, EvaluateState and RebuildPose reuse storage.
    // Data passed to Prepare must outlive this object. Copying the evaluator is safe while data stays alive.
    class Animator
    {
    public:
        using ClipResolver = std::function<const AnimationClipData*(AssetID)>;
        AnimationValidation   Prepare(const SkeletonData&, const AnimationControllerData&, const ClipResolver& = {});
        bool                  IsPrepared() const { return m_Skeleton != nullptr; }
        bool                  SetFloat(const std::string&, float);
        bool                  SetInt(const std::string&, int);
        bool                  SetBool(const std::string&, bool);
        bool                  SetTrigger(const std::string&);
        bool                  ResetTrigger(const std::string&);
        std::optional<double> GetParameter(const std::string&) const;
        bool                  SetLayerWeight(size_t, float);
        bool                  Play(size_t layer, uint32_t state, double time = 0);
        bool                  CrossFade(size_t layer, uint32_t state, double seconds, double time = 0);
        const std::vector<ShapeKey>& GetShapeKeys() const
        {
            static const std::vector<ShapeKey> empty;
            return m_Skeleton ? m_Skeleton->ShapeKeys : empty;
        }
        const std::vector<uint8_t>&          GetShapeKeyOverrides() const { return m_HasOverride; }
        const std::vector<float>&            GetShapeKeyOffsets() const { return m_Offsets; }
        const std::vector<float>&            GetShapeKeyOverrideWeights() const { return m_Overrides; }
        const std::vector<ExpressionPreset>& GetExpressions() const
        {
            static const std::vector<ExpressionPreset> empty;
            return m_Controller ? m_Controller->Expressions : empty;
        }
        bool                  SetShapeKeyWeight(const std::string&, float weight); // Absolute override until cleared.
        bool                  AddShapeKeyWeight(const std::string&, float weight); // Persistent additive offset.
        bool                  ClearShapeKeyOverride(const std::string&);
        bool                  SetExpression(const std::string&, double seconds);
        void                  Advance(double seconds, const AnimationEventSink& sink = {});
        // Independent preview: samples one state without changing parameters, playback times, triggers or events.
        bool EvaluateState(size_t layer, uint32_t state, double time, AnimationPose& output);
        // Explicit event query for editor scrubbing. Does not change playback state.
        void ScrubEvents(size_t layer, uint32_t state, double from, double to, const AnimationEventSink&) const;
        const AnimationPose&                    GetPose() const { return m_Pose; }
        AnimationPose&                          MutablePose() { return m_Pose; }
        const std::vector<AnimatorLayerStatus>& GetLayers() const { return m_Status; }
        const RootMotionDelta&                  GetRootMotion() const { return m_RootDelta; }
        const std::vector<BoundAnimationClip>&  GetBindings() const { return m_Clips; }
        // Post-sampling modifier hook: local pose/morphs -> modifiers -> model/palette -> scene entities/GPU.
        void SetPoseModifier(std::function<void(const SkeletonData&, AnimationPose&)> modifier)
        {
            m_Modifier = std::move(modifier);
        }
        void   RebuildPose();
        bool   ExtractRootMotion = false; // Removes sampled root T/R from pose; caller applies GetRootMotion().
        double Speed             = 1;

    private:
        int32_t Parameter(const std::string&) const;
        bool    Set(const std::string&, double, AnimationParameterType);
        double  Value(const std::string&) const;
        double  Duration(uint32_t motion) const;
        void    SampleMotion(uint32_t motion, double time, bool loop, AnimationPose&);
        void    StatePose(size_t layer, uint32_t state, double time, AnimationPose&);
        void MotionEvents(uint32_t motion, double from, double to, bool loop, const AnimationEventSink&, uint32_t layer)
            const;
        JointTransform  MotionRoot(uint32_t motion, double time, bool loop);
        RootMotionDelta StateRootDelta(size_t layer, uint32_t state, double from, double to);
        bool            Conditions(const AnimationTransition&) const;
        bool            Exit(const AnimationTransition&, size_t layer, uint32_t state, double from, double to) const;
        void            Consume(const AnimationTransition&);
        struct LayerWork
        {
            AnimationPose      Source, Target, Frozen;
            bool               UseFrozen = false;
            std::vector<float> Mask, MorphMask;
            float              Weight = 1;
        };
        const SkeletonData*                                      m_Skeleton   = nullptr;
        const AnimationControllerData*                           m_Controller = nullptr;
        int32_t                                                  m_Root       = -1;
        std::vector<double>                                      m_Parameters;
        std::vector<BoundAnimationClip>                          m_Clips;
        std::vector<AnimationPose>                               m_MotionPoses;
        mutable std::vector<std::vector<float>>                  m_Weights;
        std::vector<LayerWork>                                   m_Work;
        std::vector<AnimatorLayerStatus>                         m_Status;
        AnimationPose                                            m_Pose, m_Rest;
        RootMotionDelta                                          m_RootDelta;
        std::vector<float>   m_Overrides, m_Offsets, m_Expression, m_ExpressionFrom, m_ExpressionTo;
        std::vector<uint8_t> m_HasOverride;
        double               m_ExpressionElapsed = 0, m_ExpressionDuration = 0;
        std::function<void(const SkeletonData&, AnimationPose&)> m_Modifier;
    };
} // namespace Kans
