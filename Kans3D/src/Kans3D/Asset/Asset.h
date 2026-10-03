#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include "Kans3D/Asset/AssetType.h"
#include "Kans3D/Core/Base/Ref.h"
#include "Kans3D/Core/Hash.h"
#include "Kans3D/Core/UUID.h"

namespace Kans
{
    // using AssetHandle = UUID;
    class AssetPath
    {
    public:
        AssetPath() noexcept = default;
        explicit AssetPath(const std::filesystem::path& path);
        ~AssetPath() = default;

        bool                         IsValid() const noexcept { return !(m_Path.empty() || m_Hash == Hash::None); }
        const std::filesystem::path& GetPath() const noexcept { return m_Path; }
        uint64_t                     GetHash() const noexcept { return m_Hash; }
        std::string                  ToString() const { return m_Path.generic_string(); }

        bool operator==(const AssetPath& other) const noexcept { return other.m_Path == m_Path; }
        bool operator!=(const AssetPath& other) const noexcept { return !(*this == other); }

    private:
        void normalize();

    private:
        uint64_t              m_Hash = Hash::None;
        std::filesystem::path m_Path;
    };
    class SourceAssetID
    {
    public:
        SourceAssetID();
        ~SourceAssetID() = default;
        static SourceAssetID FromString(const std::string& sourceIDStr);
        static SourceAssetID Generate();
        std::string          ToString() const;

        bool     IsValid() const noexcept { return m_ID != UUID::None; }
        uint64_t GetHash() const noexcept { return m_Hash; }
        uint64_t GetUUID() const noexcept { return m_ID; }

        bool operator==(const SourceAssetID& other) const noexcept { return other.m_ID == m_ID; }
        bool operator!=(const SourceAssetID& other) const noexcept { return !(*this == other); }

    private:
        UUID     m_ID;
        uint64_t m_Hash;
    };
    class AssetID
    {
    public:
        AssetID();
        AssetID(SourceAssetID sourceAssetID, uint64_t localID);
        static AssetID FromString(const std::string& assetIDStr);

        bool operator==(const AssetID& other) const noexcept
        {
            return m_localID == other.m_localID && m_sourceID == other.m_sourceID;
        }
        bool                 operator!=(const AssetID& other) const noexcept { return !(*this == other); }
        bool                 IsValid() const noexcept { return m_sourceID.IsValid() && m_localID != UUID::None; }
        const SourceAssetID& GetSourceID() const noexcept { return m_sourceID; }
        uint64_t             GetLocalID() const noexcept { return m_localID; }
        std::string          ToString() const;

    private:
        uint64_t      m_localID;
        SourceAssetID m_sourceID;
    };

    class Asset : public RefCounter
    {
    public:
        AssetID  assetID;
        uint16_t Flags = (uint16_t)AssetFlag::None;

        virtual ~Asset() {}

        static AssetType  GetStaticType() { return AssetType::None; }
        virtual AssetType GetAssetType() const { return AssetType::None; }

        bool IsValid() const
        {
            return ((Flags & (uint16_t)AssetFlag::Missing) | (Flags & (uint16_t)AssetFlag::Invalid)) == 0;
        }

        virtual bool operator==(const Asset& other) { return assetID == other.assetID; }

        virtual bool operator!=(const Asset& other) { return !(*this == other); }

        bool IsFlagSet(AssetFlag flag) const { return Flags & (uint16_t)flag; }
        void SetFlag(AssetFlag flag, bool Value = true)
        {
            // 若要将flag某一位置1，则直接用flag进行或运算
            // 若要将flag某一位置0，则将flag取反后进行与运算
            if (Value)
                Flags |= (uint16_t)flag;
            else
                Flags &= ~(uint16_t)flag;
        }
    };
} // namespace Kans
namespace std
{
    template<>
    struct hash<Kans::SourceAssetID>
    {
        size_t operator()(const Kans::SourceAssetID& sourceHandle) const noexcept { return sourceHandle.GetHash(); }
    };
    template<>
    struct hash<Kans::AssetID>
    {
        size_t operator()(const Kans::AssetID& assetID) const noexcept
        {
            size_t sourceHash = std::hash<Kans ::SourceAssetID> {}(assetID.GetSourceID());
            size_t localHash  = std::hash<uint64_t> {}(assetID.GetLocalID());
            return Kans::Hash::HashCombine(sourceHash, localHash);
        }
    };
    template<>
    struct hash<Kans::AssetPath>
    {
        size_t operator()(const Kans::AssetPath& assetPath) const noexcept
        {
            return static_cast<size_t>(assetPath.GetHash());
        }
    };
} // namespace std
