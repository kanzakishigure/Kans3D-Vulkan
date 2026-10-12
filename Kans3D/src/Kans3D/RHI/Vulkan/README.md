# Vulkan foundation (phase 3a)

`VulkanDevice.h` provides backend-private factories and test construction policy.
`VulkanContext.cpp` owns a headless Vulkan 1.3 instance, debug messenger, physical
and logical device, one graphics queue and VMA allocator. `VulkanDevice.cpp`
implements resources, CPU command recording, submission, state tracking and
retirement. `VulkanInternal.h` holds native details; `VulkanTestProbes.h` exposes
only test diagnostics. Native types never enter the neutral RHI headers.

All buffer usages and memory intents, all nine formats, 2D/cube allocations,
mips, independent samplers, full-subresource copies, mip filtering and clear-only
dynamic rendering are supported. Graphics shader modules, layouts, bind groups,
pipelines, binding and draws throw an explicit phase-3b exception.
`GetImGuiTextureID` throws “not implemented until phase 4”. Window/surface creation
and presentation remain phase 4; the editor still uses OpenGL.

## State and graph integration

Each buffer stores its last stage/access. Each texture stores stage/access/layout
for each `{face, mip}`. Depth and stencil share a subresource state and transition
both aspects together; separate-aspect layouts are not enabled. Host upload writes
flush VMA allocations and become host-write accesses. Device-local writes,
attachments, copies and blits infer their accesses automatically. Readback copies
emit host visibility barriers, and VMA invalidates noncoherent memory at Read.
Host visibility is an export; tracking preserves the actual GPU producer for the
next queue dependency.

Command lists record owning captures, rather than immediately baking native
barriers. `Submit` replays them in queue submission order, starting from each
resource's persistent state. Recording lists out of submission order therefore
cannot corrupt the layout history. `Execution::Use` plans transitions into buffer
and image barrier arrays; `FlushBarriers` emits one synchronization2 dependency
before the native operation. Multiple attachments or source/destination resources
already share that batch. State persists across lists, frames, standalone
submissions and transient-pool reuse. Barriers are conservative, including
read/write attachment accesses regardless of Load/Clear.

A future graph adapter can merge compiled `Resources().Accesses` sequences by
physical `Texture(id)`, expand whole-texture uses across mips/faces, translate
neutral graph accesses into this backend's `Access`, and feed `Use`/`FlushBarriers`
at pass boundaries. It must start from the live queue state, including imported
prefixes and direct RHI uploads. Command inference remains authoritative for
concrete subresources and for work outside the graph. No graph dependency is
needed for correctness, and no caller-issued barrier API was added.

D24S8 uses the neutral native-uint32 packing: depth in bits 31..8, stencil in
bits 7..0 (the existing OpenGL `UNSIGNED_INT_24_8` representation). Vulkan aspect
copies use separate depth and stencil staging allocations. A submitted prefix is
waited before CPU repacking; upload sources and download destinations can still
be device-local. These transfers deliberately favor correctness over throughput.
Their internal queue waits do not retire public readback tokens or command lists.

## Frames and ownership

There are `FramesInFlight` frame slots, each with its own command pool and reusable
fence, plus a standalone pool. Submit seals a list and retains it on the device;
its captures retain all referenced resources and internal staging allocations.
Readback pending-write tokens are acquired at recording, released on cancellation
or retirement, and counted independently for every recorded/submitted writer.
An older retirement cannot expose a buffer still targeted by a newer list.

`EndFrame` appends a fence signal to the ordered queue, waits, retires all submitted
lists (including preceding standalone work), drains deferred native destruction,
and advances the frame slot. It is synchronous, preserving existing EndFrame
conformance expectations and satisfying any positive frames-in-flight upper bound.
This foundation does not yet overlap GPU frames. `WaitIdle` retires submitted work
without ending an active frame. Unsubmitted lists remain untouched. Pools are
reset only after completion; fences are waited before reuse and reset before
signaling. Native statistics include deferred allocations. Release handles before
the device, as required by the existing contract.

## Coordinates and safety

Positive viewport heights are used. The query reports downward clip Y, depth
0..1 and upper-left framebuffer/copy-row origin. There is no negative-viewport
correction; the renderer owns convention conversion through the neutral query.
Scissors extending outside the framebuffer are intersected with the active
attachment bounds, preserving the neutral signed-offset semantics. Rasterized
convention conformance is deferred to the existing phase-3b cases.

