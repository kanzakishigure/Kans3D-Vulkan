#include "EditorAssetCachePath.h"
#include <system_error>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Kans
{
    std::variant<std::filesystem::path, AssetError> GetEditorAssetCachePath()
    {
        std::filesystem::path executable;
#if defined(__linux__)
        std::error_code ec;
        executable = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec)
            return AssetError {AssetErrorCode::IoError, "Cannot locate executable: " + ec.message()};
#elif defined(_WIN32)
        std::vector<wchar_t> buffer(512);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!length)
                return AssetError {AssetErrorCode::IoError, "Cannot locate executable"};
            if (length < buffer.size())
            {
                executable = std::wstring(buffer.data(), length);
                break;
            }
            if (buffer.size() >= 32768)
                return AssetError {AssetErrorCode::IoError, "Executable path is too long"};
            buffer.resize(buffer.size() * 2);
        }
#else
        return AssetError {AssetErrorCode::IoError, "Executable path lookup is unsupported on this platform"};
#endif
        return executable.parent_path() / "SourceAssetDatabase.cache.yaml";
    }
} // namespace Kans
