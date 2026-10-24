using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Kans.Interop;

public static unsafe class Entry
{
    [UnmanagedCallersOnly(EntryPoint = "kans_scripts_initialize", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Initialize(NativeApi* native, int nativeSize, ManagedApi* managed, int managedSize)
    {
        int result = Host.InitializeTables(native, nativeSize, managed, managedSize);
        if (result != 0) return result;
        try
        {
            // Direct reference roots the same factories used by the collectible JIT assembly.
            ScriptRegistry.Register(new Kans.Generated.ScriptRegistry());
            return Host.InitializeTables(native, nativeSize, managed, managedSize);
        }
        catch (System.Exception exception)
        {
            try { Kans.Log.Error($"Script bootstrap exception: {exception}"); } catch { }
            return -1;
        }
    }
}
