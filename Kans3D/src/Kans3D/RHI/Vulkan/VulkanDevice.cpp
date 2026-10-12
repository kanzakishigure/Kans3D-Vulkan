#include "kspch.h"
#include <cstdlib>
#include <cstring>
#include <deque>
#include "VulkanInternal.h"
#include "VulkanTestProbes.h"

namespace Kans::RenderHardware
{
    namespace
    {
        using namespace VulkanDetail;
        struct Commands;
        struct State
        {
            Context         Native;
            DeviceStats     Stats;
            bool            InFrame = false, Alive = true;
            std::thread::id Thread = std::this_thread::get_id();
            struct Frame
            {
                VkCommandPool Pool  = VK_NULL_HANDLE;
                VkFence       Fence = VK_NULL_HANDLE;
            };
            std::vector<Frame>                     Frames;
            Frame                                  Standalone;
            size_t                                 FrameIndex = 0;
            std::vector<std::function<void()>>     Deletions;
            std::vector<std::shared_ptr<Commands>> Submitted;
            State(const DeviceDesc& desc, const VulkanDeviceOptions& options) :
                Native(options), Frames(desc.FramesInFlight)
            {
                try
                {
                    for (auto& frame : Frames)
                        CreateFrame(frame);
                    CreateFrame(Standalone);
                }
                catch (...)
                {
                    DestroyFrames();
                    throw;
                }
            }
            void CheckThread() const
            {
                Require(std::this_thread::get_id() == Thread, "Device requires its owning thread");
            }
            void CreateFrame(Frame& frame)
            {
                VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pool.queueFamilyIndex = Native.QueueFamily;
                Check(vkCreateCommandPool(Native.Logical, &pool, nullptr, &frame.Pool));
                VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                Check(vkCreateFence(Native.Logical, &fence, nullptr, &frame.Fence));
            }
            void DestroyFrames()
            {
                auto destroy = [this](Frame& frame) {
                    if (frame.Fence)
                        vkDestroyFence(Native.Logical, frame.Fence, nullptr);
                    if (frame.Pool)
                        vkDestroyCommandPool(Native.Logical, frame.Pool, nullptr);
                    frame = {};
                };
                for (auto& frame : Frames)
                    destroy(frame);
                destroy(Standalone);
            }
            ~State() { DestroyFrames(); }
            Frame& Current() { return InFrame ? Frames[FrameIndex] : Standalone; }
            void   Drain()
            {
                // Destructors may enqueue more work while retained command captures
                // are being released. Drain only after queue/fence completion.
                while (!Deletions.empty())
                {
                    auto deletions = std::move(Deletions);
                    Deletions.clear();
                    for (auto& destroy : deletions)
                    {
                        destroy();
                        --Stats.LiveResources;
                        --Stats.PendingDestructions;
                    }
                }
            }
            void Retire();
        };
        struct Allocation
        {
            std::shared_ptr<State> Owner;
            std::function<void()>  Delete;
            explicit Allocation(std::shared_ptr<State> state) : Owner(std::move(state))
            {
                ++Owner->Stats.LiveResources;
            }
            ~Allocation()
            {
                if (Owner->Alive && Delete)
                {
                    Owner->Deletions.push_back(std::move(Delete));
                    ++Owner->Stats.PendingDestructions;
                }
                else if (!Delete)
                    --Owner->Stats.LiveResources;
            }
        };
        struct VKBuffer final : Buffer
        {
            Allocation    Memory;
            BufferDesc    Desc;
            VkBuffer      Native = VK_NULL_HANDLE;
            VmaAllocation Alloc  = VK_NULL_HANDLE;
            void*         Mapped = nullptr;
            Access        Last;
            size_t        PendingWrites = 0;
            VKBuffer(std::shared_ptr<State> owner, BufferDesc desc) : Memory(std::move(owner)), Desc(desc)
            {
                Require(desc.Bytes && uint32_t(desc.Usage) && !(uint32_t(desc.Usage) & ~63u),
                        "Invalid buffer description");
                VkBufferCreateInfo buffer {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                buffer.size = desc.Bytes;
                if (Has(desc.Usage, BufferUsage::Vertex))
                    buffer.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                if (Has(desc.Usage, BufferUsage::Index))
                    buffer.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                if (Has(desc.Usage, BufferUsage::Uniform))
                    buffer.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                if (Has(desc.Usage, BufferUsage::Storage))
                    buffer.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                if (Has(desc.Usage, BufferUsage::CopySource))
                    buffer.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                if (Has(desc.Usage, BufferUsage::CopyDestination))
                    buffer.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                VmaAllocationCreateInfo allocation {};
                allocation.usage = desc.Memory == MemoryUsage::DeviceLocal ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE :
                                                                             VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                if (desc.Memory != MemoryUsage::DeviceLocal)
                    allocation.flags =
                        VMA_ALLOCATION_CREATE_MAPPED_BIT |
                        (desc.Memory == MemoryUsage::Upload ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT :
                                                              VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
                VmaAllocationInfo info {};
                auto              allocator = Memory.Owner->Native.Allocator;
                Check(vmaCreateBuffer(allocator, &buffer, &allocation, &Native, &Alloc, &info));
                Mapped        = info.pMappedData;
                Memory.Delete = [allocator, buffer = Native, alloc = Alloc] {
                    vmaDestroyBuffer(allocator, buffer, alloc);
                };
            }
            const BufferDesc& GetDesc() const override { return Desc; }
            void              Write(size_t offset, const void* data, size_t bytes) override
            {
                Memory.Owner->CheckThread();
                Require(Desc.Memory == MemoryUsage::Upload && (data || !bytes), "Write requires an upload buffer");
                Range(Desc.Bytes, offset, bytes);
                if (bytes)
                    std::memcpy(static_cast<uint8_t*>(Mapped) + offset, data, bytes);
                Check(vmaFlushAllocation(Memory.Owner->Native.Allocator, Alloc, offset, bytes));
                // Host writes precede submission; the next inferred use includes
                // the host access in its dependency (also for noncoherent memory).
                Last = {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT};
            }
            std::vector<uint8_t> Read(size_t offset, size_t bytes) const override
            {
                Memory.Owner->CheckThread();
                Require(Desc.Memory == MemoryUsage::Readback && PendingWrites == 0,
                        "Readback requires frame retirement or WaitIdle");
                Range(Desc.Bytes, offset, bytes);
                Check(vmaInvalidateAllocation(Memory.Owner->Native.Allocator, Alloc, offset, bytes));
                const auto* start = static_cast<const uint8_t*>(Mapped) + offset;
                return {start, start + bytes};
            }
        };
        uint32_t Width(const TextureDesc& d, uint32_t mip) { return std::max(1u, d.Width >> mip); }
        uint32_t Height(const TextureDesc& d, uint32_t mip) { return std::max(1u, d.Height >> mip); }
        size_t   TextureBytes(const TextureDesc& d, TextureSubresource sub)
        {
            Require(sub.Mip < d.MipLevels && sub.Face < (d.Dimension == TextureDimension::Cube ? 6u : 1u),
                    "Invalid texture subresource");
            return size_t(Width(d, sub.Mip)) * Height(d, sub.Mip) * Info(d.PixelFormat).Bytes;
        }
        struct VKTexture final : Texture
        {
            Allocation               Memory;
            TextureDesc              Desc;
            VkImage                  Native = VK_NULL_HANDLE;
            VmaAllocation            Alloc  = VK_NULL_HANDLE;
            std::vector<VkImageView> Views;
            std::vector<Access>      Last;
            size_t                   Index(TextureSubresource sub) const { return sub.Face * Desc.MipLevels + sub.Mip; }
            VKTexture(std::shared_ptr<State> owner, TextureDesc desc) : Memory(std::move(owner)), Desc(desc)
            {
                Require(desc.Width && desc.Height && desc.MipLevels && uint32_t(desc.Usage) &&
                            !(uint32_t(desc.Usage) & ~31u),
                        "Invalid texture description");
                uint32_t maxMips = 1;
                for (uint32_t size = std::max(desc.Width, desc.Height); size > 1; size >>= 1)
                    ++maxMips;
                Require(desc.MipLevels <= maxMips &&
                            (desc.Dimension != TextureDimension::Cube || desc.Width == desc.Height),
                        "Invalid texture mip count or cube extent");
                auto f = Info(desc.PixelFormat);
                Require((!Has(desc.Usage, TextureUsage::RenderTarget) || !f.Depth()) &&
                            (!Has(desc.Usage, TextureUsage::DepthStencil) || f.Depth()),
                        "Attachment format/usage mismatch");
                VkImageCreateInfo image {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                image.flags       = desc.Dimension == TextureDimension::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
                image.imageType   = VK_IMAGE_TYPE_2D;
                image.format      = f.Native;
                image.extent      = {desc.Width, desc.Height, 1};
                image.mipLevels   = desc.MipLevels;
                image.arrayLayers = desc.Dimension == TextureDimension::Cube ? 6 : 1;
                image.samples     = VK_SAMPLE_COUNT_1_BIT;
                image.tiling      = VK_IMAGE_TILING_OPTIMAL;
                if (Has(desc.Usage, TextureUsage::Sampled))
                    image.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
                if (Has(desc.Usage, TextureUsage::RenderTarget))
                    image.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                if (Has(desc.Usage, TextureUsage::DepthStencil))
                    image.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                if (Has(desc.Usage, TextureUsage::CopySource))
                    image.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                if (Has(desc.Usage, TextureUsage::CopyDestination))
                    image.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                VkImageFormatProperties properties;
                Check(vkGetPhysicalDeviceImageFormatProperties(Memory.Owner->Native.Physical,
                                                               image.format,
                                                               image.imageType,
                                                               image.tiling,
                                                               image.usage,
                                                               image.flags,
                                                               &properties));
                VmaAllocationCreateInfo allocation {};
                allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                auto allocator   = Memory.Owner->Native.Allocator;
                auto device      = Memory.Owner->Native.Logical;
                Check(vmaCreateImage(allocator, &image, &allocation, &Native, &Alloc, nullptr));
                try
                {
                    Last.resize(size_t(image.arrayLayers) * image.mipLevels);
                    if (image.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
                    {
                        Views.resize(Last.size(), VK_NULL_HANDLE);
                        for (uint32_t face = 0; face < image.arrayLayers; ++face)
                            for (uint32_t mip = 0; mip < image.mipLevels; ++mip)
                            {
                                VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                                view.image            = Native;
                                view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
                                view.format           = f.Native;
                                view.subresourceRange = {f.Aspects, mip, 1, face, 1};
                                Check(vkCreateImageView(device, &view, nullptr, &Views[Index({mip, face})]));
                            }
                    }
                    Memory.Delete = [allocator, device, image = Native, alloc = Alloc, views = Views] {
                        for (auto view : views)
                            vkDestroyImageView(device, view, nullptr);
                        vmaDestroyImage(allocator, image, alloc);
                    };
                }
                catch (...)
                {
                    for (auto view : Views)
                        if (view)
                            vkDestroyImageView(device, view, nullptr);
                    vmaDestroyImage(allocator, Native, Alloc);
                    throw;
                }
            }
            const TextureDesc& GetDesc() const override { return Desc; }
        };
        struct VKSampler final : Sampler
        {
            Allocation Memory;
            VkSampler  Native = VK_NULL_HANDLE;
            VKSampler(std::shared_ptr<State> owner, const SamplerDesc& desc) : Memory(std::move(owner))
            {
                Require(std::isfinite(desc.MinLod) && std::isfinite(desc.MaxLod) && desc.MinLod >= 0 &&
                            desc.MaxLod >= desc.MinLod,
                        "Invalid sampler LOD range");
                auto address = [](AddressMode value) {
                    switch (value)
                    {
                        case AddressMode::Repeat:
                            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                        case AddressMode::ClampToEdge:
                            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                        case AddressMode::MirrorRepeat:
                            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    }
                    throw std::invalid_argument("Invalid sampler address mode");
                };
                VkSamplerCreateInfo sampler {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                sampler.minFilter = desc.Min == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                sampler.magFilter = desc.Mag == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                sampler.mipmapMode =
                    desc.Mip == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
                sampler.addressModeU = address(desc.U);
                sampler.addressModeV = address(desc.V);
                sampler.addressModeW = address(desc.W);
                sampler.minLod       = desc.MinLod;
                sampler.maxLod       = desc.MaxLod;
                auto device          = Memory.Owner->Native.Logical;
                Check(vkCreateSampler(device, &sampler, nullptr, &Native));
                Memory.Delete = [device, sampler = Native] { vkDestroySampler(device, sampler, nullptr); };
            }
        };
        template<class T, class U>
        std::shared_ptr<T> Cast(const Handle<U>& resource, const std::shared_ptr<State>& state)
        {
            auto value = std::dynamic_pointer_cast<T>(resource);
            Require(value && value->Memory.Owner == state, "Resource belongs to another device or backend");
            return value;
        }
        constexpr Access TransferRead {VK_PIPELINE_STAGE_2_COPY_BIT,
                                       VK_ACCESS_2_TRANSFER_READ_BIT,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
        constexpr Access TransferWrite {VK_PIPELINE_STAGE_2_COPY_BIT,
                                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
        constexpr Access HostRead {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT};
        struct Execution
        {
            std::shared_ptr<State>              Owner;
            VkCommandPool                       Pool;
            VkCommandBuffer                     Native = VK_NULL_HANDLE;
            std::vector<Handle<Resource>>       Temporary;
            std::vector<VkBufferMemoryBarrier2> BufferBarriers;
            std::vector<VkImageMemoryBarrier2>  ImageBarriers;
            // Transition planning is independent of emission. A future compiled
            // graph access sequence can call Use for a whole pass, then flush
            // once, while command-inferred uses remain the correctness fallback.
            void FlushBarriers()
            {
                if (BufferBarriers.empty() && ImageBarriers.empty())
                    return;
                VkDependencyInfo dependency {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.bufferMemoryBarrierCount = uint32_t(BufferBarriers.size());
                dependency.pBufferMemoryBarriers    = BufferBarriers.data();
                dependency.imageMemoryBarrierCount  = uint32_t(ImageBarriers.size());
                dependency.pImageMemoryBarriers     = ImageBarriers.data();
                vkCmdPipelineBarrier2(Native, &dependency);
                BufferBarriers.clear();
                ImageBarriers.clear();
            }
            explicit Execution(std::shared_ptr<State> state) : Owner(std::move(state)), Pool(Owner->Current().Pool)
            {
                Begin();
            }
            void Begin()
            {
                VkCommandBufferAllocateInfo allocation {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocation.commandPool        = Pool;
                allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocation.commandBufferCount = 1;
                Check(vkAllocateCommandBuffers(Owner->Native.Logical, &allocation, &Native));
                VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(vkBeginCommandBuffer(Native, &begin));
            }
            void Submit()
            {
                FlushBarriers();
                Check(vkEndCommandBuffer(Native));
                VkCommandBufferSubmitInfo buffer {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
                buffer.commandBuffer = Native;
                VkSubmitInfo2 submit {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
                submit.commandBufferInfoCount = 1;
                submit.pCommandBufferInfos    = &buffer;
                Check(vkQueueSubmit2(Owner->Native.Queue, 1, &submit, VK_NULL_HANDLE));
            }
            void FlushAndWait()
            {
                // Packed depth/stencil requires CPU repacking between aspect copies.
                // Waiting here does NOT retire public readback tokens or lists.
                Submit();
                Check(vkQueueWaitIdle(Owner->Native.Queue));
                Begin();
            }
            std::shared_ptr<VKBuffer> Staging(size_t bytes, MemoryUsage memory)
            {
                auto value = std::make_shared<VKBuffer>(
                    Owner, BufferDesc {bytes, BufferUsage::CopySource | BufferUsage::CopyDestination, memory});
                Temporary.push_back(value);
                return value;
            }
            void Use(VKBuffer& buffer, Access next)
            {
                VkBufferMemoryBarrier2 barrier {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                barrier.srcStageMask        = buffer.Last.Stage;
                barrier.srcAccessMask       = buffer.Last.Mask;
                barrier.dstStageMask        = next.Stage;
                barrier.dstAccessMask       = next.Mask;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer                                            = buffer.Native;
                barrier.size                                              = VK_WHOLE_SIZE;
                BufferBarriers.push_back(barrier);
                buffer.Last = next;
            }
            void Use(VKTexture& texture, TextureSubresource sub, Access next)
            {
                auto&                 last = texture.Last.at(texture.Index(sub));
                VkImageMemoryBarrier2 barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask        = last.Stage;
                barrier.srcAccessMask       = last.Mask;
                barrier.dstStageMask        = next.Stage;
                barrier.dstAccessMask       = next.Mask;
                barrier.oldLayout           = last.Layout;
                barrier.newLayout           = next.Layout;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                                             = texture.Native;
                barrier.subresourceRange = {Info(texture.Desc.PixelFormat).Aspects, sub.Mip, 1, sub.Face, 1};
                ImageBarriers.push_back(barrier);
                last = next;
            }
            void Copy(VKBuffer& source, size_t from, VKBuffer& destination, size_t to, size_t bytes)
            {
                if (&source == &destination)
                    Use(source,
                        {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT});
                else
                {
                    Use(source, TransferRead);
                    Use(destination, TransferWrite);
                }
                FlushBarriers();
                VkBufferCopy copy {from, to, bytes};
                vkCmdCopyBuffer(Native, source.Native, destination.Native, 1, &copy);
                if (destination.Desc.Memory == MemoryUsage::Readback)
                {
                    Use(destination, HostRead);
                    FlushBarriers();
                    // Host visibility is an export, not the last GPU access.
                    // Preserve the producer for later queue dependencies.
                    destination.Last = TransferWrite;
                }
            }
            void ImageCopy(VKBuffer&          buffer,
                           size_t             offset,
                           VKTexture&         texture,
                           TextureSubresource sub,
                           VkImageAspectFlags aspect,
                           bool               upload)
            {
                Use(buffer, upload ? TransferRead : TransferWrite);
                Use(texture, sub, upload ? TransferWrite : TransferRead);
                FlushBarriers();
                VkBufferImageCopy region {};
                region.bufferOffset     = offset;
                region.imageSubresource = {aspect, sub.Mip, sub.Face, 1};
                region.imageExtent      = {Width(texture.Desc, sub.Mip), Height(texture.Desc, sub.Mip), 1};
                if (upload)
                    vkCmdCopyBufferToImage(
                        Native, buffer.Native, texture.Native, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                else
                {
                    vkCmdCopyImageToBuffer(
                        Native, texture.Native, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.Native, 1, &region);
                    if (buffer.Desc.Memory == MemoryUsage::Readback)
                    {
                        Use(buffer, HostRead);
                        FlushBarriers();
                        buffer.Last = TransferWrite;
                    }
                }
            }
            void PackedCopy(VKBuffer& buffer, size_t offset, VKTexture& texture, TextureSubresource sub, bool upload)
            {
                const size_t bytes = TextureBytes(texture.Desc, sub), pixels = bytes / 4;
                auto         depth   = Staging(bytes, upload ? MemoryUsage::Upload : MemoryUsage::Readback);
                auto         stencil = Staging(pixels, upload ? MemoryUsage::Upload : MemoryUsage::Readback);
                if (upload)
                {
                    auto packed = Staging(bytes, MemoryUsage::Readback);
                    Copy(buffer, offset, *packed, 0, bytes);
                    FlushAndWait();
                    auto                  raw = packed->Read(0, bytes);
                    std::vector<uint32_t> d(pixels);
                    std::vector<uint8_t>  s(pixels);
                    for (size_t i = 0; i < pixels; ++i)
                    {
                        uint32_t value;
                        std::memcpy(&value, raw.data() + i * 4, 4);
                        d[i] = value >> 8;
                        s[i] = uint8_t(value);
                    }
                    depth->Write(0, d.data(), bytes);
                    stencil->Write(0, s.data(), pixels);
                    ImageCopy(*depth, 0, texture, sub, VK_IMAGE_ASPECT_DEPTH_BIT, true);
                    ImageCopy(*stencil, 0, texture, sub, VK_IMAGE_ASPECT_STENCIL_BIT, true);
                }
                else
                {
                    ImageCopy(*depth, 0, texture, sub, VK_IMAGE_ASPECT_DEPTH_BIT, false);
                    ImageCopy(*stencil, 0, texture, sub, VK_IMAGE_ASPECT_STENCIL_BIT, false);
                    FlushAndWait();
                    auto                  d = depth->Read(0, bytes), s = stencil->Read(0, pixels);
                    std::vector<uint32_t> raw(pixels);
                    for (size_t i = 0; i < pixels; ++i)
                    {
                        uint32_t value;
                        std::memcpy(&value, d.data() + i * 4, 4);
                        raw[i] = (value << 8) | s[i];
                    }
                    auto packed = Staging(bytes, MemoryUsage::Upload);
                    packed->Write(0, raw.data(), bytes);
                    Copy(*packed, 0, buffer, offset, bytes);
                }
            }
        };
        VkAttachmentLoadOp Load(LoadOp op)
        {
            switch (op)
            {
                case LoadOp::Load:
                    return VK_ATTACHMENT_LOAD_OP_LOAD;
                case LoadOp::Clear:
                    return VK_ATTACHMENT_LOAD_OP_CLEAR;
                case LoadOp::Discard:
                    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            throw std::invalid_argument("Invalid load operation");
        }
        VkAttachmentStoreOp Store(StoreOp op)
        {
            switch (op)
            {
                case StoreOp::Store:
                    return VK_ATTACHMENT_STORE_OP_STORE;
                case StoreOp::Discard:
                    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
            throw std::invalid_argument("Invalid store operation");
        }
        [[noreturn]] void Phase3b() { throw std::logic_error("Vulkan graphics is not implemented: phase 3b"); }
        struct Commands final : CommandList
        {
            std::shared_ptr<State>                       Owner;
            bool                                         Sealed = false, Rendering = false;
            uint32_t                                     RenderingWidth = 0, RenderingHeight = 0;
            std::vector<std::function<void(Execution&)>> Recorded;
            std::vector<std::shared_ptr<VKBuffer>>       Readbacks;
            std::vector<Handle<Resource>>                Temporary;
            explicit Commands(std::shared_ptr<State> owner) : Owner(std::move(owner)) {}
            ~Commands() { Retire(); }
            void Retire()
            {
                for (const auto& b : Readbacks)
                    --b->PendingWrites;
                Readbacks.clear();
                Recorded.clear();
                Temporary.clear();
            }
            void Mutable()
            {
                Owner->CheckThread();
                Require(!Sealed, "Command list is sealed");
            }
            void Outside()
            {
                Mutable();
                Require(!Rendering, "Transfer requires no rendering scope");
            }
            void Mark(const std::shared_ptr<VKBuffer>& b)
            {
                if (b->Desc.Memory == MemoryUsage::Readback)
                {
                    Readbacks.push_back(b);
                    ++b->PendingWrites;
                }
            }
            void BeginRendering(const RenderingDesc& desc) override
            {
                Outside();
                Require(!desc.Colors.empty() || desc.Depth.Target, "Rendering requires an attachment");
                Require(desc.Colors.size() <= Owner->Native.Properties.limits.maxColorAttachments,
                        "Too many color attachments");
                uint32_t                                   width = 0, height = 0;
                std::vector<std::pair<VKTexture*, size_t>> seen;
                auto validate = [&](const Handle<Texture>& target, TextureSubresource sub, bool depth) {
                    auto t = Cast<VKTexture>(target, Owner);
                    TextureBytes(t->Desc, sub);
                    Require(Info(t->Desc.PixelFormat).Depth() == depth &&
                                Has(t->Desc.Usage, depth ? TextureUsage::DepthStencil : TextureUsage::RenderTarget),
                            "Invalid attachment usage or format");
                    if (!width)
                    {
                        width  = Width(t->Desc, sub.Mip);
                        height = Height(t->Desc, sub.Mip);
                    }
                    Require(width == Width(t->Desc, sub.Mip) && height == Height(t->Desc, sub.Mip),
                            "Attachment extents differ");
                    auto key = std::make_pair(t.get(), t->Index(sub));
                    Require(std::find(seen.begin(), seen.end(), key) == seen.end(), "Duplicate rendering attachment");
                    seen.push_back(key);
                };
                for (const auto& color : desc.Colors)
                {
                    validate(color.Target, color.Subresource, false);
                    Load(color.Load);
                    Store(color.Store);
                }
                if (desc.Depth.Target)
                {
                    validate(desc.Depth.Target, desc.Depth.Subresource, true);
                    Load(desc.Depth.Load);
                    Store(desc.Depth.Store);
                    Require(desc.Depth.ClearDepth >= 0 && desc.Depth.ClearDepth <= 1, "Invalid depth clear");
                }
                Recorded.push_back([state = Owner, desc, width, height](Execution& e) {
                    std::vector<VkRenderingAttachmentInfo> colors;
                    for (const auto& a : desc.Colors)
                    {
                        auto t = Cast<VKTexture>(a.Target, state);
                        e.Use(*t,
                              a.Subresource,
                              {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
                        VkRenderingAttachmentInfo attachment {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                        attachment.imageView   = t->Views[t->Index(a.Subresource)];
                        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        attachment.loadOp      = Load(a.Load);
                        attachment.storeOp     = Store(a.Store);
                        if (t->Desc.PixelFormat == Format::R32Uint)
                            attachment.clearValue.color.uint32[0] = a.ClearUint;
                        else
                            std::copy(a.ClearColor.begin(), a.ClearColor.end(), attachment.clearValue.color.float32);
                        colors.push_back(attachment);
                    }
                    VkRenderingAttachmentInfo depth {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                    bool                      stencil = false;
                    if (desc.Depth.Target)
                    {
                        const auto& a = desc.Depth;
                        auto        t = Cast<VKTexture>(a.Target, state);
                        e.Use(
                            *t,
                            a.Subresource,
                            {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL});
                        depth.imageView               = t->Views[t->Index(a.Subresource)];
                        depth.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        depth.loadOp                  = Load(a.Load);
                        depth.storeOp                 = Store(a.Store);
                        depth.clearValue.depthStencil = {a.ClearDepth, a.ClearStencil};
                        stencil                       = Info(t->Desc.PixelFormat).Stencil();
                    }
                    VkRenderingInfo rendering {VK_STRUCTURE_TYPE_RENDERING_INFO};
                    rendering.renderArea.extent    = {width, height};
                    rendering.layerCount           = 1;
                    rendering.colorAttachmentCount = uint32_t(colors.size());
                    rendering.pColorAttachments    = colors.data();
                    rendering.pDepthAttachment     = desc.Depth.Target ? &depth : nullptr;
                    rendering.pStencilAttachment   = stencil ? &depth : nullptr;
                    e.FlushBarriers();
                    vkCmdBeginRendering(e.Native, &rendering);
                });
                RenderingWidth  = width;
                RenderingHeight = height;
                Rendering       = true;
            }
            void EndRendering() override
            {
                Mutable();
                Require(Rendering, "No active rendering scope");
                Recorded.push_back([](Execution& e) { vkCmdEndRendering(e.Native); });
                Rendering = false;
            }
            void SetViewport(const Viewport& v) override
            {
                Mutable();
                Require(Rendering && v.Width > 0 && v.Height > 0 && v.MinDepth >= 0 && v.MaxDepth <= 1 &&
                            v.MinDepth <= v.MaxDepth,
                        "Invalid viewport");
                Recorded.push_back([v](Execution& e) {
                    VkViewport native {v.X, v.Y, v.Width, v.Height, v.MinDepth, v.MaxDepth};
                    vkCmdSetViewport(e.Native, 0, 1, &native);
                });
            }
            void SetScissor(const Scissor& s) override
            {
                Mutable();
                Require(Rendering, "Scissor requires a rendering scope");
                // Neutral scissors may extend beyond the framebuffer. Intersect
                // with the attachment bounds before forming a native rectangle,
                // whose offsets must be nonnegative and sums fit signed 32 bits.
                const int64_t  x      = std::clamp<int64_t>(s.X, 0, RenderingWidth);
                const int64_t  y      = std::clamp<int64_t>(s.Y, 0, RenderingHeight);
                const int64_t  right  = std::clamp<int64_t>(int64_t(s.X) + s.Width, x, RenderingWidth);
                const int64_t  bottom = std::clamp<int64_t>(int64_t(s.Y) + s.Height, y, RenderingHeight);
                const VkRect2D native {{int32_t(x), int32_t(y)}, {uint32_t(right - x), uint32_t(bottom - y)}};
                Recorded.push_back([native](Execution& e) { vkCmdSetScissor(e.Native, 0, 1, &native); });
            }
            void BindPipeline(const Handle<Pipeline>&) override { Phase3b(); }
            void BindGroup(const Handle<RenderHardware::BindGroup>&) override { Phase3b(); }
            void PushConstants(uint32_t, const void*, uint32_t) override { Phase3b(); }
            void BindVertexBuffers(const std::vector<VertexBufferView>&) override { Phase3b(); }
            void BindIndexBuffer(const Handle<Buffer>&, IndexType, size_t) override { Phase3b(); }
            void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override { Phase3b(); }
            void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override { Phase3b(); }
            void CopyBuffer(const Handle<Buffer>& source,
                            size_t                from,
                            const Handle<Buffer>& destination,
                            size_t                to,
                            size_t                bytes) override
            {
                Outside();
                auto s = Cast<VKBuffer>(source, Owner), d = Cast<VKBuffer>(destination, Owner);
                Require(Has(s->Desc.Usage, BufferUsage::CopySource) && Has(d->Desc.Usage, BufferUsage::CopyDestination),
                        "Buffer copy usage mismatch");
                Range(s->Desc.Bytes, from, bytes);
                Range(d->Desc.Bytes, to, bytes);
                Require(s != d || from + bytes <= to || to + bytes <= from, "Overlapping buffer copy");
                Recorded.push_back([s, d, from, to, bytes](Execution& e) {
                    if (bytes)
                        e.Copy(*s, from, *d, to, bytes);
                });
                Mark(d);
            }
            void CopyBufferToTexture(const Handle<Buffer>&  source,
                                     size_t                 offset,
                                     const Handle<Texture>& destination,
                                     TextureSubresource     sub) override
            {
                Outside();
                auto s = Cast<VKBuffer>(source, Owner);
                auto d = Cast<VKTexture>(destination, Owner);
                Require(Has(s->Desc.Usage, BufferUsage::CopySource) &&
                            Has(d->Desc.Usage, TextureUsage::CopyDestination),
                        "Texture upload usage mismatch");
                Require(offset % Info(d->Desc.PixelFormat).Bytes == 0, "Texture copy offset must be texel aligned");
                Range(s->Desc.Bytes, offset, TextureBytes(d->Desc, sub));
                Recorded.push_back([s, d, offset, sub](Execution& e) {
                    auto f = Info(d->Desc.PixelFormat);
                    if (f.Stencil())
                        e.PackedCopy(*s, offset, *d, sub, true);
                    else
                        e.ImageCopy(*s, offset, *d, sub, f.Aspects, true);
                });
            }
            void CopyTextureToBuffer(const Handle<Texture>& source,
                                     TextureSubresource     sub,
                                     const Handle<Buffer>&  destination,
                                     size_t                 offset) override
            {
                Outside();
                auto s = Cast<VKTexture>(source, Owner);
                auto d = Cast<VKBuffer>(destination, Owner);
                Require(Has(s->Desc.Usage, TextureUsage::CopySource) &&
                            Has(d->Desc.Usage, BufferUsage::CopyDestination),
                        "Texture readback usage mismatch");
                Require(offset % Info(s->Desc.PixelFormat).Bytes == 0, "Texture copy offset must be texel aligned");
                Range(d->Desc.Bytes, offset, TextureBytes(s->Desc, sub));
                Recorded.push_back([s, d, offset, sub](Execution& e) {
                    auto f = Info(s->Desc.PixelFormat);
                    if (f.Stencil())
                        e.PackedCopy(*d, offset, *s, sub, false);
                    else
                        e.ImageCopy(*d, offset, *s, sub, f.Aspects, false);
                });
                Mark(d);
            }
            void GenerateMips(const Handle<Texture>& texture) override
            {
                Outside();
                auto t = Cast<VKTexture>(texture, Owner);
                auto f = Info(t->Desc.PixelFormat);
                Require(Has(t->Desc.Usage, TextureUsage::CopySource) &&
                            Has(t->Desc.Usage, TextureUsage::CopyDestination) && t->Desc.MipLevels > 1 && !f.Depth() &&
                            t->Desc.PixelFormat != Format::R32Uint,
                        "Texture cannot generate mips");
                VkFormatProperties properties;
                vkGetPhysicalDeviceFormatProperties(Owner->Native.Physical, f.Native, &properties);
                const auto required = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
                Require((properties.optimalTilingFeatures & required) == required, "Texture format cannot filter mips");
                Recorded.push_back([t](Execution& e) {
                    for (uint32_t face = 0; face < (t->Desc.Dimension == TextureDimension::Cube ? 6u : 1u); ++face)
                        for (uint32_t mip = 1; mip < t->Desc.MipLevels; ++mip)
                        {
                            e.Use(*t,
                                  {mip - 1, face},
                                  {VK_PIPELINE_STAGE_2_BLIT_BIT,
                                   VK_ACCESS_2_TRANSFER_READ_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL});
                            e.Use(*t,
                                  {mip, face},
                                  {VK_PIPELINE_STAGE_2_BLIT_BIT,
                                   VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL});
                            e.FlushBarriers();
                            VkImageBlit region {};
                            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, face, 1};
                            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1};
                            region.srcOffsets[1]  = {
                                int32_t(Width(t->Desc, mip - 1)), int32_t(Height(t->Desc, mip - 1)), 1};
                            region.dstOffsets[1] = {int32_t(Width(t->Desc, mip)), int32_t(Height(t->Desc, mip)), 1};
                            vkCmdBlitImage(e.Native,
                                           t->Native,
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           t->Native,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                           1,
                                           &region,
                                           VK_FILTER_LINEAR);
                        }
                });
            }
        };
        void State::Retire()
        {
            for (auto& list : Submitted)
                list->Retire();
            Submitted.clear();
            Drain();
            // Reset only after all submitted command buffers are known complete.
            for (auto& frame : Frames)
                Check(vkResetCommandPool(Native.Logical, frame.Pool, 0));
            Check(vkResetCommandPool(Native.Logical, Standalone.Pool, 0));
        }
        class VKDevice final : public Device
        {
            DeviceDesc             Desc;
            std::shared_ptr<State> Owner;

        public:
            VKDevice(DeviceDesc desc, const VulkanDeviceOptions& options) : Desc(desc)
            {
                Require(desc.FramesInFlight > 0, "FramesInFlight must be positive");
                Owner = std::make_shared<State>(desc, options);
            }
            ~VKDevice() override
            {
                // Destructors cannot throw. Normal WaitIdle reports device loss.
                vkDeviceWaitIdle(Owner->Native.Logical);
                Owner->Retire();
                Owner->Alive = false;
            }
            const Context&        GetContext() const { return Owner->Native; }
            const DeviceDesc&     GetDesc() const override { return Desc; }
            CoordinateConventions GetCoordinateConventions() const override
            {
                return {ClipYDirection::Down, DepthRange::ZeroToOne, FramebufferOrigin::UpperLeft};
            }
            DeviceStats GetStats() const override
            {
                Owner->CheckThread();
                return Owner->Stats;
            }
            size_t GetBufferBindingAlignment(BindingType type) const override
            {
                switch (type)
                {
                    case BindingType::UniformBuffer:
                        return Owner->Native.Properties.limits.minUniformBufferOffsetAlignment;
                    case BindingType::StorageBuffer:
                        return Owner->Native.Properties.limits.minStorageBufferOffsetAlignment;
                    default:
                        throw std::invalid_argument("Binding type is not a buffer");
                }
            }
            Handle<Buffer> CreateBuffer(const BufferDesc& d) override
            {
                Owner->CheckThread();
                return std::make_shared<VKBuffer>(Owner, d);
            }
            Handle<Texture> CreateTexture(const TextureDesc& d) override
            {
                Owner->CheckThread();
                return std::make_shared<VKTexture>(Owner, d);
            }
            Handle<Sampler> CreateSampler(const SamplerDesc& d) override
            {
                Owner->CheckThread();
                return std::make_shared<VKSampler>(Owner, d);
            }
            Handle<Shader>          CreateShader(const Kans::ShaderData&) override { Phase3b(); }
            Handle<BindGroupLayout> CreateBindGroupLayout(const BindGroupLayoutDesc&) override { Phase3b(); }
            Handle<BindGroup>       CreateBindGroup(const BindGroupDesc&) override { Phase3b(); }
            Handle<Pipeline>        CreatePipeline(const PipelineDesc&) override { Phase3b(); }
            Handle<CommandList>     CreateCommandList() override
            {
                Owner->CheckThread();
                return std::make_shared<Commands>(Owner);
            }
            void BeginFrame() override
            {
                Owner->CheckThread();
                Require(!Owner->InFrame, "Frame is already active");
                auto& frame = Owner->Frames[Owner->FrameIndex];
                Check(vkWaitForFences(Owner->Native.Logical, 1, &frame.Fence, VK_TRUE, UINT64_MAX));
                Owner->InFrame = true;
            }
            void Submit(const Handle<CommandList>& commands) override
            {
                Owner->CheckThread();
                auto list = std::dynamic_pointer_cast<Commands>(commands);
                Require(list && list->Owner == Owner && !list->Sealed && !list->Rendering,
                        "Invalid command submission");
                list->Sealed = true;
                Owner->Submitted.push_back(list);
                Execution execution(Owner);
                try
                {
                    for (const auto& command : list->Recorded)
                        command(execution);
                    execution.Submit();
                    list->Temporary = std::move(execution.Temporary);
                }
                catch (...)
                {
                    // A packed transfer may already have submitted a prefix.
                    // Keep every staging allocation alive until explicit retirement.
                    list->Temporary = std::move(execution.Temporary);
                    throw;
                }
            }
            void EndFrame() override
            {
                Owner->CheckThread();
                Require(Owner->InFrame, "No active frame");
                auto& frame = Owner->Frames[Owner->FrameIndex];
                Check(vkResetFences(Owner->Native.Logical, 1, &frame.Fence));
                VkSubmitInfo2 tail {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
                Check(vkQueueSubmit2(Owner->Native.Queue, 1, &tail, frame.Fence));
                Check(vkWaitForFences(Owner->Native.Logical, 1, &frame.Fence, VK_TRUE, UINT64_MAX));
                Owner->Retire();
                Owner->InFrame    = false;
                Owner->FrameIndex = (Owner->FrameIndex + 1) % Desc.FramesInFlight;
            }
            void WaitIdle() override
            {
                Owner->CheckThread();
                Check(vkDeviceWaitIdle(Owner->Native.Logical));
                Owner->Retire();
            }
            uintptr_t GetImGuiTextureID(const Handle<Texture>&, const SamplerDesc*) const override
            {
                throw std::logic_error("Vulkan GetImGuiTextureID is not implemented until phase 4");
            }
        };
    } // namespace
    std::unique_ptr<Device> CreateVulkanDevice(const DeviceDesc& desc, const VulkanDeviceOptions& options)
    {
        return std::make_unique<VKDevice>(desc, options);
    }
    std::unique_ptr<Device> CreateVulkanDevice(const DeviceDesc& desc)
    {
        VulkanDeviceOptions options;
#if defined(KS_DEBUG) || !defined(NDEBUG)
        options.Validation = true;
#endif
        const char* allow        = std::getenv("KANS_VULKAN_ALLOW_NON_CPU");
        options.RequireCpuDevice = !allow || std::strcmp(allow, "1") != 0;
        return CreateVulkanDevice(desc, options);
    }
} // namespace Kans::RenderHardware

namespace Kans::RenderHardware::VulkanTestProbes
{
    Diagnostics Inspect(const Device& device)
    {
        const auto& context = dynamic_cast<const VKDevice&>(device).GetContext();
        return {context.Properties.deviceType,
                context.Properties.apiVersion,
                context.Options.Validation,
                context.Options.SynchronizationValidation};
    }
    void EmitValidationError(const Device& device, const char* message)
    {
        const auto& context = dynamic_cast<const VKDevice&>(device).GetContext();
        auto        submit  = reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(
            vkGetInstanceProcAddr(context.Instance, "vkSubmitDebugUtilsMessageEXT"));
        VulkanDetail::Require(submit != nullptr, "Debug message entry point is missing");
        VkDebugUtilsMessengerCallbackDataEXT data {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
        data.pMessage = message;
        submit(context.Instance,
               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
               &data);
    }
} // namespace Kans::RenderHardware::VulkanTestProbes
