#include "ReferenceImage.h"
#include <png.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Kans::Test
{
    namespace
    {
        void Validate(const RgbaImage& image)
        {
            if (!image.Width || !image.Height ||
                uint64_t(image.Width) * image.Height > std::numeric_limits<size_t>::max() / 4 ||
                image.Pixels.size() != size_t(image.Width) * image.Height * 4)
                throw std::runtime_error("Invalid tightly packed RGBA8 image");
        }
        struct Png
        {
            png_image Image {};
            Png() { Image.version = PNG_IMAGE_VERSION; }
            ~Png() { png_image_free(&Image); }
        };
    } // namespace
    void WritePng(const std::filesystem::path& path, const RgbaImage& image)
    {
        Validate(image);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        Png png;
        png.Image.width  = image.Width;
        png.Image.height = image.Height;
        png.Image.format = PNG_FORMAT_RGBA;
        if (!png_image_write_to_file(&png.Image, path.string().c_str(), 0, image.Pixels.data(), 0, nullptr))
            throw std::runtime_error("Writing " + path.string() + ": " + png.Image.message);
    }
    RgbaImage ReadPng(const std::filesystem::path& path)
    {
        Png png;
        if (!png_image_begin_read_from_file(&png.Image, path.string().c_str()))
            throw std::runtime_error("Reading " + path.string() + ": " + png.Image.message);
        png.Image.format = PNG_FORMAT_RGBA;
        RgbaImage image {png.Image.width, png.Image.height, std::vector<uint8_t>(PNG_IMAGE_SIZE(png.Image))};
        if (!png_image_finish_read(&png.Image, nullptr, image.Pixels.data(), 0, nullptr))
            throw std::runtime_error("Decoding " + path.string() + ": " + png.Image.message);
        return image;
    }
    ReferenceResult CompareReference(const RgbaImage&             actual,
                                     const std::filesystem::path& reference,
                                     const std::filesystem::path& outputDirectory,
                                     ImageTolerance               tolerance)
    {
        ReferenceResult result;
        try
        {
            Validate(actual);
            if (!std::isfinite(tolerance.MaxDifferentPixelFraction) || tolerance.MaxDifferentPixelFraction < 0 ||
                tolerance.MaxDifferentPixelFraction > 1)
                throw std::runtime_error("Pixel fraction tolerance must be in [0, 1]");
            const char* update = std::getenv("KANS_UPDATE_REFERENCES");
            if (update && std::string(update) == "1")
            {
                WritePng(reference, actual);
                result.Passed  = true;
                result.Message = "Updated " + reference.string();
                return result;
            }
            RgbaImage   diff {actual.Width, actual.Height, std::vector<uint8_t>(actual.Pixels.size(), 0)};
            std::string reason;
            try
            {
                const auto expected = ReadPng(reference);
                if (actual.Width != expected.Width || actual.Height != expected.Height)
                    throw std::runtime_error("Reference dimensions differ: " + std::to_string(expected.Width) + "x" +
                                             std::to_string(expected.Height));
                for (size_t pixel = 0; pixel < actual.Pixels.size() / 4; ++pixel)
                {
                    unsigned maximum = 0;
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        const size_t   i     = pixel * 4 + channel;
                        const unsigned delta = unsigned(std::abs(int(actual.Pixels[i]) - int(expected.Pixels[i])));
                        maximum              = std::max(maximum, delta);
                        if (channel < 3)
                            diff.Pixels[i] = uint8_t(std::min(255u, delta * 8));
                    }
                    // Alpha-only differences must also be visible in the diff.
                    diff.Pixels[pixel * 4] = std::max(
                        diff.Pixels[pixel * 4],
                        uint8_t(std::min(
                            255,
                            std::abs(int(actual.Pixels[pixel * 4 + 3]) - int(expected.Pixels[pixel * 4 + 3])) * 8)));
                    diff.Pixels[pixel * 4 + 3]  = 255;
                    result.MaxChannelDifference = std::max(result.MaxChannelDifference, maximum);
                    result.DifferentPixels += maximum > tolerance.MaxChannelDifference;
                }
            }
            catch (const std::exception& error)
            {
                reason                      = error.what();
                result.DifferentPixels      = actual.Pixels.size() / 4;
                result.MaxChannelDifference = 255;
                std::fill(diff.Pixels.begin(), diff.Pixels.end(), 255);
            }
            result.DifferentPixelFraction = double(result.DifferentPixels) / (actual.Pixels.size() / 4);
            result.Passed = reason.empty() && result.DifferentPixelFraction <= tolerance.MaxDifferentPixelFraction;
            std::ostringstream message;
            message << reference << ": " << result.DifferentPixels << "/" << actual.Pixels.size() / 4
                    << " pixels exceed " << unsigned(tolerance.MaxChannelDifference)
                    << ", fraction=" << result.DifferentPixelFraction
                    << " (allowed=" << tolerance.MaxDifferentPixelFraction
                    << "), max channel difference=" << result.MaxChannelDifference;
            if (!result.Passed)
            {
                const auto actualPath = outputDirectory / (reference.stem().string() + ".actual.png");
                const auto diffPath   = outputDirectory / (reference.stem().string() + ".diff.png");
                WritePng(actualPath, actual);
                WritePng(diffPath, diff);
                message << "; " << reason << "; actual=" << actualPath << "; diff=" << diffPath
                        << " (absolute difference amplified 8x)";
            }
            result.Message = message.str();
        }
        catch (const std::exception& error)
        {
            result.Message = error.what();
        }
        return result;
    }
} // namespace Kans::Test
