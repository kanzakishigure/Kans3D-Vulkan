#include "kspch.h"
#include "AnimationProduct.h"
#include <cstring>
#include <stdexcept>
namespace Kans
{
    namespace
    {
        void Require(bool value)
        {
            if (!value)
                throw std::runtime_error("Invalid animation Product fields");
        }
        void       Sequence(const YAML::Node& n, size_t max = 1000000) { Require(n.IsSequence() && n.size() <= max); }
        YAML::Node Vec(glm::vec3 v)
        {
            YAML::Node n;
            for (int i = 0; i < 3; ++i)
                n.push_back(v[i]);
            return n;
        }
        YAML::Node Quat(glm::quat q)
        {
            YAML::Node n;
            // Use named components: GLM quaternion operator[] order depends on its build configuration.
            for (float value : {q.x, q.y, q.z, q.w})
                n.push_back(value);
            return n;
        }
        glm::vec3 ReadVec(const YAML::Node& n)
        {
            Sequence(n, 3);
            Require(n.size() == 3);
            return {n[0].as<float>(), n[1].as<float>(), n[2].as<float>()};
        }
        glm::quat ReadQuat(const YAML::Node& n)
        {
            Sequence(n, 4);
            Require(n.size() == 4);
            return {n[3].as<float>(), n[0].as<float>(), n[1].as<float>(), n[2].as<float>()};
        }
        YAML::Node Transform(const JointTransform& t)
        {
            YAML::Node n;
            n["t"] = Vec(t.Translation);
            n["r"] = Quat(t.Rotation);
            n["s"] = Vec(t.Scale);
            return n;
        }
        JointTransform ReadTransform(const YAML::Node& n)
        {
            return {ReadVec(n["t"]), ReadQuat(n["r"]), ReadVec(n["s"])};
        }
        YAML::Node Target(const IKTarget& t)
        {
            YAML::Node n;
            n["entity"]   = t.Entity;
            n["position"] = Vec(t.Position);
            n["rotation"] = Quat(t.Rotation);
            return n;
        }
        IKTarget ReadTarget(const YAML::Node& n)
        {
            return {n["entity"].as<uint64_t>(), ReadVec(n["position"]), ReadQuat(n["rotation"])};
        }
        AssetID ReadID(const YAML::Node& n)
        {
            auto text = n.as<std::string>();
            auto id   = AssetID::FromString(text);
            Require(id.ToString() == text);
            return id;
        }
        template<class T>
        YAML::Node Track(const AnimationTrack<T>& t)
        {
            YAML::Node n;
            n["interpolation"] = int(t.Interpolation);
            n["keys"]          = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& k : t.Keys)
            {
                YAML::Node a;
                a.push_back(k.Time);
                if constexpr (std::is_same_v<T, glm::quat>)
                    a.push_back(Quat(k.Value));
                else if constexpr (std::is_same_v<T, float>)
                    a.push_back(k.Value);
                else
                    a.push_back(Vec(k.Value));
                n["keys"].push_back(a);
            }
            return n;
        }
        template<class T>
        AnimationTrack<T> ReadTrack(const YAML::Node& n)
        {
            AnimationTrack<T> t;
            t.Interpolation = TrackInterpolation(n["interpolation"].as<unsigned>());
            Sequence(n["keys"]);
            for (const auto& k : n["keys"])
            {
                Sequence(k, 2);
                Require(k.size() == 2);
                if constexpr (std::is_same_v<T, glm::quat>)
                    t.Keys.push_back({k[0].as<double>(), ReadQuat(k[1])});
                else if constexpr (std::is_same_v<T, float>)
                    t.Keys.push_back({k[0].as<double>(), k[1].as<float>()});
                else
                    t.Keys.push_back({k[0].as<double>(), ReadVec(k[1])});
            }
            return t;
        }
        YAML::Node ShapeKeys(const std::vector<ShapeKey>& keys)
        {
            YAML::Node n(YAML::NodeType::Sequence);
            for (const auto& k : keys)
            {
                YAML::Node v;
                v["name"]   = k.Name;
                v["weight"] = k.DefaultWeight;
                n.push_back(v);
            }
            return n;
        }
        std::vector<ShapeKey> ReadShapeKeys(const YAML::Node& n)
        {
            std::vector<ShapeKey> keys;
            if (!n)
                return keys; // v1 products have no morph channels.
            Sequence(n, 65536);
            for (const auto& v : n)
                keys.push_back({v["name"].as<std::string>(), v["weight"].as<float>()});
            return keys;
        }
        YAML::Node ShapeKeys(const std::vector<ShapeKeyWeight>& keys)
        {
            YAML::Node n(YAML::NodeType::Sequence);
            for (const auto& k : keys)
            {
                YAML::Node v;
                v["name"]   = k.Name;
                v["weight"] = k.Weight;
                n.push_back(v);
            }
            return n;
        }
        std::vector<ShapeKeyWeight> ReadShapeKeyWeights(const YAML::Node& n)
        {
            std::vector<ShapeKeyWeight> keys;
            if (!n)
                return keys;
            Sequence(n, 65536);
            for (const auto& v : n)
                keys.push_back({v["name"].as<std::string>(), v["weight"].as<float>()});
            return keys;
        }
        YAML::Node Clip(const AnimationClipData& c)
        {
            YAML::Node n;
            n["name"]       = c.Name;
            n["source"]     = c.SourceName;
            n["duration"]   = c.Duration;
            n["frame_rate"] = c.FrameRate;
            n["tracks"]     = YAML::Node(YAML::NodeType::Sequence);
            n["events"]     = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& t : c.Tracks)
            {
                YAML::Node v;
                v["joint"]       = t.Joint;
                v["translation"] = Track(t.Translation);
                v["rotation"]    = Track(t.Rotation);
                v["scale"]       = Track(t.Scale);
                n["tracks"].push_back(v);
            }
            n["morph_tracks"] = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& t : c.MorphTracks)
            {
                YAML::Node v;
                v["shape_key"] = t.ShapeKey;
                v["weight"]    = Track(t.Weight);
                n["morph_tracks"].push_back(v);
            }
            for (const auto& e : c.Events)
            {
                YAML::Node v;
                v["time"]    = e.Time;
                v["name"]    = e.Name;
                v["payload"] = e.Payload;
                n["events"].push_back(v);
            }
            return n;
        }
        void Valid(const AnimationValidation& v)
        {
            if (auto* e = std::get_if<AssetError>(&v))
                throw std::runtime_error(e->Message);
        }
        AnimationClipData ReadClip(const YAML::Node& n)
        {
            AnimationClipData c;
            c.Name       = n["name"].as<std::string>();
            c.SourceName = n["source"].as<std::string>();
            c.Duration   = n["duration"].as<double>();
            c.FrameRate  = n["frame_rate"].as<double>();
            Sequence(n["tracks"], 65536);
            Sequence(n["events"]);
            for (const auto& v : n["tracks"])
            {
                JointAnimationTrack t;
                t.Joint       = v["joint"].as<std::string>();
                t.Translation = ReadTrack<glm::vec3>(v["translation"]);
                t.Rotation    = ReadTrack<glm::quat>(v["rotation"]);
                t.Scale       = ReadTrack<glm::vec3>(v["scale"]);
                c.Tracks.push_back(std::move(t));
            }
            for (const auto& v : n["events"])
                c.Events.push_back(
                    {v["time"].as<double>(), v["name"].as<std::string>(), v["payload"].as<std::string>()});
            if (n["morph_tracks"])
            {
                Sequence(n["morph_tracks"], 65536);
                for (const auto& v : n["morph_tracks"])
                    c.MorphTracks.push_back({v["shape_key"].as<std::string>(), ReadTrack<float>(v["weight"])});
            }
            Valid(ValidateAnimationClip(c));
            return c;
        }
        AnimationProductBytes Bytes(const char* magic, const YAML::Node& data)
        {
            YAML::Node root;
            root["magic"]   = magic;
            root["version"] = 2;
            root["data"]    = data;
            YAML::Emitter out;
            out.SetFloatPrecision(9);
            out.SetDoublePrecision(17);
            out << root;
            Require(out.good() && out.size() <= 64 * 1024 * 1024);
            std::vector<std::byte> bytes(out.size());
            std::memcpy(bytes.data(), out.c_str(), out.size());
            return bytes;
        }
        YAML::Node Root(const std::byte* data, size_t size, const char* magic)
        {
            Require(data && size && size <= 64 * 1024 * 1024);
            auto docs = YAML::LoadAll(std::string(reinterpret_cast<const char*>(data), size));
            Require(docs.size() == 1);
            auto root = docs.front();
            Require(root.IsMap() && root.size() == 3 && root["magic"].as<std::string>() == magic);
            Require(root["version"].as<unsigned>() == 1 || root["version"].as<unsigned>() == 2);
            return root["data"];
        }
        // Long baked FBX clips must not construct millions of YAML nodes. KCLP v3
        // stores explicit little-endian words; small/legacy clips retain their YAML codec.
        struct ClipWriter
        {
            std::vector<std::byte> Data;
            void                   U32(uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    Data.push_back(std::byte((v >> (i * 8)) & 255));
            }
            void F32(float v)
            {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                U32(bits);
            }
            void F64(double v)
            {
                uint64_t bits;
                std::memcpy(&bits, &v, 8);
                U32(uint32_t(bits));
                U32(uint32_t(bits >> 32));
            }
            void Text(const std::string& v)
            {
                U32(uint32_t(v.size()));
                for (auto c : v)
                    Data.push_back(std::byte(uint8_t(c)));
            }
            template<class T>
            void Track(const AnimationTrack<T>& track)
            {
                U32(uint32_t(track.Interpolation));
                U32(uint32_t(track.Keys.size()));
                for (const auto& key : track.Keys)
                {
                    F64(key.Time);
                    if constexpr (std::is_same_v<T, float>)
                        F32(key.Value);
                    else if constexpr (std::is_same_v<T, glm::quat>)
                    {
                        F32(key.Value.x);
                        F32(key.Value.y);
                        F32(key.Value.z);
                        F32(key.Value.w);
                    }
                    else
                    {
                        F32(key.Value.x);
                        F32(key.Value.y);
                        F32(key.Value.z);
                    }
                }
            }
        };
        struct ClipReader
        {
            const std::byte* Data;
            size_t           Size, At = 0;
            uint32_t         U32()
            {
                Require(At <= Size && Size - At >= 4);
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(Data[At++]) << (8 * i);
                return v;
            }
            float F32()
            {
                auto  bits = U32();
                float v;
                std::memcpy(&v, &bits, 4);
                return v;
            }
            double F64()
            {
                uint64_t bits = U32();
                bits |= uint64_t(U32()) << 32;
                double v;
                std::memcpy(&v, &bits, 8);
                return v;
            }
            uint32_t Count(uint32_t maximum, size_t minimumBytes)
            {
                auto count = U32();
                Require(count <= maximum && count <= (Size - At) / minimumBytes);
                return count;
            }
            std::string Text()
            {
                const auto  size = Count(4096, 1);
                std::string result(reinterpret_cast<const char*>(Data + At), size);
                At += size;
                return result;
            }
            template<class T>
            AnimationTrack<T> Track()
            {
                AnimationTrack<T> result;
                const auto        interpolation = U32();
                Require(interpolation <= uint32_t(TrackInterpolation::Spherical));
                result.Interpolation        = TrackInterpolation(interpolation);
                constexpr size_t components = std::is_same_v<T, float> ? 1 : std::is_same_v<T, glm::quat> ? 4 : 3;
                const auto       count      = Count(1000000, 8 + components * 4);
                result.Keys.reserve(count);
                for (uint32_t i = 0; i < count; ++i)
                {
                    AnimationKey<T> key;
                    key.Time = F64();
                    if constexpr (std::is_same_v<T, float>)
                        key.Value = F32();
                    else if constexpr (std::is_same_v<T, glm::quat>)
                    {
                        const float x = F32(), y = F32(), z = F32(), w = F32();
                        key.Value = {w, x, y, z};
                    }
                    else
                    {
                        const float x = F32(), y = F32(), z = F32();
                        key.Value = {x, y, z};
                    }
                    result.Keys.push_back(key);
                }
                return result;
            }
        };
        std::vector<std::byte> PackedClip(const AnimationClipData& c)
        {
            ClipWriter out;
            out.U32(0x504c434b);
            out.U32(3);
            out.Text(c.Name);
            out.Text(c.SourceName);
            out.F64(c.Duration);
            out.F64(c.FrameRate);
            out.U32(uint32_t(c.Tracks.size()));
            for (const auto& t : c.Tracks)
            {
                out.Text(t.Joint);
                out.Track(t.Translation);
                out.Track(t.Rotation);
                out.Track(t.Scale);
            }
            out.U32(uint32_t(c.MorphTracks.size()));
            for (const auto& t : c.MorphTracks)
            {
                out.Text(t.ShapeKey);
                out.Track(t.Weight);
            }
            out.U32(uint32_t(c.Events.size()));
            for (const auto& e : c.Events)
            {
                out.F64(e.Time);
                out.Text(e.Name);
                out.Text(e.Payload);
            }
            Require(out.Data.size() <= 512 * 1024 * 1024);
            return std::move(out.Data);
        }
        AnimationClipData ReadPackedClip(const std::byte* bytes, size_t size)
        {
            Require(size <= 512 * 1024 * 1024);
            ClipReader in {bytes, size};
            Require(in.U32() == 0x504c434b && in.U32() == 3);
            AnimationClipData c;
            c.Name       = in.Text();
            c.SourceName = in.Text();
            c.Duration   = in.F64();
            c.FrameRate  = in.F64();
            auto count   = in.Count(65536, 28);
            c.Tracks.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                JointAnimationTrack t;
                t.Joint       = in.Text();
                t.Translation = in.Track<glm::vec3>();
                t.Rotation    = in.Track<glm::quat>();
                t.Scale       = in.Track<glm::vec3>();
                c.Tracks.push_back(std::move(t));
            }
            count = in.Count(65536, 12);
            c.MorphTracks.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                MorphAnimationTrack t;
                t.ShapeKey = in.Text();
                t.Weight   = in.Track<float>();
                c.MorphTracks.push_back(std::move(t));
            }
            count = in.Count(1000000, 16);
            c.Events.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                AnimationEvent e;
                e.Time    = in.F64();
                e.Name    = in.Text();
                e.Payload = in.Text();
                c.Events.push_back(std::move(e));
            }
            Require(in.At == size);
            Valid(ValidateAnimationClip(c));
            return c;
        }
        AssetError Error(const std::exception& e) { return {AssetErrorCode::InvalidData, e.what()}; }
    } // namespace
    YAML::Node EncodeAnimationControllerNode(const AnimationControllerData& c)
    {
        Valid(ValidateAnimationController(c));
        YAML::Node n;
        n["root_motion_joint"] = c.RootMotionJoint;
        n["root_motion_planar"]              = c.RootMotionPlanar;
        n["root_motion_keep_initial_offset"] = c.RootMotionKeepInitialOffset;
        n["parameters"]        = YAML::Node(YAML::NodeType::Sequence);
        n["motions"]           = YAML::Node(YAML::NodeType::Sequence);
        n["layers"]            = YAML::Node(YAML::NodeType::Sequence);
        n["expressions"]       = YAML::Node(YAML::NodeType::Sequence);
        n["two_bone_ik"]       = YAML::Node(YAML::NodeType::Sequence);
        n["look_at_ik"]        = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& ik : c.TwoBoneConstraints)
        {
            YAML::Node v;
            v["name"]            = ik.Name;
            v["root"]            = ik.Root;
            v["middle"]          = ik.Middle;
            v["end"]             = ik.End;
            v["target"]          = Target(ik.Target);
            v["pole_entity"]     = ik.PoleEntity;
            v["pole_direction"]  = Vec(ik.PoleDirection);
            v["weight"]          = ik.Weight;
            v["rotation_weight"] = ik.RotationWeight;
            v["stretch"]         = ik.Stretch;
            n["two_bone_ik"].push_back(v);
        }
        for (const auto& ik : c.LookAtConstraints)
        {
            YAML::Node v;
            v["name"]        = ik.Name;
            v["target"]      = Target(ik.Target);
            v["world_up"]    = Vec(ik.WorldUp);
            v["weight"]      = ik.Weight;
            v["angle_limit"] = ik.AngleLimit;
            v["bones"]       = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& b : ik.Bones)
            {
                YAML::Node a;
                a["joint"]   = b.Joint;
                a["weight"]  = b.Weight;
                a["forward"] = Vec(b.Forward);
                a["up"]      = Vec(b.Up);
                v["bones"].push_back(a);
            }
            n["look_at_ik"].push_back(v);
        }
        for (const auto& p : c.Expressions)
        {
            YAML::Node v;
            v["name"]    = p.Name;
            v["weights"] = ShapeKeys(p.Weights);
            n["expressions"].push_back(v);
        }
        for (const auto& p : c.Parameters)
        {
            YAML::Node v;
            v["name"]    = p.Name;
            v["type"]    = int(p.Type);
            v["default"] = p.Default;
            n["parameters"].push_back(v);
        }
        for (const auto& m : c.Motions)
        {
            YAML::Node v;
            v["type"] = int(m.Type);
            v["clip"] = m.Clip.ToString();
            if (m.InlineClip)
                v["inline_clip"] = Clip(*m.InlineClip);
            v["strip_prefix"] = m.Mapping.StripPrefix;
            v["mapping"]      = m.Mapping.Names;
            v["x"]            = m.ParameterX;
            v["y"]            = m.ParameterY;
            v["children"]     = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& child : m.Children)
            {
                YAML::Node a;
                a["motion"]    = child.Motion;
                a["threshold"] = child.Threshold;
                a["x"]         = child.Position.x;
                a["y"]         = child.Position.y;
                v["children"].push_back(a);
            }
            n["motions"].push_back(v);
        }
        for (const auto& l : c.Layers)
        {
            YAML::Node v;
            v["name"]        = l.Name;
            v["weight"]      = l.Weight;
            v["mode"]        = int(l.Mode);
            v["entry"]       = l.EntryState;
            v["mask"]        = YAML::Node(YAML::NodeType::Sequence);
            v["states"]      = YAML::Node(YAML::NodeType::Sequence);
            v["transitions"] = YAML::Node(YAML::NodeType::Sequence);
            v["shape_key_mask"] = ShapeKeys(l.ShapeKeyMask);
            for (const auto& m : l.Mask)
            {
                YAML::Node a;
                a["joint"]       = m.Joint;
                a["weight"]      = m.Weight;
                a["descendants"] = m.IncludeChildren;
                v["mask"].push_back(a);
            }
            for (const auto& s : l.States)
            {
                YAML::Node a;
                a["name"]   = s.Name;
                a["motion"] = s.Motion;
                a["speed"]  = s.Speed;
                a["loop"]   = s.Loop;
                if (s.EditorPosition)
                {
                    a["editor_x"] = s.EditorPosition->x;
                    a["editor_y"] = s.EditorPosition->y;
                }
                v["states"].push_back(a);
            }
            for (const auto& t : l.Transitions)
            {
                YAML::Node a;
                a["source"]       = t.Source;
                a["target"]       = t.Target;
                a["duration"]     = t.Duration;
                a["interruption"] = int(t.Interruption);
                a["self"]         = t.CanTransitionToSelf;
                if (t.ExitTime)
                    a["exit"] = *t.ExitTime;
                a["conditions"] = YAML::Node(YAML::NodeType::Sequence);
                for (const auto& cond : t.Conditions)
                {
                    YAML::Node b;
                    b["parameter"] = cond.Parameter;
                    b["op"]        = int(cond.Op);
                    b["value"]     = cond.Value;
                    a["conditions"].push_back(b);
                }
                v["transitions"].push_back(a);
            }
            n["layers"].push_back(v);
        }
        return n;
    }
    AnimationControllerData DecodeAnimationControllerNode(const YAML::Node& n)
    {
        AnimationControllerData c;
        c.RootMotionJoint = n["root_motion_joint"].as<std::string>();
        c.RootMotionPlanar = n["root_motion_planar"] ? n["root_motion_planar"].as<bool>() : false;
        c.RootMotionKeepInitialOffset =
            n["root_motion_keep_initial_offset"] ? n["root_motion_keep_initial_offset"].as<bool>() : false;
        Sequence(n["parameters"], 4096);
        Sequence(n["motions"], 4096);
        Sequence(n["layers"], 64);
        if (n["expressions"])
        {
            Sequence(n["expressions"], 4096);
            for (const auto& p : n["expressions"])
                c.Expressions.push_back({p["name"].as<std::string>(), ReadShapeKeyWeights(p["weights"])});
        }
        if (n["two_bone_ik"])
        {
            Sequence(n["two_bone_ik"], 256);
            for (const auto& v : n["two_bone_ik"])
            {
                TwoBoneIK ik;
                ik.Name           = v["name"].as<std::string>();
                ik.Root           = v["root"].as<std::string>();
                ik.Middle         = v["middle"].as<std::string>();
                ik.End            = v["end"].as<std::string>();
                ik.Target         = ReadTarget(v["target"]);
                ik.PoleEntity     = v["pole_entity"].as<uint64_t>();
                ik.PoleDirection  = ReadVec(v["pole_direction"]);
                ik.Weight         = v["weight"].as<float>();
                ik.RotationWeight = v["rotation_weight"].as<float>();
                ik.Stretch        = v["stretch"].as<bool>();
                c.TwoBoneConstraints.push_back(std::move(ik));
            }
        }
        if (n["look_at_ik"])
        {
            Sequence(n["look_at_ik"], 256);
            for (const auto& v : n["look_at_ik"])
            {
                LookAtIK ik;
                ik.Name       = v["name"].as<std::string>();
                ik.Target     = ReadTarget(v["target"]);
                ik.WorldUp    = ReadVec(v["world_up"]);
                ik.Weight     = v["weight"].as<float>();
                ik.AngleLimit = v["angle_limit"].as<float>();
                Sequence(v["bones"], 64);
                for (const auto& a : v["bones"])
                    ik.Bones.push_back({a["joint"].as<std::string>(),
                                        a["weight"].as<float>(),
                                        ReadVec(a["forward"]),
                                        ReadVec(a["up"])});
                c.LookAtConstraints.push_back(std::move(ik));
            }
        }
        for (const auto& v : n["parameters"])
            c.Parameters.push_back({v["name"].as<std::string>(),
                                    AnimationParameterType(v["type"].as<unsigned>()),
                                    v["default"].as<double>()});
        for (const auto& v : n["motions"])
        {
            AnimationMotion m;
            m.Type = AnimationMotionType(v["type"].as<unsigned>());
            m.Clip = ReadID(v["clip"]);
            if (v["inline_clip"])
                m.InlineClip = ReadClip(v["inline_clip"]);
            m.Mapping.StripPrefix = v["strip_prefix"].as<std::string>();
            m.Mapping.Names       = v["mapping"].as<std::map<std::string, std::string>>();
            m.ParameterX          = v["x"].as<std::string>();
            m.ParameterY          = v["y"].as<std::string>();
            Sequence(v["children"], 256);
            for (const auto& a : v["children"])
                m.Children.push_back(
                    {a["motion"].as<uint32_t>(), a["threshold"].as<float>(), {a["x"].as<float>(), a["y"].as<float>()}});
            c.Motions.push_back(std::move(m));
        }
        for (const auto& v : n["layers"])
        {
            AnimationLayer l;
            l.ShapeKeyMask = ReadShapeKeyWeights(v["shape_key_mask"]);
            l.Name       = v["name"].as<std::string>();
            l.Weight     = v["weight"].as<float>();
            l.Mode       = AnimationLayerMode(v["mode"].as<unsigned>());
            l.EntryState = v["entry"].as<uint32_t>();
            Sequence(v["mask"], 65536);
            Sequence(v["states"], 4096);
            Sequence(v["transitions"], 16384);
            for (const auto& a : v["mask"])
                l.Mask.push_back({a["joint"].as<std::string>(), a["weight"].as<float>(), a["descendants"].as<bool>()});
            for (const auto& a : v["states"])
            {
                l.States.push_back({a["name"].as<std::string>(),
                                    a["motion"].as<uint32_t>(),
                                    a["speed"].as<double>(),
                                    a["loop"].as<bool>()});
                if (a["editor_x"] && a["editor_y"])
                    l.States.back().EditorPosition = glm::vec2(a["editor_x"].as<float>(), a["editor_y"].as<float>());
            }
            for (const auto& a : v["transitions"])
            {
                AnimationTransition t;
                t.Source              = a["source"].as<int32_t>();
                t.Target              = a["target"].as<uint32_t>();
                t.Duration            = a["duration"].as<double>();
                t.Interruption        = AnimationInterruption(a["interruption"].as<unsigned>());
                t.CanTransitionToSelf = a["self"].as<bool>();
                if (a["exit"])
                    t.ExitTime = a["exit"].as<double>();
                Sequence(a["conditions"], 4096);
                for (const auto& b : a["conditions"])
                    t.Conditions.push_back({b["parameter"].as<std::string>(),
                                            AnimationConditionOp(b["op"].as<unsigned>()),
                                            b["value"].as<double>()});
                l.Transitions.push_back(std::move(t));
            }
            c.Layers.push_back(std::move(l));
        }
        Valid(ValidateAnimationController(c));
        return c;
    }
    AnimationProductBytes EncodeSkeletonProduct(const SkeletonData& s)
    {
        try
        {
            Valid(ValidateSkeleton(s));
            YAML::Node n(YAML::NodeType::Sequence);
            for (const auto& j : s.Joints)
            {
                YAML::Node v;
                v["name"]   = j.Name;
                v["path"]   = j.Path;
                v["node"]   = j.NodeKey;
                v["parent"] = j.Parent;
                v["rest"]   = Transform(j.RestLocal);
                v["bind"]   = Transform(j.BindLocal);
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        v["inverse_bind"].push_back(j.InverseBind[c][r]);
                n.push_back(v);
            }
            YAML::Node data;
            data["joints"]     = n;
            data["shape_keys"] = ShapeKeys(s.ShapeKeys);
            return Bytes("KSKL", data);
        }
        catch (const std::exception& e)
        {
            return Error(e);
        }
    }
    AnimationProductBytes EncodeAnimationClipProduct(const AnimationClipData& c)
    {
        try
        {
            Valid(ValidateAnimationClip(c));
            size_t keys = 0;
            for (const auto& t : c.Tracks)
                keys += t.Translation.Keys.size() + t.Rotation.Keys.size() + t.Scale.Keys.size();
            for (const auto& t : c.MorphTracks)
                keys += t.Weight.Keys.size();
            if (keys > 100000)
                return PackedClip(c);
            return Bytes("KCLP", Clip(c));
        }
        catch (const std::exception& e)
        {
            return Error(e);
        }
    }
    AnimationProductBytes EncodeAnimationControllerProduct(const AnimationControllerData& c)
    {
        try
        {
            return Bytes("KACT", EncodeAnimationControllerNode(c));
        }
        catch (const std::exception& e)
        {
            return Error(e);
        }
    }
    std::variant<SkeletonData, AssetError> DecodeSkeletonProduct(const std::byte* bytes, size_t size)
    {
        try
        {
            auto n = Root(bytes, size, "KSKL");
            SkeletonData s;
            if (n.IsMap())
            {
                s.ShapeKeys = ReadShapeKeys(n["shape_keys"]);
                n           = n["joints"];
            }
            Sequence(n, 65536);
            for (const auto& v : n)
            {
                SkeletonJoint j;
                j.Name      = v["name"].as<std::string>();
                j.Path      = v["path"].as<std::string>();
                j.NodeKey   = v["node"].as<std::string>();
                j.Parent    = v["parent"].as<int32_t>();
                j.RestLocal = ReadTransform(v["rest"]);
                j.BindLocal = ReadTransform(v["bind"]);
                Sequence(v["inverse_bind"], 16);
                Require(v["inverse_bind"].size() == 16);
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        j.InverseBind[c][r] = v["inverse_bind"][c * 4 + r].as<float>();
                s.Joints.push_back(std::move(j));
            }
            Valid(ValidateSkeleton(s));
            return s;
        }
        catch (const std::exception& e)
        {
            return Error(e);
        }
    }
    std::variant<AnimationClipData, AssetError> DecodeAnimationClipProduct(const std::byte* bytes, size_t size)
    {
        try
        {
            if (bytes && size >= 4 && std::memcmp(bytes, "KCLP", 4) == 0)
                return ReadPackedClip(bytes, size);
            return ReadClip(Root(bytes, size, "KCLP"));
        }
        catch (const std::exception& e)
        {
            return Error(e);
        }
    }
    std::variant<AnimationControllerData, AssetError> DecodeAnimationControllerProduct(const std::byte* bytes,
                                                                                       size_t           size)
    {
        try
        {
            return DecodeAnimationControllerNode(Root(bytes, size, "KACT"));
        }
        catch (const std::exception& e)
        {
            return Error(e);
        }
    }
} // namespace Kans
