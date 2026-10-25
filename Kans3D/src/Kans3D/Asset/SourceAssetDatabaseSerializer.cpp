#include "SourceAssetDatabaseSerializer.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>
#include <yaml-cpp/yaml.h>
#include "SourceImportConfigurationYaml.h"
#include "Storage/ProductFiles.h"

namespace Kans
{
    namespace
    {
        namespace fs                         = std::filesystem;
        constexpr uint64_t MaximumCacheBytes = 128 * 1024 * 1024;
        constexpr size_t   MaximumSources    = 100000;
        using Duration                       = fs::file_time_type::duration;
        static_assert(std::is_integral<Duration::rep>::value && std::is_signed<Duration::rep>::value &&
                      sizeof(Duration::rep) <= sizeof(int64_t));

        AssetError Error(AssetErrorCode code, const fs::path& file, const std::string& message)
        {
            return {code, file.generic_u8string() + ": " + message};
        }

        // C++17 file_clock epochs are implementation-defined. Reject snapshots from a
        // different standard library/platform instead of interpreting their timestamps.
        std::string ClockId()
        {
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

        std::variant<fs::path, AssetError> RootPath(const fs::path& root)
        {
            if (root.empty())
                return Error(AssetErrorCode::InvalidArgument, root, "Empty asset root");
            std::error_code ec;
            const auto      canonical = fs::canonical(root, ec);
            if (ec)
                return Error(AssetErrorCode::IoError, root, ec.message());
            const bool directory = fs::is_directory(canonical, ec);
            if (ec)
                return Error(AssetErrorCode::IoError, root, ec.message());
            if (!directory)
                return Error(AssetErrorCode::InvalidArgument, root, "Root is not a directory");
            return canonical;
        }

        bool Valid(const SourceAssetMetadata& data)
        {
            return data.IsValid() && (data.MetaVersion >= 1 && data.MetaVersion <= 3) && !(data.Exists && data.Missing);
        }

        std::variant<std::monostate, AssetError> CheckDestination(const fs::path& path)
        {
            std::error_code ec;
            const auto      status = fs::symlink_status(path, ec);
            if (ec && ec != std::errc::no_such_file_or_directory)
                return Error(AssetErrorCode::IoError, path, ec.message());
            if (status.type() == fs::file_type::not_found)
                return std::monostate {};
            if (!fs::is_regular_file(status))
                return Error(AssetErrorCode::InvalidData, path, "Cache destination must be a real regular file");
            return std::monostate {};
        }

        // Own only a directory created exclusively by this save. Never truncate a
        // pre-existing temporary file. The payload and destination share a filesystem.
        struct TemporaryFile
        {
            fs::path Directory;
            ~TemporaryFile()
            {
                if (Directory.empty())
                    return;
                std::error_code ec;
                fs::remove(Directory / "payload", ec);
                fs::remove(Directory, ec);
            }
        };
    } // namespace

