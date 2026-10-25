#include "kspch.h"
#include "MaterialBindingRecipe.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace Kans
{
    namespace
    {
        constexpr size_t   MaximumBytes = 1024 * 1024;
        constexpr uint32_t Magic        = 0x52424d4b; // KMBR, little endian
        struct Writer
        {
            std::vector<std::byte> Bytes;
            void                   Word(uint32_t value)
            {
                if (Bytes.size() > MaximumBytes - 4)
                    throw std::runtime_error("Binding recipe exceeds byte limit");
                for (unsigned i = 0; i < 4; ++i)
                    Bytes.push_back(std::byte((value >> (8 * i)) & 255));
            }
        };
        struct Reader
        {
            const std::byte* Bytes;
            size_t           Size, Position = 0;
            uint32_t         Word()
            {
                if (Size - Position < 4)
                    throw std::runtime_error("Truncated binding recipe");
                uint32_t value = 0;
                for (unsigned i = 0; i < 4; ++i)
                    value |= uint32_t(std::to_integer<unsigned char>(Bytes[Position++])) << (8 * i);
                return value;
            }
            uint32_t Count(uint32_t maximum)
            {
                auto count = Word();
                if (count > maximum || count > (Size - Position) / 4)
                    throw std::runtime_error("Invalid binding recipe count");
                return count;
            }
        };
        void WritePath(Writer& out, const std::vector<ShaderUniformPathElement>& path)
        {
            if (path.empty() || path.size() > 17)
                throw std::runtime_error("Invalid uniform path length");
            out.Word(uint32_t(path.size()));
            for (const auto& element : path)
            {
                out.Word(element.Member);
                out.Word(element.ArrayElement);
            }
        }
        std::vector<ShaderUniformPathElement> ReadPath(Reader& in)
        {
            auto count = in.Count(17);
            if (!count)
                throw std::runtime_error("Empty uniform path");
            std::vector<ShaderUniformPathElement> path;
            for (uint32_t i = 0; i < count; ++i)
                path.push_back({in.Word(), in.Word()});
            return path;
        }
        void Slot(std::set<std::tuple<uint32_t, uint32_t, uint32_t>>& slots,
                  uint32_t                                            set,
                  uint32_t                                            binding,
                  uint32_t                                            element)
        {
            if (set > 31 || binding > 65535 || element >= 4096 || !slots.emplace(set, binding, element).second)
                throw std::runtime_error("Invalid or duplicate descriptor assignment");
        }
    } // namespace

    std::variant<std::vector<std::byte>, AssetError> EncodeMaterialBindingRecipe(const MaterialBindingRecipe& recipe)
    {
        try
        {
            if (recipe.Uniforms.size() > 4096 || recipe.Textures.size() > 4096 - recipe.Uniforms.size())
                throw std::runtime_error("Too many descriptor assignments");
            Writer out;
            out.Word(Magic);
            out.Word(1);
            out.Word(uint32_t(recipe.Uniforms.size()));
            out.Word(uint32_t(recipe.Textures.size()));
            size_t                                             writes = 0;
            std::set<std::tuple<uint32_t, uint32_t, uint32_t>> slots;
            for (const auto& uniform : recipe.Uniforms)
            {
                Slot(slots, uniform.Set, uniform.Binding, uniform.Element);
                if (uniform.Parameters.size() > 4096 - writes ||
                    uniform.Values.size() > 4096 - writes - uniform.Parameters.size())
                    throw std::runtime_error("Too many uniform writes");
                writes += uniform.Parameters.size() + uniform.Values.size();
                out.Word(uniform.Set);
                out.Word(uniform.Binding);
                out.Word(uniform.Element);
                out.Word(uint32_t(uniform.Parameters.size()));
                out.Word(uint32_t(uniform.Values.size()));
                for (const auto& parameter : uniform.Parameters)
                {
                    auto kind = uint32_t(parameter.Parameter);
                    if (kind > uint32_t(MaterialParameter::Roughness))
                        throw std::runtime_error("Unknown material parameter");
                    out.Word(kind);
                    WritePath(out, parameter.Path);
                }
                for (const auto& value : uniform.Values)
                {
                    WritePath(out, value.Path);
                    out.Word(uint32_t(value.Values.index()));
                    std::visit(
                        [&](const auto& values) {
                            if (values.empty() || values.size() > 16)
                                throw std::runtime_error("Invalid uniform value count");
                            out.Word(uint32_t(values.size()));
                            for (auto scalar : values)
                            {
                                uint32_t bits;
                                using T = decltype(scalar);
                                if constexpr (std::is_same_v<T, float>)
                                {
                                    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
                                    if (!std::isfinite(scalar))
                                        throw std::runtime_error("Nonfinite uniform value");
                                    std::memcpy(&bits, &scalar, 4);
                                }
                                else
                                    bits = uint32_t(scalar);
                                out.Word(bits);
                            }
                        },
                        value.Values);
                }
            }
            for (const auto& texture : recipe.Textures)
            {
                Slot(slots, texture.Set, texture.Binding, texture.Element);
                out.Word(texture.Set);
                out.Word(texture.Binding);
                out.Word(texture.Element);
                out.Word(texture.TextureIndex);
            }
            return std::move(out.Bytes);
        }
        catch (const std::exception&)
        {
            return AssetError {AssetErrorCode::InvalidData, "Invalid Material binding recipe"};
        }
    }

    std::variant<MaterialBindingRecipe, AssetError> DecodeMaterialBindingRecipe(const std::byte* bytes, size_t size)
    {
        if (!bytes || size < 16 || size > MaximumBytes)
            return AssetError {AssetErrorCode::InvalidData, "Invalid binding recipe size"};
        try
        {
            Reader in {bytes, size};
            if (in.Word() != Magic)
                throw std::runtime_error("Invalid binding recipe magic");
            if (in.Word() != 1)
                return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported binding recipe version"};
            auto                  uniforms = in.Count(4096);
            auto                  textures = in.Count(4096 - uniforms);
            MaterialBindingRecipe result;
            size_t                writes = 0;
            for (uint32_t i = 0; i < uniforms; ++i)
            {
                MaterialUniformAssignment uniform;
                uniform.Set     = in.Word();
                uniform.Binding = in.Word();
                uniform.Element = in.Word();
                auto parameters = in.Count(uint32_t(4096 - writes));
                writes += parameters;
                auto values = in.Count(uint32_t(4096 - writes));
                writes += values;
                for (uint32_t j = 0; j < parameters; ++j)
                {
                    auto kind = in.Word();
                    if (kind > uint32_t(MaterialParameter::Roughness))
                        throw std::runtime_error("Unknown material parameter");
                    uniform.Parameters.push_back({MaterialParameter(kind), ReadPath(in)});
                }
                for (uint32_t j = 0; j < values; ++j)
                {
                    ShaderUniformWrite write;
                    write.Path = ReadPath(in);
                    auto kind  = in.Word();
                    auto count = in.Count(16);
                    if (!count || kind > 2)
                        throw std::runtime_error("Invalid typed uniform values");
                    if (kind == 0)
                    {
                        std::vector<float> scalars;
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            auto  bits = in.Word();
                            float scalar;
                            std::memcpy(&scalar, &bits, 4);
                            scalars.push_back(scalar);
                        }
                        write.Values = std::move(scalars);
                    }
                    else if (kind == 1)
                    {
                        std::vector<int32_t> scalars;
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            auto bits = in.Word();
                            // Defined conversion without implementation-defined unsigned-to-signed narrowing.
                            scalars.push_back(bits <= INT32_MAX ? int32_t(bits) :
                                                                  int32_t(int64_t(bits) - 0x100000000ll));
                        }
                        write.Values = std::move(scalars);
                    }
                    else
                    {
                        std::vector<uint32_t> scalars;
                        for (uint32_t k = 0; k < count; ++k)
                            scalars.push_back(in.Word());
                        write.Values = std::move(scalars);
                    }
                    uniform.Values.push_back(std::move(write));
                }
                result.Uniforms.push_back(std::move(uniform));
            }
            for (uint32_t i = 0; i < textures; ++i)
                result.Textures.push_back({in.Word(), in.Word(), in.Word(), in.Word()});
            if (in.Position != size)
                throw std::runtime_error("Trailing binding recipe bytes");
            auto validated = EncodeMaterialBindingRecipe(result);
            if (auto error = std::get_if<AssetError>(&validated))
                return *error;
            return result;
        }
        catch (const std::exception&)
        {
            return AssetError {AssetErrorCode::InvalidData, "Malformed Material binding recipe"};
        }
    }
} // namespace Kans
