#include "ImporterRegistry.h"
#include <algorithm>
#include <cctype>

namespace Kans {
namespace {
std::string Normalize(std::string extension) {
  std::transform(extension.begin(), extension.end(), extension.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}
}
std::variant<std::monostate, AssetError> ImporterRegistry::Register(
    const std::string &extension, std::shared_ptr<AssetImporterBase> importer) {
  if (!importer || extension.size() < 2 || extension.front() != '.' ||
      extension.find_first_of("/\\") != std::string::npos)
    return AssetError{AssetErrorCode::InvalidArgument, "Invalid importer registration"};
  if (!m_Importers.emplace(Normalize(extension), std::move(importer)).second)
    return AssetError{AssetErrorCode::InvalidArgument, "Importer already registered for extension"};
  return std::monostate{};
}
std::shared_ptr<const AssetImporterBase>
ImporterRegistry::Find(const std::filesystem::path &sourceFile) const {
  const auto it = m_Importers.find(Normalize(sourceFile.extension().string()));
  return it == m_Importers.end() ? nullptr : it->second;
}
} // namespace Kans