    std::variant<SourceAssetDatabase, AssetError> SourceAssetDatabaseSerializer::Load(const fs::path& cachePath,
                                                                                      const fs::path& assetRoot)
    {
        if (cachePath.empty())
            return Error(AssetErrorCode::InvalidArgument, cachePath, "Empty cache path");
        const auto root = RootPath(assetRoot);
        if (const auto* error = std::get_if<AssetError>(&root))
            return *error;
        try
        {
            std::error_code statusError;
            const auto      status = fs::symlink_status(cachePath, statusError);
            if (status.type() == fs::file_type::not_found)
                return Error(AssetErrorCode::NotFound, cachePath, "Missing source cache");
            if (statusError)
                return Error(AssetErrorCode::IoError, cachePath, statusError.message());
            if (!fs::is_regular_file(status))
                return Error(AssetErrorCode::InvalidData, cachePath, "Source cache must be a real regular file");
            auto bytes = ReadProductFile(cachePath, MaximumCacheBytes);
            if (const auto* error = std::get_if<AssetError>(&bytes))
                return *error;
            const auto& dataBytes = std::get<std::vector<std::byte>>(bytes);
            if (dataBytes.empty())
                return Error(AssetErrorCode::InvalidData, cachePath, "Empty source cache");
            const auto documents =
                YAML::LoadAll(std::string(reinterpret_cast<const char*>(dataBytes.data()), dataBytes.size()));
            if (documents.size() != 1)
                return Error(AssetErrorCode::InvalidData, cachePath, "Cache requires one YAML document");
            const auto document = documents.front();
            if (!document.IsMap() || !document["version"].IsScalar())
                return Error(AssetErrorCode::InvalidData, cachePath, "Invalid cache header");
            const auto version = document["version"].as<uint32_t>();
            if (version != 1 && version != 2)
                return Error(AssetErrorCode::UnsupportedVersion, cachePath, "Unsupported cache version");
            auto fields = SourceMetadataDetail::CheckFields(
                document, {"version", "asset_root", "clock_id", "clock_period_num", "clock_period_den", "sources"});
            if (const auto* error = std::get_if<AssetError>(&fields))
                return *error;
            if (document["asset_root"].as<std::string>() != std::get<fs::path>(root).generic_u8string())
                return Error(AssetErrorCode::InvalidData, cachePath, "Cache belongs to another asset root");
            if (document["clock_id"].as<std::string>() != ClockId() ||
                document["clock_period_num"].as<int64_t>() != Duration::period::num ||
                document["clock_period_den"].as<int64_t>() != Duration::period::den)
                return Error(AssetErrorCode::UnsupportedVersion, cachePath, "Incompatible file clock");
            const auto sources = document["sources"];
            if (!sources.IsSequence() || sources.size() > MaximumSources)
                return Error(AssetErrorCode::InvalidData, cachePath, "sources must be a sequence");

            SourceAssetDatabase database;
            for (const auto& entry : sources)
            {
                if (!entry.IsMap())
                    return Error(AssetErrorCode::InvalidData, cachePath, "Source record must be a map");
                auto checked = version == 1 ? SourceMetadataDetail::CheckFields(entry,
                                                                                {"source_id",
                                                                                 "path",
                                                                                 "meta_version",
                                                                                 "exists",
                                                                                 "missing",
                                                                                 "file_size",
                                                                                 "last_write_ticks"}) :
                                              SourceMetadataDetail::CheckFields(entry,
                                                                                {"source_id",
                                                                                 "path",
                                                                                 "meta_version",
                                                                                 "exists",
                                                                                 "missing",
                                                                                 "file_size",
                                                                                 "last_write_ticks",
                                                                                 "import"});
                if (const auto* error = std::get_if<AssetError>(&checked))
                    return *error;
                if (!entry["source_id"].IsScalar() || entry["source_id"].Scalar().size() != 16 ||
                    !entry["path"].IsScalar() ||
                    entry["path"].Scalar().size() > SourceImportConfiguration::MaximumString ||
                    entry["path"].Scalar().find('\0') != std::string::npos)
                    return Error(AssetErrorCode::InvalidData, cachePath, "Invalid source ID length");
                const auto          id = entry["source_id"].as<std::string>();
                SourceAssetMetadata data;
                data.sourceAssetID = SourceAssetID::FromString(id);
                data.Path          = AssetPath(fs::u8path(entry["path"].as<std::string>()));
                data.MetaVersion   = entry["meta_version"].as<uint32_t>();
                if (version == 1 && data.MetaVersion != 1)
                    return Error(
                        AssetErrorCode::InvalidData, cachePath, "Legacy cache has unsupported metadata schema");
                if (version == 2)
                {
                    auto configuration = SourceMetadataDetail::DecodeImport(entry["import"]);
                    if (const auto* error = std::get_if<AssetError>(&configuration))
                        return *error;
                    data.Import = std::get<SourceImportConfiguration>(std::move(configuration));
                }
                data.Exists      = entry["exists"].as<bool>();
                data.Missing     = entry["missing"].as<bool>();
                data.FileSize    = entry["file_size"].as<uint64_t>();
                const auto ticks = entry["last_write_ticks"].as<int64_t>();
                if (ticks < std::numeric_limits<Duration::rep>::min() ||
                    ticks > std::numeric_limits<Duration::rep>::max() || !Valid(data))
                    return Error(AssetErrorCode::InvalidData, cachePath, "Invalid source record: " + id);
                data.last_edit_time   = fs::file_time_type(Duration(static_cast<Duration::rep>(ticks)));
                const auto registered = database.RegisterSource(data);
                if (const auto* error = std::get_if<AssetError>(&registered))
                    return Error(error->Code, cachePath, data.Path.ToString() + ": " + error->Message);
            }
            return database;
        }
        catch (const YAML::BadFile& exception)
        {
            std::error_code ec;
            const bool      exists = fs::exists(cachePath, ec);
            return Error(
                !ec && !exists ? AssetErrorCode::NotFound : AssetErrorCode::IoError, cachePath, exception.what());
        }
        catch (const YAML::Exception& exception)
        {
            return Error(AssetErrorCode::InvalidData, cachePath, exception.what());
        }
        catch (const fs::filesystem_error& exception)
        {
            return Error(AssetErrorCode::IoError, cachePath, exception.what());
        }
    }

