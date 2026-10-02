#include "SourceAssetDatabaseSerializer.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>
#include <yaml-cpp/yaml.h>

namespace Kans {
namespace {
namespace fs = std::filesystem;
using Duration = fs::file_time_type::duration;
static_assert(std::is_integral<Duration::rep>::value &&
              std::is_signed<Duration::rep>::value &&
              sizeof(Duration::rep) <= sizeof(int64_t));

AssetError Error(AssetErrorCode code, const fs::path &file,
                 const std::string &message) {
  return {code, file.generic_string() + ": " + message};
}

// C++17 file_clock epochs are implementation-defined. Reject snapshots from a
// different standard library/platform instead of interpreting their timestamps.
std::string ClockId() {
#if defined(_WIN32)
  std::string id = "windows/";
#elif defined(__linux__)
  std::string id = "linux/";
#else
  std::string id = "other/";
#endif
#if defined(__GLIBCXX__)
  return id + "libstdc++/" + std::to_string(__GLIBCXX__);
#elif defined(_LIBCPP_VERSION)
  return id + "libc++/" + std::to_string(_LIBCPP_VERSION);
#elif defined(_MSC_VER)
  return id + "msvc/" + std::to_string(_MSC_VER);
#else
  return id + "unknown";
#endif
}

std::variant<fs::path, AssetError> RootPath(const fs::path &root) {
  if (root.empty())
    return Error(AssetErrorCode::InvalidArgument, root, "Empty asset root");
  std::error_code ec;
  const auto canonical = fs::canonical(root, ec);
  if (ec)
    return Error(AssetErrorCode::IoError, root, ec.message());
  const bool directory = fs::is_directory(canonical, ec);
  if (ec)
    return Error(AssetErrorCode::IoError, root, ec.message());
  if (!directory)
    return Error(AssetErrorCode::InvalidArgument, root,
                 "Root is not a directory");
  return canonical;
}

bool Valid(const SourceAssetMetadata &data) {
  return data.IsValid() && data.MetaVersion == 1 &&
         !(data.Exists && data.Missing);
}

// Own only a directory created exclusively by this save. Never truncate a
// pre-existing temporary file. The payload and destination share a filesystem.
struct TemporaryFile {
  fs::path Directory;
  ~TemporaryFile() {
    if (Directory.empty())
      return;
    std::error_code ec;
    fs::remove(Directory / "payload", ec);
    fs::remove(Directory, ec);
  }
};
} // namespace

std::variant<SourceAssetDatabase, AssetError>
SourceAssetDatabaseSerializer::Load(const fs::path &cachePath,
                                    const fs::path &assetRoot) {
  if (cachePath.empty())
    return Error(AssetErrorCode::InvalidArgument, cachePath,
                 "Empty cache path");
  const auto root = RootPath(assetRoot);
  if (const auto *error = std::get_if<AssetError>(&root))
    return *error;
  try {
    const auto document = YAML::LoadFile(cachePath.string());
    if (!document.IsMap() || !document["version"].IsScalar())
      return Error(AssetErrorCode::InvalidData, cachePath,
                   "Invalid cache header");
    if (document["version"].as<uint32_t>() != 1)
      return Error(AssetErrorCode::UnsupportedVersion, cachePath,
                   "Unsupported cache version");
    if (document["asset_root"].as<std::string>() !=
        std::get<fs::path>(root).generic_string())
      return Error(AssetErrorCode::InvalidData, cachePath,
                   "Cache belongs to another asset root");
    if (document["clock_id"].as<std::string>() != ClockId() ||
        document["clock_period_num"].as<int64_t>() != Duration::period::num ||
        document["clock_period_den"].as<int64_t>() != Duration::period::den)
      return Error(AssetErrorCode::UnsupportedVersion, cachePath,
                   "Incompatible file clock");
    const auto sources = document["sources"];
    if (!sources.IsSequence())
      return Error(AssetErrorCode::InvalidData, cachePath,
                   "sources must be a sequence");

    SourceAssetDatabase database;
    for (const auto &entry : sources) {
      if (!entry.IsMap())
        return Error(AssetErrorCode::InvalidData, cachePath,
                     "Source record must be a map");
      const auto id = entry["source_id"].as<std::string>();
      if (id.size() != 16)
        return Error(AssetErrorCode::InvalidData, cachePath,
                     "Invalid source ID length");
      SourceAssetMetadata data;
      data.sourceAssetID = SourceAssetID::FromString(id);
      data.Path = AssetPath(entry["path"].as<std::string>());
      data.MetaVersion = entry["meta_version"].as<uint32_t>();
      data.Exists = entry["exists"].as<bool>();
      data.Missing = entry["missing"].as<bool>();
      data.FileSize = entry["file_size"].as<uint64_t>();
      const auto ticks = entry["last_write_ticks"].as<int64_t>();
      if (ticks < std::numeric_limits<Duration::rep>::min() ||
          ticks > std::numeric_limits<Duration::rep>::max() || !Valid(data))
        return Error(AssetErrorCode::InvalidData, cachePath,
                     "Invalid source record: " + id);
      data.last_edit_time =
          fs::file_time_type(Duration(static_cast<Duration::rep>(ticks)));
      const auto registered = database.RegisterSource(data);
      if (const auto *error = std::get_if<AssetError>(&registered))
        return Error(error->Code, cachePath,
                     data.Path.ToString() + ": " + error->Message);
    }
    return database;
  } catch (const YAML::BadFile &exception) {
    std::error_code ec;
    const bool exists = fs::exists(cachePath, ec);
    return Error(!ec && !exists ? AssetErrorCode::NotFound
                                : AssetErrorCode::IoError,
                 cachePath, exception.what());
  } catch (const YAML::Exception &exception) {
    return Error(AssetErrorCode::InvalidData, cachePath, exception.what());
  } catch (const fs::filesystem_error &exception) {
    return Error(AssetErrorCode::IoError, cachePath, exception.what());
  }
}

std::variant<std::monostate, AssetError>
SourceAssetDatabaseSerializer::Save(const fs::path &cachePath,
                                    const fs::path &assetRoot,
                                    const SourceAssetDatabase &database) {
  if (cachePath.empty())
    return Error(AssetErrorCode::InvalidArgument, cachePath,
                 "Empty cache path");
  const auto root = RootPath(assetRoot);
  if (const auto *error = std::get_if<AssetError>(&root))
    return *error;
  TemporaryFile temporary;
  try {
    auto records = database.GetAllSources();
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
      return a.Path.ToString() < b.Path.ToString();
    });
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << 1;
    out << YAML::Key << "asset_root" << YAML::Value
        << std::get<fs::path>(root).generic_string();
    out << YAML::Key << "clock_id" << YAML::Value << ClockId();
    out << YAML::Key << "clock_period_num" << YAML::Value
        << Duration::period::num;
    out << YAML::Key << "clock_period_den" << YAML::Value
        << Duration::period::den;
    out << YAML::Key << "sources" << YAML::Value << YAML::BeginSeq;
    for (const auto &data : records) {
      if (!Valid(data))
        return Error(AssetErrorCode::InvalidData, cachePath,
                     "Invalid source record: " + data.Path.ToString());
      out << YAML::BeginMap;
      out << YAML::Key << "source_id" << YAML::Value
          << data.sourceAssetID.ToString();
      out << YAML::Key << "path" << YAML::Value << data.Path.ToString();
      out << YAML::Key << "meta_version" << YAML::Value << data.MetaVersion;
      out << YAML::Key << "exists" << YAML::Value << data.Exists;
      out << YAML::Key << "missing" << YAML::Value << data.Missing;
      out << YAML::Key << "file_size" << YAML::Value << data.FileSize;
      out << YAML::Key << "last_write_ticks" << YAML::Value
          << static_cast<int64_t>(
                 data.last_edit_time.time_since_epoch().count());
      out << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::EndMap;
    if (!out.good())
      return Error(AssetErrorCode::InvalidData, cachePath, out.GetLastError());
    for (int attempt = 0; attempt < 8; ++attempt) {
      fs::path candidate = cachePath;
      candidate += ".tmp-" + SourceAssetID::Generate().ToString();
      if (fs::create_directory(candidate)) {
        temporary.Directory = candidate;
        break;
      }
    }
    if (temporary.Directory.empty())
      return Error(AssetErrorCode::IoError, cachePath,
                   "Cannot create cache temporary directory");
    const auto payload = temporary.Directory / "payload";
    std::ofstream stream(payload, std::ios::binary | std::ios::trunc);
    if (!stream)
      return Error(AssetErrorCode::IoError, cachePath,
                   "Cannot open cache temporary file");
    stream << out.c_str() << '\n';
    stream.flush();
    if (!stream)
      return Error(AssetErrorCode::IoError, cachePath,
                   "Cannot write cache temporary file");
    stream.close();
    if (!stream)
      return Error(AssetErrorCode::IoError, cachePath,
                   "Cannot close cache temporary file");
    fs::rename(payload, cachePath);
    return std::monostate{};
  } catch (const YAML::Exception &exception) {
    return Error(AssetErrorCode::InvalidData, cachePath, exception.what());
  } catch (const fs::filesystem_error &exception) {
    return Error(AssetErrorCode::IoError, cachePath, exception.what());
  }
}
} // namespace Kans
