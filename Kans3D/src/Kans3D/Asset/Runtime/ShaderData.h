#pragma once
#include <map>
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    enum class ShaderProductStage : uint32_t
    {
        Vertex   = 0,
        Fragment = 4,
        Compute  = 5
    };
    struct ShaderModuleCode
    {
        ShaderProductStage    Stage      = ShaderProductStage::Vertex;
        std::string           EntryPoint = "main";
        std::vector<uint32_t> Words;
    };
    // Target contract: SPIR-V for Vulkan 1.2; no native handles or source files.
    struct ShaderProgramCode
    {
        std::vector<ShaderModuleCode> Modules;
    };
    enum class ShaderBindingKind
    {
        UniformBuffer,
        CombinedImageSampler
    };
    enum class ShaderImageDimension
    {
        Texture2D,
        Cube
    };
    enum class ShaderUniformType
    {
        Float,
        Signed,
        Unsigned,
        Boolean,
        Struct
    };
    struct ShaderUniformMember
    {
        std::string       Name;
        uint64_t          Offset = 0, Bytes = 0;
        ShaderUniformType Type       = ShaderUniformType::Float;
        uint32_t          Components = 1, Columns = 1, ArrayCount = 1;
        uint64_t          ArrayStride = 0, MatrixStride = 0;
        bool              RowMajor = false;
        // Child offsets are relative to this struct, not to the enclosing block.
        std::vector<ShaderUniformMember> Members;
    };
    struct ShaderResourceBinding
    {
        ShaderBindingKind                Kind;
        uint32_t                         Set = 0, Binding = 0, Count = 1;
        uint64_t                         BlockBytes = 0;
        ShaderImageDimension             Dimension  = ShaderImageDimension::Texture2D; // Samplers only.
        std::string                      Name;
        std::vector<ShaderUniformMember> Members;
    };
    enum class ShaderInterfaceScalar
    {
        Float,
        Signed,
        Unsigned
    };
    struct ShaderInterfaceVariable
    {
        uint32_t              Location = 0, Components = 1, Columns = 1;
        ShaderInterfaceScalar Scalar = ShaderInterfaceScalar::Float;
        std::string           Name;
    };
    struct ShaderStageReflection
    {
        ShaderProductStage                   Stage;
        std::vector<ShaderResourceBinding>   Bindings;
        std::vector<ShaderInterfaceVariable> Inputs, Outputs;
    };
    // Default values of material parameters (uniform members by name), declared by
    // the Shader source; scalars in declaration order, as in MaterialSource.
    using ShaderParameterDefaults = std::map<std::string, std::vector<double>>;

    struct ShaderData
    {
        ShaderProgramCode       Code;
        ShaderParameterDefaults ParameterDefaults;
        // Derived from validated bytecode, never trusted caller-supplied metadata.
        std::vector<ShaderStageReflection> Reflection;
    };
    std::variant<std::vector<ShaderStageReflection>, AssetError> InspectShaderProgram(const ShaderProgramCode& code);
    class ShaderDataAsset                                        final : public Asset
    {
    public:
        explicit ShaderDataAsset(ShaderData data) : m_Data(std::move(data)) {}
        static AssetType  GetStaticType() { return AssetType::Shader; }
        AssetType         GetAssetType() const override { return GetStaticType(); }
        const ShaderData& GetData() const { return m_Data; }

    private:
        ShaderData m_Data;
    };
} // namespace Kans
