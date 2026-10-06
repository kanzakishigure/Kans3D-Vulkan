#include "kspch.h"
#include "ShaderProduct.h"
namespace Kans
{
    namespace
    {
        constexpr uint32_t Magic = 0x4448534b, Version = 1, TargetVulkan12 = 1;
        constexpr size_t   MaximumBytes = 16 * 1024 * 1024;
        void               Write(std::vector<std::byte>& bytes, uint32_t word)
        {
            for (unsigned i = 0; i < 4; ++i)
                bytes.push_back(static_cast<std::byte>((word >> (8 * i)) & 255));
        }
        uint32_t Read(const std::byte*& cursor)
        {
            uint32_t word = 0;
            for (unsigned i = 0; i < 4; ++i)
                word |= uint32_t(std::to_integer<uint8_t>(*cursor++)) << (8 * i);
            return word;
        }
    } // namespace
    std::variant<std::vector<std::byte>, AssetError> EncodeShaderProduct(const ShaderProgramCode& code)
    {
        auto inspected = InspectShaderProgram(code);
        if (auto* error = std::get_if<AssetError>(&inspected))
            return *error;
        std::vector<std::byte> bytes;
        Write(bytes, Magic);
        Write(bytes, Version);
        Write(bytes, uint32_t(AssetType::Shader));
        Write(bytes, TargetVulkan12);
        Write(bytes, uint32_t(code.Modules.size()));
        Write(bytes, 0);
        for (const auto& module : code.Modules)
        {
            Write(bytes, uint32_t(module.Stage));
            Write(bytes, uint32_t(module.EntryPoint.size()));
            Write(bytes, uint32_t(module.Words.size()));
            Write(bytes, 0);
            for (auto c : module.EntryPoint)
                bytes.push_back(static_cast<std::byte>(c));
            for (auto word : module.Words)
                Write(bytes, word);
        }
        return bytes;
    }
    std::variant<ShaderData, AssetError> DecodeShaderProduct(const std::byte* bytes, size_t size)
    {
        if (!bytes || size < 24 || size > MaximumBytes)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader Product byte length"};
        const auto* cursor = bytes;
        if (Read(cursor) != Magic)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader Product magic"};
        if (Read(cursor) != Version)
            return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported Shader Product version"};
        if (Read(cursor) != uint32_t(AssetType::Shader) || Read(cursor) != TargetVulkan12)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader Product type/target"};
        const auto count = Read(cursor);
        if (!count || count > 3 || Read(cursor) != 0)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Shader Product module count/flags"};
        ShaderData result;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (size_t(cursor - bytes) > size - 16)
                return AssetError {AssetErrorCode::InvalidData, "Truncated Shader module header"};
            ShaderModuleCode module;
            module.Stage              = static_cast<ShaderProductStage>(Read(cursor));
            const auto     entryBytes = Read(cursor), words = Read(cursor), flags = Read(cursor);
            const uint64_t payload = uint64_t(entryBytes) + uint64_t(words) * 4;
            if (!entryBytes || entryBytes > 255 || words < 5 || words > 1048576 || flags ||
                payload > size - size_t(cursor - bytes))
                return AssetError {AssetErrorCode::InvalidData, "Invalid Shader module payload"};
            module.EntryPoint.assign(reinterpret_cast<const char*>(cursor), entryBytes);
            cursor += entryBytes;
            module.Words.reserve(words);
            for (uint32_t j = 0; j < words; ++j)
                module.Words.push_back(Read(cursor));
            result.Code.Modules.push_back(std::move(module));
        }
        if (size_t(cursor - bytes) != size)
            return AssetError {AssetErrorCode::InvalidData, "Trailing Shader Product bytes"};
        auto reflected = InspectShaderProgram(result.Code);
        if (auto* error = std::get_if<AssetError>(&reflected))
            return *error;
        result.Reflection = std::get<std::vector<ShaderStageReflection>>(std::move(reflected));
        return result;
    }
} // namespace Kans
