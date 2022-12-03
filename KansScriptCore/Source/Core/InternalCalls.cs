using Kans;
using System;
using System.Runtime.CompilerServices;


namespace Kans
{
    public class InternalCalls
    {


        #region EntityInterCalls
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal bool Entity_HasComponent(ulong id,Type type);


        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal float Entity_GetTranslation(ulong id, out Vector3 parameter);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal float Entity_SetTranslation(ulong id, ref Vector3 parameter);
        #endregion

        #region Log
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal void NativeLog(ref Vector3 parameter);
        #endregion

        #region Math Function
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal float VectorDot(ref Vector3 parameter_1, ref Vector3 parameter_2);
        #endregion



        #region Input
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern static internal bool Input_IsKeyPressed(KeyCode Keycode);
        #endregion

    }
}
