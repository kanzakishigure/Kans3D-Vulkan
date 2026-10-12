#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Storage/ProductStore.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/Resource/Texture.h"
#include "AssetTestUtilities.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;
    TextureData R8(uint32_t width, uint32_t height, std::initializer_list<unsigned> pixels)
    {
        TextureData data;
        data.Width     = width;
        data.Height    = height;
        data.Format    = TexturePixelFormat::R8;
        data.Usage     = TextureUsage::Data;
        data.AlphaMode = TextureAlphaMode::Opaque;
        data.Mips.resize(1);
        for (unsigned value : pixels)
            data.Mips[0].Pixels.push_back(static_cast<std::byte>(value));
        return data;
    }
    TextureData RGBA(uint32_t width, uint32_t height, std::initializer_list<unsigned> pixels)
    {
        auto data      = R8(width, height, pixels);
        data.Format    = TexturePixelFormat::RGBA8;
        data.Usage     = TextureUsage::Color;
        data.AlphaMode = TextureAlphaMode::Straight;
        return data;
    }
    void AppendFloat(std::vector<std::byte>& bytes, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(static_cast<std::byte>((bits >> (i * 8)) & 255));
    }
    float ReadFloat(const std::vector<std::byte>& bytes, size_t index)
    {
        uint32_t bits = 0;
        for (unsigned i = 0; i < 4; ++i)
            bits |= uint32_t(std::to_integer<uint8_t>(bytes[index * 4 + i])) << (i * 8);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    TextureData FloatData()
    {
        auto data   = RGBA(2, 1, {});
        data.Format = TexturePixelFormat::RGBA32Float;
        for (float value : {4.0f, 0.5f, -1.0f, 1.0f, 8.0f, 0.5f, -1.0f, 1.0f})
            AppendFloat(data.Mips[0].Pixels, value);
        return data;
    }
    std::vector<std::byte> Encoded(const TextureData& texture)
    {
        auto result = EncodeTextureProduct(texture);
        EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
        return std::get<std::vector<std::byte>>(std::move(result));
    }
    TextureData Generated(const TextureData& texture)
    {
        auto result = GenerateTextureMips(texture);
        EXPECT_TRUE(std::holds_alternative<TextureData>(result));
        return std::get<TextureData>(std::move(result));
    }

    TEST(TextureProduct, LittleEndianHeaderAndOwnedCpuLoaderRoundTrip)
    {
        auto data   = Generated(RGBA(2, 1, {255, 0, 0, 255, 0, 0, 255, 255}));
        data.Origin = TextureOrigin::BottomLeft;
        auto bytes  = Encoded(data);
        ASSERT_EQ(bytes.size(), 60u);
        EXPECT_EQ(bytes[0], std::byte {'K'});
        EXPECT_EQ(bytes[1], std::byte {'T'});
        EXPECT_EQ(bytes[2], std::byte {'E'});
        EXPECT_EQ(bytes[3], std::byte {'X'});
        EXPECT_EQ(bytes[4], std::byte {1});
        EXPECT_EQ(bytes[6], std::byte {2});
        EXPECT_EQ(bytes[16], std::byte {2});
        EXPECT_EQ(bytes[32], std::byte {2});
        EXPECT_EQ(bytes[38], std::byte {1});
        EXPECT_EQ(bytes[40], std::byte {12});
        const auto id     = AssetID(SourceAssetID::Generate(), 3);
        auto       loaded = Loader<TextureAsset>().Load({id, AssetType::Texture, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<TextureAsset>>(loaded));
        auto asset = std::get<Ref<TextureAsset>>(loaded);
        EXPECT_EQ(asset->assetID, id);
        EXPECT_EQ(asset->GetAssetType(), AssetType::Texture);
        EXPECT_EQ(Encoded(asset->GetData()), bytes);
        bytes.clear();
        EXPECT_EQ(asset->GetData().Mips[0].Pixels, data.Mips[0].Pixels);
        EXPECT_EQ(asset->GetData().Mips[1].Pixels, data.Mips[1].Pixels);
    }

    TEST(TextureProduct, HandlesUnalignedInputAndRejectsEveryTruncatedLengthAndTrailingData)
    {
        const auto bytes = Encoded(R8(3, 1, {1, 2, 3}));
        for (size_t length = 0; length < bytes.size(); ++length)
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeTextureProduct(bytes.data(), length))) << length;
        std::vector<std::byte> unaligned {std::byte {0}};
        unaligned.insert(unaligned.end(), bytes.begin(), bytes.end());
        EXPECT_TRUE(std::holds_alternative<TextureData>(DecodeTextureProduct(unaligned.data() + 1, bytes.size())));
        unaligned.push_back(std::byte {0});
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeTextureProduct(unaligned.data() + 1, bytes.size() + 1)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeTextureProduct(nullptr, bytes.size())));
    }

    TEST(TextureProduct, RejectsInvalidHeaderEnumsDimensionsMipCountsAndPayload)
    {
        const auto good = Encoded(R8(3, 1, {1, 2, 3}));
        for (const auto mutation : std::vector<std::pair<size_t, unsigned>> {{0, 0},
                                                                             {4, 2},
                                                                             {6, 0},
                                                                             {8, 99},
                                                                             {10, 99},
                                                                             {12, 99},
                                                                             {14, 99},
                                                                             {16, 0},
                                                                             {20, 0},
                                                                             {24, 0},
                                                                             {28, 0},
                                                                             {32, 0},
                                                                             {32, 16},
                                                                             {36, 99},
                                                                             {38, 99},
                                                                             {40, 255}})
        {
            auto bytes            = good;
            bytes[mutation.first] = static_cast<std::byte>(mutation.second);
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeTextureProduct(bytes.data(), bytes.size())))
                << mutation.first;
        }
        auto bytes = good;
        for (unsigned i = 0; i < 4; ++i)
            bytes[16 + i] = std::byte {255};
        EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeTextureProduct(bytes.data(), bytes.size())));
        bytes    = good;
        bytes[4] = std::byte {2};
        EXPECT_EQ(std::get<AssetError>(DecodeTextureProduct(bytes.data(), bytes.size())).Code,
                  AssetErrorCode::UnsupportedVersion);
    }

    TEST(TextureProduct, ValidatesShapeFormatSpaceUsageAlphaAndExactPixelLengths)
    {
        auto                     base = R8(1, 1, {1});
        std::vector<TextureData> bad;
        auto                     data = base;
        data.Width                    = 0;
        bad.push_back(data);
        data       = base;
        data.Depth = 2;
        bad.push_back(data);
        data        = base;
        data.Layers = 2;
        bad.push_back(data);
        data = base;
        data.Mips.clear();
        bad.push_back(data);
        data = base;
        data.Mips.push_back(data.Mips[0]);
        bad.push_back(data);
        data = base;
        data.Mips[0].Pixels.push_back(std::byte {0});
        bad.push_back(data);
        data        = base;
        data.Format = TexturePixelFormat::RGBA8;
        bad.push_back(data);
        data            = base;
        data.ColorSpace = TextureColorSpace::SRGB;
        bad.push_back(data);
        data           = base;
        data.AlphaMode = TextureAlphaMode::Straight;
        bad.push_back(data);
        data       = base;
        data.Usage = TextureUsage::Normal;
        bad.push_back(data);
        data      = base;
        data.Kind = TextureKind::Cube;
        bad.push_back(data);
        data           = RGBA(1, 1, {0, 0, 0, 255});
        data.Usage     = TextureUsage::Data;
        data.AlphaMode = TextureAlphaMode::Premultiplied;
        bad.push_back(data);
        for (const auto& invalid : bad)
            EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeTextureProduct(invalid)));
        data       = base;
        data.Width = 16384;
        EXPECT_EQ(std::get<uint64_t>(TextureMipByteSize(data, 14)), 1u);
        EXPECT_TRUE(std::holds_alternative<AssetError>(TextureMipByteSize(data, 15)));
        data.Height = data.Depth = 16384;
        data.Kind                = TextureKind::Volume3D;
        EXPECT_TRUE(std::holds_alternative<AssetError>(TextureMipByteSize(data, 0)));
    }

    TEST(TextureProduct, PreservesFloatHdrAndRejectsNonFiniteValuesAndInvalidColorAlpha)
    {
        auto texture = FloatData();
        auto bytes   = Encoded(texture);
        auto decoded = DecodeTextureProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<TextureData>(decoded));
        EXPECT_FLOAT_EQ(ReadFloat(std::get<TextureData>(decoded).Mips[0].Pixels, 0), 4);
        auto mips = Generated(texture);
        EXPECT_FLOAT_EQ(ReadFloat(mips.Mips[1].Pixels, 0), 6);
        EXPECT_FLOAT_EQ(ReadFloat(mips.Mips[1].Pixels, 2), -1);
        for (uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00001u})
        {
            auto invalid = bytes;
            for (unsigned i = 0; i < 4; ++i)
                invalid[48 + i] = static_cast<std::byte>((bits >> (i * 8)) & 255);
            EXPECT_TRUE(std::holds_alternative<AssetError>(DecodeTextureProduct(invalid.data(), invalid.size())));
        }
        texture.ColorSpace = TextureColorSpace::SRGB;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeTextureProduct(texture)));
        texture.ColorSpace = TextureColorSpace::Linear;
        texture.Mips[0].Pixels.clear();
        for (float value : {1.0f, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1.0f, 2.0f})
            AppendFloat(texture.Mips[0].Pixels, value);
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeTextureProduct(texture)));
        texture.Usage = TextureUsage::Data;
        EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(EncodeTextureProduct(texture)));
    }

    TEST(TextureMips, LinearAndSrgbColorsAreAveragedInLinearSpace)
    {
        auto texture = RGBA(2, 1, {0, 0, 0, 255, 255, 255, 255, 255});
        EXPECT_EQ(Generated(texture).Mips[1].Pixels[0], std::byte {128});
        texture.ColorSpace = TextureColorSpace::SRGB;
        auto generated     = Generated(texture);
        EXPECT_EQ(generated.Mips[1].Pixels[0], std::byte {188});
        EXPECT_EQ(generated.Mips[1].Pixels[3], std::byte {255});
        EXPECT_EQ(generated.ColorSpace, TextureColorSpace::SRGB);
    }

    TEST(TextureMips, StraightAlphaAvoidsTransparentColorFringesAndPremultipliedRemainsAssociated)
    {
        auto texture  = RGBA(2, 1, {255, 0, 0, 255, 0, 0, 255, 0});
        auto straight = Generated(texture);
        EXPECT_EQ(straight.Mips[1].Pixels,
                  (std::vector<std::byte> {std::byte {255}, std::byte {0}, std::byte {0}, std::byte {128}}));
        texture.Mips[0].Pixels[6] = std::byte {0};
        texture.AlphaMode         = TextureAlphaMode::Premultiplied;
        EXPECT_EQ(Generated(texture).Mips[1].Pixels[0], std::byte {128});
        texture.Usage     = TextureUsage::Data;
        texture.AlphaMode = TextureAlphaMode::Straight;
        EXPECT_EQ(Generated(texture).Mips[1].Pixels[0], std::byte {128});
    }

    TEST(TextureMips, FullyTransparentBlocksKeepTheirAverageColorInsteadOfBlack)
    {
        // Textures that store a mask in alpha are often almost entirely alpha 0.
        auto texture = RGBA(2, 1, {200, 100, 50, 0, 100, 50, 150, 0});
        auto mip     = Generated(texture).Mips[1].Pixels;
        EXPECT_EQ(mip, (std::vector<std::byte> {std::byte {150}, std::byte {75}, std::byte {100}, std::byte {0}}));
    }

    TEST(TextureMips, OddDimensionsIncludeLastRowsColumnsAndFractionalAreaCoverage)
    {
        EXPECT_EQ(Generated(R8(3, 1, {0, 0, 255})).Mips[1].Pixels[0], std::byte {85});
        EXPECT_EQ(Generated(R8(1, 3, {0, 0, 255})).Mips[1].Pixels[0], std::byte {85});
        auto generated = Generated(R8(5, 1, {0, 0, 255, 255, 255}));
        ASSERT_EQ(generated.Mips.size(), 3u);
        EXPECT_EQ(generated.Mips[1].Pixels[0], std::byte {51});
        EXPECT_EQ(generated.Mips[1].Pixels[1], std::byte {255});
        EXPECT_EQ(generated.Mips[2].Pixels[0], std::byte {153});
    }

    TEST(TextureMips, ArrayLayersRemainIndependentAndVolumeDepthShrinks)
    {
        auto array     = R8(2, 1, {255, 255, 0, 0});
        array.Kind     = TextureKind::Array2D;
        array.Layers   = 2;
        auto generated = Generated(array);
        EXPECT_EQ(generated.Mips[1].Pixels, (std::vector<std::byte> {std::byte {255}, std::byte {0}}));
        auto volume  = R8(3, 3, {});
        volume.Kind  = TextureKind::Volume3D;
        volume.Depth = 3;
        volume.Mips[0].Pixels.resize(27);
        volume.Mips[0].Pixels.back() = std::byte {255};
        generated                    = Generated(volume);
        ASSERT_EQ(generated.Mips.size(), 2u);
        EXPECT_EQ(generated.Mips[1].Pixels.size(), 1u);
        EXPECT_EQ(generated.Mips[1].Pixels[0], std::byte {9});
        EXPECT_EQ(Encoded(generated).size(), 76u);
    }

    TEST(TextureMips, NormalVectorsAreRenormalizedWithDefinedCancellationFallback)
    {
        auto texture     = RGBA(2, 1, {255, 128, 128, 255, 128, 255, 128, 255});
        texture.Usage    = TextureUsage::Normal;
        auto   generated = Generated(texture);
        double length    = 0;
        for (unsigned i = 0; i < 3; ++i)
        {
            double value = std::to_integer<unsigned>(generated.Mips[1].Pixels[i]) / 255.0 * 2 - 1;
            length += value * value;
        }
        EXPECT_NEAR(std::sqrt(length), 1, 0.01);
        texture       = RGBA(2, 1, {255, 128, 128, 255, 0, 127, 127, 255});
        texture.Usage = TextureUsage::Normal;
        EXPECT_EQ(Generated(texture).Mips[1].Pixels,
                  (std::vector<std::byte> {std::byte {128}, std::byte {128}, std::byte {255}, std::byte {255}}));
    }

    TEST(TextureProduct, RG8ChannelsAndPartialAuthoredChainsRoundTripAndRegenerationUsesBase)
    {
        auto texture   = R8(2, 2, {0, 255, 64, 128, 128, 64, 255, 0});
        texture.Format = TexturePixelFormat::RG8;
        auto filtered  = Generated(texture);
        EXPECT_EQ(filtered.Mips[1].Pixels, (std::vector<std::byte> {std::byte {112}, std::byte {112}}));
        auto bytes = Encoded(filtered);
        EXPECT_EQ(bytes.size(), 58u);
        EXPECT_EQ(bytes[10], std::byte {1});
        auto decoded = DecodeTextureProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<TextureData>(decoded));
        EXPECT_EQ(Encoded(std::get<TextureData>(decoded)), bytes);

        texture.Width = texture.Height = 4;
        texture.Mips[0].Pixels.assign(32, std::byte {0});
        texture.Mips.push_back({std::vector<std::byte>(8, std::byte {255})});
        bytes   = Encoded(texture);
        decoded = DecodeTextureProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<TextureData>(decoded));
        EXPECT_EQ(std::get<TextureData>(decoded).Mips.size(), 2u);
        EXPECT_EQ(std::get<TextureData>(decoded).Mips[1].Pixels[0], std::byte {255});
        filtered = Generated(texture);
        EXPECT_EQ(filtered.Mips.size(), 3u);
        EXPECT_EQ(filtered.Mips[1].Pixels[0], std::byte {0});
        EXPECT_EQ(filtered.Mips[2].Pixels[0], std::byte {0});
        EXPECT_EQ(texture.Mips[1].Pixels[0], std::byte {255});
    }

    TEST(TextureMips, FloatNormalsPreservePrecisionAndValidateEncodedVectorRange)
    {
        auto texture   = RGBA(2, 1, {});
        texture.Format = TexturePixelFormat::RGBA32Float;
        texture.Usage  = TextureUsage::Normal;
        for (float value : {1.0f, 0.5f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f})
            AppendFloat(texture.Mips[0].Pixels, value);
        auto generated = Generated(texture);
        EXPECT_NEAR(ReadFloat(generated.Mips[1].Pixels, 0), 0.8535533906, 1e-6);
        EXPECT_NEAR(ReadFloat(generated.Mips[1].Pixels, 1), 0.8535533906, 1e-6);
        EXPECT_FLOAT_EQ(ReadFloat(generated.Mips[1].Pixels, 2), 0.5f);
        auto bytes = Encoded(generated);
        EXPECT_TRUE(std::holds_alternative<TextureData>(DecodeTextureProduct(bytes.data(), bytes.size())));
        texture.Mips[0].Pixels.clear();
        for (float value : {-1.0f, 0.5f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f})
            AppendFloat(texture.Mips[0].Pixels, value);
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeTextureProduct(texture)));
    }

    TEST(TextureProduct, CubeFacesRoundTripButSeamAwareGenerationIsExplicitlyPending)
    {
        auto texture   = RGBA(1, 1, {});
        texture.Kind   = TextureKind::Cube;
        texture.Layers = 6;
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned value : {face * 40, 0u, 0u, 255u})
                texture.Mips[0].Pixels.push_back(static_cast<std::byte>(value));
        auto bytes   = Encoded(texture);
        auto decoded = DecodeTextureProduct(bytes.data(), bytes.size());
        ASSERT_TRUE(std::holds_alternative<TextureData>(decoded));
        EXPECT_EQ(std::get<TextureData>(decoded).Mips[0].Pixels, texture.Mips[0].Pixels);
        EXPECT_EQ(std::get<AssetError>(GenerateTextureMips(texture)).Code, AssetErrorCode::NotImplemented);
        texture.Kind   = TextureKind::CubeArray;
        texture.Layers = 12;
        texture.Mips[0].Pixels.resize(48);
        EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(EncodeTextureProduct(texture)));
        texture.Layers = 7;
        EXPECT_TRUE(std::holds_alternative<AssetError>(EncodeTextureProduct(texture)));
    }

    TEST(TextureProduct, LoaderRejectsInvalidIdentityTypeAndBytesWithoutFallback)
    {
        auto                 bytes = Encoded(R8(1, 1, {9}));
        const auto           id    = AssetID(SourceAssetID::Generate(), 1);
        Loader<TextureAsset> loader;
        for (const auto& context :
             std::vector<AssetLoadContext> {{{}, AssetType::Texture, bytes.data(), bytes.size()},
                                            {id, AssetType::StaticMesh, bytes.data(), bytes.size()},
                                            {id, AssetType::Texture, nullptr, bytes.size()},
                                            {id, AssetType::Texture, bytes.data(), 0}})
            EXPECT_TRUE(std::holds_alternative<AssetError>(loader.Load(context)));
        bytes[0] = std::byte {0};
        EXPECT_TRUE(
            std::holds_alternative<AssetError>(loader.Load({id, AssetType::Texture, bytes.data(), bytes.size()})));
    }

    TEST(TextureRuntimeIntegration, StagedTexturePublishesReopensAndLoadsThroughProviderAndManager)
    {
        const auto workspace = fs::temp_directory_path() / ("Kans3D-Texture-" + SourceAssetID::Generate().ToString());
        fs::create_directory(workspace);
        struct Cleanup
        {
            fs::path Root;
            ~Cleanup()
            {
                std::error_code error;
                fs::remove_all(Root, error);
            }
        } cleanup {workspace};
        ImportContext context;
        context.SourceID         = SourceAssetID::Generate();
        context.SourceFile       = workspace / "texture.png";
        context.StagingDirectory = workspace / "stage";
        ASSERT_TRUE(fs::create_directory(context.StagingDirectory));
        auto texture = Generated(RGBA(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}));
        auto bytes   = Encoded(texture);
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(WriteProductFile(context.StagingDirectory / "image.ktex", bytes)));
        ImportResult imported;
        imported.LocalIDs = {{"texture:root", 1}};
        imported.Products = {{1, AssetType::Texture, "image.ktex", {}}};
        const auto root   = workspace / "store";
        auto       opened = ProductStore::Open(root);
        ASSERT_TRUE(std::holds_alternative<Scope<ProductStore>>(opened));
        {
            auto store = std::get<Scope<ProductStore>>(std::move(opened));
            ASSERT_TRUE(std::holds_alternative<Ref<const AssetRegistry>>(
                PublishStaged(*store, context, imported, ComputeContentDigest(bytes))));
        }
        fs::remove_all(context.StagingDirectory);
        auto reopened = ProductStore::Open(root);
        ASSERT_TRUE(std::holds_alternative<Scope<ProductStore>>(reopened));
        auto store    = std::get<Scope<ProductStore>>(std::move(reopened));
        auto provider = LooseFileAssetProvider::Open(root);
        ASSERT_TRUE(std::holds_alternative<Ref<LooseFileAssetProvider>>(provider));
        auto manager =
            std::make_unique<AssetManager>(std::get<Ref<LooseFileAssetProvider>>(provider), store->Snapshot());
        auto loaded  = manager->Load<TextureAsset>(AssetID(context.SourceID, 1));
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(loaded));
        auto view = std::get<Ref<const TextureAsset>>(loaded);
        ASSERT_NE(view, nullptr);
        EXPECT_EQ(view->GetData().Mips[1].Pixels, texture.Mips[1].Pixels);
        EXPECT_EQ(manager->GetResidentCount(), 1u);
    }
    TEST(TextureProductUpload, RejectsAbsentContextAndUnimplementedBackendWithoutSourceIo)
    {
        auto data = R8(1, 1, {17});
        struct BackendRestore
        {
            RenderHardware::Backend Previous = RenderHardware::GetBackend();
            ~BackendRestore() { RenderHardware::SetBackend(Previous); }
        } restore;
        RenderHardware::SetBackend(RenderHardware::Backend::OpenGL);
        EXPECT_EQ(std::get<AssetError>(Texture2D::CreateFromProduct(data)).Code, AssetErrorCode::InvalidArgument);
        RenderHardware::SetBackend(RenderHardware::Backend::Vulkan);
        EXPECT_EQ(std::get<AssetError>(Texture2D::CreateFromProduct(data)).Code, AssetErrorCode::NotImplemented);
        RenderHardware::SetBackend(RenderHardware::Backend::None);
        EXPECT_EQ(std::get<AssetError>(Texture2D::CreateFromProduct(data)).Code, AssetErrorCode::NotImplemented);
    }
    TEST(TextureProductUpload, DeprecatedIdAliasKeepsPointerWidth)
    {
        // Bug: the compatibility alias narrows a descriptor/pointer-sized ImGui ID to the old uint32 GL name.
        class TokenTexture final : public Texture
        {
            fs::path Path;

        public:
            uint32_t        GetWidth() const override { return 1; }
            uint32_t        GetHeight() const override { return 1; }
            uintptr_t       GetImGuiTextureID() const override { return uintptr_t(UINT64_C(0x12345678abcdef01)); }
            const fs::path& GetPath() const override { return Path; }
        } texture;
        // On 64-bit platforms the high word must survive; 32-bit platforms retain their full pointer width.
        EXPECT_EQ(texture.GetRenererID(), uintptr_t(UINT64_C(0x12345678abcdef01)));
    }
} // namespace
