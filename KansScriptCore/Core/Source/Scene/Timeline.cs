using System;
using System.Collections.Generic;
using System.Text;
using Kans.Interop;
namespace Kans
{
    public class TimelinePlayer : Component
    {
        private unsafe bool Call(int operation, double time = 0, string? binding = null, ulong target = 0)
        {
            if (binding?.Contains('\0') == true) throw new ArgumentException("Binding name cannot contain NUL.");
            byte[]? text = binding == null ? null : Encoding.UTF8.GetBytes(binding + "\0");
            fixed (byte* p = text) return InternalCalls.Api.TimelineCommand(Entity.ID, operation, p, target, &time) > 0;
        }
        public bool Play() => Call(0);
        public void Pause() => Call(1);
        public void Stop() => Call(2);
        public bool Seek(double time) => Call(3, time);
        public bool SetBinding(string name, Entity entity) => Call(4, binding: name, target: entity.ID);
        public unsafe double Time { get { double t = 0; InternalCalls.Api.TimelineCommand(Entity.ID, 5, null, 0, &t); return t; } }
        public bool IsPlaying => Call(6);
        public event System.Action Finished
        {
            add => TimelineEvents.Add(Entity.ID, value);
            remove => TimelineEvents.Remove(Entity.ID, value);
        }
        public event System.Action<string, string> Event
        {
            add => TimelineEvents.Add(Entity.ID, value);
            remove => TimelineEvents.Remove(Entity.ID, value);
        }
    }
    public class TimelinePlayerComponent : TimelinePlayer { }
    public static class Timeline
    {
        public static event System.Action<string, string> Event
        {
            add => TimelineEvents.Add(0, value);
            remove => TimelineEvents.Remove(0, value);
        }
    }
    internal static unsafe class TimelineEvents
    {
        private sealed record Subscription(ulong Entity, ulong Owner, Delegate Handler);
        private static readonly List<Subscription> s_Subscriptions = new();
        internal static void Reset() => s_Subscriptions.Clear();
        internal static void RemoveOwner(ulong owner) => s_Subscriptions.RemoveAll(s => s.Owner == owner);
        internal static void Add(ulong entity, Delegate handler)
        {
            foreach (var callback in handler.GetInvocationList()) s_Subscriptions.Add(new(entity, UI.Owner, callback));
        }
        internal static void Remove(ulong entity, Delegate handler)
        {
            var callbacks = handler.GetInvocationList();
            var indices = new List<int>();
            for (int i = 0; i < s_Subscriptions.Count; ++i)
                if (s_Subscriptions[i].Entity == entity && s_Subscriptions[i].Handler.GetType() == handler.GetType()) indices.Add(i);
            for (int end = indices.Count; end >= callbacks.Length; --end)
            {
                int first = end - callbacks.Length; bool match = true;
                for (int i = 0; i < callbacks.Length; ++i) match &= s_Subscriptions[indices[first + i]].Handler == callbacks[i];
                if (!match) continue;
                for (int i = callbacks.Length - 1; i >= 0; --i) s_Subscriptions.RemoveAt(indices[first + i]);
                return;
            }
        }
        private static string Read(int index, int field, ref TimelineEventData data)
        {
            fixed (TimelineEventData* d = &data)
            {
                int count = InternalCalls.Api.ReadTimelineEvent(index, d, field, null, 0);
                if (count <= 0) throw new InvalidOperationException("Timeline event unavailable.");
                byte[] text = new byte[count];
                fixed (byte* p = text) InternalCalls.Api.ReadTimelineEvent(index, d, field, p, count);
                return Encoding.UTF8.GetString(text, 0, count - 1);
            }
        }
        internal static void Drain()
        {
            if (!UI.Playing) return;
            TimelineEventData data = default;
            for (int index = 0; InternalCalls.Api.ReadTimelineEvent(index, &data, 0, null, 0) > 0; ++index)
            {
                var name = Read(index, 0, ref data); var payload = Read(index, 1, ref data); var method = Read(index, 2, ref data);
                foreach (var s in s_Subscriptions.ToArray())
                {
                    if (!UI.Playing) return;
                    if ((s.Entity != 0 && s.Entity != data.Player) || !s_Subscriptions.Contains(s)) continue;
                    using var scope = new UI.ScriptScope(s.Owner);
                    try {
                        if (data.Finished != 0 && s.Handler is System.Action done) done();
                        if (data.Finished == 0 && s.Handler is System.Action<string, string> callback) callback(name, payload);
                    } catch (Exception ex) { Log.Error($"Timeline handler exception entity={data.Player}: {ex}"); }
                }
                if (UI.Playing && data.Finished == 0 && data.Target != 0 && method.Length != 0) Host.InvokeTimeline(data.Target, method, payload);
            }
        }
    }
}
