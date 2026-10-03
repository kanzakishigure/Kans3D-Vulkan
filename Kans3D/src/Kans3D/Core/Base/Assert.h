#pragma once
#include <signal.h>

#ifdef _MSC_VER
#define KS_DEBUGBREAK() __debugbreak()
#elif defined(__clang__)
#define KS_DEBUGBREAK() __builtin_debugtrap()
#elif defined(__GNUC__)
#define KS_DEBUGBREAK() __builtin_trap()
#else
#include <signal.h>
#define KS_DEBUGBREAK() raise(SIGTRAP)
#endif

#ifdef KS_DEBUG
#define ENABLE_ASSERTS
#endif // KS_DEBUG

#ifdef ENABLE_ASSERTS
#define CORE_ASSERT_CALL(...) \
    ::Kans::Log::PrintAssertMessage(::Kans::Log::Type::Core, "Assertion Failed" __VA_OPT__(, ) __VA_ARGS__)
#define CLIENT_ASSERT_CALL(...) \
    ::Kans::Log::PrintAssertMessage(::Kans::Log::Type::Client, "Assertion Failed" __VA_OPT__(, ) __VA_ARGS__)
#define CORE_ASSERT(x, ...) \
    { \
        if (!(x)) \
        { \
            CORE_ASSERT_CALL(__VA_ARGS__); \
            KS_DEBUGBREAK(); \
        } \
    }
#define CLIENT_ASSERT(x, ...) \
    { \
        if (!(x)) \
        { \
            CLIENT_ASSERT_CALL(__VA_ARGS__); \
            KS_DEBUGBREAK(); \
        } \
    }
#else
#define CLIENT_ASSERT(x, ...)
#define CORE_ASSERT(x, ...)
#endif // HZ_ENABLE_ASSERTS