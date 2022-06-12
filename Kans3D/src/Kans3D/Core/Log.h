#pragma once
#include"Kans3D/Core/Base.h"
//spdlogÍ·ÎÄ¼þ¿â
#include"spdlog/spdlog.h"
#include"spdlog/fmt/ostr.h"
#include"spdlog/fmt/fmt.h"
namespace Kans {

	class  Log
	{
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger>  s_CoreLogger;
		static std::shared_ptr<spdlog::logger>  s_ClientLogger;
		
	};
	
}
//core logging marcos
#define HZ_CORE_TRACE(...)      ::Kans::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define HZ_CORE_INFO(...)       ::Kans::Log::GetCoreLogger()->info(__VA_ARGS__)
#define HZ_CORE_WARN(...)       ::Kans::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define HZ_CORE_ERROR(...)      ::Kans::Log::GetCoreLogger()->error(__VA_ARGS__)
#define HZ_CORE_CRITICAL(...)   ::Kans::Log::GetCoreLogger()->critical(__VA_ARGS__)

//client logging marcos
#define HZ_TRACE(...)    ::Kans::Log::GetClientLogger()->trace(__VA_ARGS__)
#define HZ_INFO(...)     ::Kans::Log::GetClientLogger()->info(__VA_ARGS__)
#define HZ_WARN(...)     ::Kans::Log::GetClientLogger()->warn(__VA_ARGS__)
#define HZ_ERROR(...)    ::Kans::Log::GetClientLogger()->error(__VA_ARGS__)
#define HZ_CRITICAL(...) ::Kans::Log::GetClientLogger()->critical(__VA_ARGS__)

