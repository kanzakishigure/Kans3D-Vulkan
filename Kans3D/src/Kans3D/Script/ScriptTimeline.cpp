#include "kspch.h"
#include "ScriptTimeline.h"
#include <cstring>
#include "Kans3D/Scene/Entity.h"
#include "ScriptEngine.h"
namespace Kans::ScriptTimeline
{
    namespace
    {
        struct Event
        {
            uint64_t      Player = 0, Target = 0;
            TimelineEvent Value;
            bool          Finished = false;
        };
        std::vector<Event> Pending, Frame;
        int32_t KANS_SCRIPT_CALL
        Command(uint64_t id, int32_t op, const char* binding, uint64_t target, double* time) noexcept
        {
            try
            {
                auto scene = ScriptEngine::GetSceneContext();
                auto e     = scene ? scene->GetEntityByUUID(id) : Entity {};
                if (!e || !scene->PrepareTimeline(e))
                    return 0;
                auto p = e.GetComponent<TimelinePlayerComponent>().Player;
                switch (op)
                {
                    case 0:
                        return p->Play();
                    case 1:
                        p->Pause();
                        return 1;
                    case 2:
                        p->Stop();
                        return 1;
                    case 3:
                        return time && p->Seek(*time);
                    case 4:
                        return binding && p->SetBinding(binding, UUID(target));
                    case 5:
                        if (time)
                            *time = p->Time();
                        return time ? 1 : 0;
                    case 6:
                        return p->IsPlaying();
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
        Read(int32_t index, Scripting::TimelineEventData* data, int32_t field, char* buffer, int32_t capacity) noexcept
        {
            try
            {
                if (index < 0 || size_t(index) >= Frame.size())
                    return 0;
                if (!data || field < 0 || field > 2)
                    return -1;
                const auto& e    = Frame[index];
                *data            = {e.Player, e.Target, e.Value.Time, e.Finished ? 1 : 0, 0};
                const auto& text = field == 0 ? e.Value.Name : field == 1 ? e.Value.Payload : e.Value.Method;
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
        api.TimelineCommand   = Command;
        api.ReadTimelineEvent = Read;
    }
    void Queue(UUID player, UUID target, const TimelineEvent& e, bool finished)
    {
        Pending.push_back({player, target, e, finished});
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
} // namespace Kans::ScriptTimeline
