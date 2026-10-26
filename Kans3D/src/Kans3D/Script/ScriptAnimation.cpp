#include "kspch.h"
#include "ScriptAnimation.h"
#include <cstring>
#include "Kans3D/Scene/Entity.h"
#include "ScriptEngine.h"
namespace Kans::ScriptAnimation
{
    namespace
    {
        struct Event
        {
            uint64_t                 Entity;
            AnimationEventOccurrence Value;
        };
        std::vector<Event>       Pending, Frame;
        int32_t KANS_SCRIPT_CALL Command(uint64_t    entity,
                                         int32_t     op,
                                         const char* asset,
                                         const char* slot,
                                         const char* from,
                                         const char* to,
                                         float       in,
                                         float       out,
                                         float       speed) noexcept
        {
            try
            {
                auto scene = ScriptEngine::GetSceneContext();
                auto e     = scene ? scene->GetEntityByUUID(UUID(entity)) : Entity {};
                if (!e || !e.HasComponent<AnimatorComponent>() || !scene->PrepareAnimator(e))
                    return 0;
                auto              player   = e.GetComponent<AnimatorComponent>().Player;
                const std::string slotName = slot ? slot : "";
                const auto        id       = AssetID::FromString(asset ? asset : "");
                switch (op)
                {
                    case 0:
                        return player->PlayAction(
                            id, slot ? std::optional<std::string>(slotName) : std::nullopt, speed);
                    case 1:
                        return player->PlayClip(slotName, id, in, out, speed);
                    case 2:
                        return player->StopAction(slotName, out);
                    case 3:
                        return player->JumpToSection(slotName, from ? from : "");
                    case 4:
                        return player->IsActionPlaying(slotName);
                    case 5:
                        return player->SetNextSection(slotName, from ? from : "", to ? to : "");
                    default:
                        return -1;
                }
            }
            catch (...)
            {
                return -1;
            }
        }
        int32_t KANS_SCRIPT_CALL
        Read(int32_t index, Scripting::AnimationEventData* data, int32_t field, char* buffer, int32_t capacity) noexcept
        {
            try
            {
                if (index < 0 || size_t(index) >= Frame.size())
                    return 0;
                const auto& e = Frame[index];
                const auto& v = e.Value;
                if (!data || field < 0 || field > 5)
                    return -1;
                *data           = {e.Entity, v.CrossingTime, int32_t(v.Kind), v.Interrupted ? 1 : 0};
                const auto text = field == 0 ? v.Event->Name :
                                  field == 1 ? v.Event->Payload :
                                  field == 2 ? v.Slot :
                                  field == 3 ? v.Section :
                                  field == 4 ? v.Action.ToString() :
                                               v.Clip.ToString();
                if (buffer && capacity > int32_t(text.size()))
                    std::memcpy(buffer, text.c_str(), text.size() + 1);
                return int32_t(text.size() + 1);
            }
            catch (...)
            {
                return -1;
            }
        }
    } // namespace
    void Install(Scripting::NativeApi& api)
    {
        api.AnimationCommand   = Command;
        api.ReadAnimationEvent = Read;
    }
    void Queue(uint64_t entity, const AnimationEventOccurrence& event)
    {
        if (!ScriptEngine::GetSceneContext() || !event.Event)
            return;
        auto copy = event;
        if (!copy.OwnedEvent)
            copy.OwnedEvent = std::make_shared<AnimationEvent>(*copy.Event);
        copy.Event = copy.OwnedEvent.get();
        Pending.push_back({entity, std::move(copy)});
    }
    void BeginFrame()
    {
        Frame.clear();
        Frame.swap(Pending);
    }
    void Reset()
    {
        Pending.clear();
        Frame.clear();
    }
} // namespace Kans::ScriptAnimation
