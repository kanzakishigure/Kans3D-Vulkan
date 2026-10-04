#include "kspch.h"
#include "SourceAssetDatabase.h"
#include <map>
#include <tuple>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/SourceAssetMetaData.h"

namespace Kans
{
    std::vector<SourceAssetMetadata> SourceAssetDatabase::GetAllSources() const
    {
        std::vector<SourceAssetMetadata> records;
        records.reserve(m_DataBase.size());
        for (const auto& entry : m_DataBase)
            records.push_back(entry.second);
        return records;
    }

    std::variant<std::monostate, AssetError> SourceAssetDatabase::RegisterSource(const SourceAssetMetadata& data)
    {
        if (!data.IsValid())
        {
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid source asset metadata"};
        }
        if (Contains(data.sourceAssetID))
        {
            return AssetError {AssetErrorCode::DuplicateID, "Source asset ID already registered"};
        }
        if (Contains(data.Path))
        {
            return AssetError {AssetErrorCode::DuplicatePath, "Source asset path already registered"};
        }

        const auto inserted = m_DataBase.emplace(data.sourceAssetID, data);
        try
        {
            if (!m_AssetPaths.emplace(data.Path, data.sourceAssetID).second)
            {
                m_DataBase.erase(inserted.first);
                return AssetError {AssetErrorCode::DuplicatePath, "Source asset path already registered"};
            }
        }
        catch (...)
        {
            // Preserve both indexes if allocating/copying the second entry fails.
            m_DataBase.erase(data.sourceAssetID);
            throw;
        }
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> SourceAssetDatabase::UnregisterSource(SourceAssetID id)
    {
        if (!id.IsValid())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid source asset ID"};
        auto it = m_DataBase.find(id);
        if (it == m_DataBase.end())
            return AssetError {AssetErrorCode::NotFound, "Source asset ID is not registered"};
        m_AssetPaths.erase(it->second.Path);
        m_DataBase.erase(it);
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> SourceAssetDatabase::UpdateSource(const SourceAssetMetadata& data)
    {
        if (!data.IsValid() || (data.Exists && data.Missing))
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid source metadata update"};
        auto it = m_DataBase.find(data.sourceAssetID);
        if (it == m_DataBase.end())
            return AssetError {AssetErrorCode::NotFound, "Source asset ID is not registered"};
        if (data.Path != it->second.Path)
            return AssetError {AssetErrorCode::InvalidArgument, "Use ChangeSourcePath to change a source path"};

        // Identity and path stay untouched; the remaining fields do not allocate.
        it->second.MetaVersion    = data.MetaVersion;
        it->second.Exists         = data.Exists;
        it->second.Missing        = data.Missing;
        it->second.FileSize       = data.FileSize;
        it->second.last_edit_time = data.last_edit_time;
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> SourceAssetDatabase::ChangeSourcePath(SourceAssetID    id,
                                                                                   const AssetPath& path)
    {
        if (!id.IsValid() || !path.IsValid())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid source path change"};
        auto it = m_DataBase.find(id);
        if (it == m_DataBase.end())
            return AssetError {AssetErrorCode::NotFound, "Source asset ID is not registered"};
        if (path == it->second.Path)
            return std::monostate {};
        auto replacement = path;
        if (!m_AssetPaths.emplace(replacement, id).second)
            return AssetError {AssetErrorCode::DuplicatePath, "Source asset path already registered"};
        // After insertion, commit without allocation; replacement becomes the old path.
        it->second.Path.Swap(replacement);
        m_AssetPaths.erase(replacement);
        return std::monostate {};
    }

    SourceAssetMetadata SourceAssetDatabase::FindByID(const SourceAssetID id) const
    {
        if (!id.IsValid())
        {
            return {};
        }
        const auto it = m_DataBase.find(id);
        if (it != m_DataBase.end())
        {
            return it->second;
        }
        return {};
    }
    SourceAssetID SourceAssetDatabase::FindIDByPath(const AssetPath& path) const
    {
        if (!path.IsValid())
        {
            return {};
        }
        const auto it = m_AssetPaths.find(path);
        if (it != m_AssetPaths.end())
        {
            return it->second;
        }
        return {};
    }

    bool SourceAssetDatabase::Contains(SourceAssetID id) const
    {
        return id.IsValid() && m_DataBase.find(id) != m_DataBase.end();
    }

    bool SourceAssetDatabase::Contains(const AssetPath& path) const
    {
        return path.IsValid() && m_AssetPaths.find(path) != m_AssetPaths.end();
    }
} // namespace Kans