    std::variant<std::monostate, AssetError> SourceAssetDatabaseSerializer::Save(const fs::path&            cachePath,
                                                                                 const fs::path&            assetRoot,
                                                                                 const SourceAssetDatabase& database)
    {
        if (cachePath.empty())
            return Error(AssetErrorCode::InvalidArgument, cachePath, "Empty cache path");
        const auto root = RootPath(assetRoot);
        if (const auto* error = std::get_if<AssetError>(&root))
            return *error;
        TemporaryFile temporary;
        try
        {
            if (std::get<fs::path>(root).generic_u8string().size() > SourceImportConfiguration::MaximumString)
                return Error(AssetErrorCode::InvalidArgument, cachePath, "Asset root exceeds string limit");
            auto destination = CheckDestination(cachePath);
            if (const auto* error = std::get_if<AssetError>(&destination))
                return *error;
            if (database.GetSourceCount() > MaximumSources)
                return Error(AssetErrorCode::InvalidData, cachePath, "Source cache record limit exceeded");
            auto records = database.GetSourceIDs();
            std::sort(
                records.begin(), records.end(), [](const auto& a, const auto& b) { return a.GetUUID() < b.GetUUID(); });
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "version" << YAML::Value << 2;
            out << YAML::Key << "asset_root" << YAML::Value << std::get<fs::path>(root).generic_u8string();
            out << YAML::Key << "clock_id" << YAML::Value << ClockId();
            out << YAML::Key << "clock_period_num" << YAML::Value << Duration::period::num;
            out << YAML::Key << "clock_period_den" << YAML::Value << Duration::period::den;
            out << YAML::Key << "sources" << YAML::Value << YAML::BeginSeq;
            for (const auto& id : records)
            {
                const auto data = database.FindByID(id);
                if (!Valid(data) || data.Path.ToString().size() > SourceImportConfiguration::MaximumString)
                    return Error(
                        AssetErrorCode::InvalidData, cachePath, "Invalid source record: " + data.Path.ToString());
                out << YAML::BeginMap;
                out << YAML::Key << "source_id" << YAML::Value << data.sourceAssetID.ToString();
                out << YAML::Key << "path" << YAML::Value << data.Path.ToString();
                out << YAML::Key << "meta_version" << YAML::Value << data.MetaVersion;
                out << YAML::Key << "exists" << YAML::Value << data.Exists;
                out << YAML::Key << "missing" << YAML::Value << data.Missing;
                out << YAML::Key << "file_size" << YAML::Value << data.FileSize;
                out << YAML::Key << "last_write_ticks" << YAML::Value
                    << static_cast<int64_t>(data.last_edit_time.time_since_epoch().count());
                out << YAML::Key << "import" << YAML::Value;
                SourceMetadataDetail::EncodeImport(out, data.Import);
                out << YAML::EndMap;
                if (out.size() > MaximumCacheBytes)
                    return Error(AssetErrorCode::InvalidData, cachePath, "Encoded source cache exceeds byte limit");
            }
            out << YAML::EndSeq << YAML::EndMap;
            if (!out.good())
                return Error(AssetErrorCode::InvalidData, cachePath, out.GetLastError());
            if (out.size() >= MaximumCacheBytes)
                return Error(AssetErrorCode::InvalidData, cachePath, "Encoded source cache exceeds byte limit");
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                fs::path candidate = cachePath;
                candidate += ".tmp-" + SourceAssetID::Generate().ToString();
                if (fs::create_directory(candidate))
                {
                    temporary.Directory = candidate;
                    break;
                }
            }
            if (temporary.Directory.empty())
                return Error(AssetErrorCode::IoError, cachePath, "Cannot create cache temporary directory");
            const auto             payload = temporary.Directory / "payload";
            std::vector<std::byte> bytes;
            bytes.reserve(out.size() + 1);
            for (size_t i = 0; i < out.size(); ++i)
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(out.c_str()[i])));
            bytes.push_back(std::byte {'\n'});
            auto written = WriteProductFile(payload, bytes);
            if (const auto* error = std::get_if<AssetError>(&written))
                return *error;
            destination = CheckDestination(cachePath);
            if (const auto* error = std::get_if<AssetError>(&destination))
                return *error;
            auto replaced = ReplaceProductCatalog(payload, cachePath);
            if (const auto* error = std::get_if<AssetError>(&replaced))
                return *error;
            return std::monostate {};
        }
        catch (const YAML::Exception& exception)
        {
            return Error(AssetErrorCode::InvalidData, cachePath, exception.what());
        }
        catch (const fs::filesystem_error& exception)
        {
            return Error(AssetErrorCode::IoError, cachePath, exception.what());
        }
    }
} // namespace Kans
