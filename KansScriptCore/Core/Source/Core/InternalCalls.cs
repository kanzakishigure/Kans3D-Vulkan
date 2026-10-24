using System;
using System.Text;
using System.Buffers;
using Kans.Interop;

namespace Kans
{
    public static unsafe class InternalCalls
    {
        internal static NativeApi Api;
        internal static bool HasComponent(ulong id, uint type) => Api.HasComponent(id, type) != 0;
        internal static Vector3 GetTranslation(ulong id) { Vector3 v = default; Api.GetTranslation(id, &v); return v; }
        internal static void SetTranslation(ulong id, Vector3 v) => Api.SetTranslation(id, &v);
        internal static Vector3 GetRotation(ulong id) { Vector3 v = default; Api.GetRotation(id, &v); return v; }
        internal static void SetRotation(ulong id, Vector3 v) => Api.SetRotation(id, &v);
        internal static bool Input_IsKeyPressed(KeyCode key) => Api.IsKeyPressed((int)key) != 0;
        public static float VectorDot(ref Vector3 a, ref Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        public static void NativeLog(ref Vector3 v)
        {
            Span<char> text = stackalloc char[128];
            if (text.TryWrite($"{v.X}, {v.Y}, {v.Z}", out int written)) WriteLog(0, text.Slice(0, written));
        }
        internal static void WriteLog(int level, ReadOnlySpan<char> message)
        {
            int length = Encoding.UTF8.GetByteCount(message);
            byte[]? rented = null;
            Span<byte> bytes = length < 4096 ? stackalloc byte[length + 1] : (rented = ArrayPool<byte>.Shared.Rent(length + 1));
            try
            {
                Encoding.UTF8.GetBytes(message, bytes);
                bytes[length] = 0;
                fixed (byte* p = bytes) Api.Log(level, p);
            }
            finally { if (rented != null) ArrayPool<byte>.Shared.Return(rented); }
        }
    }
    public static class Log
    {
        public static void Info(string message) => InternalCalls.WriteLog(0, message);
        public static void Warn(string message) => InternalCalls.WriteLog(1, message);
        public static void Error(string message) => InternalCalls.WriteLog(2, message);
    }
}
