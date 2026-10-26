#include "kspch.h"
#include "UIProduct.h"
namespace Kans
{
    std::variant<std::vector<std::byte>, AssetError> EncodeUIProduct(const UIData& d)
    {
        auto result = SerializeUISource(d);
        if (auto e = std::get_if<AssetError>(&result))
            return *e;
        auto&                  s = std::get<std::string>(result);
        std::vector<std::byte> out;
        auto                   u = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i)
                out.push_back(std::byte((v >> (i * 8)) & 255));
        };
        u(0x4249554b);
        u(UIProductVersion);
        u(uint32_t(AssetType::UI));
        u(uint32_t(s.size()));
        auto p = reinterpret_cast<const std::byte*>(s.data());
        out.insert(out.end(), p, p + s.size());
        return out;
    }
    std::variant<UIData, AssetError> DecodeUIProduct(const std::byte* p, size_t size)
    {
        if (!p || size < 16 || size > 16 * 1024 * 1024 + 16)
            return AssetError {AssetErrorCode::InvalidData, "Invalid UI Product size"};
        auto u = [&](size_t at) {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
                v |= uint32_t(p[at + i]) << (i * 8);
            return v;
        };
        if (u(0) != 0x4249554b || u(4) != UIProductVersion || u(8) != uint32_t(AssetType::UI) || u(12) != size - 16)
            return AssetError {AssetErrorCode::InvalidData, "Invalid or unsupported UI Product header/version"};
        return ParseUISource(p + 16, size - 16);
    }
} // namespace Kans
