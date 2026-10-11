#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kans::Test
{
    // RGBA8, tightly packed, top row first; no implicit gamma conversion.
    struct RgbaImage
    {
        uint32_t             Width = 0, Height = 0;
        std::vector<uint8_t> Pixels;
    };
    struct ImageTolerance
    {
        uint8_t MaxChannelDifference;
        double  MaxDifferentPixelFraction;
    };
    struct ReferenceResult
    {
        bool        Passed                 = false;
        size_t      DifferentPixels        = 0;
        unsigned    MaxChannelDifference   = 0;
        double      DifferentPixelFraction = 0;
        std::string Message;
    };
    // KANS_UPDATE_REFERENCES=1 writes the PNG. Missing/malformed references fail.
    // A pixel differs if ANY of its four channels exceeds MaxChannelDifference.
    // Artifacts are named <reference stem>.actual.png and .diff.png in outputDirectory.
    ReferenceResult CompareReference(const RgbaImage&             actual,
                                     const std::filesystem::path& reference,
                                     const std::filesystem::path& outputDirectory,
                                     ImageTolerance               tolerance);
    void            WritePng(const std::filesystem::path& path, const RgbaImage& image);
    RgbaImage       ReadPng(const std::filesystem::path& path);
} // namespace Kans::Test
