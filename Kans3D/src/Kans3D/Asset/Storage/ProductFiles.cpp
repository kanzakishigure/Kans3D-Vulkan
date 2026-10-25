#include "kspch.h"
#include "ProductFiles.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include "Kans3D/Asset/Product/ContentDigest.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sys/file.h>
#endif

namespace Kans
{
    namespace
    {
        AssetError FileError(const std::filesystem::path& path, const char* operation, int code)
        {
            return {AssetErrorCode::IoError,
                    path.u8string() + ": " + operation + ": " +
                        std::error_code(code, std::system_category()).message()};
        }

        struct IncompleteWrite
        {
            const std::filesystem::path& Path;
            bool Complete = false;
            ~IncompleteWrite()
            {
                if (!Complete)
                {
                    std::error_code ignored;
                    std::filesystem::remove(Path, ignored);
                }
            }
        };
    } // namespace

    ProductStoreLock::~ProductStoreLock()
    {
#ifdef _WIN32
        if (m_Handle != -1)
            CloseHandle(reinterpret_cast<HANDLE>(m_Handle));
#else
        if (m_Handle != -1)
            close(static_cast<int>(m_Handle));
#endif
    }

    std::variant<Scope<ProductStoreLock>, AssetError> ProductStoreLock::Acquire(const std::filesystem::path& file)
    {
#ifdef _WIN32
        const HANDLE handle = CreateFileW(file.c_str(),
                                          GENERIC_READ | GENERIC_WRITE,
                                          0,
                                          nullptr,
                                          OPEN_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                          nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return FileError(file, "acquire exclusive writer lock", GetLastError());
        BY_HANDLE_FILE_INFORMATION info {};
        if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            CloseHandle(handle);
            return AssetError {AssetErrorCode::IoError, "Writer lock cannot be a reparse point"};
        }
        return Scope<ProductStoreLock>(new ProductStoreLock(reinterpret_cast<intptr_t>(handle)));
#else
        const int handle = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0)
            return FileError(file, "open writer lock", errno);
        if (flock(handle, LOCK_EX | LOCK_NB) != 0)
        {
            const int error = errno;
            close(handle);
            return FileError(file, "acquire exclusive writer lock", error);
        }
        return Scope<ProductStoreLock>(new ProductStoreLock(handle));
#endif
    }

    std::variant<std::monostate, AssetError> WriteProductFile(const std::filesystem::path&  path,
                                                              const std::vector<std::byte>& bytes)
    {
#ifdef _WIN32
        const HANDLE file =
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return FileError(path, "create Product file", GetLastError());
        IncompleteWrite cleanup {path};
        size_t offset = 0;
        int    error  = 0;
        while (offset < bytes.size())
        {
            const auto count   = static_cast<DWORD>((std::min)(bytes.size() - offset, size_t(1024 * 1024)));
            DWORD      written = 0;
            if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || !written)
            {
                error = GetLastError();
                if (!error)
                    error = ERROR_WRITE_FAULT;
                break;
            }
            offset += written;
        }
        if (!error && !FlushFileBuffers(file))
            error = GetLastError();
        if (!CloseHandle(file) && !error)
            error = GetLastError();
        if (error)
            return FileError(path, "write/flush Product file", error);
#else
        const int file = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (file < 0)
            return FileError(path, "create Product file", errno);
        IncompleteWrite cleanup {path};
        size_t offset = 0;
        int    error  = 0;
        while (offset < bytes.size())
        {
            const auto count   = (std::min)(bytes.size() - offset, size_t(1024 * 1024));
            const auto written = write(file, bytes.data() + offset, count);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
            {
                error = written < 0 ? errno : EIO;
                break;
            }
            offset += static_cast<size_t>(written);
        }
        if (!error && fsync(file) != 0)
            error = errno;
        if (close(file) != 0 && !error)
            error = errno;
        if (error)
            return FileError(path, "write/flush Product file", error);
