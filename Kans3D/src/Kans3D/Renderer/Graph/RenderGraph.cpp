#include "kspch.h"
#include "RenderGraph.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace Kans
{
    namespace
    {
        bool Equal(const RenderHardware::TextureDesc& a, const RenderHardware::TextureDesc& b)
        {
            return a.Width == b.Width && a.Height == b.Height && a.MipLevels == b.MipLevels &&
                   a.Dimension == b.Dimension && a.PixelFormat == b.PixelFormat && a.Usage == b.Usage;
        }
        bool Reads(GraphAccess a)
        {
            return a == GraphAccess::SampledRead || a == GraphAccess::TransferRead ||
                   a == GraphAccess::ColorReadWrite || a == GraphAccess::DepthReadWrite;
        }
        bool Writes(GraphAccess a) { return a != GraphAccess::SampledRead && a != GraphAccess::TransferRead; }
        void Validate(const RenderHardware::TextureDesc& d, GraphAccess a, const std::string& name)
        {
            using namespace RenderHardware;
            TextureUsage needed = a == GraphAccess::SampledRead   ? TextureUsage::Sampled :
                                  a == GraphAccess::TransferRead  ? TextureUsage::CopySource :
                                  a == GraphAccess::TransferWrite ? TextureUsage::CopyDestination :
                                  (a == GraphAccess::DepthWrite || a == GraphAccess::DepthReadWrite) ?
                                                                    TextureUsage::DepthStencil :
                                                                    TextureUsage::RenderTarget;
            const bool   depth  = d.PixelFormat == Format::Depth24Stencil8 || d.PixelFormat == Format::Depth32Float;
            if (!(uint32_t(d.Usage) & uint32_t(needed)) || (needed == TextureUsage::DepthStencil && !depth) ||
                (needed == TextureUsage::RenderTarget && depth))
                throw std::invalid_argument("RenderGraph: incompatible access for texture '" + name + "'");
        }
    } // namespace
    std::vector<RenderHardware::Handle<RenderHardware::Texture>>
    TransientTexturePool::Acquire(const std::vector<RenderHardware::TextureDesc>& descriptions, const Factory& create)
    {
        // A frame without transient consumers (e.g. deselection) must not discard
        // allocations needed again at the same viewport on the next frame.
        if (descriptions.empty())
            return {};
        std::vector<RenderHardware::Handle<RenderHardware::Texture>> textures;
        std::vector<bool>                                            used(m_Textures.size(), false);
        for (const auto& desc : descriptions)
        {
            RenderHardware::Handle<RenderHardware::Texture> image;
            for (size_t i = 0; i < m_Textures.size(); ++i)
                if (!used[i] && Equal(desc, m_Textures[i]->GetDesc()))
                {
                    used[i] = true;
                    image   = m_Textures[i];
                    break;
                }
            if (!image)
                image = create(desc);
            if (!image || !Equal(desc, image->GetDesc()))
                throw std::invalid_argument("RenderGraph: texture factory returned an incompatible allocation");
            textures.push_back(std::move(image));
        }
        m_Textures = textures;
        return textures;
    }
    GraphTexture RenderGraph::ImportTexture(std::string name, RenderHardware::Handle<RenderHardware::Texture> image)
    {
        if (!image)
            throw std::invalid_argument("RenderGraph: null imported texture '" + name + "'");
        // One logical identity for an imported image, so repeated sampling creates real dependencies.
        for (uint32_t i = 0; i < m_Resources.size(); ++i)
            if (m_Resources[i].Imported && m_Resources[i].Image == image)
                return i;
        m_Compiled = false;
        auto desc  = image->GetDesc();
        m_Resources.push_back({std::move(name), desc, std::move(image), true, false});
        return uint32_t(m_Resources.size() - 1);
    }
    GraphTexture RenderGraph::CreateTexture(std::string name, RenderHardware::TextureDesc desc)
    {
        if (!desc.Width || !desc.Height || !desc.MipLevels ||
            (desc.Dimension == RenderHardware::TextureDimension::Cube && desc.Width != desc.Height))
            throw std::invalid_argument("RenderGraph: invalid texture description '" + name + "'");
        m_Compiled = false;
        m_Resources.push_back({std::move(name), desc, {}, false, false});
        return uint32_t(m_Resources.size() - 1);
    }
    GraphPass RenderGraph::AddPass(std::string name, std::vector<GraphUse> uses, Record record, bool sideEffect)
    {
        m_Compiled = false;
        m_Passes.push_back({std::move(name), std::move(uses), std::move(record), sideEffect, {}});
        return uint32_t(m_Passes.size() - 1);
    }
    GraphPass RenderGraph::AddRecordedPass(std::string                                         name,
                                           std::vector<GraphUse>                               uses,
                                           RenderHardware::Handle<RenderHardware::CommandList> commands)
    {
        if (!commands)
            throw std::invalid_argument("RenderGraph: null recorded commands");
        auto pass               = AddPass(std::move(name), std::move(uses), {});
        m_Passes[pass].Commands = std::move(commands);
        return pass;
    }
    void RenderGraph::DependsOn(GraphPass pass, GraphPass prerequisite)
    {
        if (pass >= m_Passes.size() || prerequisite >= m_Passes.size())
            throw std::invalid_argument("RenderGraph: invalid pass dependency");
        m_Compiled = false;
        m_Passes[pass].Dependencies.push_back(prerequisite);
    }
    void RenderGraph::ExportTexture(GraphTexture texture)
    {
        if (texture >= m_Resources.size())
            throw std::invalid_argument("RenderGraph: invalid exported texture");
        m_Compiled                    = false;
        m_Resources[texture].Exported = true;
    }
    void RenderGraph::Compile()
    {
        m_Compiled = false;
        m_Order.clear();
        m_Slots.clear();
        m_CompiledResources.clear();
        const uint32_t                     count = uint32_t(m_Passes.size()), none = uint32_t(-1);
        std::vector<std::set<GraphPass>>   edges(count), inputs(count);
        std::vector<uint32_t>              latest(m_Resources.size(), none), firstWriter(m_Resources.size(), none);
        std::vector<std::vector<uint32_t>> readers(m_Resources.size());
        for (uint32_t p = 0; p < count; ++p)
        {
            std::set<GraphTexture> seen;
            for (auto use : m_Passes[p].Uses)
            {
                if (use.Texture >= m_Resources.size())
                    throw std::invalid_argument("RenderGraph: invalid texture in pass '" + m_Passes[p].Name + "'");
                if (!seen.insert(use.Texture).second)
                    throw std::invalid_argument("RenderGraph: duplicate texture use in pass '" + m_Passes[p].Name +
                                                "'");
                Validate(m_Resources[use.Texture].Description, use.Access, m_Resources[use.Texture].Name);
                if (Writes(use.Access) && firstWriter[use.Texture] == none)
                    firstWriter[use.Texture] = p;
            }
        }
        auto edge = [&](uint32_t from, uint32_t to) {
            if (from != none && from != to)
                edges[from].insert(to);
        };
        for (uint32_t p = 0; p < count; ++p)
        {
            for (auto dependency : m_Passes[p].Dependencies)
            {
                if (dependency == p)
                    throw std::invalid_argument("RenderGraph: cycle at pass '" + m_Passes[p].Name + "'");
                edge(dependency, p);
                inputs[p].insert(dependency);
            }
            for (auto use : m_Passes[p].Uses)
            {
                const auto r = use.Texture;
                if (Reads(use.Access))
                {
                    uint32_t producer = latest[r];
                    if (producer == none && !m_Resources[r].Imported)
                    {
                        producer = firstWriter[r];
                        if (producer == none || producer == p || Writes(use.Access))
                            throw std::invalid_argument("RenderGraph: read before initialization of texture '" +
                                                        m_Resources[r].Name + "' in pass '" + m_Passes[p].Name + "'");
                    }
                    if (producer != none)
                    {
                        edge(producer, p);
                        inputs[p].insert(producer);
                    }
                    readers[r].push_back(p);
                }
                if (Writes(use.Access))
                {
                    edge(latest[r], p); // WAW; pure overwrite does not keep the older producer alive.
                    for (auto reader : readers[r])
                        if (!(latest[r] == none && !m_Resources[r].Imported && firstWriter[r] == p))
                            edge(reader, p); // WAR, including imported initial contents.
                    readers[r].clear();
                    latest[r] = p;
                }
            }
        }
        // Validate the whole graph before culling, with deterministic declaration-order ties.
        std::vector<uint32_t> indegree(count, 0), sorted;
        for (const auto& successors : edges)
            for (auto p : successors)
                ++indegree[p];
        std::set<uint32_t> available;
        for (uint32_t p = 0; p < count; ++p)
            if (!indegree[p])
                available.insert(p);
        while (!available.empty())
        {
            auto p = *available.begin();
            available.erase(available.begin());
            sorted.push_back(p);
            for (auto next : edges[p])
                if (!--indegree[next])
                    available.insert(next);
        }
        if (sorted.size() != count)
            throw std::invalid_argument("RenderGraph: dependency cycle");
        std::vector<bool>             live(count, false);
        std::function<void(uint32_t)> keep = [&](uint32_t p) {
            if (p == none || live[p])
                return;
            live[p] = true;
            for (auto dependency : inputs[p])
                keep(dependency);
        };
        for (uint32_t p = 0; p < count; ++p)
            if (m_Passes[p].SideEffect)
                keep(p);
        for (uint32_t r = 0; r < m_Resources.size(); ++r)
            if (m_Resources[r].Exported)
            {
                if (!m_Resources[r].Imported && latest[r] == none)
                    throw std::invalid_argument("RenderGraph: exported texture has no producer '" +
                                                m_Resources[r].Name + "'");
                keep(latest[r]);
            }
        for (auto p : sorted)
            if (live[p])
                m_Order.push_back(p);
        for (auto& r : m_Resources)
            m_CompiledResources.push_back({r.Name, r.Description, r.Imported, none, {}});
        for (uint32_t i = 0; i < m_Order.size(); ++i)
            for (auto use : m_Passes[m_Order[i]].Uses)
                m_CompiledResources[use.Texture].Accesses.push_back({m_Order[i], i, use.Access});
        // Exported transients remain live through output consumption at the end of execution.
        std::vector<uint32_t> resources, slotLast;
        for (uint32_t r = 0; r < m_Resources.size(); ++r)
            if (!m_Resources[r].Imported && !m_CompiledResources[r].Accesses.empty())
                resources.push_back(r);
        std::stable_sort(resources.begin(), resources.end(), [&](uint32_t a, uint32_t b) {
            return m_CompiledResources[a].Accesses.front().ExecutionIndex <
                   m_CompiledResources[b].Accesses.front().ExecutionIndex;
        });
        for (auto r : resources)
        {
            auto&    compiled = m_CompiledResources[r];
            uint32_t slot = 0, start = compiled.Accesses.front().ExecutionIndex;
            for (; slot < m_Slots.size(); ++slot)
                if (slotLast[slot] < start && Equal(m_Slots[slot], compiled.Description))
                    break;
            const auto end =
                m_Resources[r].Exported ? uint32_t(m_Order.size()) : compiled.Accesses.back().ExecutionIndex;
            if (slot == m_Slots.size())
            {
                m_Slots.push_back(compiled.Description);
                slotLast.push_back(end);
            }
            else
                slotLast[slot] = end;
            compiled.PhysicalSlot = slot;
        }
        if (m_Order.size() < m_Submitted.size() || !std::equal(m_Submitted.begin(), m_Submitted.end(), m_Order.begin()))
            throw std::invalid_argument("RenderGraph: new dependencies reorder an already submitted prefix");
        m_Compiled = true;
    }
    void RenderGraph::Allocate(TransientTexturePool& pool, const TransientTexturePool::Factory& create)
    {
        if (!m_Compiled)
            throw std::logic_error("RenderGraph: Compile required before allocation");
        auto textures = pool.Acquire(m_Slots, create);
        for (uint32_t r = 0; r < m_Resources.size(); ++r)
            if (!m_Resources[r].Imported)
            {
                auto slot            = m_CompiledResources[r].PhysicalSlot;
                m_Resources[r].Image = slot == uint32_t(-1) ? nullptr : textures.at(slot);
            }
    }
    void RenderGraph::ExecutePass(RenderHardware::Device& device, GraphPass p)
    {
        auto& pass = m_Passes[p];
        if (pass.Submitted)
            return;
        if (pass.Commands)
            device.Submit(pass.Commands);
        else
        {
            auto               commands = device.CreateCommandList();
            RenderGraphContext context {device, *commands, *this};
            if (pass.Recorder)
                pass.Recorder(context);
            device.Submit(commands);
        }
        pass.Submitted = true;
        m_Submitted.push_back(p);
    }
    void RenderGraph::Execute(RenderHardware::Device& device, TransientTexturePool& pool)
    {
        Allocate(pool, [&](const RenderHardware::TextureDesc& desc) { return device.CreateTexture(desc); });
        for (auto p : m_Order)
            ExecutePass(device, p);
    }
    void RenderGraph::ExecuteImportedPrefix(RenderHardware::Device& device)
    {
        if (!m_Compiled)
            throw std::logic_error("RenderGraph: Compile required before prefix execution");
        for (auto p : m_Order)
            for (auto use : m_Passes[p].Uses)
                if (!m_Resources[use.Texture].Imported)
                    throw std::invalid_argument("RenderGraph: prefix execution cannot use transient textures");
        for (auto p : m_Order)
        {
            // An already observable operation cannot subsequently be culled.
            m_Passes[p].SideEffect = true;
            ExecutePass(device, p);
        }
    }
    RenderHardware::Handle<RenderHardware::Texture> RenderGraph::Texture(GraphTexture texture) const
    {
        return m_Resources.at(texture).Image;
    }
    RenderHardware::Handle<RenderHardware::Texture> RenderGraphContext::Texture(GraphTexture texture) const
    {
        return Graph.Texture(texture);
    }
} // namespace Kans
