# Vulkan backend (phases 3a and 3b)

`VulkanDevice.h` provides backend-private factories and test construction policy.
`VulkanContext.cpp` owns a headless Vulkan 1.3 instance, debug messenger, physical
and logical device, one graphics queue and VMA allocator. `VulkanDevice.cpp`
implements resources, CPU command recording, submission, state tracking and
retirement. `VulkanInternal.h` holds native details; `VulkanTestProbes.h` exposes
only test diagnostics. Native types never enter the neutral RHI headers.

All buffer usages and memory intents, all nine formats, 2D/cube allocations,
mips, independent samplers, full-subresource copies, mip filtering and clear-only
dynamic rendering are supported. Graphics shader modules, reflection-driven layouts, bind groups, pipelines,
binding and draws are implemented; see the phase-3b design and validation report below.
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
convention conformance runs on both backends with unchanged assertions.

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
Phase 3b removes all 31 drawing/pipeline skips. All 46 original cases run on
both backends with unchanged assertions; seven additional graphics cases are
listed in the phase-3b test audit.

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

## Phase 3b graphics and renderer

Graphics consume the original asset/test/internal-shader SPIR-V words, including
KSHD v6 pass products. SPIRV-Tools validates each vertex/fragment module;
spirv-cross is used for **reflection only**, never Vulkan source generation or
cross-compilation. Entry points, buffer sizes, image dimensions, binding kinds
and push-block byte layouts are checked. Asset binding metadata is checked
against the bytecode. `GpuShader` builds its neutral engine/material layouts from
product reflection; pipeline creation checks those layouts against the merged
module bindings. Sparse `(group, slot)` addresses remain native set/binding
addresses. Separate images/samplers and combined samplers are supported.

`VulkanGraphics.inl` contains backend-private modules, layouts, groups and
pipelines. Each pipeline owns a native pipeline layout, with empty layouts for
missing lower set indices, and a native push range shared by graphics stages.
Push bytes retain the original SPIR-V layout, including scalar arrays, nested
structs, row/column-major matrices and mixed integer/float data. Recorded draws
own snapshots of the push bytes and bound resources.

Dynamic-rendering pipelines map every `PipelineDesc` field: target/depth/stencil
formats, topology, vertex formats/offsets/strides/divisors, culling/winding,
wireframe, constant/slope depth bias, depth test/write/compare, both stencil faces'
operations/reference/masks, and independent colour/alpha blend equations/factors
and per-target write masks. `RHI/PipelineKey.h` serializes fields without padding;
both backends use its hash and full equality key, with weak resource entries.
Vulkan additionally owns an in-memory `VkPipelineCache`. Scene and sprite owners
retain their warmed internal pipelines, so frame retirement does not expire them.
Material and extra-pass owners already retain their pipelines independently.
There is **no per-draw pipeline creation after warm-up**.

