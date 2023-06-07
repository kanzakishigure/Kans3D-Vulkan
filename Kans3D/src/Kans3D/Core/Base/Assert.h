#pragma once
#ifdef KS_DEBUG
#define ENABLE_ASSERTS
#endif // KS_DEBUG

#ifdef ENABLE_ASSERTS
#define CORE_ASSERT_CALL(...)		::Kans::Log::PrintAssertMessage(::Kans::Log::Type::Core, "Assertion Failed", __VA_ARGS__)
#define CLIENT_ASSERT_CALL(...)		::Kans::Log::PrintAssertMessage(::Kans::Log::Type::Client, "Assertion Failed", __VA_ARGS__)
#define CORE_ASSERT(x,...) {if(!(x)){CORE_ASSERT_CALL(__VA_ARGS__);__debugbreak();}}
#define CLIENT_ASSERT(x,...) {if(!(x)){CLIENT_ASSERT_CALL(__VA_ARGS__);__debugbreak();}}
#else
#define CLIENT_ASSERT(x,...)
#define CORE_ASSERT(x,...)
#endif // HZ_ENABLE_ASSERTS