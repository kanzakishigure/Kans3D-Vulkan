#pragma once
#include"Kans3D/Core/Base.h"

#include<glm/gtx/string_cast.hpp>



//spdlogÍ·ÎÄ¼þ¿â
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/fmt/fmt.h>
#include "spdlog/fmt/bundled/format.h"


namespace Kans {

	class  Log
	{
	public:
		static void Init();
		static void ShutDown();
		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger>  s_CoreLogger;
		static std::shared_ptr<spdlog::logger>  s_ClientLogger;
		
	};
	
}


template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<< (OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
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

