#include "kspch.h"
#include "GpuShader.h"
#include <glad/glad.h>
#include <spirv_cross/spirv_glsl.hpp>

namespace Kans
{
    GpuUniformBuffer::GpuUniformBuffer(size_t bytes, const void* data)
    {
        glGenBuffers(1, &m_ID);
        glBindBuffer(GL_UNIFORM_BUFFER, m_ID);
        glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    GpuUniformBuffer::~GpuUniformBuffer()
    {
        if (m_ID)
            glDeleteBuffers(1, &m_ID);
    }

    GpuUniformBuffer::GpuUniformBuffer(GpuUniformBuffer&& other) noexcept : m_ID(std::exchange(other.m_ID, 0)) {}

    GpuUniformBuffer& GpuUniformBuffer::operator=(GpuUniformBuffer&& other) noexcept
    {
        std::swap(m_ID, other.m_ID);
        return *this;
    }

    void GpuUniformBuffer::Update(const void* data, size_t bytes)
    {
        glBindBuffer(GL_UNIFORM_BUFFER, m_ID);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void GpuUniformBuffer::Bind(uint32_t point) const { glBindBufferBase(GL_UNIFORM_BUFFER, point, m_ID); }

    namespace
    {
        AssetError Failure(std::string message)
        {
            return AssetError {AssetErrorCode::InvalidData, "Material Shader: " + std::move(message)};
        }

        std::optional<std::string> CompileStage(GLenum stage, const std::string& source, GLuint& shader)
        {
            shader            = glCreateShader(stage);
            const char* text  = source.c_str();
            glShaderSource(shader, 1, &text, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_TRUE)
                return std::nullopt;
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
            glGetShaderInfoLog(shader, length, nullptr, log.data());
            return log;
        }
    } // namespace

    std::variant<GpuShader, AssetError> GpuShader::Create(const ShaderData& data)
    {
        if (!glGetString || !glGetString(GL_VERSION))
            return AssetError {AssetErrorCode::InvalidArgument, "Shader upload requires a current OpenGL context"};
        auto inspected = InspectShaderProgram(data.Code);
        if (auto* error = std::get_if<AssetError>(&inspected))
            return std::move(*error);
        const auto& reflection = std::get<std::vector<ShaderStageReflection>>(inspected);
        auto        contract   = ValidateMaterialShaderInterface(reflection);
        if (auto* error = std::get_if<AssetError>(&contract))
            return std::move(*error);

        const auto resources = MaterialShaderResources(reflection);
        for (const auto& [name, role] : data.TextureFallbacks)
            if (std::none_of(resources.begin(), resources.end(), [&](const auto& resource) {
                    return resource.Kind == ShaderBindingKind::CombinedImageSampler && resource.Name == name;
                }))
                return Failure("texture fallback '" + name + "' does not name a material sampler");
        GpuShader shader;
        for (const auto& resource : resources)
        {
            if (resource.Count != 1)
                return Failure("descriptor arrays are not supported ('" + resource.Name + "')");
            if (resource.Kind == ShaderBindingKind::UniformBuffer)
                shader.m_BlockPoints.emplace(resource.Binding,
                                             FirstMaterialBlockPoint + uint32_t(shader.m_BlockPoints.size()));
            else
            {
                const auto found = data.TextureFallbacks.find(resource.Name);
                const auto role  = found == data.TextureFallbacks.end() ? ShaderTextureFallback::White : found->second;
                if (uint32_t(role) > uint32_t(ShaderTextureFallback::Normal))
                    return Failure("invalid texture fallback");
                shader.m_SamplerFallbacks.emplace(resource.Binding, role);
                shader.m_SamplerUnits.emplace(resource.Binding,
                                              FirstMaterialUnit + uint32_t(shader.m_SamplerUnits.size()));
            }
        }
        // Guaranteed minimums of GL 4.3 per stage.
        if (shader.m_BlockPoints.size() > 10 || FirstMaterialUnit + shader.m_SamplerUnits.size() > 16)
            return Failure("too many material uniform blocks (max 10) or samplers (max 13)");
        const auto point = [&](uint32_t set, uint32_t binding, bool block) -> uint32_t {
            if (set == EngineDescriptorSet)
                switch (static_cast<EngineBinding>(binding))
                {
                    case EngineBinding::Frame: return FrameBlockPoint;
                    case EngineBinding::Object: return ObjectBlockPoint;
                    case EngineBinding::IrradianceMap: return IrradianceUnit;
                    case EngineBinding::PrefilterMap: return PrefilterUnit;
                    default: return BrdfLUTUnit;
                }
            return block ? shader.m_BlockPoints.at(binding) : shader.m_SamplerUnits.at(binding);
        };

        std::vector<GLuint> stages;
        const auto          release = [&] {
            for (GLuint stage : stages)
                glDeleteShader(stage);
        };
        try
        {
            for (const auto& module : data.Code.Modules)
            {
                spirv_cross::CompilerGLSL glsl(module.Words);
                auto                      options = glsl.get_common_options();
                options.version                   = 430;
                options.es                        = false;
                options.vulkan_semantics          = false;
                glsl.set_common_options(options);
                const auto resources = glsl.get_shader_resources();
                const auto remap     = [&](const auto& list, bool block) {
                    for (const auto& resource : list)
                    {
                        const auto set     = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
                        const auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
                        glsl.set_decoration(resource.id, spv::DecorationBinding, point(set, binding, block));
                        glsl.unset_decoration(resource.id, spv::DecorationDescriptorSet);
                    }
                };
                remap(resources.uniform_buffers, true);
                remap(resources.sampled_images, false);
                const auto source = glsl.compile();
                GLuint     stage  = 0;
                auto       error  = CompileStage(
                    module.Stage == ShaderProductStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER, source, stage);
                stages.push_back(stage);
                if (error)
                {
                    release();
                    return Failure("OpenGL compilation failed: " + error->substr(0, 2048));
                }
            }
        }
        catch (const std::exception& error)
        {
            release();
            return Failure(std::string("cross-compilation failed: ") + error.what());
        }

        shader.m_Program = glCreateProgram();
        for (GLuint stage : stages)
            glAttachShader(shader.m_Program, stage);
        glLinkProgram(shader.m_Program);
        for (GLuint stage : stages)
            glDetachShader(shader.m_Program, stage);
        release();
        GLint linked = GL_FALSE;
        glGetProgramiv(shader.m_Program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            GLint length = 0;
            glGetProgramiv(shader.m_Program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(shader.m_Program, length, nullptr, log.data());
            return Failure("OpenGL link failed: " + log.substr(0, 2048));
        }
        return shader;
    }

    GpuShader::~GpuShader()
    {
        if (m_Program)
            glDeleteProgram(m_Program);
    }

    GpuShader::GpuShader(GpuShader&& other) noexcept :
        m_SamplerFallbacks(std::move(other.m_SamplerFallbacks)), m_Program(std::exchange(other.m_Program, 0)),
        m_BlockPoints(std::move(other.m_BlockPoints)), m_SamplerUnits(std::move(other.m_SamplerUnits))
    {
    }

    GpuShader& GpuShader::operator=(GpuShader&& other) noexcept
    {
        std::swap(m_Program, other.m_Program);
        std::swap(m_SamplerFallbacks, other.m_SamplerFallbacks);
        std::swap(m_BlockPoints, other.m_BlockPoints);
        std::swap(m_SamplerUnits, other.m_SamplerUnits);
        return *this;
    }

    void GpuShader::Bind() const { glUseProgram(m_Program); }
} // namespace Kans
