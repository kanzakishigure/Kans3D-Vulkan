#pragma once
#include <cstdint>

#if defined(_WIN32)
#define KANS_SCRIPT_CALL __cdecl
#else
#define KANS_SCRIPT_CALL
#endif

namespace Kans::Scripting
{
    constexpr uint32_t ApiVersion = 1;
    enum class ComponentType : uint32_t
    {
#define KANS_COMPONENT(id, name) name = id,
#include "ComponentTypes.def"
#undef KANS_COMPONENT
    };
    struct Vector3
    {
        float X, Y, Z;
    };
    // ABI v1: UTF-8, NUL terminated strings; Cdecl callbacks; int32 booleans/status;
    // no engine pointers cross this boundary. Handles are opaque uint64 values.
    struct NativeApi
    {
        uint32_t Version = ApiVersion, Size = sizeof(NativeApi);
        void(KANS_SCRIPT_CALL* Log)(int32_t, const char*)                = nullptr;
        int32_t(KANS_SCRIPT_CALL* HasComponent)(uint64_t, uint32_t)      = nullptr;
        void(KANS_SCRIPT_CALL* GetTranslation)(uint64_t, Vector3*)       = nullptr;
        void(KANS_SCRIPT_CALL* SetTranslation)(uint64_t, const Vector3*) = nullptr;
        void(KANS_SCRIPT_CALL* GetRotation)(uint64_t, Vector3*)          = nullptr;
        void(KANS_SCRIPT_CALL* SetRotation)(uint64_t, const Vector3*)    = nullptr;
        int32_t(KANS_SCRIPT_CALL* IsKeyPressed)(int32_t)                 = nullptr;
    };
    struct ManagedApi
    {
        uint32_t Version = ApiVersion, Size = sizeof(ManagedApi);
        void(KANS_SCRIPT_CALL* Shutdown)()                              = nullptr;
        int32_t(KANS_SCRIPT_CALL* LoadAssembly)(const char*)            = nullptr;
        int32_t(KANS_SCRIPT_CALL* ReloadAssembly)()                     = nullptr;
        int32_t(KANS_SCRIPT_CALL* ClassCount)()                         = nullptr;
        int32_t(KANS_SCRIPT_CALL* ClassName)(int32_t, char*, int32_t)   = nullptr;
        int32_t(KANS_SCRIPT_CALL* ClassExists)(const char*)             = nullptr;
        uint64_t(KANS_SCRIPT_CALL* Create)(uint64_t, const char*)       = nullptr;
        void(KANS_SCRIPT_CALL* Destroy)(uint64_t)                       = nullptr;
        void(KANS_SCRIPT_CALL* Update)(const uint64_t*, int32_t, float) = nullptr;
        void(KANS_SCRIPT_CALL* SetPlaying)(int32_t)                     = nullptr;
        int64_t(KANS_SCRIPT_CALL* AllocatedBytes)()                     = nullptr;
        int32_t(KANS_SCRIPT_CALL* LastUnloadCollected)()                = nullptr;
    };
    using InitializeFn = int32_t(KANS_SCRIPT_CALL*)(const NativeApi*, int32_t, ManagedApi*, int32_t);
    static_assert(sizeof(Vector3) == 12);
    static_assert(sizeof(NativeApi) == 8 + 7 * sizeof(void*));
    static_assert(sizeof(ManagedApi) == 8 + 12 * sizeof(void*));
} // namespace Kans::Scripting
