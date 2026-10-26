#include "kspch.h"
#include "AnimationData.h"
#include <cmath>
#include <set>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Kans
{
    JointTransform SampleAnimationJoint(const JointTransform&      rest,
                                        const JointAnimationTrack& track,
                                        double                     time,
                                        AnimationSampleEndpoint    endpoint)
    {
        if (endpoint == AnimationSampleEndpoint::Interpolated)
            return {SampleAnimationTrack(track.Translation, time, rest.Translation),
                    SampleAnimationTrack(track.Rotation, time, rest.Rotation),
                    SampleAnimationTrack(track.Scale, time, rest.Scale)};
        auto sample = [&](const auto& channel, const auto& value) {
            if (channel.RootEndpoints)
                return (*channel.RootEndpoints)[endpoint == AnimationSampleEndpoint::First ? 0 : 1];
            return SampleAnimationTrack(channel, time, value);
        };
        return {sample(track.Translation, rest.Translation),
                sample(track.Rotation, rest.Rotation),
                sample(track.Scale, rest.Scale)};
    }
    size_t AnimationClipData::ResidentBytes() const
    {
        size_t bytes = sizeof(*this) + Name.capacity() + SourceName.capacity() + Compression.Joint.capacity() +
                       Compression.ShapeKey.capacity() + Tracks.capacity() * sizeof(JointAnimationTrack) +
                       MorphTracks.capacity() * sizeof(MorphAnimationTrack) +
                       Events.capacity() * sizeof(AnimationEvent);
        bytes += TimeTables.capacity() * sizeof(TimeTables[0]);
        for (const auto& table : TimeTables)
            bytes += sizeof(*table) + table->capacity() * sizeof(double) + 2 * sizeof(void*);
        for (const auto& t : Tracks)
            bytes += t.Joint.capacity() + t.Translation.ResidentBytes() + t.Rotation.ResidentBytes() +
                     t.Scale.ResidentBytes();
        for (const auto& t : MorphTracks)
            bytes += t.ShapeKey.capacity() + t.Weight.ResidentBytes();
        for (const auto& e : Events)
            bytes += e.Name.capacity() + e.Payload.capacity();
        return bytes;
    }
    glm::mat4 JointTransform::Matrix() const
    {
        return glm::translate(glm::mat4(1), Translation) * glm::mat4_cast(Rotation) * glm::scale(glm::mat4(1), Scale);
    }
    JointTransform JointTransform::FromMatrix(const glm::mat4& matrix)
    {
        JointTransform result;
        if (matrix[0][3] == 0 && matrix[1][3] == 0 && matrix[2][3] == 0 && matrix[3][3] == 1)
        {
            // The affine branch of GLM's decompose, with identical arithmetic order.
            // Skip perspective isolation and division of all 16 entries by 1;
            // reuse each computed column length instead of computing it twice.
            auto perspective = matrix;
            for (int i = 0; i < 3; ++i)
                perspective[i][3] = 0;
            if (glm::epsilonEqual(glm::determinant(perspective), 0.f, glm::epsilon<float>()))
                throw std::invalid_argument("Singular joint transform");
            result.Translation = glm::vec3(matrix[3]);
            glm::vec3 row[]    = {glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2])};
            result.Scale.x     = glm::length(row[0]);
            row[0]             = row[0] * 1.f / result.Scale.x;
            row[1]             = row[1] * 1.f + row[0] * (-glm::dot(row[0], row[1]));
            result.Scale.y     = glm::length(row[1]);
            row[1]             = row[1] * 1.f / result.Scale.y;
            row[2]             = row[2] * 1.f + row[0] * (-glm::dot(row[0], row[2]));
            row[2]             = row[2] * 1.f + row[1] * (-glm::dot(row[1], row[2]));
            result.Scale.z     = glm::length(row[2]);
            row[2]             = row[2] * 1.f / result.Scale.z;
            if (glm::dot(row[0], glm::cross(row[1], row[2])) < 0)
                for (int i = 0; i < 3; ++i)
                {
                    result.Scale[i] *= -1.f;
                    row[i] *= -1.f;
                }
            const float trace = row[0].x + row[1].y + row[2].z;
            if (trace > 0)
            {
                float root        = std::sqrt(trace + 1.f);
                result.Rotation.w = .5f * root;
                root              = .5f / root;
                result.Rotation.x = root * (row[1].z - row[2].y);
                result.Rotation.y = root * (row[2].x - row[0].z);
                result.Rotation.z = root * (row[0].y - row[1].x);
            }
            else
            {
                constexpr int next[] = {1, 2, 0};
                int           i      = 0;
                if (row[1].y > row[0].x)
                    i = 1;
                if (row[2].z > row[i][i])
                    i = 2;
                const int j = next[i], k = next[j];
                float     root = std::sqrt(row[i][i] - row[j][j] - row[k][k] + 1.f);
                float     xyz[3];
                xyz[i]            = .5f * root;
                root              = .5f / root;
                xyz[j]            = root * (row[i][j] + row[j][i]);
                xyz[k]            = root * (row[i][k] + row[k][i]);
                result.Rotation.x = xyz[0];
                result.Rotation.y = xyz[1];
                result.Rotation.z = xyz[2];
                result.Rotation.w = root * (row[j][k] - row[k][j]);
            }
            result.Rotation = glm::normalize(result.Rotation);
            return result;
        }
        glm::vec3 skew;
        glm::vec4 perspective;
        if (!glm::decompose(matrix, result.Scale, result.Rotation, result.Translation, skew, perspective))
            throw std::invalid_argument("Singular joint transform");
        result.Rotation = glm::normalize(result.Rotation);
        return result;
    }
    int32_t SkeletonData::FindJoint(const std::string& name) const
    {
        for (size_t i = 0; i < Joints.size(); ++i)
            if (Joints[i].Path == name)
                return int32_t(i);
        int32_t found = -1;
        for (size_t i = 0; i < Joints.size(); ++i)
            if (Joints[i].Name == name)
            {
                if (found >= 0)
                    return -1; // Ambiguous names must be addressed by path.
                found = int32_t(i);
            }
        return found;
    }
    int32_t SkeletonData::FindShapeKey(const std::string& name) const
    {
        for (size_t i = 0; i < ShapeKeys.size(); ++i)
            if (ShapeKeys[i].Name == name)
                return int32_t(i);
        return -1;
    }
    std::string AnimationNameMapping::Map(const std::string& source) const
    {
        const auto name  = !StripPrefix.empty() && source.compare(0, StripPrefix.size(), StripPrefix) == 0 ?
                               source.substr(StripPrefix.size()) :
                               source;
        auto       found = Names.find(name);
        return found == Names.end() ? name : found->second;
    }
    BoundAnimationClip
    BindAnimationClip(const SkeletonData& skeleton, const AnimationClipData& clip, const AnimationNameMapping& mapping)
    {
        BoundAnimationClip result;
        result.Clip = &clip;
        result.JointTracks.assign(skeleton.Joints.size(), -1);
        for (size_t t = 0; t < clip.Tracks.size(); ++t)
        {
            const auto joint = skeleton.FindJoint(mapping.Map(clip.Tracks[t].Joint));
            if (joint >= 0)
            {
                if (result.JointTracks[joint] >= 0)
                    throw std::invalid_argument("Multiple animation tracks map to the same joint");
                result.JointTracks[joint] = int32_t(t);
            }
        }
        result.MorphTracks.assign(skeleton.ShapeKeys.size(), -1);
        for (size_t t = 0; t < clip.MorphTracks.size(); ++t)
        {
            const auto key = skeleton.FindShapeKey(mapping.Map(clip.MorphTracks[t].ShapeKey));
            if (key < 0)
                continue;
            if (result.MorphTracks[key] >= 0)
                throw std::invalid_argument("Multiple tracks map to the same shape key");
            result.MorphTracks[key] = int32_t(t);
        }
        for (size_t j = 0; j < result.JointTracks.size(); ++j)
            if (result.JointTracks[j] < 0)
                result.MissingJoints.push_back(skeleton.Joints[j].Path);
        return result;
    }
    namespace
    {
        AssetError Invalid(const std::string& message) { return {AssetErrorCode::InvalidData, message}; }
        bool       Finite(float v) { return std::isfinite(v); }
        bool       Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        bool       Finite(glm::quat q)
        {
            return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w) &&
                   std::abs(glm::dot(q, q) - 1.f) < 1e-3f;
        }
        bool Finite(const JointTransform& t)
        {
            return Finite(t.Translation) && Finite(t.Rotation) && Finite(t.Scale) && t.Scale.x != 0 && t.Scale.y != 0 &&
                   t.Scale.z != 0;
        }
        bool Text(const std::string& s) { return s.size() <= 4096 && s.find('\0') == std::string::npos; }
        template<class T>
        bool Track(const AnimationTrack<T>& track, double duration)
        {
            if (track.Interpolation > TrackInterpolation::Spherical || track.KeyCount() > 1000000)
                return false;
            if (track.RootEndpoints &&
                (!track.KeyCount() || !Finite((*track.RootEndpoints)[0]) || !Finite((*track.RootEndpoints)[1])))
                return false;
            double previous = -1;
            if (track.PackedCount)
            {
                if (!track.Keys.empty() || track.TimeEncoding > 5 ||
                    (track.TimeEncoding == 0 && track.Times.size() != track.PackedCount) ||
                    ((track.TimeEncoding == 1 || track.TimeEncoding == 2) &&
                     (!std::isfinite(track.Start) || !std::isfinite(track.Interval) || track.Interval <= 0)) ||
                    (track.RunEnds.empty() ?
                         track.Values.size() != track.PackedCount :
                         track.RunEnds.size() != track.Values.size() || track.RunEnds.back() != track.PackedCount))
                    return false;
                if ((track.TimeEncoding != 0 && !track.Times.empty()) ||
                    (track.TimeEncoding < 3 &&
                     (track.TimeTable || !track.TimeIndices16.empty() || !track.TimeIndices32.empty())) ||
                    (track.TimeEncoding != 4 && !track.TimeIndices16.empty()) ||
                    (track.TimeEncoding != 5 && !track.TimeIndices32.empty()))
                    return false;
                if (track.TimeEncoding >= 3)
                {
                    if (!track.TimeTable || track.TimeTable->empty() || !track.Times.empty() ||
                        (track.TimeEncoding == 3 && track.TimeTable->size() != track.PackedCount) ||
                        (track.TimeEncoding == 4 &&
                         (track.TimeTable->size() > 65536 || track.TimeIndices16.size() != track.PackedCount)) ||
                        (track.TimeEncoding == 5 && track.TimeIndices32.size() != track.PackedCount))
                        return false;
                    for (auto index : track.TimeIndices16)
                        if (index >= track.TimeTable->size())
                            return false;
                    for (auto index : track.TimeIndices32)
                        if (index >= track.TimeTable->size())
                            return false;
                }
                uint32_t end = 0;
                for (auto next : track.RunEnds)
                {
                    if (next <= end || next > track.PackedCount)
                        return false;
                    end = next;
                }
            }
            for (size_t i = 0; i < track.KeyCount(); ++i)
            {
                const auto key = track.KeyAt(i);
                if (!std::isfinite(key.Time) || key.Time <= previous || key.Time < 0 || key.Time > duration ||
                    !Finite(key.Value))
                    return false;
                previous = key.Time;
            }
            return true;
        }
    } // namespace
    AnimationValidation ValidateSkeleton(const SkeletonData& data)
    {
        if (data.Joints.empty() || data.Joints.size() > 65536)
            return Invalid("Invalid skeleton joint count");
        std::set<std::string> shapeNames;
        if (data.ShapeKeys.size() > 65536)
            return Invalid("Too many shape keys");
        for (const auto& key : data.ShapeKeys)
            if (key.Name.empty() || !Text(key.Name) || !Finite(key.DefaultWeight) ||
                !shapeNames.insert(key.Name).second)
                return Invalid("Invalid logical shape key");
        std::set<std::string> paths;
        for (size_t j = 0; j < data.Joints.size(); ++j)
        {
            const auto& joint = data.Joints[j];
            if (joint.Parent < -1 || joint.Parent >= int64_t(j) || joint.Name.empty() || joint.Path.empty() ||
                !Text(joint.Name) || !Text(joint.Path) || !Text(joint.NodeKey) || !paths.insert(joint.Path).second ||
                !Finite(joint.RestLocal) || !Finite(joint.BindLocal))
                return Invalid("Invalid skeleton hierarchy or transform");
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(joint.InverseBind[c][r]))
                        return Invalid("Non-finite inverse bind matrix");
        }
        return std::monostate {};
    }
    AnimationValidation ValidateAnimationClip(const AnimationClipData& clip)
    {
        if (!Text(clip.Name) || !Text(clip.SourceName) || !std::isfinite(clip.Duration) || clip.Duration < 0 ||
            !std::isfinite(clip.FrameRate) || clip.FrameRate <= 0 || clip.Tracks.size() > 65536 ||
            clip.Events.size() > 1000000)
            return Invalid("Invalid animation clip metadata");
        std::set<std::string> names;
        for (const auto& t : clip.Tracks)
            if (t.Joint.empty() || !Text(t.Joint) || !names.insert(t.Joint).second ||
                !Track(t.Translation, clip.Duration) || !Track(t.Rotation, clip.Duration) ||
                !Track(t.Scale, clip.Duration))
                return Invalid("Invalid animation track");
        names.clear();
        if (clip.MorphTracks.size() > 65536)
            return Invalid("Too many morph tracks");
        for (const auto& t : clip.MorphTracks)
            if (t.ShapeKey.empty() || !Text(t.ShapeKey) || !names.insert(t.ShapeKey).second ||
                !Track(t.Weight, clip.Duration) || t.Weight.Interpolation == TrackInterpolation::Spherical)
                return Invalid("Invalid morph animation track");
        double previous = -1;
        for (const auto& e : clip.Events)
        {
            if (!std::isfinite(e.Time) || e.Time < previous || e.Time < 0 || e.Time > clip.Duration || e.Name.empty() ||
                !Text(e.Name) || !Text(e.Payload))
                return Invalid("Invalid animation event");
            previous = e.Time;
        }
        return std::monostate {};
    }
    AnimationValidation ValidateAnimationController(const AnimationControllerData& c)
    {
        if (c.Motions.empty() || c.Motions.size() > 4096 || c.Layers.empty() || c.Layers.size() > 64 ||
            c.Parameters.size() > 4096 || !Text(c.RootMotionJoint))
            return Invalid("Invalid controller counts");
        auto unitWeight  = [](float w) { return std::isfinite(w) && w >= 0 && w <= 1; };
        auto targetValid = [](const IKTarget& t) {
            return Finite(t.Position) && Finite(t.Rotation) && glm::dot(t.Rotation, t.Rotation) > 1e-12f;
        };
        std::set<std::string> ikNames;
        auto nameValid = [&](const std::string& n) { return !n.empty() && Text(n) && ikNames.insert(n).second; };
        if (c.TwoBoneConstraints.size() > 256 || c.LookAtConstraints.size() > 256)
            return Invalid("Too many IK constraints");
        for (const auto& ik : c.TwoBoneConstraints)
            if (!nameValid(ik.Name) || !Text(ik.Root) || !Text(ik.Middle) || !Text(ik.End) || !targetValid(ik.Target) ||
                !Finite(ik.PoleDirection) || !unitWeight(ik.Weight) || !unitWeight(ik.RotationWeight))
                return Invalid("Invalid two-bone IK constraint");
        for (const auto& ik : c.LookAtConstraints)
        {
            if (!nameValid(ik.Name) || !targetValid(ik.Target) || !unitWeight(ik.Weight) || !Finite(ik.WorldUp) ||
                !std::isfinite(ik.AngleLimit) || ik.AngleLimit < 0 || ik.AngleLimit > glm::pi<float>() ||
                ik.Bones.empty() || ik.Bones.size() > 64)
                return Invalid("Invalid look-at constraint");
            for (const auto& bone : ik.Bones)
                if (!Text(bone.Joint) || !unitWeight(bone.Weight) || !Finite(bone.Forward) || !Finite(bone.Up) ||
                    glm::length2(bone.Forward) < 1e-12f || glm::length2(glm::cross(bone.Forward, bone.Up)) < 1e-12f)
                    return Invalid("Invalid look-at bone axes/weight");
        }
        std::set<std::string> expressionNames;
        for (const auto& p : c.Expressions)
        {
            if (p.Name.empty() || !Text(p.Name) || !expressionNames.insert(p.Name).second)
                return Invalid("Invalid expression name");
            std::set<std::string> keys;
            for (const auto& k : p.Weights)
                if (k.Name.empty() || !Text(k.Name) || !Finite(k.Weight) || !keys.insert(k.Name).second)
                    return Invalid("Invalid expression weight");
        }
        std::set<std::string> parameters;
        for (const auto& p : c.Parameters)
            if (p.Name.empty() || !Text(p.Name) || !parameters.insert(p.Name).second || !std::isfinite(p.Default) ||
                p.Type > AnimationParameterType::Trigger ||
                (p.Type == AnimationParameterType::Int &&
                 (p.Default < INT32_MIN || p.Default > INT32_MAX || std::trunc(p.Default) != p.Default)) ||
                (p.Type >= AnimationParameterType::Bool && p.Default != 0 && p.Default != 1))
                return Invalid("Invalid controller parameter");
        std::vector<uint8_t>                    visited(c.Motions.size());
        std::function<bool(uint32_t, uint32_t)> visit = [&](uint32_t i, uint32_t depth) {
            if (i >= c.Motions.size() || depth > 64 || visited[i] == 1)
                return false;
            if (visited[i] == 2)
                return true;
            visited[i]    = 1;
            const auto& m = c.Motions[i];
            if (m.Type > AnimationMotionType::Blend2D)
                return false;
            if (m.Type == AnimationMotionType::Clip)
            {
                // A clip motion without a clip is valid: it holds the rest pose (a state
                // the editor has added but not assigned yet).
                if (m.InlineClip && std::holds_alternative<AssetError>(ValidateAnimationClip(*m.InlineClip)))
                    return false;
            }
            else
            {
                if (m.Children.empty() || m.Children.size() > 256 || !parameters.count(m.ParameterX) ||
                    (m.Type == AnimationMotionType::Blend2D && !parameters.count(m.ParameterY)))
                    return false;
                for (size_t j = 0; j < m.Children.size(); ++j)
                {
                    const auto& child = m.Children[j];
                    if (!std::isfinite(child.Threshold) || !std::isfinite(child.Position.x) ||
                        !std::isfinite(child.Position.y) || !visit(child.Motion, depth + 1))
                        return false;
                    for (size_t k = 0; k < j; ++k)
                        if ((m.Type == AnimationMotionType::Blend1D && m.Children[k].Threshold >= child.Threshold) ||
                            (m.Type == AnimationMotionType::Blend2D && m.Children[k].Position == child.Position))
                            return false;
                }
            }
            visited[i] = 2;
            return true;
        };
        for (uint32_t i = 0; i < c.Motions.size(); ++i)
            if (!visit(i, 0))
                return Invalid("Invalid motion graph (cycle, depth, clip or blend tree)");
        std::set<std::string> slots;
        for (const auto& layer : c.Layers)
        {
            if (!layer.Slot.empty() && (!Text(layer.Slot) || !slots.insert(layer.Slot).second))
                return Invalid("Duplicate or invalid animation slot: " + layer.Slot);
            if (layer.States.empty() || layer.States.size() > 4096 || layer.Transitions.size() > 16384 ||
                layer.EntryState >= layer.States.size() || !std::isfinite(layer.Weight) || layer.Weight < 0 ||
                layer.Weight > 1 || layer.Mode > AnimationLayerMode::Additive || !Text(layer.Name))
                return Invalid("Invalid animation layer");
            for (const auto& key : layer.ShapeKeyMask)
                if (key.Name.empty() || !Text(key.Name) || !Finite(key.Weight) || key.Weight < 0 || key.Weight > 1)
                    return Invalid("Invalid shape key mask");
            for (const auto& mask : layer.Mask)
                if (!Text(mask.Joint) || !std::isfinite(mask.Weight) || mask.Weight < 0 || mask.Weight > 1)
                    return Invalid("Invalid avatar mask");
            for (const auto& state : layer.States)
                if (!Text(state.Name) || state.Motion >= c.Motions.size() || !std::isfinite(state.Speed))
                    return Invalid("Invalid animation state");
            for (const auto& t : layer.Transitions)
            {
                if (t.Source < -1 || t.Source >= int64_t(layer.States.size()) || t.Target >= layer.States.size() ||
                    !std::isfinite(t.Duration) || t.Duration < 0 || t.Interruption > AnimationInterruption::Both ||
                    (t.ExitTime && (!std::isfinite(*t.ExitTime) || *t.ExitTime < 0)))
                    return Invalid("Invalid animation transition");
                for (const auto& condition : t.Conditions)
                    if (!parameters.count(condition.Parameter) || condition.Op > AnimationConditionOp::IfNot ||
                        !std::isfinite(condition.Value))
                        return Invalid("Invalid transition condition");
            }
        }
        return std::monostate {};
    }
} // namespace Kans
