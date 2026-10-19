#include "kspch.h"
#include "Skinning.h"
#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>
namespace Kans
{
    std::variant<ShaderData, AssetError> MakeSkinnedShader(const ShaderData& data)
    {
        try
        {
            ShaderData result  = data;
            auto       convert = [](ShaderProgramCode& code) {
                for (auto& module : code.Modules)
                    if (module.Stage == ShaderProductStage::Vertex)
                    {
                        spirv_cross::CompilerGLSL compiler(module.Words);
                        auto                      options = compiler.get_common_options();
                        options.version                   = 450;
                        options.vulkan_semantics          = true;
                        compiler.set_common_options(options);
                        bool position = false, normal = false, tangent = false;
                        for (const auto& input : compiler.get_shader_resources().stage_inputs)
                        {
                            const auto location = compiler.get_decoration(input.id, spv::DecorationLocation);
                            if (location == 0)
                            {
                                compiler.set_name(input.id, "kans_Position");
                                position = true;
                            }
                            if (location == 1)
                            {
                                compiler.set_name(input.id, "kans_Normal");
                                normal = true;
                            }
                            if (location == 4)
                            {
                                compiler.set_name(input.id, "kans_Tangent");
                                tangent = true;
                            }
                        }
                        if (!position)
                            throw std::runtime_error("Skinned vertex shader requires position at location 0");
                        std::string source = compiler.compile();
                        auto        input  = [&](const char* type, const char* name, const char* raw) {
                            const std::string declaration = std::string("in ") + type + " " + name + ";";
                            const auto        at          = source.find(declaration);
                            if (at == std::string::npos)
                                throw std::runtime_error("Cannot build skinning vertex prologue");
                            source.replace(at,
                                           declaration.size(),
                                           std::string("in ") + type + " " + raw + ";\n" + type + " " + name + ";");
                        };
                        input("vec3", "kans_Position", "kans_RawPosition");
                        if (normal)
                            input("vec3", "kans_Normal", "kans_RawNormal");
                        if (tangent)
                            input("vec4", "kans_Tangent", "kans_RawTangent");
                        const auto main = source.find("void main()");
                        if (main == std::string::npos)
                            throw std::runtime_error("Missing shader main");
                        source.replace(main, 11, "void kans_MaterialMain()");
                        source += R"(
layout(location=5) in uvec4 kans_Joints;
layout(location=6) in vec4 kans_Weights;
layout(set=2,binding=0,std430) readonly buffer KansSkinningPalette { mat4 kans_Palette[]; };
void main() {
    mat4 skin=kans_Palette[kans_Joints.x]*kans_Weights.x+kans_Palette[kans_Joints.y]*kans_Weights.y+
              kans_Palette[kans_Joints.z]*kans_Weights.z+kans_Palette[kans_Joints.w]*kans_Weights.w;
    kans_Position=(skin*vec4(kans_RawPosition,1)).xyz;
)";
                        if (normal)
                            source += "kans_Normal=normalize(transpose(inverse(mat3(skin)))*kans_RawNormal);\n";
                        if (tangent)
                            source += "kans_Tangent=vec4(normalize(mat3(skin)*kans_RawTangent.xyz),kans_RawTangent.w*("
                                      "determinant(mat3(skin))<0 ? -1 : 1));\n";
                        source += "kans_MaterialMain();\n}\n";
                        shaderc::Compiler       shaderCompiler;
                        shaderc::CompileOptions compileOptions;
                        compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
                        auto compiled = shaderCompiler.CompileGlslToSpv(
                            source, shaderc_vertex_shader, "KansSkinning.vert", compileOptions);
                        if (compiled.GetCompilationStatus() != shaderc_compilation_status_success)
                            throw std::runtime_error(compiled.GetErrorMessage());
                        module.Words.assign(compiled.cbegin(), compiled.cend());
                        module.EntryPoint = "main";
                    }
            };
            convert(result.Code);
            for (auto& pass : result.Passes)
                convert(pass.Code);
            return result;
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, e.what()};
        }
    }
    RenderHardware::Handle<RenderHardware::BindGroupLayout> SkinningLayout(RenderHardware::Device& device)
    {
        return device.CreateBindGroupLayout({2, {{0, RenderHardware::BindingType::StorageBuffer}}});
    }
    void BindSkinning(RenderHardware::Device&                                        device,
                      RenderHardware::CommandList&                                   commands,
                      const RenderHardware::Handle<RenderHardware::BindGroupLayout>& layout,
                      const RenderHardware::Handle<RenderHardware::Buffer>&          palette)
    {
        if (palette)
            commands.BindGroup(device.CreateBindGroup({layout, {{0, palette, 0, palette->GetDesc().Bytes}}}));
    }
    Bounds SkinnedBounds(const std::vector<Bounds>& boxes, const SkinningPose& pose)
    {
        Bounds bounds;
        for (size_t j = 0; j < boxes.size() && j < pose.Palette.size(); ++j)
            if (boxes[j].IsValid())
                bounds.Expand(boxes[j].Transformed(pose.Palette[j]));
        return bounds; // A convex weighted vertex lies inside the union AABB of all influenced transformed boxes.
    }
} // namespace Kans
