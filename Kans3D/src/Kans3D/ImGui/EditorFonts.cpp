#include "kspch.h"
#include "EditorFonts.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "Kans3D/FileSystem/FileSystem.h"

namespace Kans
{
    namespace
    {
        ImFont* LoadFont(const std::filesystem::path& path,
                         float                        size,
                         ImFontConfig                 config = {},
                         const ImWchar*               ranges = nullptr)
        {
            // AddFontFromFileTTF asserts when reading fails. Read first so missing/unreadable resources can fall back.
            size_t bytes = 0;
            void*  data  = ImFileLoadToMemory(path.generic_string().c_str(), "rb", &bytes);
            if (!data || bytes == 0 || bytes > size_t(INT_MAX))
            {
                if (data)
                    ImGui::MemFree(data);
                if (Log::GetCoreLogger())
                    CORE_ERROR("Cannot load editor font '{}'; using built-in text font or omitting optional icons.",
                               path.string());
                else
                    std::cerr << "Cannot load editor font '" << path.string()
                              << "'; using built-in text font or omitting optional icons.\n";
                return nullptr;
            }
            ImFormatString(
                config.Name, IM_ARRAYSIZE(config.Name), "%s, %.0fpx", path.filename().string().c_str(), size);
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, int(bytes), size, &config, ranges);
        }
    } // namespace

    void LoadEditorFonts()
    {
        auto&                io           = ImGui::GetIO();
        static const ImWchar iconRanges[] = {0xe000, 0xf8ff, 0};
        const auto           folder       = KansFileSystem::GetFontsFolder();
        const auto           mergeIcons   = [&] {
            ImFontConfig config;
            config.MergeMode        = true;
            config.PixelSnapH       = true;
            config.GlyphMinAdvanceX = 16.0f;
            LoadFont(folder / "FontAwesome/fa-solid-900.ttf", 14.0f, config, iconRanges);
        };
        const auto textFont = [&](const char* name) {
            auto* font = LoadFont(folder / name, 16.0f);
            if (!font)
            {
                ImFontConfig config;
                config.SizePixels = 16.0f;
                font              = io.Fonts->AddFontDefault(&config);
            }
            mergeIcons();
            return font;
        };
        io.FontDefault = textFont("Roboto/Roboto-Regular.ttf");
        textFont("Roboto/Roboto-Bold.ttf");
    }
} // namespace Kans