#endif
        cleanup.Complete = true;
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> ReplaceProductCatalog(const std::filesystem::path& temporary,
                                                                   const std::filesystem::path& destination)
    {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return FileError(destination, "replace catalog", GetLastError());
#else
        if (rename(temporary.c_str(), destination.c_str()) != 0)
            return FileError(destination, "replace catalog", errno);
#endif
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> SyncProductDirectory(const std::filesystem::path& directory)
    {
#ifndef _WIN32
        const int handle = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (handle < 0)
            return FileError(directory, "open directory for sync", errno);
        const int result = fsync(handle);
        const int error  = errno;
        close(handle);
        if (result != 0)
            return FileError(directory, "sync directory", error);
#else
        // Windows catalog replacement requests write-through. Directory flush
        // semantics are not exposed here; no stronger power-loss guarantee is claimed.
        (void)directory;
#endif
        return std::monostate {};
    }

    std::variant<std::filesystem::path, AssetError> ResolveProductFile(const std::filesystem::path& root,
                                                                       const AssetPath&             relative)
    {
        if (!root.is_absolute() || !relative.IsValid())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Product root/relative path"};
        std::error_code rootError;
        const auto      rootStatus = std::filesystem::symlink_status(root, rootError);
        if (rootError)
            return FileError(root, "inspect Product root", rootError.value());
        if (!std::filesystem::is_directory(rootStatus))
            return AssetError {AssetErrorCode::InvalidData, "Product root must be a real directory"};
        auto path = root;
        for (const auto& component : relative.GetPath())
        {
            path /= component;
            std::error_code error;
            const auto      status = std::filesystem::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
                return FileError(path, "inspect Product path", error.value());
            if (error || status.type() == std::filesystem::file_type::not_found)
                return AssetError {AssetErrorCode::NotFound, "Missing Product path: " + path.u8string()};
            if (std::filesystem::is_symlink(status))
                return AssetError {AssetErrorCode::InvalidData, "Product path traverses a symlink: " + path.u8string()};
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return AssetError {AssetErrorCode::InvalidData, "Product path is not a regular file"};
        return path;
    }

    std::variant<std::vector<std::byte>, AssetError> ReadProductFile(const std::filesystem::path& path,
                                                                     uint64_t                     maximumBytes)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            return AssetError {AssetErrorCode::IoError, "Cannot open Product file: " + path.u8string()};
        const auto length = input.tellg();
        if (length < 0 || uint64_t(length) > maximumBytes ||
            uint64_t(length) > uint64_t(std::numeric_limits<size_t>::max()))
            return AssetError {AssetErrorCode::InvalidData, "Product file exceeds byte limit"};
        std::vector<std::byte> bytes(static_cast<size_t>(length));
        input.seekg(0);
        if (!bytes.empty())
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input || input.peek() != std::char_traits<char>::eof())
            return AssetError {AssetErrorCode::IoError, "Product file changed or cannot be read completely"};
        return bytes;
    }

    std::variant<std::vector<std::byte>, AssetError> ReadRegisteredProduct(const std::filesystem::path& root,
                                                                           const ProductRecord&         record)
    {
        if (!record.Location.ByteSize || !IsContentDigest(record.Location.ContentDigest))
            return AssetError {AssetErrorCode::InvalidData, "Invalid registered Product integrity metadata"};
        auto path = ResolveProductFile(root, record.Location.RelativeFile);
        if (const auto* error = std::get_if<AssetError>(&path))
            return *error;
        auto bytes = ReadProductFile(std::get<std::filesystem::path>(path),
                                     (std::min)(record.Location.ByteSize, uint64_t(512 * 1024 * 1024)));
        if (const auto* error = std::get_if<AssetError>(&bytes))
            return *error;
        const auto& data = std::get<std::vector<std::byte>>(bytes);
        if (data.size() != record.Location.ByteSize || ComputeContentDigest(data) != record.Location.ContentDigest)
            return AssetError {AssetErrorCode::InvalidData, "Registered Product size/digest mismatch"};
        return bytes;
    }
} // namespace Kans
