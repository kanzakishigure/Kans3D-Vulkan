#pragma once

#include <functional>
#include <filesystem>
#include "Kans3D/Core/Buffer.h"
namespace Kans
{
	struct FileSystemSpecification
	{
		std::filesystem::path WorkSpace;
		std::filesystem::path ResoucesDir;
		std::filesystem::path ShaderDir;
		std::filesystem::path FontsDir;
		std::filesystem::path ScriptDLLPath;
		std::filesystem::path AssetsDir;
		std::filesystem::path ProfileSpecicationDir;
	};


	class KansFileSystem 
	{
		public:
			static void Init(const std::filesystem::path& configPath);
			static void ShutDown();
			static bool  Exists(const std::string& filepath);
			static  Buffer ReadBytes(const std::filesystem::path& path);

			static const std::filesystem::path& GetShaderFolder() { return s_Specification->ShaderDir;}
			static const std::filesystem::path& GetAssetFolder() { return s_Specification->AssetsDir; }
			static const std::filesystem::path& GetFontsFolder() { return s_Specification->FontsDir; }
			static const std::filesystem::path& GetScriptDLLPath() { return s_Specification->ScriptDLLPath; }
		private:
			static FileSystemSpecification* s_Specification;


	};
}