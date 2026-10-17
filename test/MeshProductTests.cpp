#include <limits>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImportedModelData.h"
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"

namespace
{
    using namespace Kans;

    MeshData Triangle()
    {
        MeshData data;
        data.Vertices.resize(3);
        data.Vertices[0].Position = {1, -2, 0.5f};
        data.Vertices[1].Position = {0, 1, 0};
        data.Vertices[2].Position = {0, 0, 1};
        data.Indices              = {0, 1, 2};
        data.Material             = AssetID(SourceAssetID::FromString("0123456789abcdef"), 19);
        BuildSingleSection(data);
        return data;
    }

    void Set32(std::vector<std::byte>& bytes, size_t offset, uint32_t value)
    {
        for (size_t i = 0; i < 4; ++i)
            bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8)) & 255);
    }

    std::vector<std::byte> Encode(const MeshData& mesh)
    {
        auto encoded = EncodeMeshProduct(mesh);
        EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        return std::holds_alternative<std::vector<std::byte>>(encoded) ?
                   std::get<std::vector<std::byte>>(std::move(encoded)) :
                   std::vector<std::byte> {};
    }

    TEST(MeshProduct, RoundTripPreservesGeometryMaterialAndExplicitLittleEndianLayout)
    {
        const auto original = Triangle();
        const auto bytes    = Encode(original);
        ASSERT_EQ(bytes.size(), 56 + 3 * 64 + 3 * 4 + 36 + 40);
        EXPECT_EQ(bytes[0], std::byte {'K'});
        EXPECT_EQ(bytes[1], std::byte {'M'});
        EXPECT_EQ(bytes[2], std::byte {'S'});
        EXPECT_EQ(bytes[3], std::byte {'H'});
        // First float is 1.0f, IEEE 754 0x3f800000, encoded little endian.
        EXPECT_EQ(bytes[56], std::byte {0});
        EXPECT_EQ(bytes[58], std::byte {0x80});
        EXPECT_EQ(bytes[59], std::byte {0x3f});
        auto decoded = DecodeMeshProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<MeshData>(decoded));
        const auto& mesh = std::get<MeshData>(decoded);
        EXPECT_EQ(mesh.Material, original.Material);
        EXPECT_EQ(mesh.Indices, original.Indices);
        ASSERT_EQ(mesh.Vertices.size(), original.Vertices.size());
        for (size_t i = 0; i < mesh.Vertices.size(); ++i)
        {
            EXPECT_EQ(mesh.Vertices[i].Position, original.Vertices[i].Position);
            EXPECT_EQ(mesh.Vertices[i].Normal, original.Vertices[i].Normal);
            EXPECT_EQ(mesh.Vertices[i].UV, original.Vertices[i].UV);
            EXPECT_EQ(mesh.Vertices[i].Color, original.Vertices[i].Color);
            EXPECT_EQ(mesh.Vertices[i].Tangent, original.Vertices[i].Tangent);
        }
    }

    TEST(MeshProduct, RejectsEveryTruncationAndTrailingBytes)
    {
        auto bytes = Encode(Triangle());
        for (size_t length = 0; length < bytes.size(); ++length)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(bytes.data(), length))) << length;
        bytes.push_back(std::byte {0});
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(bytes.data(), bytes.size())));
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(nullptr, bytes.size())));
    }

    TEST(MeshProduct, RejectsHeaderCorruptionAndUnsupportedVersion)
    {
        const auto original = Encode(Triangle());
        for (auto mutation : {std::pair<size_t, uint32_t> {0, 0},
                              {8, 2},
                              {12, 0},
                              {16, 0},
                              {16, 0xffffffff},
                              {20, 0xffffffff},
                              {20, 2}})
        {
            auto bytes = original;
            Set32(bytes, mutation.first, mutation.second);
            auto decoded = DecodeMeshProduct(bytes.data(), bytes.size());
            ASSERT_TRUE(std::holds_alternative<AssetError>(decoded));
            EXPECT_EQ(std::get<AssetError>(decoded).Code, AssetErrorCode::InvalidData);
        }
        auto bytes = original;
        Set32(bytes, 4, 4);
        EXPECT_EQ(std::get<AssetError>(DecodeMeshProduct(bytes.data(), bytes.size())).Code,
                  AssetErrorCode::UnsupportedVersion);
    }

    TEST(MeshProduct, RejectsNonFiniteVerticesAndOutOfBoundsIndices)
    {
        auto mesh                   = Triangle();
        mesh.Vertices[0].Tangent[3] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMeshProduct(mesh)));
        mesh            = Triangle();
        mesh.Indices[0] = 3;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMeshProduct(mesh)));
        auto bytes = Encode(Triangle());
        Set32(bytes, 56, 0x7f800000); // +infinity
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(bytes.data(), bytes.size())));
        bytes = Encode(Triangle());
        Set32(bytes, 56 + 3 * 64, 3);
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(bytes.data(), bytes.size())));
    }

    TEST(MeshProduct, RejectsEmptyGeometryAndPartialMaterialIdentity)
    {
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMeshProduct({})));
        auto mesh     = Triangle();
        mesh.Material = AssetID(SourceAssetID::Generate(), 0);
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMeshProduct(mesh)));
        mesh                  = Triangle();
        mesh.AuthoredMaterial = AssetID(SourceAssetID::Generate(), 0);
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMeshProduct(mesh)));
        mesh.AuthoredMaterial = {};
        mesh.Material      = {};
        BuildSingleSection(mesh);
        const auto bytes   = Encode(mesh);
        auto       decoded = DecodeMeshProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<MeshData>(decoded));
        EXPECT_FALSE(std::get<MeshData>(decoded).Material.IsValid());
        auto broken = bytes;
        Set32(broken, 312, 1);
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(broken.data(), broken.size())));
    }

    TEST(MeshProduct, RejectsEmptyOverflowingSectionsInvalidSlotsAndNonFiniteStoredBounds)
    {
        // Bugs: uint32 range addition wraps; an empty section slips through;
        // uploading a malformed slot or non-finite persisted AABB reaches the GPU.
        const auto rejects = [](const MeshData& mesh) {
            EXPECT_TRUE(std::holds_alternative<AssetError>(ValidateMeshData(mesh)));
            EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeMeshProduct(mesh)));
        };
        auto mesh = Triangle();
        mesh.Sections.clear();
        rejects(mesh);
        mesh                        = Triangle();
        mesh.Sections[0].IndexCount = 0;
        rejects(mesh);
        mesh                        = Triangle();
        mesh.Sections[0].FirstIndex = UINT32_MAX - 2;
        mesh.Sections[0].IndexCount = 6;
        rejects(mesh);
        mesh                          = Triangle();
        mesh.Sections[0].MaterialSlot = 1;
        rejects(mesh);
        mesh                   = Triangle();
        mesh.LocalBounds.Min.x = std::numeric_limits<float>::infinity();
        rejects(mesh);
        mesh                               = Triangle();
        mesh.Sections[0].LocalBounds.Max.y = std::numeric_limits<float>::quiet_NaN();
        rejects(mesh);
        const auto original = Encode(Triangle());
        for (auto mutation : {std::pair<size_t, uint32_t> {32, 0x7f800000},
                              {260, UINT32_MAX - 2},
                              {264, 0},
                              {268, 1},
                              {272, 0x7fc00000}})
        {
            auto bytes = original;
            Set32(bytes, mutation.first, mutation.second);
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeMeshProduct(bytes.data(), bytes.size())));
        }
        for (uint32_t version : {1u, 2u})
        {
            auto bytes = original;
            Set32(bytes, 4, version);
            auto decoded = DecodeMeshProduct(bytes.data(), bytes.size());
            ASSERT_TRUE(std::holds_alternative<AssetError>(decoded));
            EXPECT_EQ(std::get<AssetError>(decoded).Code, AssetErrorCode::UnsupportedVersion);
        }
    }

    TEST(MeshProduct, SupportsUnalignedInputAndOwnsDecodedData)
    {
        auto bytes = Encode(Triangle());
        bytes.insert(bytes.begin(), std::byte {0});
        auto decoded = DecodeMeshProduct(bytes.data() + 1, bytes.size() - 1);
        ASSERT_TRUE(std::holds_alternative<MeshData>(decoded));
        bytes.clear();
        bytes.shrink_to_fit();
        EXPECT_EQ(std::get<MeshData>(decoded).Indices.at(2), 2u);
        EXPECT_EQ(std::get<MeshData>(decoded).Vertices.at(0).Position[0], 1.0f);
    }

    TEST(MeshProduct, LoaderCreatesCpuAssetWithIdentityAndNoLegacySource)
    {
        auto             bytes = Encode(Triangle());
        AssetLoadContext context;
        context.ID   = AssetID(SourceAssetID::Generate(), 7);
        context.Type = AssetType::StaticMesh;
        context.Data = bytes.data();
        context.Size = bytes.size();
        auto loaded  = Loader<MeshAsset>().Load(context);
        ASSERT_TRUE(std::holds_alternative<Ref<MeshAsset>>(loaded));
        auto asset = std::get<Ref<MeshAsset>>(loaded);
        bytes.clear();
        bytes.shrink_to_fit();
        EXPECT_EQ(asset->assetID, context.ID);
        EXPECT_EQ(asset->GetAssetType(), AssetType::StaticMesh);
        EXPECT_EQ(asset->GetData().Indices.size(), 3u);
        context.Type = AssetType::Texture;
        EXPECT_EQ(std::get<AssetError>(Loader<MeshAsset>().Load(context)).Code, AssetErrorCode::InvalidArgument);
    }

    TEST(MeshProduct, RealModelGeometryRoundTripsThroughProductAndLoader)
    {
        auto parsed = ParseModelCpu(std::filesystem::path(KANS_TEST_ASSET_DIR) / "nanosuit/nanosuit.obj");
        ASSERT_TRUE(std::holds_alternative<ImportedModelData>(parsed));
        const auto& source = std::get<ImportedModelData>(parsed);
        for (const auto& geometry : source.Meshes)
        {
            MeshData         data {geometry.Vertices, geometry.Indices, {}};
            BuildSingleSection(data);
            const auto       bytes = Encode(data);
            AssetLoadContext context;
            context.ID   = AssetID(SourceAssetID::Generate(), 1);
            context.Type = AssetType::StaticMesh;
            context.Data = bytes.data();
            context.Size = bytes.size();
            auto loaded  = Loader<MeshAsset>().Load(context);
            ASSERT_TRUE(std::holds_alternative<Ref<MeshAsset>>(loaded));
            const auto* mesh = &std::get<Ref<MeshAsset>>(loaded)->GetData();
            ASSERT_NE(mesh, nullptr);
            EXPECT_EQ(mesh->Indices, geometry.Indices);
            EXPECT_EQ(mesh->Vertices.size(), geometry.Vertices.size());
        }
    }
    TEST(MeshProductUpload, RejectsMalformedCpuDataBeforeContextOrBackendAccess)
    {
        auto data       = Triangle();
        data.Indices[0] = 99;
        EXPECT_EQ(std::get<AssetError>(UploadMesh(data)).Code, AssetErrorCode::InvalidData);
        data                        = Triangle();
        data.Vertices[1].Tangent[3] = std::numeric_limits<float>::infinity();
        EXPECT_EQ(std::get<AssetError>(ValidateMeshData(data)).Code, AssetErrorCode::InvalidData);
        EXPECT_EQ(std::get<AssetError>(UploadMesh(data)).Code, AssetErrorCode::InvalidData);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(ValidateMeshData(Triangle())));
    }
    TEST(MeshProductUpload, RejectsAbsentDeviceAndUnselectedBackend)
    {
        struct BackendRestore
        {
            RenderHardware::Backend Previous = RenderHardware::GetBackend();
            ~BackendRestore() { RenderHardware::SetBackend(Previous); }
        } restore;
        RenderHardware::SetBackend(RenderHardware::Backend::OpenGL);
        EXPECT_EQ(std::get<AssetError>(UploadMesh(Triangle())).Code, AssetErrorCode::InvalidArgument);
        RenderHardware::SetBackend(RenderHardware::Backend::Vulkan);
        EXPECT_EQ(std::get<AssetError>(UploadMesh(Triangle())).Code, AssetErrorCode::InvalidArgument);
        RenderHardware::SetBackend(RenderHardware::Backend::None);
        EXPECT_EQ(std::get<AssetError>(UploadMesh(Triangle())).Code, AssetErrorCode::NotImplemented);
    }
} // namespace
