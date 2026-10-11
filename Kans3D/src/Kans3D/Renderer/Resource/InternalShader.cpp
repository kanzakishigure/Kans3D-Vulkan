#include "kspch.h"
#include "InternalShader.h"
#include <mutex>
#include <tuple>
#include "Kans3D/Asset/Importer/ShaderImporter.h"

namespace Kans
{
    const InternalShaderData&
    GetInternalShaderData(const std::string& name, const std::string& vertex, const std::string& fragment)
    {
        static std::mutex                                                                      mutex;
        static std::map<std::tuple<std::string, std::string, std::string>, InternalShaderData> cache;
        const std::lock_guard<std::mutex>                                                      lock(mutex);
        const auto key = std::make_tuple(name, vertex, fragment);
        if (auto found = cache.find(key); found != cache.end())
            return found->second;
        InternalShaderData result;
        try
        {
            result = CompileEmbeddedShaderProgram("KansShaderProgram 1\nvertex=Internal.vert\nfragment=Internal.frag\n",
                                                  {{"Internal.vert", vertex}, {"Internal.frag", fragment}});
        }
        catch (const std::exception& error)
        {
            result = AssetError {AssetErrorCode::InvalidData, error.what()};
        }
        if (auto* error = std::get_if<AssetError>(&result))
            error->Message = "Internal shader '" + name + "': " + error->Message;
        return cache.emplace(key, std::move(result)).first->second;
    }

    std::variant<RenderHardware::Handle<RenderHardware::Shader>, AssetError>
    CreateInternalShader(RenderHardware::Device& device,
                         const std::string&      name,
                         const std::string&      vertex,
                         const std::string&      fragment)
    {
        const auto& product = GetInternalShaderData(name, vertex, fragment);
        if (auto* error = std::get_if<AssetError>(&product))
            return *error;
        try
        {
            return device.CreateShader(std::get<ShaderData>(product));
        }
        catch (const std::exception& error)
        {
            return AssetError {AssetErrorCode::InvalidData, "Internal shader '" + name + "': " + error.what()};
        }
    }

    glm::mat4 DeviceProjection(const glm::mat4& projection, RenderHardware::CoordinateConventions coordinates)
    {
        glm::mat4 conversion(1);
        if (coordinates.ClipY == RenderHardware::ClipYDirection::Down)
            conversion[1][1] = -1;
        if (coordinates.Depth == RenderHardware::DepthRange::ZeroToOne)
        {
            conversion[2][2] = 0.5f;
            conversion[3][2] = 0.5f;
        }
        return conversion * projection;
    }

    uint32_t DeviceRow(uint32_t row, uint32_t height, RenderHardware::CoordinateConventions coordinates)
    {
        return coordinates.Origin == RenderHardware::FramebufferOrigin::LowerLeft ? row : height - row - 1;
    }
} // namespace Kans
