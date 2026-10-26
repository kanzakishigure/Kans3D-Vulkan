using System;
using System.Runtime.InteropServices;
using System.Text;
using Kans.Interop;

namespace Kans
{
    // One fixed numeric value and caller-owned UTF-8 buffers are shared by JIT and NativeAOT.
    internal static unsafe class Properties
    {
        internal static byte[] Utf8(string text)
        {
            ArgumentNullException.ThrowIfNull(text);
            if (text.Contains('\0')) throw new ArgumentException("Strings cannot contain NUL.", nameof(text));
            return Encoding.UTF8.GetBytes(text + "\0");
        }
        internal static PropertyValue Value(Vector2 x) => new() { X = x.X, Y = x.Y };
        internal static PropertyValue Value(Vector4 x) => new() { X = x.X, Y = x.Y, Z = x.Z, W = x.W };
        internal static Vector2 V2(PropertyValue x) => new(x.X, x.Y);
        internal static Vector4 V4(PropertyValue x) => new(x.X, x.Y, x.Z, x.W);
        internal static int Call(ulong entity, uint component, UIWidget? widget, string name, bool write,
                                 ref PropertyValue value, byte[]? text)
        {
            fixed (byte* p = Utf8(name), buffer = text)
            fixed (PropertyValue* v = &value)
            {
                if (widget == null)
                    return InternalCalls.Api.ComponentProperty(entity, component, p, write ? 1 : 0, v, buffer, text?.Length ?? 0);
                fixed (byte* path = Utf8(widget.HandlePath), key = Utf8(widget.Key))
                {
                    WidgetAddress a = new() { Entity = widget.EntityID, Screen = widget.Screen ? 1 : 0, Path = path, Key = key };
                    return InternalCalls.Api.WidgetProperty(&a, p, write ? 1 : 0, v, buffer, text?.Length ?? 0);
                }
            }
        }
        internal static void Check(int result, string name)
        {
            if (result == 0) throw new InvalidOperationException($"The UI widget or component no longer exists ({name}).");
            if (result < 0) throw new ArgumentException($"Invalid or unsupported value/property: {name}.");
        }
        internal static string ReadString(ulong id, uint component, UIWidget? widget, string name, long index = 0)
        {
            PropertyValue value = new() { Integer = index };
            int size = Call(id, component, widget, name, false, ref value, null);
            Check(size, name);
            byte[] buffer = new byte[size];
            Check(Call(id, component, widget, name, false, ref value, buffer), name);
            return Encoding.UTF8.GetString(buffer, 0, size - 1);
        }
    }
    public enum TextBillboard { None, Full, Vertical }
    public enum TextHorizontalAlignment { Left, Center, Right }
    public enum TextVerticalAlignment { Top, Middle, Bottom, Baseline }

    public abstract class VisualComponent : Component
    {
        internal abstract uint TypeID { get; }
        internal PropertyValue Read(string name)
        {
            PropertyValue v = default;
            Properties.Check(Properties.Call(Entity.ID, TypeID, null, name, false, ref v, null), name);
            return v;
        }
        internal void Write(string name, PropertyValue value) => Properties.Check(Properties.Call(Entity.ID, TypeID, null, name, true, ref value, null), name);
        internal string ReadText(string name) => Properties.ReadString(Entity.ID, TypeID, null, name);
        internal void WriteText(string name, string text)
        {
            PropertyValue v = default;
            Properties.Check(Properties.Call(Entity.ID, TypeID, null, name, true, ref v, Properties.Utf8(text)), name);
        }
    }
    public sealed class TextComponent : VisualComponent
    {
        internal override uint TypeID => ComponentIds.TextComponent;
        public string Text { get => ReadText("Text"); set => WriteText("Text", value); }
        public Vector4 Color { get => Properties.V4(Read("Color")); set => Write("Color", Properties.Value(value)); }
        public float FontSize { get => Read("FontSize").X; set => Write("FontSize", new() { X = value }); }
        public TextHorizontalAlignment HorizontalAlignment { get => (TextHorizontalAlignment)Read("HorizontalAlignment").Integer; set => Write("HorizontalAlignment", new() { Integer = (int)value }); }
        public TextVerticalAlignment VerticalAlignment { get => (TextVerticalAlignment)Read("VerticalAlignment").Integer; set => Write("VerticalAlignment", new() { Integer = (int)value }); }
        public float WrapWidth { get => Read("WrapWidth").X; set => Write("WrapWidth", new() { X = value }); }
        public float OutlineWidth { get => Read("OutlineWidth").X; set => Write("OutlineWidth", new() { X = value }); }
        public Vector4 OutlineColor { get => Properties.V4(Read("OutlineColor")); set => Write("OutlineColor", Properties.Value(value)); }
        public Vector2 ShadowOffset { get => Properties.V2(Read("ShadowOffset")); set => Write("ShadowOffset", Properties.Value(value)); }
        public float ShadowSoftness { get => Read("ShadowSoftness").X; set => Write("ShadowSoftness", new() { X = value }); }
        public Vector4 ShadowColor { get => Properties.V4(Read("ShadowColor")); set => Write("ShadowColor", Properties.Value(value)); }
        public TextBillboard Billboard { get => (TextBillboard)Read("Billboard").Integer; set => Write("Billboard", new() { Integer = (int)value }); }
    }
    public sealed class SpriteRendererComponent : VisualComponent
    {
        internal override uint TypeID => ComponentIds.SpriteRendererComponent;
        /// <summary>Canonical engine AssetID string; empty selects the white texture.</summary>
        public string Texture { get => ReadText("Texture"); set => WriteText("Texture", value); }
        public Vector4 Color { get => Properties.V4(Read("Color")); set => Write("Color", Properties.Value(value)); }
        public Vector4 UVRect { get => Properties.V4(Read("UVRect")); set => Write("UVRect", Properties.Value(value)); }
        public bool FlipX { get => Read("FlipX").Integer != 0; set => Write("FlipX", new() { Integer = value ? 1 : 0 }); }
        public bool FlipY { get => Read("FlipY").Integer != 0; set => Write("FlipY", new() { Integer = value ? 1 : 0 }); }
        public int SortingOrder { get => (int)Read("SortingOrder").Integer; set => Write("SortingOrder", new() { Integer = value }); }
    }
    public abstract class UISurfaceComponent : VisualComponent
    {
        internal abstract bool Screen { get; }
        /// <summary>Canonical engine AssetID string. The instance becomes available when the asset is ready.</summary>
        public string Asset { get => ReadText("Asset"); set => WriteText("Asset", value); }
        public bool Visible { get => Read("Visible").Integer != 0; set => Write("Visible", new() { Integer = value ? 1 : 0 }); }
        public int SortOrder { get => (int)Read("SortOrder").Integer; set => Write("SortOrder", new() { Integer = value }); }
        public UIWidget? Root => UIWidget.Resolve(Entity.ID, Screen, "");
    }
    public sealed class UIScreenComponent : UISurfaceComponent
    {
        internal override uint TypeID => ComponentIds.UIScreenComponent;
        internal override bool Screen => true;
    }
    public sealed class UIWorldComponent : UISurfaceComponent
    {
        internal override uint TypeID => ComponentIds.UIWorldComponent;
        internal override bool Screen => false;
        public TextBillboard Billboard { get => (TextBillboard)Read("Billboard").Integer; set => Write("Billboard", new() { Integer = (int)value }); }
        public bool AlwaysOnTop { get => Read("AlwaysOnTop").Integer != 0; set => Write("AlwaysOnTop", new() { Integer = value ? 1 : 0 }); }
    }
}
