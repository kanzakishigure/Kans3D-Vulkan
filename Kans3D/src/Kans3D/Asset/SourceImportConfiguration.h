#pragma once
#include <cstddef>
#include <map>
#include <string>

namespace Kans
{
    struct SourceImportConfiguration
    {
        // Empty selects the registered default for the source extension.
        std::string                        Importer;
        std::map<std::string, std::string> Settings;
        static constexpr size_t            MaximumBytes    = 1024 * 1024;
        static constexpr size_t            MaximumString   = 64 * 1024;
        static constexpr size_t            MaximumSettings = 4096;

        bool IsValid() const noexcept
        {
            if (Importer.size() > MaximumString || Importer.find('\0') != std::string::npos ||
                !IsPersistableText(Importer) || Settings.size() > MaximumSettings)
                return false;
            size_t bytes = Importer.size();
            for (const auto& [key, value] : Settings)
            {
                if (key.empty() || key.size() > MaximumString || key.find('\0') != std::string::npos ||
                    value.size() > MaximumString || key.size() > MaximumBytes - bytes || !IsPersistableText(key) ||
                    !IsPersistableText(value))
                    return false;
                bytes += key.size();
                if (value.size() > MaximumBytes - bytes)
                    return false;
                bytes += value.size();
            }
            return true;
        }
        void Swap(SourceImportConfiguration& other) noexcept
        {
            Importer.swap(other.Importer);
            Settings.swap(other.Settings);
        }

    private:
        static bool IsPersistableText(const std::string& text) noexcept;
    };
} // namespace Kans
