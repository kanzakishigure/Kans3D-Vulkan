#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Kans3D/RHI/RHI.h"

namespace Kans
{
    using GraphTexture = uint32_t;
    using GraphPass    = uint32_t;
    enum class GraphAccess
    {
        SampledRead,
        ColorWrite,
        ColorReadWrite,
        DepthWrite,
        DepthReadWrite,
        TransferRead,
        TransferWrite
    };
    struct GraphUse
    {
        GraphTexture Texture;
        GraphAccess  Access;
    };
    struct CompiledResourceAccess
    {
        GraphPass   Pass;
        uint32_t    ExecutionIndex;
        GraphAccess Access;
    };
    struct CompiledGraphResource
    {
        std::string                         Name;
        RenderHardware::TextureDesc         Description;
        bool                                Imported;
        uint32_t                            PhysicalSlot = uint32_t(-1);
        std::vector<CompiledResourceAccess> Accesses;
    };
    // Retain one allocation per compatible physical slot, across graph instances/frames.
    // Release unmatched descriptions after allocation (submitted commands retain old images).
    class TransientTexturePool
    {
    public:
        using Factory =
            std::function<RenderHardware::Handle<RenderHardware::Texture>(const RenderHardware::TextureDesc&)>;
        std::vector<RenderHardware::Handle<RenderHardware::Texture>>
               Acquire(const std::vector<RenderHardware::TextureDesc>& descriptions, const Factory& create);
        size_t Size() const { return m_Textures.size(); }

    private:
        std::vector<RenderHardware::Handle<RenderHardware::Texture>> m_Textures;
    };
    class RenderGraph;
    struct RenderGraphContext
    {
        RenderHardware::Device&                         Device;
        RenderHardware::CommandList&                    Commands;
        const RenderGraph&                              Graph;
        RenderHardware::Handle<RenderHardware::Texture> Texture(GraphTexture texture) const;
    };
    class RenderGraph
    {
    public:
        using Record = std::function<void(RenderGraphContext&)>;
        GraphTexture ImportTexture(std::string name, RenderHardware::Handle<RenderHardware::Texture> texture);
        GraphTexture CreateTexture(std::string name, RenderHardware::TextureDesc description);
        GraphPass    AddPass(std::string name, std::vector<GraphUse> uses, Record record, bool sideEffect = false);
        GraphPass    AddRecordedPass(std::string                                         name,
                                     std::vector<GraphUse>                               uses,
                                     RenderHardware::Handle<RenderHardware::CommandList> commands);
        void         DependsOn(GraphPass pass, GraphPass prerequisite);
        void         ExportTexture(GraphTexture texture);
        // Throws invalid_argument for invalid use or a cycle, including in culled passes.
        // No device calls: scheduling, culling, access sequences and alias plan are CPU only.
        void Compile();
        void Allocate(TransientTexturePool& pool, const TransientTexturePool::Factory& create);
        void Execute(RenderHardware::Device& device, TransientTexturePool& pool);
        // Run an imported-resource prefix without touching transient allocations.
        // Its access history survives subsequent pass additions and compilation.
        void                                      ExecuteImportedPrefix(RenderHardware::Device& device);
        size_t                                    PassCount() const { return m_Passes.size(); }
        const std::vector<GraphPass>&             ExecutionOrder() const { return m_Order; }
        const std::vector<CompiledGraphResource>& Resources() const { return m_CompiledResources; }
        const std::string&                        PassName(GraphPass pass) const { return m_Passes.at(pass).Name; }
        RenderHardware::Handle<RenderHardware::Texture> Texture(GraphTexture texture) const;

    private:
        struct Resource
        {
            std::string                                     Name;
            RenderHardware::TextureDesc                     Description;
            RenderHardware::Handle<RenderHardware::Texture> Image;
            bool                                            Imported = false, Exported = false;
        };
        struct Pass
        {
            std::string                                         Name;
            std::vector<GraphUse>                               Uses;
            Record                                              Recorder;
            bool                                                SideEffect;
            std::vector<GraphPass>                              Dependencies;
            RenderHardware::Handle<RenderHardware::CommandList> Commands;
            bool                                                Submitted = false;
        };
        std::vector<Resource>                    m_Resources;
        std::vector<Pass>                        m_Passes;
        std::vector<GraphPass>                   m_Order;
        std::vector<CompiledGraphResource>       m_CompiledResources;
        std::vector<RenderHardware::TextureDesc> m_Slots;
        std::vector<GraphPass>                   m_Submitted;
        void                                     ExecutePass(RenderHardware::Device& device, GraphPass pass);
        bool                                     m_Compiled = false;
    };
} // namespace Kans