Lavapipe supports `VK_EXT_line_rasterization` with Bresenham lines. Enabling it
makes the wireframe reference pass its unchanged 1-byte/zero-outlier bound.
The optional feature is backend-private; other permitted devices can fall back
to default native lines. It implements the diamond-exit rule described in the
[locally installed Vulkan specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#primsrast-lines-bresenham),
read at `/usr/share/doc/vulkan/vkspec.html` without network access.

### Per-draw group 0 and measured allocation strategy

The public phase-1c-1 decision is preserved: each draw owns an immutable neutral
bind group with explicit buffer ranges. No dynamic-offset method, native type or
native binding term was added to `RHI.h`; GL continues binding those ranges as
before. Vulkan implements group-0 buffer bindings using dynamic descriptors.
A weak native-set cache keys the layout, buffers, ranges/base offsets, image
views and samplers, while per-draw offsets are retained by the group and supplied
at draw replay. Thus different objects sharing the frame/object arenas share a
native set without sharing their range selections. Very large offsets retain an
aligned descriptor base and a representable uint32 dynamic offset. Group 1 keeps
ordinary descriptors and the renderer's existing material-group cache.

Sets come from reusable device-owned slabs: 1024 sets and 4096 descriptors of each
supported descriptor kind per slab. Slabs grow by capacity, never one per draw;
there is **no per-draw descriptor-pool creation**. Set resources remain owned by
recorded/submitted groups; deferred frees occur only after retirement. Weak set
entries are pruned at retirement. The strategy also supports standalone picker
and preview work, and long-lived immutable material groups.

Measured on this machine, Debug lavapipe with Khronos and synchronization
validation enabled, after 512 warm-up material draws and over 1536 additional
draws (three complete frames):

| Implementation | Mean group creation | Native allocations over 1536 draws | Pools | New pipelines after warm-up |
| --- | ---: | ---: | ---: | ---: |
| Ordinary pooled set per draw | 12,023 ns | 1536 | 1 | 0 |
| Shared group-0 set + internal offsets | 5,324 ns (prior repeats 5,118 / 5,163 ns) | 3 | 1 | 0 |

This removes about 56% of measured group creation time and 511/512 native set
allocations/updates, while preserving the DX-portable buffer-view API. Remaining
CPU group construction is about 2.7 ms for this artificial 512-draw Debug frame;
this is a validation-enabled construction measurement, not a release throughput
claim. The test asserts set counts, constant pool count, no new pipelines and
rendered pixels. No elapsed-time threshold makes correctness depend on load.

### Scope preparation and synchronization

Recorded scopes gather actual shader-binding and vertex/index uses before native
`vkCmdBeginRendering`; barriers are batched outside dynamic rendering. Sampled
views cover all mips/faces, while attachment views select one subresource. Draws
validate layouts, block ranges, dimensions and render-target compatibility, then
bind pipelines/sets, native push bytes, vertex/index views and issue the draw.
Viewport/scissor defaults cover the attachment and explicit values are replayed.

A storage-write dependency between draws splits the native scope, preserves
attachments, emits barriers, and resumes with Load. Switching attached depth
between writing and sampling does the same, including separating an initial
Clear from a sampled read-only draw. Viewport/scissor and public scope boundaries
are preserved. Read-only depth uses a depth-only sampled view and combined
read-only depth/stencil layout. Its attachment Store is still conservatively
tracked as a write: omitting it was caught by synchronization validation in the
new test. Sampling a colour attachment, or sampling and writing attached depth
in one draw, is rejected; framebuffer feedback extensions are outside this API.
Read-only accesses accumulate their stages/accesses; producers, layout changes,
storage writes, copies and attachment writes retain required dependencies.

### Renderer conventions and editor boundary

* Projections, picking, ordinary scene rows and capture/lookup-map projections
  continue using `GetCoordinateConventions`, `DeviceProjection`, `DeviceRow` and
  `DeviceTextureProjection`. Vulkan reports downward clip Y, depth 0..1 and
  upper-left framebuffer/copy origin, with positive viewport heights.
* Material/internal pipelines previously flipped winding **again** after
  `DeviceProjection` had converted Y. They now retain visual CCW facing; the
  original low-level culling conformance assertions already passed and were
  unchanged. Double-sided normals and inverted hulls now work on both backends.
* Imported images use the authored UV convention (v=0 at the image bottom),
  independently of framebuffer origin. Applying framebuffer origin to asset
  upload rows had mirrored Vulkan materials and HDR sampling. GL upload bytes
  remain identical. Captured cube/BRDF rows keep their deliberate texture
  projection conversion; those numerical tests needed no new row flip.
* The derivative normal fallback obtains +V from world-space derivatives. A
  screen-space UV determinant alone changed sign on an upper-left framebuffer;
  the existing exact normal-map test caught it. The fix is coordinate-invariant
  algebra, with no backend branch or new uniform layout.
* Default mesh/texture uploads now use the initialized shared device on either
  implemented backend. `InitializeHeadlessDevice` permits the test context to
  install a device with its CPU/validation policy; both backend instances test
  identity, coordinates and rejection of replacement/null devices.
* Preview `RenderImage` returns a neutral image/complete result. Existing editor
  `Render` calls wrap it with the OpenGL ImGui export; thumbnail rendering uses
  the image directly. The editor, windows, ImGui integration and presentation
  remain on OpenGL. Vulkan UI IDs still throw the explicit phase-4 exception.

### Unchanged references and measured Vulkan tolerances

All twelve PNG references and all OpenGL tolerances remain unchanged. Both
fixtures use neutral texture readback and normalize rows with the coordinate
query. Vulkan's filtered sampling and SPIR-V floating-point evaluation are not
specified to be bit-identical to the GL translation/compiler path. The Vulkan
specification defines retained fractional texel-weight bits
([texel coordinates](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#textures-unnormalized-to-integer))
and bounded, rather than universally identical, floating-point operations
([SPIR-V precision](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#spirvenv-precision-operation)).
The differing pixels are at high-gradient textured sphere locations; after
fixing orientation/facing, coverage and the other seven scenes pass the original
bounds. These five overrides use the exact measured maximum and **zero outliers**:

| Reference scene | GL max channel delta | Vulkan max channel delta | Pixels outside the original GL bound / 16384 |
| --- | ---: | ---: | ---: |
| standard-ibl-point | 2 | 3 | 6 |
| toon-imported | 1 | 5 | 4 |
| debug-base-color | 1 | 7 | 13 |
| debug-normals | 1 | 6 | 13 |
| preview-sphere | 2 | 12 | 5 |

Alpha/depth sorting, emission, selection outline, grid, skybox, sprite and
wireframe keep their original tolerance on Vulkan. In particular the 395-pixel
default-line mismatch was fixed with Bresenham lines, not tolerated away. Failed
comparisons save actual/diff PNGs under backend-specific `reference-output/`
subdirectories; no reference-update environment option was set.

`FullRuntimeFrameIsByteIdenticalToPhaseOne` retains the original three exact GL
hashes. After coordinate-based normalization into the oracle's lower-left row
order, Vulkan has exact per-backend hashes for colour-0 and packed depth/stencil;
colour-1 keeps the identical GL hash. Comparing emitted `runtime-gl-*.bin` and
`runtime-vk-*.bin` shows:

* Colour-0: 655 different pixels: 653 differ by at most one byte, one by two,
  and one debug-normal Bresenham boundary pixel at (63,69) differs by 173.
  Both APIs permit a one-fragment departure at a diamond-exit boundary. All
  alpha bytes are identical. Colour-1 is entirely byte-identical.
* D24: 2538 pixels differ, by -4..+3 integer depth units (at most 4/16777215).
  Converting a [-1,1] projection to [0,1] before vertex evaluation changes
  floating-point rounding versus GL's viewport depth conversion. Every stencil
  byte remains identical. The exact Vulkan oracle detects any later byte change;
  no fuzzy comparison replaces it.

The complete before/after test list and mutation targets are in
[`test/README.md`](../../../../../test/README.md#phase-3b-test-change-audit).

### Phase 4 gaps

Surface/swapchain creation, acquire/present synchronization, editor backend
selection and the ImGui Vulkan renderer/export remain phase 4. Frames still
retire synchronously; overlap needs upload-arena/frame-slot review. Pipeline-cache
persistence remains optional later work. Compute, multisampling, binding/texture
arrays, storage images and framebuffer feedback extensions remain the previously
stated outside-scope RHI features, not conformance skips.

### Final validation

`linux-gcc` Debug, configured with `cmake --preset linux-gcc` after adding files;
all builds use `-j2`. All 21 editor source translation units also pass fresh
`-fsyntax-only` checks using their existing Debug flags, two at a time (26
compile commands including test-target variants), without linking or touching
editor output directories. CPU: **404 passed / 0 skipped** (49.51 s). Serial llvmpipe
OpenGL: **156 passed / 0 skipped** (166.32 s), including the complete 4.3
extension bundle. Serial lavapipe Vulkan: **131 passed / 0 skipped** (102.08 s),
including all **53/53 conformance cases**, both safety cases, every renderer
parameter and all twelve references. After the final precompute-program cache
reuse correction, the affected 19 environment/reference cases pass again on
each backend (Vulkan 46.05 s, OpenGL 45.76 s). Khronos validation and synchronization
validation report **zero errors**.

Changed-line `git clang-format` includes the new private `.inl` and shared key
header, using temporary Git metadata under `/tmp`; `git diff --check` is clean.
The CMake cache/presets/configuration are unchanged. Ninja dependency metadata
was recompacted to repair a pre-existing truncated log. Editor outputs, sources,
resources/assets, user files and reference PNGs remain untouched. No editor
launch, hardware GPU, network, commits or pushes.
