#include "kspch.h"
#include "ShaderProduct.h"
#include <cmath>
#include <cstring>
namespace Kans
{
    namespace
    {
        // Version 2 appends parameter defaults, version 3 also parameter hints.
        constexpr uint32_t Magic = 0x4448534b, Version = 3, TargetVulkan12 = 1;
        constexpr size_t   MaximumDefaults = 4096, MaximumDefaultValues = 4096;
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
        void WriteDouble(std::vector<std::byte>& bytes, double value)
        {
            uint64_t bits = 0;
            std::memcpy(&bits, &value, 8);
            Write(bytes, uint32_t(bits));
            Write(bytes, uint32_t(bits >> 32));
        }
        double ReadDouble(const std::byte*& cursor)
        {
            const uint64_t low = Read(cursor), high = Read(cursor);
            const uint64_t bits  = low | (high << 32);
            double         value = 0;
            std::memcpy(&value, &bits, 8);
            return value;
        }
        bool ValidDefault(const std::string& name, const std::vector<double>& values)
        {
            return !name.empty() && name.size() <= 256 && !values.empty() && values.size() <= MaximumDefaultValues &&
                   std::all_of(name.begin(), name.end(), [](unsigned char c) { return c > 32 && c != 127; }) &&
                   std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
        }
        bool ValidHint(const std::string& name, const ShaderParameterHint& hint)
        {
            return ValidDefault(name, {hint.Min, hint.Max}) &&
                   (hint.Type == ShaderParameterHint::Kind::Toggle || hint.Type == ShaderParameterHint::Kind::Color ||
                    (hint.Type == ShaderParameterHint::Kind::Range && hint.Min < hint.Max));
        }
    } // namespace
    std::variant<std::vector<std::byte>, AssetError> EncodeShaderProduct(const ShaderProgramCode&       code,
                                                                         const ShaderParameterDefaults& defaults,
                                                                         const ShaderParameterHints&    hints)
    {
        if (hints.size() > MaximumDefaults)
            return AssetError {AssetErrorCode::InvalidArgument, "Too many Shader parameter hints"};
        for (const auto& [name, hint] : hints)
            if (!ValidHint(name, hint))
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid Shader parameter hint '" + name + "'"};
        auto inspected = InspectShaderProgram(code);
        if (auto* error = std::get_if<AssetError>(&inspected))
            return *error;
        if (defaults.size() > MaximumDefaults)
            return AssetError {AssetErrorCode::InvalidArgument, "Too many Shader parameter defaults"};
        for (const auto& [name, values] : defaults)
            if (!ValidDefault(name, values))
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid Shader parameter default '" + name + "'"};
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
        Write(bytes, uint32_t(defaults.size()));
        for (const auto& [name, values] : defaults)
        {
            Write(bytes, uint32_t(name.size()));
            Write(bytes, uint32_t(values.size()));
            for (auto c : name)
                bytes.push_back(static_cast<std::byte>(c));
            for (double value : values)
                WriteDouble(bytes, value);
        }
        Write(bytes, uint32_t(hints.size()));
        for (const auto& [name, hint] : hints)
        {
            Write(bytes, uint32_t(name.size()));
            Write(bytes, uint32_t(hint.Type));
            for (auto c : name)
                bytes.push_back(static_cast<std::byte>(c));
            WriteDouble(bytes, hint.Min);
            WriteDouble(bytes, hint.Max);
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
        const auto version = Read(cursor);
        if (version != 1 && version != Version)
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
        if (version >= 2)
        {
            const auto remaining = [&] { return size - size_t(cursor - bytes); };
            if (remaining() < 4)
                return AssetError {AssetErrorCode::InvalidData, "Truncated Shader parameter defaults"};
            const auto defaults = Read(cursor);
            if (defaults > MaximumDefaults)
                return AssetError {AssetErrorCode::InvalidData, "Too many Shader parameter defaults"};
            for (uint32_t i = 0; i < defaults; ++i)
            {
                if (remaining() < 8)
                    return AssetError {AssetErrorCode::InvalidData, "Truncated Shader parameter default"};
                const auto nameBytes = Read(cursor), count = Read(cursor);
                if (nameBytes > 256 || count > MaximumDefaultValues ||
                    uint64_t(nameBytes) + uint64_t(count) * 8 > remaining())
                    return AssetError {AssetErrorCode::InvalidData, "Invalid Shader parameter default"};
                std::string         name(reinterpret_cast<const char*>(cursor), nameBytes);
                std::vector<double> values;
                cursor += nameBytes;
                for (uint32_t j = 0; j < count; ++j)
                    values.push_back(ReadDouble(cursor));
                if (!ValidDefault(name, values) || !result.ParameterDefaults.emplace(name, std::move(values)).second)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid Shader parameter default"};
            }
        }
        if (version >= 3)
        {
            const auto remaining = [&] { return size - size_t(cursor - bytes); };
            if (remaining() < 4)
                return AssetError {AssetErrorCode::InvalidData, "Truncated Shader parameter hints"};
            const auto hints = Read(cursor);
            if (hints > MaximumDefaults)
                return AssetError {AssetErrorCode::InvalidData, "Too many Shader parameter hints"};
            for (uint32_t i = 0; i < hints; ++i)
            {
                if (remaining() < 8)
                    return AssetError {AssetErrorCode::InvalidData, "Truncated Shader parameter hint"};
                const auto nameBytes = Read(cursor), kind = Read(cursor);
                if (nameBytes > 256 || uint64_t(nameBytes) + 16 > remaining())
                    return AssetError {AssetErrorCode::InvalidData, "Invalid Shader parameter hint"};
                std::string name(reinterpret_cast<const char*>(cursor), nameBytes);
                cursor += nameBytes;
                ShaderParameterHint hint;
                hint.Type = static_cast<ShaderParameterHint::Kind>(kind);
                hint.Min  = ReadDouble(cursor);
                hint.Max  = ReadDouble(cursor);
                if (!ValidHint(name, hint) || !result.ParameterHints.emplace(name, hint).second)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid Shader parameter hint"};
            }
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
