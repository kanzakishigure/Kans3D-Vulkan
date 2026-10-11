#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include "ReferenceImage.h"

namespace
{
    using namespace Kans::Test;
    class ReferenceImageTest : public testing::Test
    {
    protected:
        std::filesystem::path Directory;
        void                  SetUp() override
        {
            // These tests exercise compare mode; an update-mode run of the GPU
            // baseline must not accidentally turn negative CPU tests into passes.
            const char* update = std::getenv("KANS_UPDATE_REFERENCES");
            if (update && std::string(update) == "1")
                GTEST_SKIP() << "Run utility tests without update mode";
            Directory = std::filesystem::temp_directory_path() /
                        ("kans-reference-utility-" +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(Directory);
        }
        void TearDown() override
        {
            if (!Directory.empty())
                std::filesystem::remove_all(Directory);
        }
    };
    TEST_F(ReferenceImageTest, RoundTripAndToleranceBoundariesIncludeAlpha)
    {
        RgbaImage  image {2, 2, {12, 23, 34, 255, 45, 56, 67, 128, 78, 89, 90, 64, 101, 112, 123, 0}};
        const auto reference = Directory / "expected.png";
        WritePng(reference, image);
        EXPECT_EQ(ReadPng(reference).Pixels, image.Pixels);
        EXPECT_TRUE(CompareReference(image, reference, Directory, {0, 0}).Passed);
        image.Pixels[7] += 3;
        EXPECT_TRUE(CompareReference(image, reference, Directory, {3, 0}).Passed);
        EXPECT_TRUE(CompareReference(image, reference, Directory, {2, 0.25}).Passed);
        const auto failed = CompareReference(image, reference, Directory, {2, 0.249});
        EXPECT_FALSE(failed.Passed);
        EXPECT_EQ(failed.DifferentPixels, 1u);
        EXPECT_EQ(failed.MaxChannelDifference, 3u);
        EXPECT_DOUBLE_EQ(failed.DifferentPixelFraction, 0.25);
        EXPECT_EQ(ReadPng(Directory / "expected.actual.png").Pixels, image.Pixels);
        EXPECT_EQ(ReadPng(Directory / "expected.diff.png").Pixels[4], 24);
    }
    TEST_F(ReferenceImageTest, UpdateFlagCreatesAndReplacesReference)
    {
        struct UpdateFlag
        {
            static void Set(const char* value)
            {
#ifdef _WIN32
                _putenv_s("KANS_UPDATE_REFERENCES", value ? value : "");
#else
                if (value)
                    setenv("KANS_UPDATE_REFERENCES", value, 1);
                else
                    unsetenv("KANS_UPDATE_REFERENCES");
#endif
            }
            std::string Previous;
            bool        HadPrevious;
            UpdateFlag() : HadPrevious(std::getenv("KANS_UPDATE_REFERENCES") != nullptr)
            {
                if (HadPrevious)
                    Previous = std::getenv("KANS_UPDATE_REFERENCES");
                Set("1");
            }
            ~UpdateFlag() { Set(HadPrevious ? Previous.c_str() : nullptr); }
        };
        const auto reference = Directory / "update.png";
        RgbaImage  image {1, 1, {20, 40, 60, 255}};
        {
            UpdateFlag flag;
            ASSERT_TRUE(CompareReference(image, reference, Directory, {0, 0}).Passed);
            EXPECT_EQ(ReadPng(reference).Pixels, image.Pixels);
            image.Pixels[0] = 90;
            ASSERT_TRUE(CompareReference(image, reference, Directory, {0, 0}).Passed);
            EXPECT_EQ(ReadPng(reference).Pixels, image.Pixels);
        }
        image.Pixels[0] = 20;
        EXPECT_FALSE(CompareReference(image, reference, Directory, {0, 0}).Passed);
        EXPECT_EQ(ReadPng(reference).Pixels[0], 90);
    }
    TEST_F(ReferenceImageTest, MissingMalformedWrongDimensionsAndInvalidInputsFail)
    {
        RgbaImage  image {1, 1, {1, 2, 3, 255}};
        const auto reference = Directory / "missing.png";
        EXPECT_FALSE(CompareReference(image, reference, Directory, {255, 1}).Passed);
        EXPECT_NE(CompareReference(image, reference, Directory, {0, 0}).Message.find("actual="), std::string::npos);
        {
            std::ofstream broken(reference);
            broken << "not a PNG";
        }
        EXPECT_FALSE(CompareReference(image, reference, Directory, {255, 1}).Passed);
        WritePng(reference, {2, 1, {1, 2, 3, 255, 1, 2, 3, 255}});
        EXPECT_FALSE(CompareReference(image, reference, Directory, {255, 1}).Passed);
        EXPECT_FALSE(CompareReference(image, reference, Directory, {0, -0.1}).Passed);
        EXPECT_FALSE(CompareReference({1, 1, {}}, reference, Directory, {0, 0}).Passed);
    }
} // namespace
