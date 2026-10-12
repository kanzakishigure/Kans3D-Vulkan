#pragma once
#include "Kans3D/Core/Base/Base.h"
#include "Kans3D/Core/Log/Log.h"
namespace Kans
{

    enum class ShaderDataType
    {
        NONE = 0,
        Float,
        Float2,
        Float3,
        Float4,
        Mat3,
        Mat4,
        Int,
        Int2,
        Int3,
        Int4,
        Bool,
        Color4,
        Color3
    };
    static uint32_t ShaderTypeToSize(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float:
                return 4;
            case ShaderDataType::Float2:
                return 4 * 2;
            case ShaderDataType::Float3:
                return 4 * 3;
            case ShaderDataType::Float4:
                return 4 * 4;
            case ShaderDataType::Mat3:
                return 4 * 3 * 3;
            case ShaderDataType::Mat4:
                return 4 * 4 * 4;
            case ShaderDataType::Int:
                return 4;
            case ShaderDataType::Int2:
                return 4 * 2;
            case ShaderDataType::Int3:
                return 4 * 3;
            case ShaderDataType::Int4:
                return 4 * 4;
            case ShaderDataType::Color4:
                return 4 * 4;
            case ShaderDataType::Color3:
                return 4 * 3;
            case ShaderDataType::Bool:
                return 1;
        }

        CORE_ASSERT(false, "unknown shaderdatatype！");
        return 0;
    }
    struct BufferElement
    {
        std::string    Name;
        uint32_t       Offset;
        uint32_t       Size;
        ShaderDataType Type;
        bool           Normalized;
        BufferElement(ShaderDataType type, const std::string& name, bool normalized = false) :
            Name(name), Type(type), Size(ShaderTypeToSize(type)), Offset(0), Normalized(normalized)
        {}
        uint32_t GetCompomentCount() const
        {
            switch (this->Type)
            {
                case ShaderDataType::Float:
                    return 1;
                case ShaderDataType::Float2:
                    return 2;
                case ShaderDataType::Float3:
                    return 3;
                case ShaderDataType::Float4:
                    return 4;
                case ShaderDataType::Mat3:
                    return 3 * 3;
                case ShaderDataType::Mat4:
                    return 4 * 4;
                case ShaderDataType::Int:
                    return 1;
                case ShaderDataType::Int2:
                    return 2;
                case ShaderDataType::Int3:
                    return 3;
                case ShaderDataType::Int4:
                    return 4;
                case ShaderDataType::Bool:
                    return 1;
            }
            CORE_ASSERT(false, "unknown shaderdatatype！");
            return 0;
        }
    };
    namespace ShaderUtils
    {
        enum class ShaderLang
        {
            NONE,
            GLSL,
            HLSL,
        };
    } // namespace ShaderUtils

    class ShaderUniform
    {
    public:
        ShaderUniform() = default;
        ShaderUniform(const std::string& name, uint32_t size, uint32_t offset, ShaderDataType type) :
            Name(name), Size(size), Offset(offset), Type(type)
        {}

        std::string&       GetName() { return Name; }
        const std::string& GetName() const { return Name; }

        uint32_t       GetSize() { return Size; }
        const uint32_t GetSize() const { return Size; }

        uint32_t       GetOffset() { return Offset; }
        const uint32_t GetOffset() const { return Offset; }

        ShaderDataType       GetType() { return Type; }
        const ShaderDataType GetType() const { return Type; }

    private:
        std::string    Name;
        uint32_t       Size   = 0;
        uint32_t       Offset = 0;
        ShaderDataType Type   = ShaderDataType::NONE;
    };
    struct ShaderBuffer
    {
        std::string                                    Name;
        uint32_t                                       Size = 0;
        std::unordered_map<std::string, ShaderUniform> ShaderUniforms;
    };
    class ShaderBufferLayout
    {
    public:
        ShaderBufferLayout() {}
        ShaderBufferLayout(const std::initializer_list<BufferElement>& elements) : m_Elements(elements)
        {
            CalculateOffsetAndStride();
        }

        inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }
        inline const uint32_t&                   GetStride() const { return m_Stride; }

        std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
        std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }
        std::vector<BufferElement>::iterator       begin() { return m_Elements.begin(); }
        std::vector<BufferElement>::iterator       end() { return m_Elements.end(); }

    private:
        void CalculateOffsetAndStride()
        {
            uint32_t offset = 0;
            m_Stride        = 0;
            for (auto& element : m_Elements)
            {
                element.Offset += offset;
                offset += element.Size;
                m_Stride += element.Size;
            }
        }

    private:
        std::vector<BufferElement> m_Elements;
        uint32_t                   m_Stride = 0;
    };
} // namespace Kans
