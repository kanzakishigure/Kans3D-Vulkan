using System;
using System.Collections.Generic;
using System.Text;
using Kans.Interop;

namespace Kans
{
    public enum UIEventType { PointerEnter, PointerExit, PointerDown, PointerUp, Clicked, DragBegin, Drag, DragEnd, Scroll, Focused, Blurred, Changed, Submitted, Cancelled }
    public sealed class UIEvent
    {
        public UIWidget Widget { get; }
        public ulong EntityID => Widget.EntityID;
        public bool Screen => Widget.Screen;
        public string Path { get; }
        public UIEventType Type { get; }
        public object? Value { get; }
        public Vector2 Position { get; }
        public Vector2 Delta { get; }
        internal UIEvent(UIWidget widget, UIEventData data, string path, object? value)
        {
            Widget = widget; Type = (UIEventType)data.Type; Path = path; Value = value;
            Position = new(data.PositionX, data.PositionY); Delta = new(data.DeltaX, data.DeltaY);
        }
    }
    public static unsafe class UI
    {
        internal static bool Playing;
        internal static int Generation;
        internal static ulong Owner;
        private sealed class Subscription
        {
            internal required UIWidget Widget;
            internal required UIEventType Type;
            internal required Delegate Handler;
            internal ulong Owner;
        }
        private static readonly List<Subscription> s_Subscriptions = new();
        private static IReadOnlyList<UIEvent> s_Events = Array.Empty<UIEvent>();
        /// <summary>Immutable snapshot of the frame drained before script OnUpdate. Handler mutations are delivered on the next drain.</summary>
        public static IReadOnlyList<UIEvent> Events => s_Events;
        internal readonly struct ScriptScope : IDisposable
        {
            private readonly ulong m_Previous;
            internal ScriptScope(ulong owner) { m_Previous = Owner; Owner = owner; }
            public void Dispose() { Owner = m_Previous; }
        }
        internal static void SetPlaying(bool playing)
        {
            Playing = playing; ++Generation;
            s_Subscriptions.Clear(); s_Events = Array.Empty<UIEvent>();
        }
        internal static void RemoveOwner(ulong owner) => s_Subscriptions.RemoveAll(s => s.Owner == owner);
        internal static void Subscribe(UIWidget widget, UIEventType type, Delegate handler)
        {
            foreach (var callback in handler.GetInvocationList())
                s_Subscriptions.Add(new() { Widget = widget, Type = type, Handler = callback, Owner = Owner });
        }
        internal static void Unsubscribe(UIWidget widget, UIEventType type, Delegate handler)
        {
            var callbacks = handler.GetInvocationList();
            var indices = new List<int>();
            for (int i = 0; i < s_Subscriptions.Count; ++i)
                if (Same(s_Subscriptions[i].Widget, widget) && s_Subscriptions[i].Type == type) indices.Add(i);
            // Match the target event's invocation list even when other widgets' subscriptions interleave it.
            for (int end = indices.Count; end >= callbacks.Length; --end)
            {
                int first = end - callbacks.Length;
                bool match = true;
                for (int j = 0; j < callbacks.Length; ++j)
                    match &= s_Subscriptions[indices[first + j]].Handler == callbacks[j];
                if (match)
                {
                    for (int j = callbacks.Length - 1; j >= 0; --j) s_Subscriptions.RemoveAt(indices[first + j]);
                    return;
                }
            }
        }
        private static bool Same(UIWidget a, UIWidget b) => a.EntityID == b.EntityID && a.Screen == b.Screen && a.HandlePath == b.HandlePath && a.Generation == b.Generation;
        private static string ReadEventString(int index, int field, ref UIEventData data)
        {
            fixed (UIEventData* d = &data)
            {
                int size = InternalCalls.Api.ReadUIEvent(index, d, field, null, 0);
                Properties.Check(size, "UI event");
                byte[] bytes = new byte[size];
                fixed (byte* p = bytes) Properties.Check(InternalCalls.Api.ReadUIEvent(index, d, field, p, size), "UI event");
                return Encoding.UTF8.GetString(bytes, 0, size - 1);
            }
        }
        internal static void Drain()
        {
            if (!Playing) return;
            UIEventData data = default;
            if (InternalCalls.Api.ReadUIEvent(0, &data, 0, null, 0) == 0) { s_Events = Array.Empty<UIEvent>(); return; }
            var events = new List<UIEvent>();
            for (int index = 0; InternalCalls.Api.ReadUIEvent(index, &data, 0, null, 0) > 0; ++index)
            {
                string path = ReadEventString(index, 0, ref data), key = ReadEventString(index, 1, ref data);
                var w = new UIWidget { EntityID = data.Entity, Screen = data.Screen != 0, HandlePath = path, Key = key, Generation = Generation };
                object? payload = data.ValueKind switch {
                    1 => data.Payload.Integer != 0, 2 => data.Payload.X, 3 => Properties.V2(data.Payload),
                    4 => ReadEventString(index, 2, ref data), _ => null };
                events.Add(new UIEvent(w, data, path, payload));
            }
            s_Events = events.AsReadOnly();
            // Freeze listeners for the whole drain; additions start next frame. Removals take effect immediately.
            var subscriptions = s_Subscriptions.ToArray();
            foreach (var e in events)
                foreach (var s in subscriptions)
                {
                    if (!Playing) return;
                    if (s.Type != e.Type || !Same(s.Widget, e.Widget) || !s_Subscriptions.Contains(s) || !s.Widget.IsValid || !e.Widget.IsCurrentEventTarget) continue;
                    using var scope = new ScriptScope(s.Owner);
                    try
                    {
                        if (s.Handler is Action action) action();
                        else if (s.Handler is Action<bool> boolean) boolean((bool)e.Value!);
                        else if (s.Handler is Action<float> number) number((float)e.Value!);
                        else if (s.Handler is Action<string> text) text((string)e.Value!);
                        else if (s.Handler is Action<Vector2> vector) vector(e.Type == UIEventType.Drag ? e.Delta : (Vector2)e.Value!);
                    }
                    catch (Exception ex) { Log.Error($"UI handler exception entity={s.Widget.EntityID} path={e.Path}: {ex}"); }
                }
        }
        public static UIWidget? Focused
        {
            get
            {
                if (!Playing) return null;
                UIEventData data = default;
                if (InternalCalls.Api.ReadUIEvent(-1, &data, 0, null, 0) <= 0) return null;
                return UIWidget.Resolve(data.Entity, data.Screen != 0, ReadEventString(-1, 0, ref data));
            }
        }
        public static void SetFocus(UIWidget? widget)
        {
            if (widget != null) { widget.Focus(); return; }
            PropertyValue v = default; InternalCalls.Api.UIUtility(4, null, &v);
        }
        public static bool IsPointerOverUI { get { PropertyValue v = default; return Playing && InternalCalls.Api.UIUtility(5, null, &v) > 0; } }
        private static Vector2 Map(UIScreenComponent screen, Vector2 point, int operation)
        {
            ArgumentNullException.ThrowIfNull(screen);
            WidgetAddress address = new() { Entity = screen.Entity.ID, Screen = 1 };
            var v = Properties.Value(point);
            if (InternalCalls.Api.UIUtility(operation, &address, &v) <= 0) throw new InvalidOperationException("Screen UI or viewport is unavailable.");
            return Properties.V2(v);
        }
        /// <summary>Uses UIViewport.Map, including game-view origin and DPI, in rendered canvas pixels.</summary>
        public static Vector2 ScreenToCanvas(UIScreenComponent screen, Vector2 screenPixel) => Map(screen, screenPixel, 0);
        public static Vector2 CanvasToScreen(UIScreenComponent screen, Vector2 canvasPixel) => Map(screen, canvasPixel, 1);
        public static Vector2? WorldToScreen(Vector3 world)
        {
            PropertyValue v = new() { X = world.X, Y = world.Y, Z = world.Z };
            return InternalCalls.Api.UIUtility(2, null, &v) > 0 ? Properties.V2(v) : null;
        }
    }
}
