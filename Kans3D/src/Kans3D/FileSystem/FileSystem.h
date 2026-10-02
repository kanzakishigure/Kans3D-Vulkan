#pragma once

#include "Kans3D/Core/ByteBuffer.h"
#include <filesystem>
#include <functional>

namespace Kans {
class FileSystemSpecification {
public:
  FileSystemSpecification(const std::filesystem::path &configPath);

private:
  void Serialize();
  void Deserialize();

  bool IsLegalWorkSpace(std::filesystem::path path);
  void GenRootPathHash(std::filesystem::path path);

private:
  std::filesystem::path m_WorkSpace;
  std::filesystem::path m_ResoucesDir;
  std::filesystem::path m_FontsPath;
  std::filesystem::path m_ScriptAssemblyPath;
  std::filesystem::path m_AssetsRoot;
  std::filesystem::path m_ProfileSpecicationPath;
  std::filesystem::path m_ShaderPath;
  std::filesystem::path m_ShaderCachePath;
  std::filesystem::path m_CachePath;
  std::string m_RootPathHashValue;

  std::filesystem::path m_ConfigPath;

  friend class KansFileSystem;
};

class KansFileSystem {
public:
  static void Init(const std::filesystem::path &configPath);
  static void ShutDown();

  // scope cache
  static void BenginCacheFile() { InitFileCache(); }
  static void EndCacheFile() { ResetFileCache(); }

  static bool Exists(const std::string &filepath);
  static bool Exists(const std::filesystem::path &filepath);

  static bool CreateDirectories(std::string directory);
  static bool CreateDirectories(const std::filesystem::path &directory);

  static Buffer ReadBytes(const std::filesystem::path &path);
  static std::string ReadFileSkipBOM(const std::filesystem::path &path);

public:
  static const std::filesystem::path &GetShaderFolder() {
    return s_Specification->m_ShaderPath;
  }

  static const std::filesystem::path &GetShaderCacheFolder() {
    return s_Specification->m_ShaderCachePath;
  }

  static const std::filesystem::path &GetAssetFolder() {
    return s_Specification->m_AssetsRoot;
  }
  static const std::filesystem::path &GetFontsFolder() {
    return s_Specification->m_FontsPath;
  }
  static const std::filesystem::path &GetScriptAssemblyPath() {
    return s_Specification->m_ScriptAssemblyPath;
  }

  static const std::filesystem::path &GetResoucesFolder() {
    return s_Specification->m_ResoucesDir;
  }

private:
  static void InitFileCache();
  static void ResetFileCache();

private:
  static FileSystemSpecification *s_Specification;
};

// in scope we will load file from fileCache
// but ScopeFileCacher is not thread-safe
// because the KansFileSystem is not thread-safe
class ScopeFileCacher {
  ScopeFileCacher() { KansFileSystem::BenginCacheFile(); }
  ~ScopeFileCacher() { KansFileSystem::EndCacheFile(); }
};
} // namespace Kans
