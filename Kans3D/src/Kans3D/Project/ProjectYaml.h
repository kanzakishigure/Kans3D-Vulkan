#pragma once
#include <optional>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/Storage/ProductFiles.h"

namespace Kans::ProjectYaml
{
    inline AssetError Error(AssetErrorCode code, const std::filesystem::path& file, const std::string& message)
    {
        return {code, file.generic_u8string() + ": " + message};
    }

    // Own an exclusively created sibling directory; never overwrite someone else's temporary file.
    inline std::optional<AssetError> Save(const std::filesystem::path& file, const YAML::Emitter& out)
    {
        namespace fs = std::filesystem;
        struct Temporary
        {
            fs::path Directory;
            ~Temporary()
            {
                if (!Directory.empty())
                {
                    std::error_code ec;
                    fs::remove_all(Directory, ec);
                }
            }
        } temporary;
        try
        {
            if (!out.good())
                return Error(AssetErrorCode::InvalidData, file, out.GetLastError());
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                auto candidate = file;
                candidate += ".tmp-" + SourceAssetID::Generate().ToString();
                if (fs::create_directory(candidate))
                {
                    temporary.Directory = candidate;
                    break;
                }
            }
            if (temporary.Directory.empty())
                return Error(AssetErrorCode::IoError, file, "Cannot create temporary directory");
            std::vector<std::byte> bytes;
            for (size_t i = 0; i < out.size(); ++i)
                bytes.push_back(static_cast<std::byte>(out.c_str()[i]));
            bytes.push_back(std::byte {'\n'});
            const auto payload = temporary.Directory / "payload";
            auto       written = WriteProductFile(payload, bytes);
            if (auto* error = std::get_if<AssetError>(&written))
                return Error(error->Code, file, error->Message);
            auto replaced = ReplaceProductCatalog(payload, file);
            if (auto* error = std::get_if<AssetError>(&replaced))
                return Error(error->Code, file, error->Message);
            return std::nullopt;
        }
        catch (const fs::filesystem_error& e)
        {
            return Error(AssetErrorCode::IoError, file, e.what());
        }
    }
} // namespace Kans::ProjectYaml
