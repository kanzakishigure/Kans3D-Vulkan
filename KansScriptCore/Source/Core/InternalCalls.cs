using Kans;
using System;
using System.Runtime.CompilerServices;


namespace Kans
{
    public class InternalCalls
    {
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal void NativeLog(ref Vector3 parameter);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal float VectorDot(ref Vector3 parameter_1, ref Vector3 parameter_2);


        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal float EntityGetTranslation(ulong id, out Vector3 parameter);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal float EntitySetTranslation(ulong id, ref Vector3 parameter);


        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal bool Input_IsKeyPressed(KeyCode Keycode);
    }
}
