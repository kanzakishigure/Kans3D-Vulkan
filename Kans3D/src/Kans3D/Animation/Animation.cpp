#include "kspch.h"
#include "Animation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtx/quaternion.hpp>

namespace Kans
{
    namespace
    {
        glm::quat RotationBlend(glm::quat a, glm::quat b, float w, bool spherical)
        {
            if (glm::dot(a, b) < 0)
                b = -b;
            return glm::normalize(spherical ? glm::slerp(a, b, w) : a * (1 - w) + b * w);
        }
        template<class T>
        T Interpolate(const T& a, const T& b, float w, TrackInterpolation)
        {
            return glm::mix(a, b, w);
        }
        template<>
        glm::quat Interpolate(const glm::quat& a, const glm::quat& b, float w, TrackInterpolation mode)
        {
            return RotationBlend(a, b, w, mode == TrackInterpolation::Spherical);
        }
        template<class T>
        T Sample(const AnimationTrack<T>& track, double time, const T& rest)
        {
            const auto& keys = track.Keys;
            if (keys.empty())
                return rest;
            if (time <= keys.front().Time)
                return keys.front().Value;
            if (time >= keys.back().Time)
                return keys.back().Value;
            const auto upper = std::upper_bound(
                keys.begin(), keys.end(), time, [](double t, const auto& key) { return t < key.Time; });
            const auto& a = *(upper - 1);
            const auto& b = *upper;
            if (track.Interpolation == TrackInterpolation::Step)
                return a.Value;
            return Interpolate(a.Value, b.Value, float((time - a.Time) / (b.Time - a.Time)), track.Interpolation);
        }
        JointTransform SampleJoint(const JointTransform& rest, const JointAnimationTrack& track, double time)
        {
            return {Sample(track.Translation, time, rest.Translation),
                    Sample(track.Rotation, time, rest.Rotation),
                    Sample(track.Scale, time, rest.Scale)};
        }
        glm::mat4 Power(glm::mat4 base, int64_t exponent)
        {
            if (exponent < 0)
            {
                base             = glm::inverse(base);
                exponent         = -(exponent + 1);
                glm::mat4 result = base;
                while (exponent)
                {
                    if (exponent & 1)
                        result *= base;
                    base *= base;
                    exponent >>= 1;
                }
                return result;
            }
            glm::mat4 result(1);
            while (exponent)
            {
                if (exponent & 1)
                    result *= base;
                base *= base;
                exponent >>= 1;
            }
            return result;
        }
    } // namespace
    void AnimationPose::Resize(size_t joints)
    {
        Local.resize(joints);
        Model.resize(joints);
        Palette.resize(joints);
    }
    double AnimationSampleTime(double time, double duration, bool loop)
    {
        if (!std::isfinite(time) || !(duration > 0))
            return 0;
        if (!loop)
            return std::clamp(time, 0.0, duration);
        auto result = std::fmod(time, duration);
        return result < 0 ? result + duration : result;
    }
    void RestPose(const SkeletonData& skeleton, AnimationPose& pose)
    {
        pose.Resize(skeleton.Joints.size());
        for (size_t j = 0; j < pose.Local.size(); ++j)
            pose.Local[j] = skeleton.Joints[j].RestLocal;
    }
    void SampleAnimation(const SkeletonData&       skeleton,
                         const BoundAnimationClip& binding,
                         double                    time,
                         bool                      loop,
                         AnimationPose&            pose)
    {
        RestPose(skeleton, pose);
        if (!binding.Clip)
            return;
        time = AnimationSampleTime(time, binding.Clip->Duration, loop);
        for (size_t j = 0; j < pose.Local.size(); ++j)
            if (binding.JointTracks.at(j) >= 0)
                pose.Local[j] = SampleJoint(pose.Local[j], binding.Clip->Tracks.at(binding.JointTracks[j]), time);
    }
    void BuildModelPose(const SkeletonData& skeleton, AnimationPose& pose)
    {
        pose.Resize(skeleton.Joints.size());
        for (size_t j = 0; j < skeleton.Joints.size(); ++j)
        {
            const auto& joint = skeleton.Joints[j];
            pose.Model[j] =
                joint.Parent < 0 ? pose.Local[j].Matrix() : pose.Model[joint.Parent] * pose.Local[j].Matrix();
            pose.Palette[j] = pose.Model[j] * joint.InverseBind;
        }
    }
    JointTransform BlendJoint(const JointTransform& a, const JointTransform& b, float weight)
    {
        weight = std::clamp(weight, 0.f, 1.f);
        return {glm::mix(a.Translation, b.Translation, weight),
                RotationBlend(a.Rotation, b.Rotation, weight, true),
                glm::mix(a.Scale, b.Scale, weight)};
    }
    void BlendAnimationPoses(const AnimationPose&      a,
                             const AnimationPose&      b,
                             float                     weight,
                             AnimationPose&            out,
                             const std::vector<float>& mask)
    {
        if (a.Local.size() != b.Local.size() || (!mask.empty() && mask.size() != a.Local.size()))
            throw std::invalid_argument("Pose/mask size mismatch");
        out.Resize(a.Local.size());
        for (size_t j = 0; j < a.Local.size(); ++j)
            out.Local[j] = BlendJoint(a.Local[j], b.Local[j], weight * (mask.empty() ? 1 : mask[j]));
    }
    void AddAnimationPose(AnimationPose&            pose,
                          const AnimationPose&      additive,
                          const AnimationPose&      reference,
                          float                     weight,
                          const std::vector<float>& mask)
    {
        if (pose.Local.size() != additive.Local.size() || pose.Local.size() != reference.Local.size() ||
            (!mask.empty() && mask.size() != pose.Local.size()))
            throw std::invalid_argument("Additive pose/mask size mismatch");
        for (size_t j = 0; j < pose.Local.size(); ++j)
        {
            const float w = std::clamp(weight * (mask.empty() ? 1 : mask[j]), 0.f, 1.f);
            const auto& a = additive.Local[j];
            const auto& r = reference.Local[j];
            auto&       p = pose.Local[j];
            p.Translation += (a.Translation - r.Translation) * w;
            p.Rotation = glm::normalize(
                p.Rotation * RotationBlend(glm::quat(1, 0, 0, 0), glm::inverse(r.Rotation) * a.Rotation, w, true));
            p.Scale *= glm::mix(glm::vec3(1), a.Scale / r.Scale, w);
        }
    }
    void AnimationBlendWeights(const AnimationMotion& motion, glm::vec2 p, std::vector<float>& out)
    {
        out.resize(motion.Children.size());
        std::fill(out.begin(), out.end(), 0);
        if (out.empty())
            return;
        if (motion.Type == AnimationMotionType::Blend1D)
        {
            if (p.x <= motion.Children.front().Threshold)
            {
                out.front() = 1;
                return;
            }
            if (p.x >= motion.Children.back().Threshold)
            {
                out.back() = 1;
                return;
            }
            for (size_t i = 1; i < out.size(); ++i)
                if (p.x < motion.Children[i].Threshold)
                {
                    const float w = (p.x - motion.Children[i - 1].Threshold) /
                                    (motion.Children[i].Threshold - motion.Children[i - 1].Threshold);
                    out[i - 1]    = 1 - w;
                    out[i]        = w;
                    return;
                }
        }
        else
        {
            float total = 0;
            for (size_t i = 0; i < out.size(); ++i)
            {
                float w = 1;
                for (size_t j = 0; j < out.size(); ++j)
                    if (i != j)
                    {
                        const auto d = motion.Children[j].Position - motion.Children[i].Position;
                        w = std::min(w,
                                     std::max(0.f, 1 - glm::dot(p - motion.Children[i].Position, d) / glm::dot(d, d)));
                    }
                total += out[i] = w;
            }
            if (total > 0)
                for (auto& w : out)
                    w /= total;
            else // Far outside the convex hull: deterministically choose nearest child.
            {
                size_t nearest = 0;
                for (size_t i = 1; i < out.size(); ++i)
                    if (glm::length2(p - motion.Children[i].Position) <
                        glm::length2(p - motion.Children[nearest].Position))
                        nearest = i;
                out[nearest] = 1;
            }
        }
    }
    void CrossAnimationEvents(const AnimationClipData&  clip,
                              double                    from,
                              double                    to,
                              bool                      loop,
                              const AnimationEventSink& sink,
                              uint32_t                  layer)
    {
        if (!sink || from == to || !std::isfinite(from) || !std::isfinite(to) || clip.Events.empty())
            return;
        const bool back = to < from;
        if (!loop || clip.Duration <= 0)
        {
            for (size_t n = 0; n < clip.Events.size(); ++n)
            {
                const auto& e = clip.Events[back ? clip.Events.size() - 1 - n : n];
                if ((!back && from < e.Time && e.Time <= to) || (back && to <= e.Time && e.Time < from))
                    sink({&e, e.Time, 0, layer, back});
            }
            return;
        }
        // Clip endpoints can both carry events. Keep them distinct at the shared cycle boundary.
        const double lo = std::floor(std::min(from, to) / clip.Duration) - 1;
        const double hi = std::floor(std::max(from, to) / clip.Duration);
        if (lo < double(INT64_MIN + 1) || hi >= double(INT64_MAX))
            throw std::out_of_range("Animation event time out of range");
        const auto first = int64_t(lo), last = int64_t(hi);
        for (int64_t k = back ? last : first;; k += back ? -1 : 1)
        {
            for (size_t n = 0; n < clip.Events.size(); ++n)
            {
                const auto&  e    = clip.Events[back ? clip.Events.size() - 1 - n : n];
                const double time = double(k) * clip.Duration + e.Time;
                if ((!back && from < time && time <= to) || (back && to <= time && time < from))
                    sink({&e, time, k, layer, back});
            }
            if (k == (back ? first : last))
                break;
        }
    }
    float AnimatorLayerStatus::CrossfadeWeight() const
    {
        return !Transitioning || FadeDuration <= 0 ? 1.f : float(std::clamp(FadeElapsed / FadeDuration, 0.0, 1.0));
    }
    AnimationValidation Animator::Prepare(const SkeletonData&            skeleton,
                                          const AnimationControllerData& controller,
                                          const ClipResolver&            resolve)
    {
        m_Skeleton = nullptr;
        auto valid = ValidateSkeleton(skeleton);
        if (std::holds_alternative<AssetError>(valid))
            return valid;
        valid = ValidateAnimationController(controller);
        if (std::holds_alternative<AssetError>(valid))
            return valid;
        m_Controller = &controller;
        m_Parameters.clear();
        for (const auto& p : controller.Parameters)
            m_Parameters.push_back(p.Default);
        m_Clips.clear();
        m_Clips.resize(controller.Motions.size());
        m_MotionPoses.resize(controller.Motions.size());
        m_Weights.resize(controller.Motions.size());
        try
        {
            for (size_t i = 0; i < controller.Motions.size(); ++i)
            {
                const auto& motion = controller.Motions[i];
                m_MotionPoses[i].Resize(skeleton.Joints.size());
                m_Weights[i].resize(motion.Children.size());
                if (motion.Type != AnimationMotionType::Clip)
                    continue;
                const auto* clip = motion.InlineClip ? &*motion.InlineClip : resolve ? resolve(motion.Clip) : nullptr;
                if (!clip)
                    return AssetError {AssetErrorCode::NotFound, "Animation clip is not ready"};
                valid = ValidateAnimationClip(*clip);
                if (std::holds_alternative<AssetError>(valid))
                    return valid;
                m_Clips[i] = BindAnimationClip(skeleton, *clip, motion.Mapping);
            }
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, e.what()};
        }
        m_Status.assign(controller.Layers.size(), {});
        m_Work.resize(controller.Layers.size());
        for (size_t l = 0; l < m_Work.size(); ++l)
        {
            const auto& layer = controller.Layers[l];
            auto&       work  = m_Work[l];
            work.Source.Resize(skeleton.Joints.size());
            work.Target.Resize(skeleton.Joints.size());
            work.Frozen.Resize(skeleton.Joints.size());
            work.UseFrozen    = false;
            work.Weight       = layer.Weight;
            m_Status[l].State = m_Status[l].Target = layer.EntryState;
            work.Mask.assign(skeleton.Joints.size(), layer.Mask.empty() ? 1.f : 0.f);
            for (const auto& entry : layer.Mask)
            {
                const auto root = skeleton.FindJoint(entry.Joint);
                if (root < 0)
                    return AssetError {AssetErrorCode::InvalidData,
                                       "Unknown or ambiguous avatar mask joint: " + entry.Joint};
                work.Mask[root] = entry.Weight;
                if (entry.IncludeChildren)
                    for (size_t j = root + 1; j < skeleton.Joints.size(); ++j)
                        for (auto parent = skeleton.Joints[j].Parent; parent >= 0;
                             parent      = skeleton.Joints[parent].Parent)
                            if (parent == root)
                            {
                                work.Mask[j] = entry.Weight;
                                break;
                            }
            }
        }
        m_Root = controller.RootMotionJoint.empty() ? -1 : skeleton.FindJoint(controller.RootMotionJoint);
        if (!controller.RootMotionJoint.empty() && m_Root < 0)
            return AssetError {AssetErrorCode::InvalidData, "Unknown root motion joint"};
        m_Skeleton = &skeleton;
        RestPose(skeleton, m_Rest);
        BuildModelPose(skeleton, m_Rest);
        m_Pose.Resize(skeleton.Joints.size());
        m_RootDelta = {};
        RebuildPose();
        return std::monostate {};
    }
    int32_t Animator::Parameter(const std::string& name) const
    {
        if (m_Controller)
            for (size_t i = 0; i < m_Controller->Parameters.size(); ++i)
                if (m_Controller->Parameters[i].Name == name)
                    return int32_t(i);
        return -1;
    }
    double Animator::Value(const std::string& name) const
    {
        auto i = Parameter(name);
        return i < 0 ? 0 : m_Parameters[i];
    }
    bool Animator::Set(const std::string& name, double value, AnimationParameterType type)
    {
        const auto i = Parameter(name);
        if (i < 0 || !std::isfinite(value) || m_Controller->Parameters[i].Type != type)
            return false;
        m_Parameters[i] = value;
        return true;
    }
    bool Animator::SetFloat(const std::string& n, float v) { return Set(n, v, AnimationParameterType::Float); }
    bool Animator::SetInt(const std::string& n, int v) { return Set(n, v, AnimationParameterType::Int); }
    bool Animator::SetBool(const std::string& n, bool v) { return Set(n, v, AnimationParameterType::Bool); }
    bool Animator::SetTrigger(const std::string& n) { return Set(n, 1, AnimationParameterType::Trigger); }
    bool Animator::ResetTrigger(const std::string& n) { return Set(n, 0, AnimationParameterType::Trigger); }
    std::optional<double> Animator::GetParameter(const std::string& name) const
    {
        auto i = Parameter(name);
        return i < 0 ? std::optional<double>() : m_Parameters[i];
    }
    bool Animator::SetLayerWeight(size_t layer, float weight)
    {
        if (layer >= m_Work.size() || !std::isfinite(weight) || weight < 0 || weight > 1)
            return false;
        m_Work[layer].Weight = weight;
        return true;
    }
    bool Animator::Play(size_t layer, uint32_t state, double time)
    {
        if (!IsPrepared() || layer >= m_Status.size() || state >= m_Controller->Layers[layer].States.size() ||
            !std::isfinite(time))
            return false;
        auto& s = m_Status[layer];
        s       = {};
        s.State = s.Target      = state;
        s.Time                  = time;
        m_Work[layer].UseFrozen = false;
        RebuildPose();
        return true;
    }
    bool Animator::CrossFade(size_t layer, uint32_t state, double seconds, double time)
    {
        if (!IsPrepared() || layer >= m_Status.size() || state >= m_Controller->Layers[layer].States.size() ||
            !std::isfinite(seconds) || seconds < 0 || !std::isfinite(time))
            return false;
        if (seconds == 0)
            return Play(layer, state, time);
        auto& s = m_Status[layer];
        auto& w = m_Work[layer];
        if (s.Transitioning)
        {
            if (!w.UseFrozen)
                StatePose(layer, s.State, s.Time, w.Source);
            else
                w.Source.Local = w.Frozen.Local;
            StatePose(layer, s.Target, s.TargetTime, w.Target);
            BlendAnimationPoses(w.Source, w.Target, s.CrossfadeWeight(), w.Frozen);
            w.UseFrozen = true;
        }
        s.Target        = state;
        s.TargetTime    = time;
        s.FadeElapsed   = 0;
        s.FadeDuration  = seconds;
        s.Transitioning = true;
        return true;
    }
    double Animator::Duration(uint32_t motion) const
    {
        if (m_Clips[motion].Clip)
            return m_Clips[motion].Clip->Duration;
        double duration = 0;
        for (const auto& child : m_Controller->Motions[motion].Children)
            duration = std::max(duration, Duration(child.Motion));
        return duration;
    }
    void Animator::SampleMotion(uint32_t motion, double time, bool loop, AnimationPose& out)
    {
        const auto& m = m_Controller->Motions[motion];
        if (m.Type == AnimationMotionType::Clip)
        {
            SampleAnimation(*m_Skeleton, m_Clips[motion], time, loop, out);
            return;
        }
        AnimationBlendWeights(m, {float(Value(m.ParameterX)), float(Value(m.ParameterY))}, m_Weights[motion]);
        float total = 0;
        // Trees use normalized phase: different clip lengths finish their cycles together.
        const double duration = Duration(motion);
        for (size_t i = 0; i < m.Children.size(); ++i)
        {
            const float weight = m_Weights[motion][i];
            if (weight <= 0)
                continue;
            const auto child = m.Children[i].Motion;
            auto&      pose  = m_MotionPoses[child];
            SampleMotion(child, duration > 0 ? time * Duration(child) / duration : 0, loop, pose);
            if (total == 0)
                out.Local = pose.Local;
            else
                BlendAnimationPoses(out, pose, weight / (total + weight), out);
            total += weight;
        }
        if (total == 0)
            RestPose(*m_Skeleton, out);
    }
    void Animator::StatePose(size_t layer, uint32_t state, double time, AnimationPose& out)
    {
        const auto& s = m_Controller->Layers[layer].States[state];
        SampleMotion(s.Motion, time, s.Loop, out);
    }
    bool Animator::EvaluateState(size_t layer, uint32_t state, double time, AnimationPose& out)
    {
        if (!IsPrepared() || layer >= m_Status.size() || state >= m_Controller->Layers[layer].States.size() ||
            !std::isfinite(time))
            return false;
        out.Resize(m_Skeleton->Joints.size());
        StatePose(layer, state, time, out);
        BuildModelPose(*m_Skeleton, out);
        return true;
    }
    void Animator::MotionEvents(uint32_t                  motion,
                                double                    from,
                                double                    to,
                                bool                      loop,
                                const AnimationEventSink& sink,
                                uint32_t                  layer) const
    {
        if (m_Clips[motion].Clip)
        {
            CrossAnimationEvents(*m_Clips[motion].Clip, from, to, loop, sink, layer);
            return;
        }
        const auto& m = m_Controller->Motions[motion];
        AnimationBlendWeights(m, {float(Value(m.ParameterX)), float(Value(m.ParameterY))}, m_Weights[motion]);
        // Events from the dominant child only, preventing duplicate footsteps in a blend. Ties use child order.
        size_t best       = 0;
        float  bestWeight = -1;
        // Reuse precomputed weights from the last pose rebuild (Advance rebuilds before events).
        for (size_t i = 0; i < m.Children.size(); ++i)
            if (m_Weights[motion][i] > bestWeight)
            {
                best       = i;
                bestWeight = m_Weights[motion][i];
            }
        const auto   child    = m.Children[best].Motion;
        const double duration = Duration(motion);
        const double ratio    = duration > 0 ? Duration(child) / duration : 0;
        MotionEvents(child, from * ratio, to * ratio, loop, sink, layer);
    }
    void
    Animator::ScrubEvents(size_t layer, uint32_t state, double from, double to, const AnimationEventSink& sink) const
    {
        if (!IsPrepared() || layer >= m_Status.size() || state >= m_Controller->Layers[layer].States.size())
            return;
        const auto& s = m_Controller->Layers[layer].States[state];
        MotionEvents(s.Motion, from, to, s.Loop, sink, uint32_t(layer));
    }
    JointTransform Animator::MotionRoot(uint32_t motion, double time, bool loop)
    {
        const auto& m = m_Controller->Motions[motion];
        if (m.Type == AnimationMotionType::Clip)
        {
            const auto& binding = m_Clips[motion];
            const auto& clip    = *binding.Clip;
            const auto  rest    = m_Skeleton->Joints[m_Root].RestLocal;
            const auto  t       = binding.JointTracks[m_Root];
            if (t < 0)
                return {};
            const auto parent           = m_Skeleton->Joints[m_Root].Parent;
            const auto parentModel      = parent < 0 ? glm::mat4(1) : m_Rest.Model[parent];
            auto       reference        = JointTransform::FromMatrix(m_Rest.Model[m_Root]);
            reference.Scale             = glm::vec3(1);
            const auto inverseReference = glm::inverse(reference.Matrix());
            auto       sample           = [&](double at) {
                auto local  = SampleJoint(rest, clip.Tracks[t], at);
                auto model  = JointTransform::FromMatrix(parentModel * local.Matrix());
                model.Scale = glm::vec3(1);
                // Motion is a factor in instance model space, relative to the rest joint. This
                // converts FBX ancestor unit scales/axis rotations before applying deltas to the entity.
                auto result  = JointTransform::FromMatrix(model.Matrix() * inverseReference);
                result.Scale = glm::vec3(1);
                return result;
            };
            auto result = sample(AnimationSampleTime(time, clip.Duration, loop));
            if (loop && clip.Duration > 0)
            {
                const double cycles = std::floor(time / clip.Duration);
                if (cycles < double(INT64_MIN + 1) || cycles >= double(INT64_MAX))
                    throw std::out_of_range("Root motion time out of range");
                const auto cycle = sample(clip.Duration).Matrix() * glm::inverse(sample(0).Matrix());
                result           = JointTransform::FromMatrix(Power(cycle, int64_t(cycles)) * result.Matrix());
            }
            return result;
        }
        AnimationBlendWeights(m, {float(Value(m.ParameterX)), float(Value(m.ParameterY))}, m_Weights[motion]);
        JointTransform result;
        float          total    = 0;
        const double   duration = Duration(motion);
        for (size_t i = 0; i < m.Children.size(); ++i)
        {
            const float w = m_Weights[motion][i];
            if (w <= 0)
                continue;
            const auto child = m.Children[i].Motion;
            auto       root  = MotionRoot(child, duration > 0 ? time * Duration(child) / duration : 0, loop);
            result           = total == 0 ? root : BlendJoint(result, root, w / (total + w));
            total += w;
        }
        return result;
    }
    RootMotionDelta Animator::StateRootDelta(size_t layer, uint32_t state, double from, double to)
    {
        if (m_Root < 0)
            return {};
        const auto& s = m_Controller->Layers[layer].States[state];
        const auto  a = MotionRoot(s.Motion, from, s.Loop), b = MotionRoot(s.Motion, to, s.Loop);
        const auto  inv = glm::inverse(a.Rotation);
        return {inv * (b.Translation - a.Translation), glm::normalize(inv * b.Rotation)};
    }
    bool Animator::Conditions(const AnimationTransition& transition) const
    {
        for (const auto& c : transition.Conditions)
        {
            const auto v     = Value(c.Parameter);
            bool       match = false;
            switch (c.Op)
            {
                case AnimationConditionOp::Greater:
                    match = v > c.Value;
                    break;
                case AnimationConditionOp::Less:
                    match = v < c.Value;
                    break;
                case AnimationConditionOp::Equal:
                    match = v == c.Value;
                    break;
                case AnimationConditionOp::NotEqual:
                    match = v != c.Value;
                    break;
                case AnimationConditionOp::If:
                    match = v != 0;
                    break;
                case AnimationConditionOp::IfNot:
                    match = v == 0;
                    break;
            }
            if (!match)
                return false;
        }
        return true;
    }
    bool
    Animator::Exit(const AnimationTransition& transition, size_t layer, uint32_t state, double from, double to) const
    {
        if (!transition.ExitTime)
            return true;
        const double duration = Duration(m_Controller->Layers[layer].States[state].Motion);
        if (duration <= 0)
            return true;
        from /= duration;
        to /= duration;
        const auto exit = *transition.ExitTime;
        if (exit >= 1 || !m_Controller->Layers[layer].States[state].Loop)
            return to >= from ? from <= exit && exit <= to : to <= exit && exit <= from;
        const double crossing = to >= from ? std::floor(from - exit) + 1 + exit : std::ceil(from - exit) - 1 + exit;
        return to >= from ? crossing <= to : crossing >= to;
    }
    void Animator::Consume(const AnimationTransition& transition)
    {
        for (const auto& c : transition.Conditions)
        {
            const auto i = Parameter(c.Parameter);
            if (i >= 0 && m_Controller->Parameters[i].Type == AnimationParameterType::Trigger)
                m_Parameters[i] = 0;
        }
    }
    void Animator::Advance(double seconds, const AnimationEventSink& sink)
    {
        if (!IsPrepared() || !std::isfinite(seconds) || !std::isfinite(Speed))
            return;
        const double dt = seconds * Speed;
        m_RootDelta     = {};
        for (size_t l = 0; l < m_Status.size(); ++l)
        {
            auto&       s     = m_Status[l];
            auto&       work  = m_Work[l];
            const auto& layer = m_Controller->Layers[l];
            // One transition per layer per update. Any-state has priority and obeys the active transition's
            // interruption rule.
            const auto                 permission      = s.Transitioning ? s.Interruption : AnimationInterruption::Both;
            const AnimationTransition* chosen          = nullptr;
            double                     transitionDelay = 0;
            for (int pass = 0; pass < 2 && !chosen; ++pass)
                for (const auto& t : layer.Transitions)
                {
                    if ((t.Source == -1) != (pass == 0) ||
                        (s.Transitioning && permission == AnimationInterruption::None))
                        continue;
                    uint32_t origin = s.State;
                    double   time   = s.Time;
                    if (t.Source >= 0)
                    {
                        if (uint32_t(t.Source) == s.State &&
                            (!s.Transitioning || permission == AnimationInterruption::Source ||
                             permission == AnimationInterruption::Both))
                        {
                        }
                        else if (s.Transitioning && uint32_t(t.Source) == s.Target &&
                                 (permission == AnimationInterruption::Destination ||
                                  permission == AnimationInterruption::Both))
                        {
                            origin = s.Target;
                            time   = s.TargetTime;
                        }
                        else
                            continue;
                    }
                    if ((!t.CanTransitionToSelf && t.Target == (s.Transitioning ? s.Target : s.State)) ||
                        !Conditions(t) || !Exit(t, l, origin, time, time + dt * layer.States[origin].Speed))
                        continue;
                    chosen = &t;
                    if (t.ExitTime)
                    {
                        const double duration = Duration(layer.States[origin].Motion);
                        const double advance  = dt * layer.States[origin].Speed;
                        if (duration > 0 && advance != 0)
                        {
                            const double from = time / duration, exit = *t.ExitTime;
                            const double crossing = exit >= 1 || !layer.States[origin].Loop ? exit :
                                                    advance > 0 ? std::floor(from - exit) + 1 + exit :
                                                                  std::ceil(from - exit) - 1 + exit;
                            transitionDelay       = std::clamp((crossing * duration - time) / advance, 0.0, 1.0);
                        }
                    }
                    break;
                }
            auto step = [&](double frameSeconds) {
                const double advance = frameSeconds * Speed;
                const double old     = s.Time;
                s.Time += advance * layer.States[s.State].Speed;
                auto delta = work.UseFrozen ? RootMotionDelta {} : StateRootDelta(l, s.State, old, s.Time);
                if (!s.Transitioning)
                {
                    if (sink)
                        ScrubEvents(l, s.State, old, s.Time, sink);
                }
                else
                {
                    const double previousTarget = s.TargetTime;
                    s.TargetTime += advance * layer.States[s.Target].Speed;
                    const double elapsed = s.FadeElapsed;
                    const float  before  = s.CrossfadeWeight();
                    s.FadeElapsed += std::abs(frameSeconds);
                    const float after       = s.CrossfadeWeight();
                    const float weight      = .5f * (before + after);
                    const auto  targetDelta = StateRootDelta(l, s.Target, previousTarget, s.TargetTime);
                    delta.Translation       = glm::mix(delta.Translation, targetDelta.Translation, weight);
                    delta.Rotation          = RotationBlend(delta.Rotation, targetDelta.Rotation, weight, true);
                    if (sink)
                    {
                        // Dominant stream policy. Split at the exact half-fade crossing so changing
                        // the frame size neither drops nor duplicates crossings on either stream.
                        const double sourceFraction =
                            frameSeconds == 0 ?
                                (before < .5f ? 1 : 0) :
                                std::clamp((s.FadeDuration * .5 - elapsed) / std::abs(frameSeconds), 0.0, 1.0);
                        if (!work.UseFrozen && sourceFraction > 0)
                            ScrubEvents(l, s.State, old, old + (s.Time - old) * sourceFraction, sink);
                        if (sourceFraction < 1)
                            ScrubEvents(l,
                                        s.Target,
                                        previousTarget + (s.TargetTime - previousTarget) * sourceFraction,
                                        s.TargetTime,
                                        sink);
                    }
                    if (s.FadeElapsed >= s.FadeDuration)
                    {
                        s.State         = s.Target;
                        s.Time          = s.TargetTime;
                        s.Transitioning = false;
                        work.UseFrozen  = false;
                    }
                }
                if (l == 0)
                {
                    const float weight = work.Weight * (m_Root >= 0 ? work.Mask[m_Root] : 0);
                    m_RootDelta.Translation += m_RootDelta.Rotation * (delta.Translation * weight);
                    m_RootDelta.Rotation = glm::normalize(
                        m_RootDelta.Rotation * RotationBlend(glm::quat(1, 0, 0, 0), delta.Rotation, weight, true));
                }
            };
            auto advance = [&](double frameSeconds) {
                if (s.Transitioning && std::abs(frameSeconds) > s.FadeDuration - s.FadeElapsed)
                {
                    const double untilComplete =
                        std::copysign(std::max(0.0, s.FadeDuration - s.FadeElapsed), frameSeconds);
                    step(untilComplete);
                    step(frameSeconds - untilComplete);
                }
                else
                    step(frameSeconds);
            };
            if (chosen)
            {
                advance(seconds * transitionDelay);
                CrossFade(l, chosen->Target, chosen->Duration);
                s.Interruption = chosen->Interruption;
                Consume(*chosen);
                advance(seconds * (1 - transitionDelay));
            }
            else
                advance(seconds);
        }
        RebuildPose();
    }
    void Animator::RebuildPose()
    {
        if (!IsPrepared())
            return;
        m_Pose.Local = m_Rest.Local;
        for (size_t l = 0; l < m_Status.size(); ++l)
        {
            const auto& s     = m_Status[l];
            auto&       w     = m_Work[l];
            const auto& layer = m_Controller->Layers[l];
            if (s.Transitioning && w.UseFrozen)
                w.Source.Local = w.Frozen.Local;
            else
                StatePose(l, s.State, s.Time, w.Source);
            if (s.Transitioning)
            {
                StatePose(l, s.Target, s.TargetTime, w.Target);
                BlendAnimationPoses(w.Source, w.Target, s.CrossfadeWeight(), w.Source);
            }
            if (layer.Mode == AnimationLayerMode::Additive)
                AddAnimationPose(m_Pose, w.Source, m_Rest, w.Weight, w.Mask);
            else
                BlendAnimationPoses(m_Pose, w.Source, w.Weight, m_Pose, w.Mask);
        }
        if (ExtractRootMotion && m_Root >= 0)
        {
            m_Pose.Local[m_Root].Translation = m_Rest.Local[m_Root].Translation;
            m_Pose.Local[m_Root].Rotation    = m_Rest.Local[m_Root].Rotation;
        }
        if (m_Modifier)
            m_Modifier(*m_Skeleton, m_Pose);
        BuildModelPose(*m_Skeleton, m_Pose);
    }
} // namespace Kans
