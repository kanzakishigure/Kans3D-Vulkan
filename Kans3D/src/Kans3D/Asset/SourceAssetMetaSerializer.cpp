#include "Kans3D/Asset/SourceAssetMetaSerializer.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <string>
#include <system_error>

namespace Kans
{
    namespace
    {

        constexpr uint32_t kCurrentMetaVersion = 1;

        AssetError MakeMetaError(AssetErrorCode code, const std::filesystem::path& path, const std::string& message)
        {
            return AssetError {code, path.string() + ": " + message};
        }

    } // namespace

    std::variant<SourceAssetMetadata, AssetError> SourceAssetMetaSerializer::Load(const std::filesystem::path& metaPath,
                                                                                  const AssetPath& sourcePath)
    {
        if (metaPath.empty())
            return MakeMetaError(AssetErrorCode::InvalidArgument, metaPath, "Empty .kmeta path");
        if (!sourcePath.IsValid())
            return MakeMetaError(AssetErrorCode::InvalidArgument, metaPath, "Invalid source asset path");

        try
        {
            const YAML::Node root = YAML::LoadFile(metaPath.string());
            if (!root.IsMap())
                return MakeMetaError(AssetErrorCode::InvalidData, metaPath, ".kmeta root must be a map");

            const YAML::Node versionNode = root["version"];
            const YAML::Node idNode      = root["source_id"];
            if (!versionNode.IsScalar() || !idNode.IsScalar())
                return MakeMetaError(
                    AssetErrorCode::InvalidData, metaPath, ".kmeta requires scalar version and source_id");

            const uint32_t version = versionNode.as<uint32_t>();
            if (version != kCurrentMetaVersion)
                return MakeMetaError(AssetErrorCode::UnsupportedVersion,
                                     metaPath,
                                     "Unsupported .kmeta version: " + std::to_string(version));

            const std::string idText = idNode.as<std::string>();
            if (idText.size() != 16)
                return MakeMetaError(AssetErrorCode::InvalidData, metaPath, "Invalid .kmeta source_id length");
            const SourceAssetID id = SourceAssetID::FromString(idText);
            if (!id.IsValid())
                return MakeMetaError(AssetErrorCode::InvalidData, metaPath, "Invalid .kmeta source_id");

            SourceAssetMetadata metadata;
            metadata.sourceAssetID = id;
            metadata.Path          = sourcePath;
            metadata.MetaVersion   = version;
            return metadata;
        }
        catch (const YAML::BadFile& exception)
        {
            std::error_code statusError;
            const bool      exists = std::filesystem::exists(metaPath, statusError);
            const auto      code   = !statusError && !exists ? AssetErrorCode::NotFound : AssetErrorCode::IoError;
            return MakeMetaError(code, metaPath, "Cannot open .kmeta: " + std::string(exception.what()));
        }
        catch (const YAML::Exception& exception)
        {
            return MakeMetaError(
                AssetErrorCode::InvalidData, metaPath, "Cannot parse .kmeta: " + std::string(exception.what()));
        }
        catch (const std::exception& exception)
        {
            return MakeMetaError(
                AssetErrorCode::IoError, metaPath, "Cannot load .kmeta: " + std::string(exception.what()));
        }
    }

    std::variant<std::monostate, AssetError> SourceAssetMetaSerializer::Save(const std::filesystem::path& metaPath,
                                                                             const SourceAssetMetadata&   metadata)
    {
        if (metaPath.empty())
            return MakeMetaError(AssetErrorCode::InvalidArgument, metaPath, "Empty .kmeta path");
        if (!metadata.IsValid())
            return MakeMetaError(AssetErrorCode::InvalidArgument, metaPath, "Invalid source asset metadata");
        if (metadata.MetaVersion != kCurrentMetaVersion)
            return MakeMetaError(AssetErrorCode::UnsupportedVersion,
                                 metaPath,
                                 "Unsupported .kmeta version: " + std::to_string(metadata.MetaVersion));

        std::filesystem::path temporaryPath;
        bool                  temporaryCreated    = false;
        const auto            removeTemporaryFile = [&]() {
            if (temporaryCreated)
            {
                std::error_code ignored;
                std::filesystem::remove(temporaryPath, ignored);
            }
        };
        try
        {
            YAML::Emitter emitter;
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "version" << YAML::Value << metadata.MetaVersion;
            emitter << YAML::Key << "source_id" << YAML::Value << metadata.sourceAssetID.ToString();
            emitter << YAML::EndMap;
            if (!emitter.good())
                return MakeMetaError(AssetErrorCode::InvalidData,
                                     metaPath,
                                     ".kmeta YAML emission failed: " + std::string(emitter.GetLastError()));

            // Write beside the destination so failure leaves the existing file intact.
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                temporaryPath = metaPath;
                temporaryPath += ".tmp-" + SourceAssetID::Generate().ToString();
                if (!std::filesystem::exists(temporaryPath))
                    break;
                temporaryPath.clear();
            }
            if (temporaryPath.empty())
                return MakeMetaError(AssetErrorCode::IoError, metaPath, "Cannot allocate temporary .kmeta filename");

            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!output)
                return MakeMetaError(AssetErrorCode::IoError, metaPath, "Cannot open temporary .kmeta file");
            temporaryCreated = true;
            output << emitter.c_str() << '\n';
            output.flush();
            if (!output)
            {
                output.close();
                removeTemporaryFile();
                return MakeMetaError(AssetErrorCode::IoError, metaPath, "Cannot write temporary .kmeta file");
            }
            output.close();
            if (!output)
            {
                removeTemporaryFile();
                return MakeMetaError(AssetErrorCode::IoError, metaPath, "Cannot close temporary .kmeta file");
            }

            std::error_code renameError;
            std::filesystem::rename(temporaryPath, metaPath, renameError);
            if (renameError)
            {
                removeTemporaryFile();
                return MakeMetaError(
                    AssetErrorCode::IoError, metaPath, "Cannot replace .kmeta: " + renameError.message());
            }
            return std::monostate {};
        }
        catch (const std::exception& exception)
        {
            removeTemporaryFile();
            return MakeMetaError(
                AssetErrorCode::IoError, metaPath, "Cannot save .kmeta: " + std::string(exception.what()));
        }
    }

} // namespace Kans
