#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/ImportFingerprint.h"
#include "Kans3D/Asset/Importer/TextureImporter.h"
#include "Kans3D/Asset/Importer/TextureSource.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Storage/ProductStore.h"
#include "Kans3D/Project/Project.h"
#include "stb_image/stb_image.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;
    using Bytes  = std::vector<std::byte>;
    void Big(Bytes& bytes, uint32_t value)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 255));
    }
    void Append(Bytes& bytes, std::initializer_list<unsigned> values)
    {
        for (unsigned value : values)
            bytes.push_back(static_cast<std::byte>(value));
    }
    uint32_t CRC(const std::byte* data, size_t size)
    {
        uint32_t result = 0xffffffffu;
        for (size_t i = 0; i < size; ++i)
        {
            result ^= std::to_integer<uint8_t>(data[i]);
            for (unsigned bit = 0; bit < 8; ++bit)
                result = (result >> 1) ^ ((result & 1) ? 0xedb88320u : 0u);
        }
        return result ^ 0xffffffffu;
    }
    void Chunk(Bytes& bytes, const char* type, const Bytes& data)
    {
        Big(bytes, static_cast<uint32_t>(data.size()));
        const size_t start = bytes.size();
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(static_cast<std::byte>(type[i]));
        bytes.insert(bytes.end(), data.begin(), data.end());
        Big(bytes, CRC(bytes.data() + start, data.size() + 4));
    }
    Bytes PNG(uint32_t     width,
              uint32_t     height,
              const Bytes& rows,
              unsigned     bits      = 8,
              const char*  extraType = nullptr,
              const Bytes& extra     = {})
    {
        Bytes bytes;
        Append(bytes, {137, 80, 78, 71, 13, 10, 26, 10});
        Bytes header;
        Big(header, width);
        Big(header, height);
        Append(header, {bits, 6, 0, 0, 0});
        Chunk(bytes, "IHDR", header);
        if (extraType)
            Chunk(bytes, extraType, extra);
        // An uncompressed zlib block makes fixture pixel values independent of
        // the decoder and does not need an external image-writing dependency.
        Bytes compressed;
        Append(compressed,
               {0x78,
                0x01,
                1,
                unsigned(rows.size() & 255),
                unsigned(rows.size() >> 8),
                unsigned((~rows.size()) & 255),
                unsigned(((~rows.size()) >> 8) & 255)});
        compressed.insert(compressed.end(), rows.begin(), rows.end());
        uint32_t a = 1, b = 0;
        for (std::byte value : rows)
        {
            a = (a + std::to_integer<uint8_t>(value)) % 65521;
            b = (b + a) % 65521;
        }
        Big(compressed, b << 16 | a);
        Chunk(bytes, "IDAT", compressed);
        Chunk(bytes, "IEND", {});
        return bytes;
    }
    Bytes ColorPNG(const char* extraType = nullptr, const Bytes& extra = {})
    {
        Bytes rows;
        Append(rows, {0, 255, 0, 0, 128, 0, 255, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255, 255});
        return PNG(2, 2, rows, 8, extraType, extra);
    }
    Bytes TGA()
    {
        Bytes bytes(18);
        bytes[2]  = std::byte {2};
        bytes[12] = std::byte {2};
        bytes[14] = std::byte {2};
        bytes[16] = std::byte {32};
        bytes[17] = std::byte {0x28};
        Append(bytes, {0, 0, 255, 128, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255});
        return bytes;
    }
    float FloatAt(const Bytes& bytes, size_t component)
    {
        uint32_t bits = 0;
        for (unsigned i = 0; i < 4; ++i)
            bits |= uint32_t(std::to_integer<uint8_t>(bytes[component * 4 + i])) << (8 * i);
        float result;
        std::memcpy(&result, &bits, 4);
        return result;
    }
    class TextureImporterTest : public testing::Test
    {
    protected:
        fs::path Root;
        void     SetUp() override
        {
            Root = fs::temp_directory_path() / ("Kans3D-Image-" + SourceAssetID::Generate().ToString());
            ASSERT_TRUE(fs::create_directory(Root));
        }
        void TearDown() override
        {
            std::error_code error;
            fs::remove_all(Root, error);
        }
        fs::path Write(const char* name, const Bytes& bytes)
        {
            const auto path = Root / fs::u8path(name);
            EXPECT_TRUE(std::holds_alternative<std::monostate>(WriteProductFile(path, bytes)));
            return path;
        }
        TextureData Parse(const fs::path& path, TextureImportSettings settings = {})
        {
            auto result = ParseTextureSource(path, settings);
            if (const auto* error = std::get_if<AssetError>(&result))
                ADD_FAILURE() << error->Message;
            return std::holds_alternative<TextureData>(result) ? std::get<TextureData>(std::move(result)) :
                                                                 TextureData {};
        }
        ImportContext Context(const fs::path& path, const char* stage = "stage")
        {
            ImportContext context;
            context.SourceID         = SourceAssetID::Generate();
            context.SourceFile       = path;
            context.StagingDirectory = Root / stage;
            EXPECT_TRUE(fs::create_directory(context.StagingDirectory));
            return context;
        }
    };

    TEST(TextureImportSettings, StrictParsingRejectsUnknownAndIncompatibleValues)
    {
        auto result = ParseTextureImportSettings({{"usage", "normal"},
                                                  {"color_space", "auto"},
                                                  {"alpha_mode", "auto"},
                                                  {"generate_mips", "false"},
                                                  {"flip_y", "true"},
                                                  {"normal_flip_y", "true"}});
        ASSERT_TRUE(std::holds_alternative<TextureImportSettings>(result));
        const auto& settings = std::get<TextureImportSettings>(result);
        EXPECT_EQ(settings.Usage, TextureUsage::Normal);
        EXPECT_TRUE(settings.FlipY);
        EXPECT_TRUE(settings.NormalFlipY);
        EXPECT_FALSE(settings.GenerateMips);
        EXPECT_FALSE(settings.ColorSpace);
        EXPECT_FALSE(settings.AlphaMode);
        for (const auto& values :
             std::vector<std::map<std::string, std::string>> {{{"typo", "true"}},
                                                              {{"usage", "Normal"}},
                                                              {{"color_space", "hdr"}},
                                                              {{"alpha_mode", "blend"}},
                                                              {{"generate_mips", "1"}},
                                                              {{"usage", "normal"}, {"color_space", "srgb"}},
                                                              {{"usage", "data"}, {"alpha_mode", "premultiplied"}},
                                                              {{"normal_flip_y", "true"}}})
            EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureImportSettings(values)));
        TextureImportSettings invalid;
        invalid.Usage = static_cast<TextureUsage>(65535);
        EXPECT_TRUE(std::holds_alternative<AssetError>(ValidateTextureImportSettings(invalid)));
    }

    TEST_F(TextureImporterTest, UnicodePngAndTgaPreservePixelsAndProduceColorMips)
    {
        const auto png = Parse(Write("颜色.png", ColorPNG()));
        const auto tga = Parse(Write("pixels.tga", TGA()));
        EXPECT_EQ(png.Width, 2u);
        EXPECT_EQ(png.Height, 2u);
        ASSERT_EQ(png.Mips.size(), 2u);
        EXPECT_EQ(png.ColorSpace, TextureColorSpace::SRGB);
        EXPECT_EQ(png.AlphaMode, TextureAlphaMode::Straight);
        EXPECT_EQ(png.Origin, TextureOrigin::TopLeft);
        EXPECT_EQ(png.Mips[0].Pixels, tga.Mips[0].Pixels);
        Bytes expected;
        Append(expected, {255, 0, 0, 128, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255});
        EXPECT_EQ(png.Mips[0].Pixels, expected);
        EXPECT_EQ(png.Mips[1].Pixels, tga.Mips[1].Pixels);
    }

    TEST_F(TextureImporterTest, RowFlipAndNormalConventionAreIndependent)
    {
        const auto            path = Write("normal.png", ColorPNG());
        TextureImportSettings settings;
        settings.Usage        = TextureUsage::Normal;
        settings.GenerateMips = false;
        settings.FlipY        = true;
        settings.NormalFlipY  = true;
        const auto flipped    = Parse(path, settings);
        EXPECT_EQ(flipped.ColorSpace, TextureColorSpace::Linear);
        EXPECT_EQ(flipped.Origin, TextureOrigin::BottomLeft);
        ASSERT_EQ(flipped.Mips.size(), 1u);
        Bytes expected;
        Append(expected, {0, 255, 255, 255, 255, 0, 255, 255, 255, 255, 0, 128, 0, 0, 0, 255});
        EXPECT_EQ(flipped.Mips[0].Pixels, expected);
    }

    TEST_F(TextureImporterTest, RealJpegAndBmpDecodeAndTruncatedPayloadsAreRejected)
    {
        TextureImportSettings settings;
        settings.GenerateMips = false;
        const auto jpeg =
            fs::path(KANS_TEST_ASSET_DIR) / "new room scene/food_apple_01_4k.gltf/textures/food_apple_01_rough_4k.jpg";
        const auto bmp = fs::path(KANS_TEST_ASSET_DIR) / "ht/toon05.bmp";
        for (const auto& path : {jpeg, bmp})
        {
            SCOPED_TRACE(path.u8string());
            const auto decoded = Parse(path, settings);
            EXPECT_GT(decoded.Width, 0u);
            EXPECT_GT(decoded.Height, 0u);
            ASSERT_EQ(decoded.Mips.size(), 1u);
            EXPECT_EQ(decoded.Format, TexturePixelFormat::RGBA8);
            auto read = ReadProductFile(path);
            ASSERT_TRUE(std::holds_alternative<Bytes>(read));
            auto bytes = std::get<Bytes>(std::move(read));
            bytes.resize(bytes.size() / 2);
            EXPECT_TRUE(std::holds_alternative<AssetError>(
                ParseTextureSource(Write(path.extension() == ".jpg" ? "short.jpg" : "short.bmp", bytes), settings)));
        }
    }

    TEST_F(TextureImporterTest, RleHdrAndTgaValidatePacketCountsBeforeDecoding)
    {
        const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n";
        Bytes             hdr;
        for (char c : header)
            hdr.push_back(static_cast<std::byte>(c));
        Append(hdr, {2, 2, 0, 8, 136, 128, 136, 64, 136, 32, 136, 130});
        const auto data = Parse(Write("rle.hdr", hdr));
        ASSERT_EQ(data.Width, 8u);
        EXPECT_FLOAT_EQ(FloatAt(data.Mips[0].Pixels, 0), 2.0f);
        hdr[header.size() + 4] = std::byte {137};
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("bad-rle.hdr", hdr))));
        auto tga = TGA();
        tga.resize(18);
        tga[2] = std::byte {10};
        Append(tga, {131, 0, 0, 255, 255});
        const auto rle = Parse(Write("rle.tga", tga));
        ASSERT_EQ(rle.Mips[0].Pixels.size(), 16u);
        EXPECT_EQ(rle.Mips[0].Pixels[0], std::byte {255});
        tga[18] = std::byte {132};
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("bad-rle.tga", tga))));
    }

    TEST_F(TextureImporterTest, BmpCorePaletteAndLegalPixelGapDecodeWithoutInvalidPointerArithmetic)
    {
        Bytes core;
        Append(core, {'B', 'M', 33, 0, 0, 0, 0, 0, 0, 0, 29, 0,   0, 0, 12, 0, 0,
                      0,   1,   0,  1, 0, 1, 0, 8, 0, 0, 0,  255, 0, 0, 0,  0});
        const auto parsed = Parse(Write("core.bmp", core));
        ASSERT_EQ(parsed.Width, 1u);
        EXPECT_EQ(parsed.Mips[0].Pixels[0], std::byte {255});
        core[29] = std::byte {1};
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("bad-core.bmp", core))));
        auto read = ReadProductFile(fs::path(KANS_TEST_ASSET_DIR) / "ht/toon05.bmp");
        ASSERT_TRUE(std::holds_alternative<Bytes>(read));
        auto gap = std::get<Bytes>(read);
        gap.insert(gap.begin() + 54, 4, std::byte {0});
        gap[10]             = std::byte {58};
        const uint32_t size = static_cast<uint32_t>(gap.size());
        for (unsigned i = 0; i < 4; ++i)
            gap[2 + i] = static_cast<std::byte>((size >> (8 * i)) & 255);
        const auto padded = Parse(Write("gap.bmp", gap));
        EXPECT_EQ(padded.Width, 16u);
        auto original = Parse(fs::path(KANS_TEST_ASSET_DIR) / "ht/toon05.bmp");
        EXPECT_EQ(padded.Mips[0].Pixels, original.Mips[0].Pixels);
    }

    TEST_F(TextureImporterTest, ExifOrientationMustBeIdentityAndMalformedOffsetsFail)
    {
        Bytes exif;
        Append(exif, {'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 18, 1, 3, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0});
        EXPECT_EQ(Parse(Write("identity.png", ColorPNG("eXIf", exif))).Width, 2u);
        exif[18] = std::byte {6};
        EXPECT_EQ(std::get<AssetError>(ParseTextureSource(Write("rotated.png", ColorPNG("eXIf", exif)))).Code,
                  AssetErrorCode::NotImplemented);
        exif[4] = std::byte {255};
        EXPECT_EQ(std::get<AssetError>(ParseTextureSource(Write("bad-exif.png", ColorPNG("eXIf", exif)))).Code,
                  AssetErrorCode::InvalidData);
    }

    TEST_F(TextureImporterTest, PremultiplyUsesLinearLightAndLinearMetadataIsHonored)
    {
        TextureImportSettings settings;
        settings.AlphaMode    = TextureAlphaMode::Premultiplied;
        settings.GenerateMips = false;
        const auto color      = Parse(Write("premult.png", ColorPNG()), settings);
        ASSERT_FALSE(color.Mips.empty());
        EXPECT_EQ(std::to_integer<unsigned>(color.Mips[0].Pixels[0]), 188u);
        EXPECT_EQ(color.AlphaMode, TextureAlphaMode::Premultiplied);
        Bytes gamma;
        Big(gamma, 100000);
        const auto linear = Parse(Write("linear.png", ColorPNG("gAMA", gamma)), settings);
        EXPECT_EQ(linear.ColorSpace, TextureColorSpace::Linear);
        EXPECT_EQ(std::to_integer<unsigned>(linear.Mips[0].Pixels[0]), 128u);
    }

    TEST_F(TextureImporterTest, SixteenBitPngRetainsPrecisionAndConvertsColorToLinear)
    {
        Bytes rows;
        Append(rows, {0, 0x80, 0x01, 0, 1, 0xff, 0xff, 0x80, 0});
        const auto path  = Write("wide.png", PNG(1, 1, rows, 16));
        const auto color = Parse(path);
        ASSERT_EQ(color.Format, TexturePixelFormat::RGBA32Float);
        ASSERT_EQ(color.Mips[0].Pixels.size(), 16u);
        const float source = 32769.0f / 65535.0f;
        EXPECT_NEAR(FloatAt(color.Mips[0].Pixels, 0), std::pow((source + 0.055f) / 1.055f, 2.4f), 1e-6);
        EXPECT_FLOAT_EQ(FloatAt(color.Mips[0].Pixels, 3), 32768.0f / 65535.0f);
        TextureImportSettings settings;
        settings.Usage  = TextureUsage::Data;
        const auto data = Parse(path, settings);
        EXPECT_FLOAT_EQ(FloatAt(data.Mips[0].Pixels, 0), source);
        EXPECT_FLOAT_EQ(FloatAt(data.Mips[0].Pixels, 1), 1.0f / 65535.0f);
    }

    TEST_F(TextureImporterTest, HdrPreservesValuesBeyondOneAndRejectsTruncationAndSrgb)
    {
        const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
        Bytes             bytes;
        for (char c : header)
            bytes.push_back(static_cast<std::byte>(c));
        Append(bytes, {128, 64, 32, 130});
        const auto path = Write("image.hdr", bytes);
        const auto hdr  = Parse(path);
        ASSERT_EQ(hdr.Format, TexturePixelFormat::RGBA32Float);
        EXPECT_EQ(hdr.ColorSpace, TextureColorSpace::Linear);
        EXPECT_FLOAT_EQ(FloatAt(hdr.Mips[0].Pixels, 0), 2.0f);
        EXPECT_FLOAT_EQ(FloatAt(hdr.Mips[0].Pixels, 1), 1.0f);
        EXPECT_FLOAT_EQ(FloatAt(hdr.Mips[0].Pixels, 2), 0.5f);
        EXPECT_FLOAT_EQ(FloatAt(hdr.Mips[0].Pixels, 3), 1.0f);
        TextureImportSettings settings;
        settings.ColorSpace = TextureColorSpace::SRGB;
        EXPECT_EQ(std::get<AssetError>(ParseTextureSource(path, settings)).Code, AssetErrorCode::InvalidArgument);
        bytes.pop_back();
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("short.hdr", bytes))));
    }

    TEST_F(TextureImporterTest, MetadataProfilesAnimationAndUnknownGammaAreExplicitErrors)
    {
        Bytes gamma;
        Big(gamma, 70000);
        for (const auto& fixture :
             std::vector<std::pair<std::string, Bytes>> {{"profile.png", ColorPNG("iCCP", {})},
                                                         {"animated.png", ColorPNG("acTL", Bytes(8))},
                                                         {"gamma.png", ColorPNG("gAMA", gamma)},
                                                         {"primaries.png", ColorPNG("cHRM", Bytes(32))}})
            EXPECT_EQ(std::get<AssetError>(ParseTextureSource(Write(fixture.first.c_str(), fixture.second))).Code,
                      AssetErrorCode::NotImplemented);
        TextureImportSettings data;
        data.Usage = TextureUsage::Data;
        EXPECT_EQ(Parse(Root / "profile.png", data).Usage, TextureUsage::Data);
        TextureImportSettings explicitSpace;
        explicitSpace.ColorSpace = TextureColorSpace::Linear;
        EXPECT_EQ(Parse(Root / "gamma.png", explicitSpace).ColorSpace, TextureColorSpace::Linear);
    }

    TEST_F(TextureImporterTest, MissingEmptyCorruptAndOversizedInputsDoNotCreateProducts)
    {
        EXPECT_EQ(std::get<AssetError>(ParseTextureSource(Root / "missing.png")).Code, AssetErrorCode::NotFound);
        EXPECT_EQ(std::get<AssetError>(ParseTextureSource("relative.png")).Code, AssetErrorCode::InvalidArgument);
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("empty.png", {}))));
        auto png = ColorPNG();
        png[50] ^= std::byte {1};
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("corrupt.png", png))));
        auto tga = TGA();
        tga.pop_back();
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("short.tga", tga))));
        Bytes row;
        Append(row, {0, 0, 0, 0, 255});
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(Write("huge.png", PNG(16384, 16384, row)))));
        const auto sparse = Write("large.png", {});
        fs::resize_file(sparse, uint64_t(256) * 1024 * 1024 + 1);
        EXPECT_TRUE(std::holds_alternative<AssetError>(ParseTextureSource(sparse)));
    }

    TEST_F(TextureImporterTest, PrivateDecoderIgnoresAndPreservesLegacyFlipState)
    {
        const auto bytes = ColorPNG();
        const auto path  = Write("flips.png", bytes);
        stbi_set_flip_vertically_on_load(1);
        struct Reset
        {
            ~Reset() { stbi_set_flip_vertically_on_load(0); }
        } reset;
        const auto legacyBlueFirst = [&] {
            int   width, height, channels;
            auto* decoded = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                  static_cast<int>(bytes.size()),
                                                  &width,
                                                  &height,
                                                  &channels,
                                                  4);
            EXPECT_NE(decoded, nullptr);
            const bool blue = decoded && decoded[0] == 0 && decoded[2] == 255;
            stbi_image_free(decoded);
            return blue;
        };
        EXPECT_TRUE(legacyBlueFirst());
        const auto parsed = Parse(path);
        ASSERT_FALSE(parsed.Mips.empty());
        EXPECT_EQ(parsed.Mips[0].Pixels[0], std::byte {255});
        EXPECT_EQ(parsed.Mips[0].Pixels[2], std::byte {0});
        EXPECT_TRUE(legacyBlueFirst());
    }

    TEST_F(TextureImporterTest, ConcurrentParsersOwnPixelsAndKeepSettingsIndependent)
    {
        const auto                                                        path = Write("parallel.png", ColorPNG());
        std::promise<void>                                                start;
        const auto                                                        gate = start.get_future().share();
        std::array<std::future<std::variant<TextureData, AssetError>>, 8> tasks;
        for (size_t i = 0; i < tasks.size(); ++i)
            tasks[i] = std::async(std::launch::async, [&, i] {
                gate.wait();
                TextureImportSettings settings;
                settings.FlipY        = i % 2 != 0;
                settings.GenerateMips = i % 3 != 0;
                return ParseTextureSource(path, settings);
            });
        start.set_value();
        for (size_t i = 0; i < tasks.size(); ++i)
        {
            auto result = tasks[i].get();
            ASSERT_TRUE(std::holds_alternative<TextureData>(result));
            const auto& data = std::get<TextureData>(result);
            EXPECT_EQ(data.Mips.size(), i % 3 == 0 ? 1u : 2u);
            EXPECT_EQ(data.Mips[0].Pixels[0], i % 2 == 0 ? std::byte {255} : std::byte {0});
        }
    }

    TEST_F(TextureImporterTest, StableLocalIdKeepsHistoryAndOccupiedStageIsPreserved)
    {
        auto context             = Context(Write("source.png", ColorPNG()));
        context.PreviousLocalIDs = {{"texture:root", 19}, {"removed:key", 2}};
        TextureImporter importer;
        auto            result = importer.Import(context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(result));
        const auto imported = std::get<ImportResult>(result);
        EXPECT_EQ(imported.LocalIDs, context.PreviousLocalIDs);
        ASSERT_EQ(imported.Products.size(), 1u);
        EXPECT_EQ(imported.Products[0].LocalID, 19u);
        EXPECT_TRUE(imported.Products[0].Dependencies.empty());
        auto bytes = ReadProductFile(context.StagingDirectory / imported.Products[0].StagedFile);
        ASSERT_TRUE(std::holds_alternative<Bytes>(bytes));
        EXPECT_TRUE(std::holds_alternative<AssetError>(importer.Import(context)));
        const auto preserved = ReadProductFile(context.StagingDirectory / imported.Products[0].StagedFile);
        ASSERT_TRUE(std::holds_alternative<Bytes>(preserved));
        EXPECT_EQ(std::get<Bytes>(preserved), std::get<Bytes>(bytes));
        context.StagingDirectory = Root / "again";
        ASSERT_TRUE(fs::create_directory(context.StagingDirectory));
        context.Settings = {{"usage", "normal"}, {"normal_flip_y", "true"}};
        auto again       = importer.Import(context);
        ASSERT_TRUE(std::holds_alternative<ImportResult>(again));
        EXPECT_EQ(std::get<ImportResult>(again).LocalIDs, imported.LocalIDs);
    }

    TEST_F(TextureImporterTest, ProjectBuildPublishesReloadsAndSurvivesSourceDeletion)
    {
        fs::create_directory(Root / "assets");
        const auto    file = Write("assets/asset.PNG", ColorPNG());
        ProjectConfig config;
        config.Name          = "Texture project";
        config.RootDirectory = Root;
        auto opened          = Project::Open(config);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<Project>>(opened));
        auto       project = std::get<std::unique_ptr<Project>>(std::move(opened));
        const auto source  = project->GetSourceAssetDatabase().FindIDByPath(AssetPath("asset.PNG"));
        ASSERT_TRUE(source.IsValid());
        const auto build = [&] { return project->BuildAsset(source, {}); };
        auto       first = build();
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(first)) << std::get<AssetError>(first).Message;
        const auto published = project->GetRuntimeAssetRegistry()->GetAllProducts();
        ASSERT_EQ(published.size(), 1u);
        const auto id = published[0].Metadata.assetID;

        // Invalid settings and an undecodable source fail and keep the published Product.
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            project->SetSourceImportConfiguration(source, {"", {{"typo", "true"}}})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(build()));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(project->SetSourceImportConfiguration(source, {})));
        fs::remove(file);
        Write("assets/asset.PNG", Bytes {std::byte {0}});
        EXPECT_TRUE(std::holds_alternative<AssetError>(build()));
        EXPECT_EQ(project->GetRuntimeAssetRegistry()->GetAllProducts()[0].Location.ContentDigest,
                  published[0].Location.ContentDigest);

        // New settings produce new bytes under the same identity.
        fs::remove(file);
        Write("assets/asset.PNG", ColorPNG());
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            project->SetSourceImportConfiguration(source, {"", {{"generate_mips", "false"}, {"flip_y", "true"}}})));
        auto rebuilt = build();
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(rebuilt)) << std::get<AssetError>(rebuilt).Message;
        const auto republished = project->GetRuntimeAssetRegistry()->GetAllProducts();
        ASSERT_EQ(republished.size(), 1u);
        EXPECT_EQ(republished[0].Metadata.assetID, id);
        EXPECT_NE(republished[0].Location.ContentDigest, published[0].Location.ContentDigest);

        // Products load without the source or the project.
        const auto products = project->GetProductDirectory();
        project.reset();
        fs::remove(file);
        auto store = ProductStore::Open(products);
        ASSERT_TRUE(std::holds_alternative<Scope<ProductStore>>(store));
        auto provider = LooseFileAssetProvider::Open(products);
        ASSERT_TRUE(std::holds_alternative<Ref<LooseFileAssetProvider>>(provider));
        AssetManager manager(std::get<Ref<LooseFileAssetProvider>>(provider),
                             std::get<Scope<ProductStore>>(store)->Snapshot());
        auto         loaded = manager.Load<TextureAsset>(id);
        ASSERT_TRUE(std::holds_alternative<Ref<const TextureAsset>>(loaded));
        const auto& view = std::get<Ref<const TextureAsset>>(loaded)->GetData();
        ASSERT_EQ(view.Mips.size(), 1u);
        EXPECT_EQ(view.Origin, TextureOrigin::BottomLeft);
        EXPECT_EQ(view.Mips[0].Pixels[2], std::byte {255});
    }
} // namespace
