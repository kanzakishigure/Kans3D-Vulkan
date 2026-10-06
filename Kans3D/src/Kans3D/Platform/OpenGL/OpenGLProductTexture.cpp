#include "kspch.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include "OpenGLTexture.h"

namespace Kans
{
    namespace
    {
        struct UnpackState
        {
            static constexpr GLenum Names[] = {GL_UNPACK_ALIGNMENT,
                                               GL_UNPACK_ROW_LENGTH,
                                               GL_UNPACK_IMAGE_HEIGHT,
                                               GL_UNPACK_SKIP_PIXELS,
                                               GL_UNPACK_SKIP_ROWS,
                                               GL_UNPACK_SKIP_IMAGES,
                                               GL_UNPACK_SWAP_BYTES,
                                               GL_UNPACK_LSB_FIRST};
            GLint                   Values[8] {}, Buffer = 0;
            UnpackState()
            {
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &Buffer);
                for (size_t i = 0; i < 8; ++i)
                    glGetIntegerv(Names[i], &Values[i]);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                for (size_t i = 0; i < 8; ++i)
                    glPixelStorei(Names[i], i == 0 ? 1 : 0);
            }
            ~UnpackState()
            {
                for (size_t i = 0; i < 8; ++i)
                    glPixelStorei(Names[i], Values[i]);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(Buffer));
            }
        };
    } // namespace
    std::variant<Ref<OpenGLTexture2D>, AssetError> OpenGLTexture2D::UploadProduct(const TextureData& data)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "Product upload requires IEEE binary32 floats");
        auto valid = ValidateTextureData(data);
        if (auto* error = std::get_if<AssetError>(&valid))
            return *error;
        if (data.Kind != TextureKind::Texture2D)
            return AssetError {AssetErrorCode::NotImplemented, "This upload entry requires a 2D Texture Product"};
        if (!glGetString || !glCreateTextures || !glTextureStorage2D || !glTextureSubImage2D ||
            !glGetString(GL_VERSION))
            return AssetError {AssetErrorCode::InvalidArgument,
                               "Texture upload requires a current OpenGL context and DSA"};
        GLint maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        if (maximum <= 0 || data.Width > static_cast<uint32_t>(maximum) || data.Height > static_cast<uint32_t>(maximum))
            return AssetError {AssetErrorCode::InvalidData, "Texture dimensions exceed OpenGL capabilities"};

        GLenum internal = GL_RGBA8, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
        size_t components = 4, componentBytes = 1;
        switch (data.Format)
        {
            case TexturePixelFormat::R8:
                internal   = GL_R8;
                format     = GL_RED;
                components = 1;
                break;
            case TexturePixelFormat::RG8:
                internal   = GL_RG8;
                format     = GL_RG;
                components = 2;
                break;
            case TexturePixelFormat::RGBA8:
                internal = data.ColorSpace == TextureColorSpace::SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
                break;
            case TexturePixelFormat::RGBA32Float:
                internal       = GL_RGBA32F;
                type           = GL_FLOAT;
                componentBytes = 4;
                break;
        }
        // Own aligned native float storage; canonical Product bytes are little endian.
        std::vector<std::vector<std::byte>> bytes(data.Mips.size());
        std::vector<std::vector<float>>     floats(data.Mips.size());
        for (size_t level = 0; level < data.Mips.size(); ++level)
        {
            const size_t width = std::max(1u, data.Width >> level), height = std::max(1u, data.Height >> level);
            const size_t row = width * components * componentBytes;
            if (type == GL_FLOAT)
                floats[level].resize(data.Mips[level].Pixels.size() / 4);
            else
                bytes[level].resize(data.Mips[level].Pixels.size());
            for (size_t y = 0; y < height; ++y)
            {
                const auto sourceRow = data.Origin == TextureOrigin::TopLeft ? height - 1 - y : y;
                if (type != GL_FLOAT)
                {
                    std::memcpy(bytes[level].data() + y * row, data.Mips[level].Pixels.data() + sourceRow * row, row);
                    continue;
                }
                for (size_t x = 0; x < width * components; ++x)
                {
                    uint32_t bits = 0;
                    for (size_t b = 0; b < 4; ++b)
                        bits |= uint32_t(std::to_integer<uint8_t>(data.Mips[level].Pixels[sourceRow * row + x * 4 + b]))
                                << (b * 8);
                    std::memcpy(&floats[level][y * width * components + x], &bits, 4);
                }
            }
        }
        Ref<OpenGLTexture2D> texture(new OpenGLTexture2D());
        texture->m_Width          = data.Width;
        texture->m_Height         = data.Height;
        texture->m_InternalFormat = internal;
        texture->m_DataFormat     = format;
        UnpackState unpack;
        glCreateTextures(GL_TEXTURE_2D, 1, &texture->m_RendererID);
        glTextureStorage2D(
            texture->m_RendererID, static_cast<GLsizei>(data.Mips.size()), internal, data.Width, data.Height);
        glTextureParameteri(
            texture->m_RendererID, GL_TEXTURE_MIN_FILTER, data.Mips.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTextureParameteri(texture->m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(texture->m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(texture->m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureParameteri(texture->m_RendererID, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(texture->m_RendererID, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(data.Mips.size() - 1));
        for (size_t level = 0; level < data.Mips.size(); ++level)
            glTextureSubImage2D(texture->m_RendererID,
                                static_cast<GLint>(level),
                                0,
                                0,
                                std::max(1u, data.Width >> level),
                                std::max(1u, data.Height >> level),
                                format,
                                type,
                                type == GL_FLOAT ? static_cast<const void*>(floats[level].data()) :
                                                   bytes[level].data());
        return texture;
    }
} // namespace Kans
