#pragma once
#include <cstdint>

#if defined(_WIN32)
#define KANS_SCRIPT_CALL __cdecl
#else
#define KANS_SCRIPT_CALL
#endif

namespace Kans::Scripting
{
    constexpr uint32_t ApiVersion = 3;
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
    struct Value
    {
        float   X = 0, Y = 0, Z = 0, W = 0;
        int64_t Integer = 0;
    };
    // Path/key pointers are borrowed UTF-8 only for the duration of a call. Never engine pointers.
    struct WidgetAddress
    {
        uint64_t    Entity = 0;
        int32_t     Screen = 1, Reserved = 0;
        const char* Path = nullptr;
        const char* Key  = nullptr;
    };
    struct UIEventData
    {
        uint64_t Entity = 0;
        int32_t  Screen = 1, Type = 0, ValueKind = 0, Reserved = 0;
        Value    Payload;
        float    PositionX = 0, PositionY = 0, DeltaX = 0, DeltaY = 0;
    };
    // ABI v3: UTF-8, NUL terminated strings; Cdecl callbacks; int32 booleans/status;
    // no engine pointers cross this boundary. Handles are opaque uint64 values.
    struct NativeApi
    {
        uint32_t Version = ApiVersion, Size = sizeof(NativeApi);
        void(KANS_SCRIPT_CALL* Log)(int32_t, const char*)                                 = nullptr;
        int32_t(KANS_SCRIPT_CALL* HasComponent)(uint64_t, uint32_t)                       = nullptr;
        void(KANS_SCRIPT_CALL* GetTranslation)(uint64_t, Vector3*)                        = nullptr;
        void(KANS_SCRIPT_CALL* SetTranslation)(uint64_t, const Vector3*)                  = nullptr;
        void(KANS_SCRIPT_CALL* GetRotation)(uint64_t, Vector3*)                           = nullptr;
        void(KANS_SCRIPT_CALL* SetRotation)(uint64_t, const Vector3*)                     = nullptr;
        int32_t(KANS_SCRIPT_CALL* IsKeyPressed)(int32_t)                                  = nullptr;
        void(KANS_SCRIPT_CALL* SetMaterialParameter)(uint64_t, const char*, const float*) = nullptr;
        void(KANS_SCRIPT_CALL* ClearMaterialParameter)(uint64_t, const char*)             = nullptr;
        int32_t(KANS_SCRIPT_CALL* GetMaterialParameter)(uint64_t, const char*, float*)    = nullptr;
        int32_t(KANS_SCRIPT_CALL* AddComponent)(uint64_t, uint32_t)                       = nullptr;
        int32_t(KANS_SCRIPT_CALL* DestroyEntity)(uint64_t)                                = nullptr;
        // Returns 0 for a missing target, -1 for an invalid property/value; otherwise >=1.
        // String reads return required capacity including NUL, and never truncate.
        int32_t(KANS_SCRIPT_CALL* ComponentProperty)(uint64_t, uint32_t, const char*, int32_t, Value*, char*, int32_t) =
            nullptr;
        int32_t(KANS_SCRIPT_CALL* WidgetProperty)(const WidgetAddress*, const char*, int32_t, Value*, char*, int32_t) =
            nullptr;
        int32_t(KANS_SCRIPT_CALL* ReadUIEvent)(int32_t, UIEventData*, int32_t, char*, int32_t) = nullptr;
        int32_t(KANS_SCRIPT_CALL* UIUtility)(int32_t, const WidgetAddress*, Value*)            = nullptr;
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
    static_assert(sizeof(Value) == 24);
    static_assert(sizeof(UIEventData) == 64);
    static_assert(sizeof(NativeApi) == 8 + 16 * sizeof(void*));
    static_assert(sizeof(ManagedApi) == 8 + 12 * sizeof(void*));
} // namespace Kans::Scripting
