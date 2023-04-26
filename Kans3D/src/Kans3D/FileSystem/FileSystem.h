#pragma once

#include <functional>
#include <filesystem>
#include "Kans3D/Core/byteBuffer.h"
namespace Kans
{
	struct FileSystemSpecification
	{
		std::filesystem::path WorkSpace;
		std::filesystem::path ResoucesDir;
		std::filesystem::path FontsDir;
		std::filesystem::path ScriptAssemblyPath;
		std::filesystem::path AssetsDir;
		std::filesystem::path ProfileSpecicationDir;
		std::filesystem::path ShaderDir;
		std::filesystem::path ShaderCacheDir;
		std::filesystem::path CacheDir;

	};


	class KansFileSystem 
	{
	public:
		static void Init(const std::filesystem::path& configPath);
		static void ShutDown();


		//scope cache
		static void BenginCacheFile() { InitFileCache(); }
		static void EndCacheFile() { ResetFileCache(); }

		


		static bool Exists(const std::string& filepath);
		static bool Exists(const std::filesystem::path& filepath);

		static bool CreateDirectories(std::string directory);
		static bool CreateDirectories(const std::filesystem::path& directory);

		static  Buffer ReadBytes(const std::filesystem::path& path);
		static std::string ReadFileSkipBOM(const std::filesystem::path& path);

	public:
		static const std::filesystem::path& GetShaderFolder() { return s_Specification->ShaderDir;}
		static const std::filesystem::path& GetShaderCacheFolder() { return s_Specification->ShaderCacheDir; }

		static const std::filesystem::path& GetAssetFolder() { return s_Specification->AssetsDir; }
		static const std::filesystem::path& GetFontsFolder() { return s_Specification->FontsDir; }
		static const std::filesystem::path& GetScriptAssemblyPath() { return s_Specification->ScriptAssemblyPath; }

		static const std::filesystem::path& GetResoucesFolder() { return s_Specification->ResoucesDir; }
	private:
		static void InitFileCache();
		static void ResetFileCache();
	private:
			static FileSystemSpecification* s_Specification;


	};

	// in scope we will load file from fileCache
	// but ScopeFileCacher is not thread-safe
	// because the KansFileSystem is not thread-safe
	class ScopeFileCacher
	{
		ScopeFileCacher()
		{
			KansFileSystem::BenginCacheFile();
		}
		~ScopeFileCacher()
		{
			KansFileSystem::EndCacheFile();
		}
	};
}