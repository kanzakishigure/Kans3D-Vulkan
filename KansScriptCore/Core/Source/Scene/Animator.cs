using System;
using System.Collections.Generic;
using System.Text;
using Kans.Interop;

namespace Kans
{
    public readonly record struct ActionHandle(string ID);
    public enum AnimationEventKind { Clip, Action, ActionStarted, SectionChanged, ActionBlendingOut, ActionEnded }
    public sealed record AnimationEvent(ulong EntityID, AnimationEventKind Kind, string Name, string Payload,
        string Slot, string Section, string Action, string Clip, double Time, bool Interrupted);

    public class Animator : Component
    {
        private unsafe bool Call(int operation, string? asset = null, string? slot = null,
            string? from = null, string? to = null, float blendIn = 0, float blendOut = 0, float speed = 1)
        {
            static byte[]? Utf8(string? value)
            {
                if (value == null) return null;
                if (value.Contains('\0')) throw new ArgumentException("Animation names cannot contain NUL.");
                return Encoding.UTF8.GetBytes(value + "\0");
            }
            fixed (byte* a = Utf8(asset), s = Utf8(slot), f = Utf8(from), t = Utf8(to))
                return InternalCalls.Api.AnimationCommand(Entity.ID, operation, a, s, f, t, blendIn, blendOut, speed) > 0;
        }
        public bool PlayAction(ActionHandle action, string? slot = null, float speed = 1) => PlayAction(action.ID, slot, speed);
        public bool PlayAction(string action, string? slot = null, float speed = 1) => Call(0, action, slot, speed: speed);
        public bool PlayClip(string slot, string clip, float blendIn = .2f, float blendOut = .2f, float speed = 1) =>
            Call(1, clip, slot, blendIn: blendIn, blendOut: blendOut, speed: speed);
        public bool StopAction(string slot, float blendOut = .2f) => Call(2, slot: slot, blendOut: blendOut);
        public bool JumpToSection(string slot, string name) => Call(3, slot: slot, from: name);
        public bool IsActionPlaying(string slot) => Call(4, slot: slot);
        public bool SetNextSection(string slot, string from, string to) => Call(5, slot: slot, from: from, to: to);
        public event System.Action<AnimationEvent> Event
        {
            add => AnimationEvents.Subscribe(Entity.ID, false, value);
            remove => AnimationEvents.Unsubscribe(Entity.ID, false, value);
        }
        public event System.Action<AnimationEvent> ActionEnded
        {
            add => AnimationEvents.Subscribe(Entity.ID, true, value);
            remove => AnimationEvents.Unsubscribe(Entity.ID, true, value);
        }
    }
    public class AnimatorComponent : Animator { }
    internal static unsafe class AnimationEvents
    {
        private sealed class Subscription
        {
            internal readonly ulong Entity, Owner;
            internal readonly bool EndOnly;
            internal readonly System.Action<AnimationEvent> Handler;
            internal Subscription(ulong entity, ulong owner, bool endOnly, System.Action<AnimationEvent> handler)
            { Entity = entity; Owner = owner; EndOnly = endOnly; Handler = handler; }
        }
        private static readonly List<Subscription> s_Subscriptions = new();
        internal static void Reset() => s_Subscriptions.Clear();
        internal static void RemoveOwner(ulong owner) => s_Subscriptions.RemoveAll(s => s.Owner == owner);
        internal static void Subscribe(ulong entity, bool endOnly, System.Action<AnimationEvent> handler)
        {
            foreach (System.Action<AnimationEvent> callback in handler.GetInvocationList())
                s_Subscriptions.Add(new(entity, UI.Owner, endOnly, callback));
        }
        internal static void Unsubscribe(ulong entity, bool endOnly, System.Action<AnimationEvent> handler)
        {
            var callbacks = handler.GetInvocationList();
            var indices = new List<int>();
            for (int i = 0; i < s_Subscriptions.Count; ++i)
                if (s_Subscriptions[i].Entity == entity && s_Subscriptions[i].EndOnly == endOnly) indices.Add(i);
            for (int end = indices.Count; end >= callbacks.Length; --end)
            {
                int first = end - callbacks.Length;
                bool match = true;
                for (int i = 0; i < callbacks.Length; ++i) match &= (Delegate)s_Subscriptions[indices[first + i]].Handler == callbacks[i];
                if (!match) continue;
                for (int i = callbacks.Length - 1; i >= 0; --i) s_Subscriptions.RemoveAt(indices[first + i]);
                return;
            }
        }
        private static string Read(int index, int field, ref AnimationEventData data)
        {
            fixed (AnimationEventData* d = &data)
            {
                int size = InternalCalls.Api.ReadAnimationEvent(index, d, field, null, 0);
                if (size <= 0) throw new InvalidOperationException("Animation event is unavailable.");
                byte[] text = new byte[size];
                fixed (byte* p = text) InternalCalls.Api.ReadAnimationEvent(index, d, field, p, size);
                return Encoding.UTF8.GetString(text, 0, size - 1);
            }
        }
        internal static void Drain()
        {
            if (!UI.Playing || s_Subscriptions.Count == 0) return;
            var subscriptions = s_Subscriptions.ToArray();
            AnimationEventData data = default;
            for (int index = 0; InternalCalls.Api.ReadAnimationEvent(index, &data, 0, null, 0) > 0; ++index)
            {
                var e = new AnimationEvent(data.Entity, (AnimationEventKind)data.Kind, Read(index, 0, ref data),
                    Read(index, 1, ref data), Read(index, 2, ref data), Read(index, 3, ref data), Read(index, 4, ref data),
                    Read(index, 5, ref data), data.Time, data.Interrupted != 0);
                foreach (var s in subscriptions)
                {
                    if (!UI.Playing) return;
                    if (s.Entity != e.EntityID || (s.EndOnly && e.Kind != AnimationEventKind.ActionEnded) ||
                        !s_Subscriptions.Contains(s)) continue;
                    using var scope = new UI.ScriptScope(s.Owner);
                    try { s.Handler(e); }
                    catch (Exception ex) { Log.Error($"Animation handler exception entity={e.EntityID}: {ex}"); }
                }
            }
        }
    }
}
