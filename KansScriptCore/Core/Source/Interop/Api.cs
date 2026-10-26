using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Kans.Interop
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct PropertyValue
    {
        public float X, Y, Z, W;
        public long Integer;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct WidgetAddress
    {
        public ulong Entity;
        public int Screen, Reserved;
        public byte* Path;
        public byte* Key;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct UIEventData
    {
        public ulong Entity;
        public int Screen, Type, ValueKind, Reserved;
        public PropertyValue Payload;
        public float PositionX, PositionY, DeltaX, DeltaY;
    }
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public const uint ApiVersion = 3;
        public uint Version, Size;
        public delegate* unmanaged[Cdecl]<int, byte*, void> Log;
        public delegate* unmanaged[Cdecl]<ulong, uint, int> HasComponent;
        public delegate* unmanaged[Cdecl]<ulong, Vector3*, void> GetTranslation;
        public delegate* unmanaged[Cdecl]<ulong, Vector3*, void> SetTranslation;
        public delegate* unmanaged[Cdecl]<ulong, Vector3*, void> GetRotation;
        public delegate* unmanaged[Cdecl]<ulong, Vector3*, void> SetRotation;
        public delegate* unmanaged[Cdecl]<int, int> IsKeyPressed;
        public delegate* unmanaged[Cdecl]<ulong, byte*, float*, void> SetMaterialParameter;
        public delegate* unmanaged[Cdecl]<ulong, byte*, void> ClearMaterialParameter;
        public delegate* unmanaged[Cdecl]<ulong, byte*, float*, int> GetMaterialParameter;
        internal delegate* unmanaged[Cdecl]<ulong, uint, int> AddComponent;
        internal delegate* unmanaged[Cdecl]<ulong, int> DestroyEntity;
        internal delegate* unmanaged[Cdecl]<ulong, uint, byte*, int, PropertyValue*, byte*, int, int> ComponentProperty;
        internal delegate* unmanaged[Cdecl]<WidgetAddress*, byte*, int, PropertyValue*, byte*, int, int> WidgetProperty;
        internal delegate* unmanaged[Cdecl]<int, UIEventData*, int, byte*, int, int> ReadUIEvent;
        internal delegate* unmanaged[Cdecl]<int, WidgetAddress*, PropertyValue*, int> UIUtility;
    }
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedApi
    {
        public uint Version, Size;
        public delegate* unmanaged[Cdecl]<void> Shutdown;
        public delegate* unmanaged[Cdecl]<byte*, int> LoadAssembly;
        public delegate* unmanaged[Cdecl]<int> ReloadAssembly;
        public delegate* unmanaged[Cdecl]<int> ClassCount;
        public delegate* unmanaged[Cdecl]<int, byte*, int, int> ClassName;
        public delegate* unmanaged[Cdecl]<byte*, int> ClassExists;
        public delegate* unmanaged[Cdecl]<ulong, byte*, ulong> Create;
        public delegate* unmanaged[Cdecl]<ulong, void> Destroy;
        public delegate* unmanaged[Cdecl]<ulong*, int, float, void> Update;
        public delegate* unmanaged[Cdecl]<int, void> SetPlaying;
        // Diagnostic hooks, also useful for validating packaged builds.
        public delegate* unmanaged[Cdecl]<long> AllocatedBytes;
        public delegate* unmanaged[Cdecl]<int> LastUnloadCollected;
    }

    public interface IScriptRegistry
    {
        string[] Names { get; }
        Entity? Create(string name);
    }
    public static class ScriptRegistry
    {
        [System.ThreadStatic] internal static IScriptRegistry? Pending;
        // A module initializer in each generated user assembly registers its static factories.
        public static void Register(IScriptRegistry registry) => Pending = registry;
    }
}
