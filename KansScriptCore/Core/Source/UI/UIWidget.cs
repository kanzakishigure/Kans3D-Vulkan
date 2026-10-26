using System;
using System.Collections.Generic;
using Kans.Interop;

namespace Kans
{
    public readonly struct UIRect
    {
        public Vector2 Position { get; }
        public Vector2 Size { get; }
        public UIRect(Vector2 position, Vector2 size) { Position = position; Size = size; }
    }
    /// <summary>A path-based instance handle that survives asset rebuilds. Except IsValid,
    /// member access throws InvalidOperationException after removal or the end of Play.</summary>
    public class UIWidget
    {
        internal ulong EntityID;
        internal bool Screen;
        internal string HandlePath = "", Key = "";
        internal int Generation;
        internal virtual int ExpectedType => -2;
        internal UIWidget() { }
        internal static UIWidget? Resolve(ulong entity, bool screen, string path)
        {
            var w = new UIWidget { EntityID = entity, Screen = screen, HandlePath = path, Generation = UI.Generation };
            if (!w.IsValid) return null;
            w.Key = Properties.ReadString(0, 0, w, "Key");
            return w;
        }
        private void Check()
        {
            if (!IsValid) throw new InvalidOperationException($"UI widget '{HandlePath}' on entity {EntityID} no longer exists or belongs to an ended Play session.");
        }
        internal bool IsCurrentEventTarget
        {
            get
            {
                PropertyValue v = default;
                return IsValid && Properties.Call(0, 0, this, "EventValid", false, ref v, null) > 0 && v.Integer != 0;
            }
        }
        public bool IsValid
        {
            get
            {
                if (!UI.Playing || Generation != UI.Generation) return false;
                PropertyValue v = default;
                return Properties.Call(0, 0, this, "Type", false, ref v, null) > 0 && (ExpectedType == -2 || v.Integer == ExpectedType);
            }
        }
        internal PropertyValue Read(string name)
        {
            Check();
            PropertyValue v = default;
            Properties.Check(Properties.Call(0, 0, this, name, false, ref v, null), name);
            return v;
        }
        internal void Write(string name, PropertyValue value)
        {
            Check();
            Properties.Check(Properties.Call(0, 0, this, name, true, ref value, null), name);
        }
        internal string ReadText(string name) { Check(); return Properties.ReadString(0, 0, this, name); }
        internal void WriteText(string name, string text)
        {
            Check();
            PropertyValue v = default;
            Properties.Check(Properties.Call(0, 0, this, name, true, ref v, Properties.Utf8(text)), name);
        }
        public string Name => ReadText("Name");
        public string Path { get { Check(); return HandlePath; } }
        public bool Visible { get => Read("Visible").Integer != 0; set => Write("Visible", new() { Integer = value ? 1 : 0 }); }
        public bool Enabled { get => Read("Enabled").Integer != 0; set => Write("Enabled", new() { Integer = value ? 1 : 0 }); }
        public bool Interactable { get => Read("Interactable").Integer != 0; set => Write("Interactable", new() { Integer = value ? 1 : 0 }); }
        public float Opacity { get => Read("Opacity").X; set => Write("Opacity", new() { X = value }); }
        /// <summary>Axis-aligned position and size after layout, in canvas pixels.</summary>
        public UIRect Rect { get { var v = Read("Rect"); return new(new(v.X, v.Y), new(v.Z - v.X, v.W - v.Y)); } }
        public Vector2 Position { get => Properties.V2(Read("Position")); set => Write("Position", Properties.Value(value)); }
        public Vector2 Size { get => Properties.V2(Read("Size")); set => Write("Size", Properties.Value(value)); }
        public IReadOnlyList<UIWidget> Children
        {
            get
            {
                int count = (int)Read("ChildCount").Integer;
                var result = new List<UIWidget>(count);
                for (int i = 0; i < count; ++i)
                {
                    string path = Properties.ReadString(0, 0, this, "ChildPath", i);
                    var child = Resolve(EntityID, Screen, path);
                    if (child != null) result.Add(child);
                }
                return result.AsReadOnly();
            }
        }
        public UIWidget? Find(string path)
        {
            Check();
            ArgumentNullException.ThrowIfNull(path);
            if (path.Length == 0) return this;
            return Resolve(EntityID, Screen, HandlePath.Length == 0 ? path : HandlePath + "/" + path);
        }
        /// <summary>Returns null when the current widget has a different type.</summary>
        public T? As<T>() where T : UIWidget
        {
            int type = (int)Read("Type").Integer;
            UIWidget? view = typeof(T) == typeof(UIWidget) ? new UIWidget() :
                typeof(T) == typeof(UIText) && type == 2 ? new UIText() :
                typeof(T) == typeof(UIImage) && type == 1 ? new UIImage() :
                typeof(T) == typeof(UIButton) && type == 4 ? new UIButton() :
                typeof(T) == typeof(UIToggle) && type == 5 ? new UIToggle() :
                typeof(T) == typeof(UISlider) && type == 6 ? new UISlider() :
                typeof(T) == typeof(UIScrollView) && type == 7 ? new UIScrollView() :
                typeof(T) == typeof(UIInputField) && type == 8 ? new UIInputField() : null;
            if (view == null) return null;
            view.EntityID = EntityID; view.Screen = Screen; view.HandlePath = HandlePath; view.Key = Key; view.Generation = Generation;
            return (T)view;
        }
        public void Focus() => Write("Focus", default);
        public bool IsFocused => Read("IsFocused").Integer != 0;
        internal void Subscribe(UIEventType type, Delegate? handler) { Check(); if (handler != null) UI.Subscribe(this, type, handler); }
        internal void Unsubscribe(UIEventType type, Delegate? handler) { if (handler != null) UI.Unsubscribe(this, type, handler); }
        public event Action PointerEnter { add => Subscribe(UIEventType.PointerEnter, value); remove => Unsubscribe(UIEventType.PointerEnter, value); }
        public event Action PointerExit { add => Subscribe(UIEventType.PointerExit, value); remove => Unsubscribe(UIEventType.PointerExit, value); }
        public event Action PointerDown { add => Subscribe(UIEventType.PointerDown, value); remove => Unsubscribe(UIEventType.PointerDown, value); }
        public event Action PointerUp { add => Subscribe(UIEventType.PointerUp, value); remove => Unsubscribe(UIEventType.PointerUp, value); }
        public event Action Focused { add => Subscribe(UIEventType.Focused, value); remove => Unsubscribe(UIEventType.Focused, value); }
        public event Action Blurred { add => Subscribe(UIEventType.Blurred, value); remove => Unsubscribe(UIEventType.Blurred, value); }
        public event Action DragBegin { add => Subscribe(UIEventType.DragBegin, value); remove => Unsubscribe(UIEventType.DragBegin, value); }
        public event Action<Vector2> Drag { add => Subscribe(UIEventType.Drag, value); remove => Unsubscribe(UIEventType.Drag, value); }
        public event Action DragEnd { add => Subscribe(UIEventType.DragEnd, value); remove => Unsubscribe(UIEventType.DragEnd, value); }
    }
    public sealed class UIText : UIWidget
    {
        internal override int ExpectedType => 2;
        public string Text { get => ReadText("Text"); set => WriteText("Text", value); }
        public Vector4 Color { get => Properties.V4(Read("Color")); set => Write("Color", Properties.Value(value)); }
        public float FontSize { get => Read("FontSize").X; set => Write("FontSize", new() { X = value }); }
    }
    public sealed class UIImage : UIWidget
    {
        internal override int ExpectedType => 1;
        public string Texture { get => ReadText("Texture"); set => WriteText("Texture", value); }
        public Vector4 Color { get => Properties.V4(Read("Color")); set => Write("Color", Properties.Value(value)); }
        public Vector4 UVRect { get => Properties.V4(Read("UVRect")); set => Write("UVRect", Properties.Value(value)); }
    }
    public sealed class UIButton : UIWidget
    {
        internal override int ExpectedType => 4;
        public event Action Clicked { add => Subscribe(UIEventType.Clicked, value); remove => Unsubscribe(UIEventType.Clicked, value); }
    }
    public sealed class UIToggle : UIWidget
    {
        internal override int ExpectedType => 5;
        public bool IsOn { get => Read("IsOn").Integer != 0; set => Write("IsOn", new() { Integer = value ? 1 : 0 }); }
        public event Action<bool> Changed { add => Subscribe(UIEventType.Changed, value); remove => Unsubscribe(UIEventType.Changed, value); }
    }
    public sealed class UISlider : UIWidget
    {
        internal override int ExpectedType => 6;
        public float Value { get => Read("Value").X; set => Write("Value", new() { X = value }); }
        public float Min { get => Read("Min").X; set => Write("Min", new() { X = value }); }
        public float Max { get => Read("Max").X; set => Write("Max", new() { X = value }); }
        public event Action<float> ValueChanged { add => Subscribe(UIEventType.Changed, value); remove => Unsubscribe(UIEventType.Changed, value); }
    }
    public sealed class UIScrollView : UIWidget
    {
        internal override int ExpectedType => 7;
        public Vector2 ScrollPosition { get => Properties.V2(Read("ScrollPosition")); set => Write("ScrollPosition", Properties.Value(value)); }
        public event Action<Vector2> Scrolled { add => Subscribe(UIEventType.Changed, value); remove => Unsubscribe(UIEventType.Changed, value); }
    }
    public sealed class UIInputField : UIWidget
    {
        internal override int ExpectedType => 8;
        public string Text { get => ReadText("Text"); set => WriteText("Text", value); }
        public string Placeholder { get => ReadText("Placeholder"); set => WriteText("Placeholder", value); }
        public int CharacterLimit { get => (int)Read("CharacterLimit").Integer; set => Write("CharacterLimit", new() { Integer = value }); }
        public event Action<string> Changed { add => Subscribe(UIEventType.Changed, value); remove => Unsubscribe(UIEventType.Changed, value); }
        public event Action<string> Submitted { add => Subscribe(UIEventType.Submitted, value); remove => Unsubscribe(UIEventType.Submitted, value); }
        public event Action Cancelled { add => Subscribe(UIEventType.Cancelled, value); remove => Unsubscribe(UIEventType.Cancelled, value); }
    }
}
