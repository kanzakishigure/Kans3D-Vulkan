#include "kspch.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Core/Hash.h"
#include <set>
namespace Kans{


#define DESERIALIZE_FILESYSTEMSPEC_PROPERTY(valueName,value,ostream) if(!value.empty())(ostream<<#valueName <<"="<<value<<"\n")

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
		s_Specification = new FileSystemSpecification(configPath);
		s_Specification->Serialize();

		if (!s_Specification->m_WorkSpace.empty())
		{
			std::filesystem::path tempPath = std::filesystem::current_path();
			std::filesystem::path parentPath;
			while (tempPath.has_parent_path())
			{
				tempPath = tempPath.parent_path();

				//we should use some value like hash value to check is the path is legal
				if (s_Specification->IsLegalWorkSpace(tempPath))
				{
					parentPath = tempPath;
					break;
				}
				if (tempPath == tempPath.parent_path())
				{
					break;
				}

			}
			if (!parentPath.empty())
			{
				std::filesystem::path workPath = parentPath / s_Specification->m_WorkSpace;
				std::filesystem::current_path(workPath);
				if (s_Specification->m_RootPathHashValue.empty())
				{
					s_Specification->GenRootPathHash(parentPath);
					s_Specification->Deserialize();
				}
			}
			CORE_INFO_TAG("Filesystem", "Curent workspace :[{0}]", std::filesystem::current_path());
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

	FileSystemSpecification::FileSystemSpecification(const std::filesystem::path& configPath)
		:m_ConfigPath(configPath)
	{

	}

	bool FileSystemSpecification::IsLegalWorkSpace(std::filesystem::path path)
	{
		std::set<std::string> whiteList;
		whiteList.emplace(".git");


		std::filesystem::directory_iterator it(path);
		std::string hashString;
		for (const auto& filepath : it)
		{
			std::string filename = filepath.path().filename().string();
			if (whiteList.find(filename) == whiteList.end())
			{
				hashString += filename;
			}

		}
		std::string HashValue = Hash::GenerateMD5Hash(hashString);
		return HashValue == m_RootPathHashValue;

	}

	void FileSystemSpecification::GenRootPathHash(std::filesystem::path rootpath)
	{
		std::set<std::string> whiteList;
		whiteList.emplace(".git");
		std::filesystem::directory_iterator it(rootpath);
		std::string hashString;
		for (const auto& filepath : it)
		{
			std::string filename = filepath.path().filename().string();
			if (whiteList.find(filename) == whiteList.end())
			{
				hashString += filename;
			}

		}
		m_RootPathHashValue = Hash::GenerateMD5Hash(hashString);
	}
	void FileSystemSpecification::Serialize()
	{
		std::ifstream config_file(m_ConfigPath);
		std::string config_line;

		while (std::getline(config_file, config_line))
		{
			size_t spos = config_line.find_first_of('=');
			if (spos != config_line.npos)
			{
				std::string key = config_line.substr(0, spos);
				std::string value = config_line.substr(spos + 1, config_line.length());
				if ("WorkSpace" == key)
				{
					m_WorkSpace = value;
				}
				else if ("ResoucesDir" == key)
				{
					m_ResoucesDir = value;
				}
				else if ("ShaderDir" == key)
				{
					m_ShaderDir = value;
				}
				else if ("FontsDir" == key)
				{
					m_FontsDir = value;
				}
				else if ("ScriptAssemblyPath" == key)
				{
					m_ScriptAssemblyPath = value;
				}
				else if ("AssetsDir" == key)
				{
					m_AssetsDir = value;
				}
				else if ("ProfileSpecicationDir" == key)
				{
					m_ProfileSpecicationDir = value;
				}
				else if ("ShaderCacheDir" == key)
				{
					m_ShaderCacheDir = value;
				}
				else if ("CacheDir" == key)
				{
					m_CacheDir = value;
				}
				else if ("RootPathHashValue" == key)
				{
					m_RootPathHashValue = value;
				}
			}

		}
	}

	void FileSystemSpecification::Deserialize()
	{
		std::ofstream out_config_file(m_ConfigPath,std::ios::out|std::ios::trunc);
		if (!out_config_file.good())
		{
			CORE_TRACE_TAG("[Filesystem]", "deserialize fileSystemSpecification fail");
			return;
		}

		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(WorkSpace,m_WorkSpace.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(ResoucesDir,m_ResoucesDir.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(FontsDir, m_FontsDir.string(),out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(ScriptAssemblyPath,m_ScriptAssemblyPath.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(AssetsDir,m_AssetsDir.string(),out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(ProfileSpecicationDir,m_ProfileSpecicationDir.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(ShaderDir,m_ShaderDir.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(CacheDir,m_CacheDir.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(ShaderCacheDir,m_ShaderCacheDir.string(), out_config_file);
		DESERIALIZE_FILESYSTEMSPEC_PROPERTY(RootPathHashValue,m_RootPathHashValue, out_config_file);
		

		out_config_file.close();
	}

}