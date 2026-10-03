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
