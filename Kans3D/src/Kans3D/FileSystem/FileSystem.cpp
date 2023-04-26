#include "kspch.h"
#include "Kans3D/FileSystem/FileSystem.h"
namespace Kans{


	int SkipBOM(std::istream& in)
	{
		char test[4] = { 0 };
		in.seekg(0, std::ios::beg);
		in.read(test, 3);

		if (strcmp(test, "\xEF\xBB\xBF") == 0)
		{
			in.seekg(3, std::ios::beg);
			return 3;
		}

		in.seekg(0, std::ios::beg);
		return 0;
	}

	void KansFileSystem::InitFileCache()
	{

	}

	void KansFileSystem::ResetFileCache()
	{

	}

	FileSystemSpecification* KansFileSystem::s_Specification = nullptr;
	void KansFileSystem::Init(const std::filesystem::path& configPath)
	{

		CORE_ASSERT(!configPath.empty(), "FileSystm init fail");
		s_Specification = new FileSystemSpecification();
		std::ifstream config_file(configPath);
		std::string config_line;
		while (std::getline(config_file,config_line))
		{
			size_t spos = config_line.find_first_of('=');
			if (spos != config_line.npos)
			{
				std::string key = config_line.substr(0, spos);
				std::string value = config_line.substr(spos + 1, config_line.length());
				if  (key == "WorkDir")
				{
					s_Specification->WorkSpace = value;
				}
				else if (key == "ResoucesDir")
				{
					s_Specification->ResoucesDir = value;
				}
				else if (key == "ShaderDir")
				{
					s_Specification->ShaderDir = value;
				}
				else if (key == "FontsDir")
				{
					s_Specification->FontsDir = value;
				}
				else if (key == "ScriptDLL")
				{
					s_Specification->ScriptAssemblyPath = value;
				}
				else if (key == "assetsDir")
				{
					s_Specification->AssetsDir= value;
				}
				else if (key == "ProfileSpecicationDir")
				{
					s_Specification->ProfileSpecicationDir = value;
				}
				else if (key == "ShaderCacheDir")
				{
					s_Specification->ShaderCacheDir = value;
				}
				else if (key == "CacheDir")
				{
					s_Specification->CacheDir = value;
				}
			}
			
		}

		if (!s_Specification->WorkSpace.empty())
		{
			std::filesystem::path tmpPath = std::filesystem::current_path();
			std::filesystem::path parentPath;
			while (tmpPath.has_parent_path())
			{
				tmpPath = tmpPath.parent_path();
				if (tmpPath.filename() == "Vulkan_Engine")
				{
					parentPath = tmpPath;
					break;
				}
			}
			if (!parentPath.empty())
			{
				std::filesystem::path workPath = parentPath /= s_Specification->WorkSpace;
				//std::filesystem::current_path(workPath);
			}
		}
		
	}

	bool KansFileSystem::Exists(const std::string& filepath)
	{
		return std::filesystem::exists(filepath);
	}

	bool KansFileSystem::Exists(const std::filesystem::path& filepath)
	{
		return std::filesystem::exists(filepath);
	}

	bool KansFileSystem::CreateDirectories(std::string directory)
	{
		return std::filesystem::create_directories(std::filesystem::path(directory));
	}

	bool KansFileSystem::CreateDirectories(const std::filesystem::path& directory)
	{
		return std::filesystem::create_directories(directory);
	}

	Kans::Buffer KansFileSystem::ReadBytes(const std::filesystem::path& path)
	{
		Buffer buffer;
		std::ifstream stream(path,std::ios::binary|std::ios::ate);
		CORE_ASSERT(stream,"Load file Byte stream fail : "+path.string());

		std::streampos end = stream.tellg();
		stream.seekg(0,std::ios::beg);
		size_t size = end- stream.tellg();

		buffer.Allocate(size);
		stream.read((char*)buffer.Data, size);
		stream.close();

		return buffer;

	}

	std::string KansFileSystem::ReadFileSkipBOM(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);

		CORE_ASSERT(stream, "Load file Byte stream fail : " + path.string());

		stream.seekg(0, std::ios::end);
		auto filesize = stream.tellg();

		std::string result;
		result.resize(filesize);
		stream.seekg(0, std::ios::beg);
		stream.read(result.data(), filesize);
		stream.close();
		return result;

	}

	void KansFileSystem::ShutDown()
	{
		delete s_Specification;
	}

}