The default factory accepts CPU physical devices only. Exact environment opt-in
`KANS_VULKAN_ALLOW_NON_CPU=1` permits other device types; no test invocation here
sets it. The selection predicate is checked before logical-device creation.
Tests additionally require both `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` to equal
`/usr/share/vulkan/icd.d/lvp_icd.json` before creating an instance. CTest supplies
both, labels Vulkan cases and sets `RUN_SERIAL`.
`VK_LOADER_LAYERS_DISABLE=~implicit~` isolates tests from installed third-party
implicit layers (the installed LSFG frame-generation layer reports loader errors);
the explicitly enabled Khronos layer remains active. Debug builds enable validation;
test devices also require synchronization validation. Every normal test-device
validation error calls gtest `ADD_FAILURE`, including teardown. A dedicated test
injects one synthetic debug message into a collecting sink to verify delivery.

## Legacy migration

The old layer enumeration, debug-utils setup, queue enumeration and logical-device
creation flow informed the new context, with explicit ownership, Vulkan 1.3 feature
checks, errors and CPU selection replacing global state, swapchain requirements
and hardware ranking. The VMA implementation TU under `Platform/Vulkan` is reused
unchanged. The old swapchain implementation is retained for phase 4 and now accepts
explicit instance/physical/device/graphics-family handles. Its surface-format,
present-mode, image-view and synchronization code is not exercised by phase 3a.
The existing shader compiler/cache/preprocessor and shader-layout metadata remain;
its legacy shader bridge now stores CPU bytecode and no longer creates unowned
native modules through a global device.

Deleted replacements:

- `Platform/Vulkan/VulkanRHI.{h,cpp}` and `VulkanDevice.{h,cpp}`.
- `Platform/Vulkan/VulkanImage.{h,cpp}` and `VulkanTexture.{h,cpp}`.
- `Renderer/RHI/Vulkan/RHI.h` and `Image.{h,cpp}`.

The empty old pipeline skeleton is retained without its deleted image include.
No public RHI signature changed; only D24S8 bit packing was clarified in a comment.
OpenGL already implements that packing, and the new neutral tests exercise both.

## Conformance and mutation targets

The existing Vulkan cases run unchanged: InternalShaderCreationKeepsTheCompilerDiagnostic,
StandaloneSubmissionRetainsReadbacksAndDoesNotStartAFrame, BufferUploadCopyReadbackRanges,
EveryMipAndCubeFaceRoundTrips, OddWidthSingleChannelRows,
ReleasedAllocationsSurviveUntilFrameRetires, RecordedAndSubmittedResourcesAreRetained,
MipGenerationFiltersLevelZero and OlderRetirementDoesNotExposePendingReadbackWrites.
All remaining existing cases explicitly `GTEST_SKIP() << "phase 3b"` for Vulkan;
they remain registered. OpenGL runs every existing case with unchanged assertions.

| New test | Concrete bug caught |
| --- | --- |
| ClearOnlyFormatsPreserveTypedValuesAndLoad | sRGB mapped to UNORM; full floats rounded to half; HDR half values clamped; integer clears converted to float; missing attachment/copy layout transition; Load clearing content |
| ClearOnlyDepthStencilMipReadback | Wrong mip attachment view/copy, missing depth layout transition, depth/stencil bits reversed |
| CubeMipAttachmentCopiesFollowSubmissionOrder | State planned in recording order; wrong mip/face copies; wrong mip/face attachment view; missing cross-submit transitions |
| PackedDepthStencilCopiesKeepMipBytesThroughDeviceLocalBuffers | Wrong mip or aspect transfer, missing transitions, native packing used for neutral packed words |
| RepeatedFrameSlotsAndStandaloneWorkRetireReleasedResources | Pool reset before completion, fence reuse/reset error, premature freeing of submitted memory, readback exposed early; 18 frames each at bounds 1/2/3 |
| OddWidthTwoChannelMipTransfers | Wrong mip extent/offset, RG mapped to R/RGBA, rejecting a legal two-byte-aligned offset, padded tight rows |
| VulkanSafety.CpuSelectionRejectsHardwareBeforeDeviceCreation | CPU selection gate removed or policy inverted; synthetic hardware types need no hardware enumeration |
| VulkanSafety.ValidationErrorsReachTheTestSink | Disconnected messenger lets missing layout transitions become silent test passes |

sRGB allows the existing one-byte conversion precision allowance; all other new
transfer/clear results are exact. GPU suites must run serially with the pinned
software drivers. Validation results are recorded in the parent RHI README.
