#include"hzpch.h"
#include "Log.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace Kans
{

	std::shared_ptr<spdlog::logger>  Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger>  Log::s_ClientLogger;

	void Log::Init()
	{
		spdlog::set_pattern("%^ [%T] %n: %v %$");
		//颜色开始标记 时间戳 logger类型 实际文本 颜色结束标记
		
		
		s_CoreLogger = spdlog::stdout_color_mt("KANS3D-CORE");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = spdlog::stdout_color_mt("APPLICATION");
		s_ClientLogger->set_level(spdlog::level::trace);

    }
}
