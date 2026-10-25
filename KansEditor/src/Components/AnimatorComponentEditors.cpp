#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include "Kans3D/Animation/AnimatorComponent.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Scene/Scene.h"
#include "ComponentEditors.h"
#include "UI/EditorIcons.h"
#include "UI/PropertyGrid.h"

namespace Kans::ComponentEditors
{
    namespace
    {
        // Editor-only preview of an animator: which state is shown and where in it.
        struct Preview
        {
            int    Layer = 0, State = 0;
            double Time  = 0;
            bool   Playing = false;
        };
        std::map<UUID, Preview>& Previews()
        {
            static std::map<UUID, Preview> previews;
            return previews;
        }

        bool InputName(const char* id, std::string& value)
        {
            char buffer[128] = {};
            value.copy(buffer, sizeof(buffer) - 1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (!ImGui::InputText(id, buffer, sizeof(buffer)))
                return false;
            value = buffer;
            return true;
        }

        // A joint chosen from the skeleton (empty = none).
        bool JointCombo(const char* label, std::string& joint, const SkeletonData* skeleton)
        {
            if (!PropertyGrid::Row(label))
                return false;
            ImGui::PushID(label);
            bool changed = false;
            if (ImGui::BeginCombo("##joint", joint.empty() ? "(none)" : joint.c_str(), ImGuiComboFlags_HeightLarge))
            {
                if (ImGui::Selectable("(none)", joint.empty()))
                    joint.clear(), changed = true;
                if (skeleton)
                    for (const auto& j : skeleton->Joints)
                        if (ImGui::Selectable(j.Name.c_str(), j.Name == joint))
                            joint = j.Name, changed = true;
                ImGui::EndCombo();
            }
            ImGui::PopID();
            return changed;
        }

        template<typename T>
        bool RemoveButton(std::vector<T>& items, size_t& i)
        {
            ImGui::SameLine();
            if (!ImGui::SmallButton(ICON_FA_TRASH))
                return false;
            items.erase(items.begin() + long(i));
            --i;
            return true;
        }

        double MotionDuration(const AnimationControllerData& c, uint32_t motion, ComponentUI& ui)
        {
            if (motion >= c.Motions.size() || !ui.GetAssets())
                return 0;
            const auto& m = c.Motions[motion];
            if (m.InlineClip)
                return m.InlineClip->Duration;
            if (m.Type == AnimationMotionType::Clip)
            {
                auto status = ui.GetAssets()->Request<AnimationClipAsset>(m.Clip);
                return status.IsReady() ? status.Asset->GetData().Duration : 0;
            }
            double longest = 0;
            for (const auto& child : m.Children)
                longest = std::max(longest, MotionDuration(c, child.Motion, ui));
            return longest;
        }

        // Sampling rate of a motion, for frame-accurate preview; blend trees use their fastest clip.
        double MotionFrameRate(const AnimationControllerData& c, uint32_t motion, ComponentUI& ui)
        {
            if (motion >= c.Motions.size() || !ui.GetAssets())
                return 30;
            const auto& m = c.Motions[motion];
            if (m.InlineClip)
                return m.InlineClip->FrameRate;
            if (m.Type == AnimationMotionType::Clip)
            {
                auto status = ui.GetAssets()->Request<AnimationClipAsset>(m.Clip);
                return status.IsReady() ? status.Asset->GetData().FrameRate : 30;
            }
            double fastest = 0;
            for (const auto& child : m.Children)
                fastest = std::max(fastest, MotionFrameRate(c, child.Motion, ui));
            return fastest > 0 ? fastest : 30;
        }

        bool DrawParameters(AnimationControllerData& c)
        {
            bool changed = false;
            if (!ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
                return false;
            const char* types[] = {"Float", "Int", "Bool", "Trigger"};
            for (size_t i = 0; i < c.Parameters.size(); ++i)
            {
                auto& p = c.Parameters[i];
                ImGui::PushID(int(i));
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
                char name[64] = {};
                p.Name.copy(name, sizeof(name) - 1);
                if (ImGui::InputText("##name", name, sizeof(name)))
                    p.Name = name, changed = true;
                ImGui::SameLine();
                int type = int(p.Type);
                ImGui::SetNextItemWidth(80);
                if (ImGui::Combo("##type", &type, types, 4))
                    p.Type = AnimationParameterType(type), changed = true;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                float value = float(p.Default);
                if (ImGui::DragFloat("##default", &value, 0.01f))
                    p.Default = value, changed = true;
                changed |= RemoveButton(c.Parameters, i);
                ImGui::PopID();
            }
            if (ImGui::SmallButton(ICON_FA_PLUS " Parameter"))
                c.Parameters.push_back({"Param" + std::to_string(c.Parameters.size()), AnimationParameterType::Float, 0}),
                    changed = true;
            ImGui::TreePop();
            return changed;
        }

        bool DrawLayer(AnimationControllerData& c, AnimationLayer& layer, const SkeletonData* skeleton, ComponentUI& ui)
        {
            bool changed = false;
            if (PropertyGrid::Begin("##Layer"))
            {
                if (PropertyGrid::Row("Name"))
                    changed |= InputName("##layername", layer.Name);
                changed |= PropertyGrid::Float("Weight", layer.Weight, 0.01f, 0.0f, 1.0f);
                const char* modes[] = {"Override", "Additive"};
                int         mode    = int(layer.Mode);
                if (PropertyGrid::Combo("Mode", modes, 2, mode))
                    layer.Mode = AnimationLayerMode(mode), changed = true;
                PropertyGrid::End();
            }
            // States: each owns its own motion (a clip); blend trees are edited in the graph.
            if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (size_t i = 0; i < layer.States.size(); ++i)
                {
                    auto& state = layer.States[i];
                    ImGui::PushID(int(i));
                    const bool entry = layer.EntryState == i;
                    if (ImGui::RadioButton("##entry", entry))
                        layer.EntryState = uint32_t(i), changed = true;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Entry state");
                    ImGui::SameLine();
                    const bool open = ImGui::TreeNode("##state", "%s", state.Name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_TRASH))
                    {
                        // Drop the state and its transitions; later states shift down by one.
                        const auto removed = int32_t(i);
                        layer.States.erase(layer.States.begin() + long(i));
                        for (auto it = layer.Transitions.begin(); it != layer.Transitions.end();)
                            it = (it->Source == removed || int32_t(it->Target) == removed) ? layer.Transitions.erase(it) :
                                                                                            std::next(it);
                        for (auto& t : layer.Transitions)
                        {
                            if (t.Source > removed)
                                --t.Source;
                            if (int32_t(t.Target) > removed)
                                --t.Target;
                        }
                        if (layer.EntryState >= layer.States.size())
                            layer.EntryState = layer.States.empty() ? 0 : uint32_t(layer.States.size() - 1);
                        changed = true;
                        if (open)
                            ImGui::TreePop();
                        ImGui::PopID();
                        --i;
                        continue;
                    }
                    if (open)
                    {
                        if (PropertyGrid::Begin("##StateGrid"))
                        {
                            if (PropertyGrid::Row("Name"))
                                changed |= InputName("##statename", state.Name);
                            if (state.Motion < c.Motions.size() && c.Motions[state.Motion].Type == AnimationMotionType::Clip)
                            {
                                AssetID clip = c.Motions[state.Motion].Clip;
                                if (ui.AssetPicker("Clip", AssetType::AnimationClip, clip))
                                    c.Motions[state.Motion].Clip = clip, changed = true;
                            }
                            else
                                PropertyGrid::Text("Motion", "Blend tree", true);
                            float speed = float(state.Speed);
                            if (PropertyGrid::Float("Speed", speed, 0.01f, -10.0f, 10.0f))
                                state.Speed = speed, changed = true;
                            changed |= PropertyGrid::Checkbox("Loop", state.Loop);
                            PropertyGrid::End();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton(ICON_FA_PLUS " State"))
                {
                    c.Motions.push_back({});
                    layer.States.push_back({"State" + std::to_string(layer.States.size()), uint32_t(c.Motions.size() - 1)});
                    changed = true;
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Transitions"))
            {
                const auto stateCombo = [&](const char* id, int& index, bool any) {
                    bool        picked = false;
                    const char* shown  = index < 0                         ? "Any state" :
                                         index < int(layer.States.size()) ? layer.States[index].Name.c_str() :
                                                                            "?";
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::BeginCombo(id, shown))
                    {
                        if (any && ImGui::Selectable("Any state", index < 0))
                            index = -1, picked = true;
                        for (int k = 0; k < int(layer.States.size()); ++k)
                            if (ImGui::Selectable(layer.States[k].Name.c_str(), k == index))
                                index = k, picked = true;
                        ImGui::EndCombo();
                    }
                    return picked;
                };
                const char* ops[] = {">", "<", "==", "!=", "If", "If not"};
                for (size_t i = 0; i < layer.Transitions.size(); ++i)
                {
                    auto& t = layer.Transitions[i];
                    ImGui::PushID(int(i));
                    int source = t.Source, target = int(t.Target);
                    if (stateCombo("##from", source, true))
                        t.Source = source, changed = true;
                    ImGui::SameLine();
                    ImGui::TextUnformatted(ICON_FA_ARROW_RIGHT);
                    ImGui::SameLine();
                    if (stateCombo("##to", target, false))
                        t.Target = uint32_t(target), changed = true;
                    const bool removed = RemoveButton(layer.Transitions, i);
                    changed |= removed;
                    if (!removed)
                    {
                        ImGui::Indent();
                        float duration = float(t.Duration);
                        ImGui::SetNextItemWidth(80);
                        if (ImGui::DragFloat("Fade (s)", &duration, 0.01f, 0.0f, 10.0f))
                            t.Duration = duration, changed = true;
                        ImGui::SameLine();
                        bool exit = t.ExitTime.has_value();
                        if (ImGui::Checkbox("Exit time", &exit))
                            t.ExitTime = exit ? std::optional<double>(1.0) : std::nullopt, changed = true;
                        if (t.ExitTime)
                        {
                            ImGui::SameLine();
                            float at = float(*t.ExitTime);
                            ImGui::SetNextItemWidth(60);
                            if (ImGui::DragFloat("##exit", &at, 0.01f, 0.0f, 10.0f))
                                t.ExitTime = at, changed = true;
                        }
                        for (size_t k = 0; k < t.Conditions.size(); ++k)
                        {
                            auto& condition = t.Conditions[k];
                            ImGui::PushID(int(k));
                            ImGui::SetNextItemWidth(100);
                            if (ImGui::BeginCombo("##param", condition.Parameter.c_str()))
                            {
                                for (const auto& p : c.Parameters)
                                    if (ImGui::Selectable(p.Name.c_str(), p.Name == condition.Parameter))
                                        condition.Parameter = p.Name, changed = true;
                                ImGui::EndCombo();
                            }
                            ImGui::SameLine();
                            int op = int(condition.Op);
                            ImGui::SetNextItemWidth(60);
                            if (ImGui::Combo("##op", &op, ops, 6))
                                condition.Op = AnimationConditionOp(op), changed = true;
                            if (condition.Op != AnimationConditionOp::If && condition.Op != AnimationConditionOp::IfNot)
                            {
                                ImGui::SameLine();
                                float value = float(condition.Value);
                                ImGui::SetNextItemWidth(60);
                                if (ImGui::DragFloat("##value", &value, 0.01f))
                                    condition.Value = value, changed = true;
                            }
                            changed |= RemoveButton(t.Conditions, k);
                            ImGui::PopID();
                        }
                        if (ImGui::SmallButton(ICON_FA_PLUS " Condition"))
                            t.Conditions.push_back({c.Parameters.empty() ? "" : c.Parameters.front().Name}), changed = true;
                        ImGui::Unindent();
                    }
                    ImGui::PopID();
                }
                if (!layer.States.empty() && ImGui::SmallButton(ICON_FA_PLUS " Transition"))
                    layer.Transitions.push_back({0, 0}), changed = true;
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Mask"))
            {
                ImGui::TextDisabled("Empty: the layer affects every joint and shape key.");
                for (size_t i = 0; i < layer.Mask.size(); ++i)
                {
                    auto& m = layer.Mask[i];
                    ImGui::PushID(int(i));
                    if (PropertyGrid::Begin("##MaskGrid"))
                    {
                        changed |= JointCombo("Joint", m.Joint, skeleton);
                        changed |= PropertyGrid::Float("Weight", m.Weight, 0.01f, 0.0f, 1.0f);
                        changed |= PropertyGrid::Checkbox("With children", m.IncludeChildren);
                        PropertyGrid::End();
                    }
                    if (ImGui::SmallButton(ICON_FA_TRASH " Remove joint"))
                        layer.Mask.erase(layer.Mask.begin() + long(i)), --i, changed = true;
                    ImGui::PopID();
                }
                if (ImGui::SmallButton(ICON_FA_PLUS " Joint"))
                    layer.Mask.push_back({}), changed = true;
                ImGui::TreePop();
            }
            return changed;
        }

        bool DrawIK(AnimationControllerData& c, const SkeletonData* skeleton, Scene* scene)
        {
            bool changed = false;
            if (!ImGui::TreeNode("IK"))
                return false;
            for (size_t i = 0; i < c.TwoBoneConstraints.size(); ++i)
            {
                auto& ik = c.TwoBoneConstraints[i];
                ImGui::PushID(int(i));
                ImGui::TextDisabled("Two-bone: %s", ik.Name.c_str());
                changed |= RemoveButton(c.TwoBoneConstraints, i);
                if (i < c.TwoBoneConstraints.size() && PropertyGrid::Begin("##TwoBone"))
                {
                    if (PropertyGrid::Row("Name"))
                        changed |= InputName("##ikname", ik.Name);
                    changed |= JointCombo("Root", ik.Root, skeleton);
                    changed |= JointCombo("Middle", ik.Middle, skeleton);
                    changed |= JointCombo("End", ik.End, skeleton);
                    changed |= EntityField("Target entity", ik.Target.Entity, scene);
                    if (!ik.Target.Entity)
                        changed |= PropertyGrid::Vec3("Target", ik.Target.Position, 0.0f, 0.01f);
                    changed |= EntityField("Pole entity", ik.PoleEntity, scene);
                    if (!ik.PoleEntity)
                        changed |= PropertyGrid::Vec3("Pole", ik.PoleDirection, 0.0f, 0.01f);
                    changed |= PropertyGrid::Float("Weight", ik.Weight, 0.01f, 0.0f, 1.0f);
                    changed |= PropertyGrid::Float("Rotation weight", ik.RotationWeight, 0.01f, 0.0f, 1.0f);
                    changed |= PropertyGrid::Checkbox("Stretch", ik.Stretch);
                    PropertyGrid::End();
                }
                ImGui::PopID();
            }
            for (size_t i = 0; i < c.LookAtConstraints.size(); ++i)
            {
                auto& ik = c.LookAtConstraints[i];
                ImGui::PushID(int(1000 + i));
                ImGui::TextDisabled("Look-at: %s", ik.Name.c_str());
                changed |= RemoveButton(c.LookAtConstraints, i);
                if (i < c.LookAtConstraints.size() && PropertyGrid::Begin("##LookAt"))
                {
                    if (PropertyGrid::Row("Name"))
                        changed |= InputName("##lookname", ik.Name);
                    for (size_t b = 0; b < ik.Bones.size(); ++b)
                    {
                        ImGui::PushID(int(b));
                        changed |= JointCombo("Bone", ik.Bones[b].Joint, skeleton);
                        changed |= PropertyGrid::Float("Bone weight", ik.Bones[b].Weight, 0.01f, 0.0f, 1.0f);
                        changed |= PropertyGrid::Vec3("Forward", ik.Bones[b].Forward, 0.0f, 0.01f);
                        ImGui::PopID();
                    }
                    changed |= EntityField("Target entity", ik.Target.Entity, scene);
                    if (!ik.Target.Entity)
                        changed |= PropertyGrid::Vec3("Target", ik.Target.Position, 0.0f, 0.01f);
                    changed |= PropertyGrid::Float("Weight", ik.Weight, 0.01f, 0.0f, 1.0f);
                    float degrees = glm::degrees(ik.AngleLimit);
                    if (PropertyGrid::Float("Angle limit", degrees, 0.5f, 0.0f, 180.0f, "%.0f deg"))
                        ik.AngleLimit = glm::radians(degrees), changed = true;
                    PropertyGrid::End();
                }
                if (i < c.LookAtConstraints.size() && ImGui::SmallButton(ICON_FA_PLUS " Bone"))
                    ik.Bones.push_back({}), changed = true;
                ImGui::PopID();
            }
            if (ImGui::SmallButton(ICON_FA_PLUS " Two-bone IK"))
                c.TwoBoneConstraints.push_back({"Limb" + std::to_string(c.TwoBoneConstraints.size())}), changed = true;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_PLUS " Look-at"))
                c.LookAtConstraints.push_back({"LookAt" + std::to_string(c.LookAtConstraints.size()), {{}}}), changed = true;
            ImGui::TreePop();
            return changed;
        }
    } // namespace

    void DrawAnimator(Entity entity, ComponentUI& ui)
    {
        Scene* scene = ui.GetScene();
        auto&  a     = entity.GetComponent<AnimatorComponent>();
        const SkeletonData* skeleton = a.SkeletonSource ? &a.SkeletonSource->GetData() : nullptr;
        if (scene && !skeleton)
            scene->PrepareAnimator(entity); // Loads the skeleton for the joint lists below.

        bool changed = false;
        if (PropertyGrid::Begin("##Animator"))
        {
            PropertyGrid::Text("Skeleton", skeleton ? std::to_string(skeleton->Joints.size()) + " joints" :
                                           a.Skeleton.IsValid()          ? std::string("Loading...") :
                                                                           std::string("(none)"),
                               !skeleton);
            changed |= PropertyGrid::Checkbox("Enabled", a.Enabled);
            float speed = float(a.Speed);
            if (PropertyGrid::Float("Speed", speed, 0.01f, 0.0f, 10.0f))
                a.Speed = speed, changed = true;
            changed |= PropertyGrid::Checkbox("Root motion", a.ApplyRootMotion);
            if (a.InlineController)
            {
                auto& c = *a.InlineController;
                changed |= JointCombo("Root joint", c.RootMotionJoint, skeleton);
                // Hips-rooted rigs (Mixamo): only ground travel and turning move the entity.
                changed |= PropertyGrid::Checkbox("Planar (XZ + yaw)", c.RootMotionPlanar);
                changed |= PropertyGrid::Checkbox("Keep initial offset", c.RootMotionKeepInitialOffset);
            }
            PropertyGrid::End();
        }
        if (a.LastError)
            ImGui::TextColored(ErrorColor, "%s", a.LastError->Message.c_str());

        if (!a.InlineController)
        {
            ImGui::TextDisabled("No controller: the model stays in its rest pose.");
            if (ImGui::Button(ICON_FA_PLUS "  Create controller", {-FLT_MIN, 0}))
            {
                AnimationControllerData controller;
                controller.Layers.push_back({"Base"});
                a.InlineController = std::move(controller);
                changed            = true;
            }
        }
        else
        {
            auto& c = *a.InlineController;
            changed |= DrawParameters(c);
            for (size_t i = 0; i < c.Layers.size(); ++i)
            {
                ImGui::PushID(int(i));
                const bool open = ImGui::TreeNodeEx("##layer", ImGuiTreeNodeFlags_DefaultOpen, "Layer: %s", c.Layers[i].Name.c_str());
                if (i > 0 && RemoveButton(c.Layers, i))
                    changed = true;
                else if (open)
                    changed |= DrawLayer(c, c.Layers[i], skeleton, ui);
                if (open)
                    ImGui::TreePop();
                ImGui::PopID();
            }
            if (ImGui::SmallButton(ICON_FA_PLUS " Layer"))
                c.Layers.push_back({"Layer" + std::to_string(c.Layers.size())}), changed = true;
            changed |= DrawIK(c, skeleton, scene);
        }
        // Edits change the controller the animator was prepared with.
        if (changed && scene)
            scene->ResetAnimator(entity);

        // Shape keys of this instance: live weights, with a preview override.
        if (a.Player && !a.Player->GetShapeKeys().empty() && ImGui::TreeNode("Shape keys"))
        {
            const auto& keys    = a.Player->GetShapeKeys();
            const auto& weights = a.Player->GetPose().MorphWeights;
            for (size_t k = 0; k < keys.size(); ++k)
            {
                ImGui::PushID(int(k));
                float weight = k < weights.size() ? weights[k] : keys[k].DefaultWeight;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
                if (ImGui::SliderFloat(keys[k].Name.c_str(), &weight, 0.0f, 1.0f))
                {
                    a.Player->SetShapeKeyWeight(keys[k].Name, weight);
                    a.Player->RebuildPose();
                    if (scene)
                        scene->ApplyAnimatorPose(entity, a.Player->GetPose());
                }
                ImGui::PopID();
            }
            ImGui::TextDisabled("Sliders preview this instance only and are not saved.");
            ImGui::TreePop();
        }

        // Preview: shows a state at a time without entering play mode.
        if (a.InlineController && scene && ImGui::TreeNodeEx("Preview", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& p = Previews()[entity.GetUUID()];
            auto& c = *a.InlineController;
            p.Layer = std::clamp(p.Layer, 0, int(c.Layers.size()) - 1);
            const auto& layer = c.Layers[p.Layer];
            if (layer.States.empty())
                ImGui::TextDisabled("Add a state with a clip to preview it.");
            else
            {
                p.State = std::clamp(p.State, 0, int(layer.States.size()) - 1);
                ImGui::SetNextItemWidth(120);
                if (ImGui::BeginCombo("##previewstate", layer.States[p.State].Name.c_str()))
                {
                    for (int s = 0; s < int(layer.States.size()); ++s)
                        if (ImGui::Selectable(layer.States[s].Name.c_str(), s == p.State))
                            p.State = s, p.Time = 0;
                    ImGui::EndCombo();
                }
                const uint32_t motion   = layer.States[p.State].Motion;
                const double   duration = std::max(MotionDuration(c, motion, ui), 1e-3);
                const double   fps      = std::max(MotionFrameRate(c, motion, ui), 1.0);
                // Frames are counted from 0; the last frame sits exactly at the clip's end.
                const int  lastFrame = std::max(int(std::lround(duration * fps)), 1);
                int        frame     = std::clamp(int(std::floor(p.Time * fps + 1e-6)), 0, lastFrame);
                const auto seek      = [&](int to) {
                    frame     = std::clamp(to, 0, lastFrame);
                    p.Time    = frame / fps;
                    p.Playing = false;
                };
                const bool shift = ImGui::GetIO().KeyShift; // Shift steps ten frames.
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_BACKWARD_FAST))
                    seek(0);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("First frame");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_BACKWARD_STEP))
                    seek(frame - (shift ? 10 : 1));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Previous frame (Shift: 10 frames)");
                ImGui::SameLine();
                if (ImGui::Button(p.Playing ? ICON_FA_PAUSE : ICON_FA_PLAY))
                    p.Playing = !p.Playing;
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FORWARD_STEP))
                    seek(frame + (shift ? 10 : 1));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Next frame (Shift: 10 frames)");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FORWARD_FAST))
                    seek(lastFrame);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Last frame");
                ImGui::SameLine();
                int typed = frame;
                ImGui::SetNextItemWidth(70);
                if (ImGui::InputInt("##frame", &typed, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                    seek(typed);
                ImGui::SameLine();
                ImGui::TextDisabled("/ %d  (%.0f fps)", lastFrame, fps);

                if (p.Playing)
                    p.Time = std::fmod(p.Time + ImGui::GetIO().DeltaTime * layer.States[p.State].Speed * a.Speed, duration);
                // Scrubbing snaps to whole frames; the label also shows seconds for long clips.
                int  scrub = std::clamp(int(std::floor(p.Time * fps + 1e-6)), 0, lastFrame);
                char label[48];
                std::snprintf(label, sizeof(label), "%d  (%.2f s)", scrub, p.Time);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderInt("##time", &scrub, 0, lastFrame, label))
                    seek(scrub);
                scene->PreviewAnimation(entity, size_t(p.Layer), uint32_t(p.State), p.Time);
            }
            ImGui::TreePop();
        }
    }
} // namespace Kans::ComponentEditors
