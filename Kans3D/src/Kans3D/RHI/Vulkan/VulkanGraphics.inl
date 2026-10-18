// Backend-private graphics objects, included in VulkanDevice.cpp's implementation namespace.
VkDescriptorType DescriptorType(BindingType type)
{
    switch (type)
    {
        case BindingType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case BindingType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case BindingType::SampledTexture:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case BindingType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case BindingType::CombinedTextureSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    throw std::invalid_argument("Invalid binding type");
}
VkDescriptorType LayoutDescriptorType(uint32_t group, BindingType type)
{
    if (group == 0 && type == BindingType::UniformBuffer)
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    if (group == 0 && type == BindingType::StorageBuffer)
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    return DescriptorType(type);
}
struct ShaderBinding
{
    uint32_t         Group, Slot;
    BindingType      Type;
    size_t           Bytes;
    TextureDimension Dimension;
    bool             Comparison = false;
};
// IDs and member names are deliberately excluded; compare the actual native byte layout.
std::string PushLayout(spirv_cross::Compiler& compiler, uint32_t id)
{
    const auto& type = compiler.get_type(id);
    std::string key;
    auto        add = [&](auto value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    add(type.basetype);
    add(type.width);
    add(type.vecsize);
    add(type.columns);
    add(type.array.size());
    for (auto count : type.array)
        add(count);
    if (!type.array.empty())
        add(compiler.get_decoration(id, spv::DecorationArrayStride));
    add(type.member_types.size());
    for (uint32_t i = 0; i < type.member_types.size(); ++i)
    {
        add(compiler.type_struct_member_offset(type, i));
        add(compiler.get_member_decoration(id, i, spv::DecorationMatrixStride));
        add(compiler.has_member_decoration(id, i, spv::DecorationRowMajor));
        key += PushLayout(compiler, type.member_types[i]);
    }
    return key;
}
struct VKShader final : Shader
{
    Allocation                         Memory;
    uint32_t                           PushBytes = 0;
    std::vector<ShaderBinding>         Bindings;
    std::vector<VkShaderModule>        Modules;
    std::vector<std::string>           EntryPoints;
    std::vector<VkShaderStageFlagBits> Stages;
    VKShader(std::shared_ptr<State> owner, const Kans::ShaderData& data) : Memory(std::move(owner))
    {
        Require(data.Code.Modules.size() == 2, "Graphics shader needs vertex and fragment modules");
        const auto                         device = Memory.Owner->Native.Logical;
        std::set<Kans::ShaderProductStage> kinds;
        std::string                        pushLayout;
        try
        {
            for (const auto& module : data.Code.Modules)
            {
                Require((module.Stage == Kans::ShaderProductStage::Vertex ||
                         module.Stage == Kans::ShaderProductStage::Fragment) &&
                            kinds.insert(module.Stage).second,
                        "Unsupported or duplicate shader stage");
                spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_2);
                std::string          diagnostic;
                validator.SetMessageConsumer(
                    [&](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
                        diagnostic = message;
                    });
                if (!validator.Validate(module.Words))
                    throw std::invalid_argument("Invalid shader bytecode: " + diagnostic);
                spirv_cross::Compiler reflection(module.Words);
                reflection.set_entry_point(module.EntryPoint, static_cast<spv::ExecutionModel>(module.Stage));
                auto resources = reflection.get_shader_resources();
                Require(resources.storage_images.empty() && resources.subpass_inputs.empty() &&
                            resources.atomic_counters.empty(),
                        "Unsupported shader resource");
                auto collect = [&](const auto& list, BindingType type) {
                    for (const auto& resource : list)
                    {
                        const auto& nativeType = reflection.get_type(resource.type_id);
                        Require(nativeType.array.empty(), "Binding arrays are unsupported");
                        ShaderBinding binding {reflection.get_decoration(resource.id, spv::DecorationDescriptorSet),
                                               reflection.get_decoration(resource.id, spv::DecorationBinding),
                                               type,
                                               0,
                                               TextureDimension::Texture2D};
                        Require(binding.Group < 2, "Group exceeds backend capacity");
                        if (type == BindingType::UniformBuffer || type == BindingType::StorageBuffer)
                            binding.Bytes =
                                reflection.get_declared_struct_size(reflection.get_type(resource.base_type_id));
                        if (type == BindingType::SampledTexture || type == BindingType::CombinedTextureSampler)
                        {
                            auto image = nativeType.image;
                            Require((image.dim == spv::Dim2D || image.dim == spv::DimCube) && !image.arrayed &&
                                        !image.ms,
                                    "Unsupported shader image dimension");
                            binding.Comparison = image.depth;
                            binding.Dimension =
                                image.dim == spv::DimCube ? TextureDimension::Cube : TextureDimension::Texture2D;
                        }
                        auto old = std::find_if(Bindings.begin(), Bindings.end(), [&](const auto& b) {
                            return b.Group == binding.Group && b.Slot == binding.Slot;
                        });
                        if (old == Bindings.end())
                            Bindings.push_back(binding);
                        else
                            Require(old->Type == binding.Type && old->Bytes == binding.Bytes &&
                                        old->Dimension == binding.Dimension && old->Comparison == binding.Comparison,
                                    "Shader stages disagree about a binding");
                    }
                };
                collect(resources.uniform_buffers, BindingType::UniformBuffer);
                collect(resources.storage_buffers, BindingType::StorageBuffer);
                collect(resources.sampled_images, BindingType::CombinedTextureSampler);
                collect(resources.separate_images, BindingType::SampledTexture);
                collect(resources.separate_samplers, BindingType::Sampler);
                Require(resources.push_constant_buffers.size() <= 1, "Only one push constant block is supported");
                for (const auto& resource : resources.push_constant_buffers)
                {
                    auto bytes = reflection.get_declared_struct_size(reflection.get_type(resource.base_type_id));
                    Require(bytes <= 128, "Push constant block exceeds 128 bytes");
                    auto layout = PushLayout(reflection, resource.base_type_id);
                    Require(!PushBytes || (PushBytes == bytes && pushLayout == layout),
                            "Shader stages disagree about push layout");
                    PushBytes  = uint32_t(bytes);
                    pushLayout = std::move(layout);
                }
                VkShaderModuleCreateInfo create {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                create.codeSize = module.Words.size() * sizeof(uint32_t);
                create.pCode    = module.Words.data();
                VkShaderModule native;
                Check(vkCreateShaderModule(device, &create, nullptr, &native));
                Modules.push_back(native);
                EntryPoints.push_back(module.EntryPoint);
                Stages.push_back(module.Stage == Kans::ShaderProductStage::Vertex ? VK_SHADER_STAGE_VERTEX_BIT :
                                                                                    VK_SHADER_STAGE_FRAGMENT_BIT);
            }
            for (const auto& stage : data.Reflection)
                for (const auto& binding : stage.Bindings)
                {
                    auto found = std::find_if(Bindings.begin(), Bindings.end(), [&](const auto& b) {
                        return b.Group == binding.Set && b.Slot == binding.Binding;
                    });
                    Require(found != Bindings.end() && binding.Count == 1 && found->Bytes == binding.BlockBytes &&
                                found->Type == (binding.Kind == Kans::ShaderBindingKind::UniformBuffer ?
                                                    BindingType::UniformBuffer :
                                                    BindingType::CombinedTextureSampler) &&
                                (binding.Kind != Kans::ShaderBindingKind::CombinedImageSampler ||
                                 found->Dimension == (binding.Dimension == Kans::ShaderImageDimension::Cube ?
                                                          TextureDimension::Cube :
                                                          TextureDimension::Texture2D)),
                            "Asset reflection disagrees with bytecode");
                }
            Memory.Delete = [device, modules = Modules] {
                for (auto module : modules)
                    vkDestroyShaderModule(device, module, nullptr);
            };
        }
        catch (...)
        {
            for (auto module : Modules)
                vkDestroyShaderModule(device, module, nullptr);
            throw;
        }
    }
    uint32_t GetPushConstantBytes() const override { return PushBytes; }
};
struct VKLayout final : BindGroupLayout
{
    Allocation            Memory;
    BindGroupLayoutDesc   Desc;
    VkDescriptorSetLayout Native = VK_NULL_HANDLE;
    VKLayout(std::shared_ptr<State> owner, BindGroupLayoutDesc desc) : Memory(std::move(owner)), Desc(std::move(desc))
    {
        Require(Desc.Group < 2, "Group exceeds backend capacity");
        std::sort(Desc.Entries.begin(), Desc.Entries.end(), [](auto a, auto b) { return a.Slot < b.Slot; });
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (auto entry : Desc.Entries)
        {
            Require(bindings.empty() || bindings.back().binding != entry.Slot, "Duplicate group slot");
            bindings.push_back(
                {entry.Slot, LayoutDescriptorType(Desc.Group, entry.Type), 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr});
        }
        VkDescriptorSetLayoutCreateInfo create {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        create.bindingCount = uint32_t(bindings.size());
        create.pBindings    = bindings.data();
        auto device         = Memory.Owner->Native.Logical;
        Check(vkCreateDescriptorSetLayout(device, &create, nullptr, &Native));
        Memory.Delete = [device, layout = Native] { vkDestroyDescriptorSetLayout(device, layout, nullptr); };
    }
    const BindGroupLayoutDesc& GetDesc() const override { return Desc; }
};
struct VKSet
{
    Allocation      Memory;
    BindGroupDesc   Resources;
    VkDescriptorSet Native;
    VKSet(std::shared_ptr<State> owner, BindGroupDesc resources, VkDescriptorSetLayout layout) :
        Memory(std::move(owner)), Resources(std::move(resources))
    {
        auto [pool, set] = Memory.Owner->AllocateSet(layout);
        Native           = set;
        auto device      = Memory.Owner->Native.Logical;
        Memory.Delete    = [device, pool, set] { Check(vkFreeDescriptorSets(device, pool, 1, &set)); };
    }
};
struct VKGroup final : BindGroup
{
    struct OwnerReference
    {
        std::shared_ptr<State> Owner;
    } Memory;
    BindGroupDesc          Desc;
    std::shared_ptr<VKSet> Set;
    std::vector<uint32_t>  DynamicOffsets;
    VKGroup(std::shared_ptr<State> owner, BindGroupDesc desc) : Memory {std::move(owner)}, Desc(std::move(desc))
    {
        auto  layout  = Cast<VKLayout>(Desc.Layout, Memory.Owner);
        auto& entries = Desc.Entries;
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.Slot < b.Slot; });
        Require(entries.size() == layout->Desc.Entries.size(), "Bind group entries do not match layout");
        std::string key;
        auto        add = [&](const auto& value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
        add(layout->Native);
        auto                                resources = Desc;
        std::vector<VkDescriptorBufferInfo> buffers(entries.size());
        std::vector<VkDescriptorImageInfo>  images(entries.size());
        std::vector<VkWriteDescriptorSet>   writes(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto&       entry    = entries[i];
            const auto& expected = layout->Desc.Entries[i];
            Require(entry.Slot == expected.Slot, "Bind group slot mismatch");
            bool buffer = expected.Type == BindingType::UniformBuffer || expected.Type == BindingType::StorageBuffer;
            bool texture =
                expected.Type == BindingType::SampledTexture || expected.Type == BindingType::CombinedTextureSampler;
            bool sampler =
                expected.Type == BindingType::Sampler || expected.Type == BindingType::CombinedTextureSampler;
            Require(bool(entry.BufferResource) == buffer && bool(entry.TextureResource) == texture &&
                        bool(entry.SamplerResource) == sampler,
                    "Bind group resource type mismatch");
            if (buffer)
            {
                auto b = Cast<VKBuffer>(entry.BufferResource, Memory.Owner);
                Require(Has(b->Desc.Usage,
                            expected.Type == BindingType::UniformBuffer ? BufferUsage::Uniform : BufferUsage::Storage),
                        "Binding buffer usage mismatch");
                size_t bytes = entry.Bytes ? entry.Bytes : b->Desc.Bytes - std::min(entry.Offset, b->Desc.Bytes);
                Range(b->Desc.Bytes, entry.Offset, bytes);
                auto&  limits    = Memory.Owner->Native.Properties.limits;
                size_t alignment = expected.Type == BindingType::UniformBuffer ?
                                       limits.minUniformBufferOffsetAlignment :
                                       limits.minStorageBufferOffsetAlignment;
                size_t maxBytes  = expected.Type == BindingType::UniformBuffer ? limits.maxUniformBufferRange :
                                                                                 limits.maxStorageBufferRange;
                Require(bytes > 0 && bytes <= maxBytes && entry.Offset % alignment == 0,
                        "Invalid buffer binding range or alignment");
                size_t baseOffset = entry.Offset;
                if (layout->Desc.Group == 0)
                {
                    // Keep large offsets portable too: move an aligned multiple
                    // into the descriptor base when the dynamic uint32 range ends.
                    size_t span = (size_t(UINT32_MAX) / alignment) * alignment;
                    baseOffset  = entry.Offset / span * span;
                    DynamicOffsets.push_back(uint32_t(entry.Offset - baseOffset));
                }
                buffers[i]                  = {b->Native, baseOffset, bytes};
                resources.Entries[i].Offset = baseOffset;
                resources.Entries[i].Bytes  = bytes;
                add(b->Native);
                add(baseOffset);
                add(bytes);
            }
            if (texture)
            {
                auto t = Cast<VKTexture>(entry.TextureResource, Memory.Owner);
                Require(Has(t->Desc.Usage, TextureUsage::Sampled), "Binding texture usage mismatch");
                images[i].imageView = t->SampledView;
                add(t->SampledView);
                images[i].imageLayout =
                    Info(t->Desc.PixelFormat).Depth() && Has(t->Desc.Usage, TextureUsage::DepthStencil) ?
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL :
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            if (sampler)
            {
                images[i].sampler = Cast<VKSampler>(entry.SamplerResource, Memory.Owner)->Native;
                add(images[i].sampler);
            }
            writes[i]                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstBinding      = entry.Slot;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType  = LayoutDescriptorType(layout->Desc.Group, expected.Type);
            writes[i].pBufferInfo     = buffer ? &buffers[i] : nullptr;
            writes[i].pImageInfo      = texture || sampler ? &images[i] : nullptr;
        }
        if (layout->Desc.Group == 0)
            Set = Memory.Owner->DescriptorCache[key].lock();
        if (!Set)
        {
            Set = std::make_shared<VKSet>(Memory.Owner, std::move(resources), layout->Native);
            for (auto& write : writes)
                write.dstSet = Set->Native;
            vkUpdateDescriptorSets(Memory.Owner->Native.Logical, uint32_t(writes.size()), writes.data(), 0, nullptr);
            if (layout->Desc.Group == 0)
                Memory.Owner->DescriptorCache[std::move(key)] = Set;
        }
    }
};
VkFormat AttributeFormat(VertexFormat type)
{
    static const VkFormat formats[] = {VK_FORMAT_R32_SFLOAT,
                                       VK_FORMAT_R32G32_SFLOAT,
                                       VK_FORMAT_R32G32B32_SFLOAT,
                                       VK_FORMAT_R32G32B32A32_SFLOAT,
                                       VK_FORMAT_R32_UINT,
                                       VK_FORMAT_R32G32_UINT,
                                       VK_FORMAT_R32G32B32_UINT,
                                       VK_FORMAT_R32G32B32A32_UINT,
                                       VK_FORMAT_R8G8B8A8_UNORM};
    Require(uint32_t(type) < 9, "Invalid vertex format");
    return formats[uint32_t(type)];
}
uint32_t AttributeBytes(VertexFormat type)
{
    AttributeFormat(type);
    return type == VertexFormat::Unorm8x4 ? 4 : (uint32_t(type) % 4 + 1) * 4;
}
VkCompareOp Compare(CompareOp op)
{
    Require(uint32_t(op) < 8, "Invalid compare operation");
    return VkCompareOp(op);
}
VkStencilOp Stencil(StencilOp op)
{
    Require(uint32_t(op) < 8, "Invalid stencil operation");
    return VkStencilOp(op);
}
VkBlendFactor Factor(BlendFactor factor)
{
    static const VkBlendFactor values[] = {VK_BLEND_FACTOR_ZERO,
                                           VK_BLEND_FACTOR_ONE,
                                           VK_BLEND_FACTOR_SRC_COLOR,
                                           VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                           VK_BLEND_FACTOR_SRC_ALPHA,
                                           VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                           VK_BLEND_FACTOR_DST_COLOR,
                                           VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                           VK_BLEND_FACTOR_DST_ALPHA,
                                           VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA};
    Require(uint32_t(factor) < 10, "Invalid blend factor");
    return values[uint32_t(factor)];
}
void ValidatePipeline(const std::shared_ptr<State>& owner, const PipelineDesc& desc)
{
    auto shader = Cast<VKShader>(desc.Program, owner);
    Require(desc.PushConstantBytes >= shader->PushBytes && desc.PushConstantBytes <= 128 &&
                desc.PushConstantBytes % 4 == 0,
            "Pipeline push constant range mismatch");
    Require(desc.ColorFormats.size() <= owner->Native.Properties.limits.maxColorAttachments &&
                desc.Blends.size() == desc.ColorFormats.size(),
            "Pipeline blend/target count mismatch");
    std::set<uint32_t> groups;
    for (const auto& layout : desc.GroupLayouts)
        Require(groups.insert(Cast<VKLayout>(layout, owner)->Desc.Group).second, "Duplicate pipeline group");
    for (auto binding : shader->Bindings)
    {
        bool found = false;
        for (const auto& layout : desc.GroupLayouts)
            if (layout->GetDesc().Group == binding.Group)
                for (auto entry : layout->GetDesc().Entries)
                    if (entry.Slot == binding.Slot && entry.Type == binding.Type)
                        found = true;
        Require(found, "Pipeline layout omits shader binding");
    }
    for (size_t i = 0; i < desc.ColorFormats.size(); ++i)
        Require(!Info(desc.ColorFormats[i]).Depth() &&
                    (desc.ColorFormats[i] != Format::R32Uint || !desc.Blends[i].Enabled),
                "Invalid color format or integer blending");
    if (desc.HasDepthStencil)
        Require(Info(desc.DepthStencilFormat).Depth(), "Invalid depth target format");
    Require(!desc.DepthStencil.StencilTest || (desc.HasDepthStencil && Info(desc.DepthStencilFormat).Stencil()),
            "Stencil needs a stencil format");
    std::set<uint32_t> slots, locations;
    for (auto b : desc.VertexBindings)
        Require(b.Stride && b.Slot < 16 && slots.insert(b.Slot).second, "Invalid vertex binding");
    for (auto a : desc.Attributes)
    {
        Require(a.Location < 16 && locations.insert(a.Location).second && slots.count(a.BufferSlot),
                "Invalid vertex attribute");
        auto b = std::find_if(desc.VertexBindings.begin(), desc.VertexBindings.end(), [&](auto binding) {
            return binding.Slot == a.BufferSlot;
        });
        Range(b->Stride, a.Offset, AttributeBytes(a.Type));
    }
}
struct VKPipeline final : Pipeline
{
    Allocation       Memory;
    PipelineDesc     Desc;
    VkPipeline       Native = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    Handle<VKLayout> EmptyLayout;
    VKPipeline(std::shared_ptr<State> owner, PipelineDesc desc) : Memory(std::move(owner)), Desc(std::move(desc))
    {
        auto                               device = Memory.Owner->Native.Logical;
        auto                               shader = Cast<VKShader>(Desc.Program, Memory.Owner);
        std::vector<VkDescriptorSetLayout> layouts;
        for (const auto& layout : Desc.GroupLayouts)
        {
            auto l = Cast<VKLayout>(layout, Memory.Owner);
            if (layouts.size() <= l->Desc.Group)
                layouts.resize(l->Desc.Group + 1, VK_NULL_HANDLE);
            layouts[l->Desc.Group] = l->Native;
        }
        if (std::find(layouts.begin(), layouts.end(), VK_NULL_HANDLE) != layouts.end())
        {
            EmptyLayout = std::make_shared<VKLayout>(Memory.Owner, BindGroupLayoutDesc {});
            for (auto& layout : layouts)
                if (!layout)
                    layout = EmptyLayout->Native;
        }
        VkPushConstantRange        push {VK_SHADER_STAGE_ALL_GRAPHICS, 0, Desc.PushConstantBytes};
        VkPipelineLayoutCreateInfo layout {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount         = uint32_t(layouts.size());
        layout.pSetLayouts            = layouts.data();
        layout.pushConstantRangeCount = Desc.PushConstantBytes ? 1 : 0;
        layout.pPushConstantRanges    = &push;
        Check(vkCreatePipelineLayout(device, &layout, nullptr, &Layout));
        try
        {
            std::vector<VkPipelineShaderStageCreateInfo> stages;
            for (size_t i = 0; i < shader->Modules.size(); ++i)
            {
                VkPipelineShaderStageCreateInfo stage {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stage.stage  = shader->Stages[i];
                stage.module = shader->Modules[i];
                stage.pName  = shader->EntryPoints[i].c_str();
                stages.push_back(stage);
            }
            std::vector<VkVertexInputBindingDescription> bindings;
            for (auto b : Desc.VertexBindings)
                bindings.push_back(
                    {b.Slot, b.Stride, b.PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
            std::vector<VkVertexInputAttributeDescription> attributes;
            for (auto a : Desc.Attributes)
                attributes.push_back({a.Location, a.BufferSlot, AttributeFormat(a.Type), a.Offset});
            VkPipelineVertexInputStateCreateInfo vertex {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            vertex.vertexBindingDescriptionCount   = uint32_t(bindings.size());
            vertex.pVertexBindingDescriptions      = bindings.data();
            vertex.vertexAttributeDescriptionCount = uint32_t(attributes.size());
            vertex.pVertexAttributeDescriptions    = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly {
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            switch (Desc.Primitive)
            {
                case Topology::TriangleList:
                    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    break;
                case Topology::TriangleStrip:
                    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    break;
                case Topology::LineList:
                    assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    break;
                case Topology::PointList:
                    assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    break;
                default:
                    throw std::invalid_argument("Invalid topology");
            }
            VkPipelineViewportStateCreateInfo viewport {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = 1;
            viewport.scissorCount  = 1;
            VkPipelineRasterizationStateCreateInfo raster {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            VkPipelineRasterizationLineStateCreateInfoEXT line {
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT};
            if (Memory.Owner->Native.BresenhamLines)
            {
                line.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
                raster.pNext               = &line;
            }
            raster.polygonMode =
                Desc.Rasterizer.Polygon == PolygonMode::Line ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
            raster.cullMode  = Desc.Rasterizer.Cull == CullMode::None  ? VK_CULL_MODE_NONE :
                               Desc.Rasterizer.Cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT :
                                                                         VK_CULL_MODE_BACK_BIT;
            raster.frontFace = Desc.Rasterizer.Front == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE :
                                                                                      VK_FRONT_FACE_CLOCKWISE;
            raster.depthBiasEnable = Desc.Rasterizer.DepthBiasConstant != 0 || Desc.Rasterizer.DepthBiasSlope != 0;
            raster.depthBiasConstantFactor = Desc.Rasterizer.DepthBiasConstant;
            raster.depthBiasSlopeFactor    = Desc.Rasterizer.DepthBiasSlope;
            raster.lineWidth               = 1;
            VkPipelineMultisampleStateCreateInfo multisample {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            auto stencil                     = [](const StencilFaceState& face) {
                return VkStencilOpState {Stencil(face.Fail),
                                         Stencil(face.Pass),
                                         Stencil(face.DepthFail),
                                         Compare(face.Compare),
                                         face.ReadMask,
                                         face.WriteMask,
                                         face.Reference};
            };
            VkPipelineDepthStencilStateCreateInfo depth {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable   = Desc.DepthStencil.DepthTest;
            depth.depthWriteEnable  = Desc.DepthStencil.DepthWrite;
            depth.depthCompareOp    = Compare(Desc.DepthStencil.DepthCompare);
            depth.stencilTestEnable = Desc.DepthStencil.StencilTest;
            depth.front             = stencil(Desc.DepthStencil.Front);
            depth.back              = stencil(Desc.DepthStencil.Back);
            std::vector<VkPipelineColorBlendAttachmentState> blends;
            for (auto b : Desc.Blends)
            {
                Require(uint32_t(b.ColorOp) < 5 && uint32_t(b.AlphaOp) < 5 && b.WriteMask <= 15,
                        "Invalid blend operation or mask");
                blends.push_back({b.Enabled,
                                  Factor(b.SourceColor),
                                  Factor(b.DestinationColor),
                                  VkBlendOp(b.ColorOp),
                                  Factor(b.SourceAlpha),
                                  Factor(b.DestinationAlpha),
                                  VkBlendOp(b.AlphaOp),
                                  b.WriteMask});
            }
            VkPipelineColorBlendStateCreateInfo blend {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount                            = uint32_t(blends.size());
            blend.pAttachments                               = blends.data();
            VkDynamicState                   dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates    = dynamicStates;
            std::vector<VkFormat> formats;
            for (auto format : Desc.ColorFormats)
                formats.push_back(Info(format).Native);
            VkPipelineRenderingCreateInfo rendering {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            rendering.colorAttachmentCount    = uint32_t(formats.size());
            rendering.pColorAttachmentFormats = formats.data();
            if (Desc.HasDepthStencil)
            {
                rendering.depthAttachmentFormat = Info(Desc.DepthStencilFormat).Native;
                if (Info(Desc.DepthStencilFormat).Stencil())
                    rendering.stencilAttachmentFormat = rendering.depthAttachmentFormat;
            }
            VkGraphicsPipelineCreateInfo create {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            create.pNext               = &rendering;
            create.stageCount          = uint32_t(stages.size());
            create.pStages             = stages.data();
            create.pVertexInputState   = &vertex;
            create.pInputAssemblyState = &assembly;
            create.pViewportState      = &viewport;
            create.pRasterizationState = &raster;
            create.pMultisampleState   = &multisample;
            create.pDepthStencilState  = &depth;
            create.pColorBlendState    = &blend;
            create.pDynamicState       = &dynamic;
            create.layout              = Layout;
            Check(vkCreateGraphicsPipelines(device, Memory.Owner->PipelineCache, 1, &create, nullptr, &Native));
            ++Memory.Owner->PipelineCreations;
            Memory.Delete = [device, pipeline = Native, nativeLayout = Layout] {
                vkDestroyPipeline(device, pipeline, nullptr);
                vkDestroyPipelineLayout(device, nativeLayout, nullptr);
            };
        }
        catch (...)
        {
            vkDestroyPipelineLayout(device, Layout, nullptr);
            throw;
        }
    }
    const PipelineDesc& GetDesc() const override { return Desc; }
};
