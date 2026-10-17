#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Loader/Loader.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/Resource/Texture.h"
#include "GpuReadback.h"
#include "GpuTestContext.h"

namespace
{
    class TextureGpuMipmap : public testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_NO_FATAL_FAILURE(GpuContext.SetUp());
            temp =
                std::filesystem::temp_directory_path() /
                ("kans-mipmap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tga");
        }

        void TearDown() override
        {
            std::error_code ec;
            if (!temp.empty())
                std::filesystem::remove(temp, ec);
            GpuContext.TearDown();
        }

        Kans::TextureSpecification Specification(bool mips = true)
        {
            Kans::TextureSpecification spec;
            spec.Format = Kans::RenderHardware::Format::RGBA8Unorm;
            spec.Minf = spec.Maxf = Kans::RenderHardware::Filter::Linear;
            spec.Wrap             = Kans::RenderHardware::AddressMode::ClampToEdge;
            spec.GenerateMips     = mips;
            return spec;
        }

        void WriteCheckerboard()
        {
            // Uncompressed RGBA TGA: 8x4, every 2x2 block averages to 127.5.
            std::array<unsigned char, 18> header {};
            header[2]  = 2;
            header[12] = 8;
            header[14] = 4;
            header[16] = 32;
            header[17] = 8;
            std::ofstream out(temp, std::ios::binary);
            out.write(reinterpret_cast<const char*>(header.data()), header.size());
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    unsigned char                value = ((x + y) % 2) ? 255 : 0;
                    std::array<unsigned char, 4> pixel {value, value, value, 255};
                    out.write(reinterpret_cast<const char*>(pixel.data()), pixel.size());
                }
            ASSERT_TRUE(out.good());
        }

        void CheckChain(const Kans::Ref<Kans::Texture2D>& texture, int width, int height)
        {
            uint32_t level = 0;
            for (;;)
            {
                SCOPED_TRACE(level);
                const auto pixels = ReadGpuTexture(texture->GetResource(), level);
                EXPECT_EQ(pixels.size(), size_t(width) * height * 4);
                ++level;
                if (width == 1 && height == 1)
                    break;
                width  = std::max(1, width / 2);
                height = std::max(1, height / 2);
            }
            EXPECT_EQ(texture->GetResource()->GetDesc().MipLevels, level);
            EXPECT_EQ(texture->GetSampling().Min, Kans::RenderHardware::Filter::Linear);
            EXPECT_EQ(texture->GetSampling().Mip, Kans::RenderHardware::Filter::Linear);
            EXPECT_EQ(texture->GetSampling().Mag, Kans::RenderHardware::Filter::Linear);
        }

        std::filesystem::path temp;
        GpuTestContext        GpuContext {GpuTestBackend::Vulkan};
    };

    TEST_F(TextureGpuMipmap, FileGeneratesCompleteChainAndAveragedPixels)
    {
        ASSERT_NO_FATAL_FAILURE(WriteCheckerboard());
        auto texture = Kans::Texture2D::Create(Specification(), temp);
        CheckChain(texture, 8, 4);
        const auto pixel = ReadGpuTexture(texture->GetResource(), 3);
        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(pixel[i], 128, 1);
        EXPECT_EQ(pixel[3], 255);
    }

    TEST_F(TextureGpuMipmap, DisabledKeepsOnlyBaseLevel)
    {
        ASSERT_NO_FATAL_FAILURE(WriteCheckerboard());
        auto                  texture = Kans::Texture2D::Create(Specification(false), temp);
        EXPECT_EQ(texture->GetResource()->GetDesc().MipLevels, 1u);
        EXPECT_EQ(texture->GetSampling().Min, Kans::RenderHardware::Filter::Linear);
        EXPECT_EQ(texture->GetSampling().MaxLod, 0);
    }

    TEST_F(TextureGpuMipmap, EditorIconsGenerateNonPowerOfTwoChains)
    {
        for (const char* name : {"FBX",
                                 "Folder",
                                 "Image",
                                 "Model",
                                 "Scene",
                                 "Material",
                                 "Script",
                                 "Shader",
                                 "Audio",
                                 "File",
                                 "OBJ",
                                 "GLTF",
                                 "GLB",
                                 "Blend",
                                 "DAE",
                                 "Mesh"})
        {
            SCOPED_TRACE(name);
            const auto path = std::filesystem::path(KANS_TEST_ICON_DIR) / (std::string(name) + ".png");
            ASSERT_TRUE(std::filesystem::exists(path));
            auto texture = Kans::Texture2D::Create(Specification(), path);
            CheckChain(texture, texture->GetWidth(), texture->GetHeight());
            const auto levels = texture->GetResource()->GetDesc().MipLevels;
            const auto pixel  = ReadGpuTexture(texture->GetResource(), levels - 1);
            EXPECT_GT(pixel[3], 0);
            EXPECT_GT(pixel[0] + pixel[1] + pixel[2], 0);
        }
    }
    TEST_F(TextureGpuMipmap, ProductUploadPreservesAuthoredMipsAndSrgb)
    {
        using namespace Kans;
        TextureData data;
        data.Width      = 3;
        data.Height     = 2;
        data.ColorSpace = TextureColorSpace::SRGB;
        data.Mips.resize(2);
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 3; ++x)
                for (unsigned component = 0; component < 4; ++component)
                    data.Mips[0].Pixels.push_back(std::byte(component == 3 ? 255 : y * 30 + x * 4 + component));
        data.Mips[1].Pixels = {std::byte {17}, std::byte {53}, std::byte {91}, std::byte {127}};
        auto encoded        = EncodeTextureProduct(data);
        ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
        const auto& bytes  = std::get<std::vector<std::byte>>(encoded);
        auto        loaded = Loader<TextureAsset> {}.Load(
            {AssetID {SourceAssetID::Generate(), 1}, AssetType::Texture, bytes.data(), bytes.size()});
        ASSERT_TRUE(std::holds_alternative<Ref<TextureAsset>>(loaded));
        auto uploaded = Texture2D::CreateFromProduct(std::get<Ref<TextureAsset>>(loaded)->GetData());
        ASSERT_TRUE(std::holds_alternative<Ref<Texture2D>>(uploaded));
        const auto& texture = std::get<Ref<Texture2D>>(uploaded);
        EXPECT_EQ(texture->GetResource()->GetDesc().PixelFormat, RenderHardware::Format::RGBA8Srgb);
        EXPECT_EQ(texture->GetResource()->GetDesc().MipLevels, 2u);
        const auto actual = ReadGpuTexture(texture->GetResource());
        EXPECT_TRUE(std::equal(
            actual.begin(), actual.begin() + 12, reinterpret_cast<const uint8_t*>(data.Mips[0].Pixels.data()) + 12));
        EXPECT_TRUE(std::equal(
            actual.begin() + 12, actual.end(), reinterpret_cast<const uint8_t*>(data.Mips[0].Pixels.data())));
        const auto mip = ReadGpuTexture(texture->GetResource(), 1);
        EXPECT_TRUE(std::equal(mip.begin(), mip.end(), reinterpret_cast<const uint8_t*>(data.Mips[1].Pixels.data())));
    }

    TEST_F(TextureGpuMipmap, ProductUploadSupportsOddWidthDataAndNativeFloatValues)
    {
        using namespace Kans;
        for (auto format : {TexturePixelFormat::R8,
                            TexturePixelFormat::RG8,
                            TexturePixelFormat::RGBA8,
                            TexturePixelFormat::RGBA32Float})
        {
            TextureData data;
            data.Width     = 3;
            data.Height    = 1;
            data.Format    = format;
            data.Origin    = TextureOrigin::BottomLeft;
            data.Usage     = TextureUsage::Data;
            data.AlphaMode = TextureAlphaMode::Opaque;
            data.Mips.resize(1);
            size_t components = format == TexturePixelFormat::R8 ? 1 : format == TexturePixelFormat::RG8 ? 2 : 4;
            for (size_t i = 0; i < 3 * components; ++i)
            {
                if (format == TexturePixelFormat::RGBA32Float)
                {
                    float    value = float(i) * 0.25f - 1;
                    uint32_t bits  = 0;
                    std::memcpy(&bits, &value, 4);
                    for (unsigned b = 0; b < 4; ++b)
                        data.Mips[0].Pixels.push_back(std::byte((bits >> (b * 8)) & 255));
                }
                else
                    data.Mips[0].Pixels.push_back(std::byte(i * 9 + 1));
            }
            auto uploaded = Texture2D::CreateFromProduct(data);
            ASSERT_TRUE(std::holds_alternative<Ref<Texture2D>>(uploaded));
            if (format == TexturePixelFormat::RGBA32Float)
            {
                const auto actual = ReadGpuFloatTexture(std::get<Ref<Texture2D>>(uploaded)->GetResource());
                for (size_t i = 0; i < actual.size(); ++i)
                    EXPECT_FLOAT_EQ(actual[i], float(i) * 0.25f - 1);
            }
            else
            {
                std::vector<std::byte> actual(3 * components);
                const auto             bytes = ReadGpuTexture(std::get<Ref<Texture2D>>(uploaded)->GetResource());
                std::memcpy(actual.data(), bytes.data(), bytes.size());
                EXPECT_EQ(actual, data.Mips[0].Pixels);
            }
        }
    }

    TEST_F(TextureGpuMipmap, ProductUploadRejectsMalformedAndUnsupportedShapes)
    {
        using namespace Kans;
        TextureData data;
        EXPECT_EQ(std::get<AssetError>(Texture2D::CreateFromProduct(data)).Code, AssetErrorCode::InvalidData);
        data.Kind  = TextureKind::Array2D;
        data.Width = data.Height = 1;
        data.Layers              = 2;
        data.Mips                = {{std::vector<std::byte>(8)}};
        EXPECT_EQ(std::get<AssetError>(Texture2D::CreateFromProduct(data)).Code, AssetErrorCode::NotImplemented);
    }
} // namespace
