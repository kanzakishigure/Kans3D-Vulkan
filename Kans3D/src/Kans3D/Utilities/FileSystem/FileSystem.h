#pragma once

#include <functional>
#include <filesystem>
namespace Kans
{
	class KansFileSystem 
	{
		public:
			static bool Exists(const std::string& filepath);
	};
}