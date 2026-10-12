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

### Gaps at the phase 3 boundary

Surface/swapchain creation, acquire/present synchronization, editor selection,
ImGui and window frame overlap are implemented in phase 4 below. Pipeline-cache
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

## Phase 4: windowed application and ImGui

`Application` resolves `RenderBackend=OpenGL|Vulkan` in its existing configuration
before creating the window. Missing settings default to **OpenGL**. The engine
entry point supplies `--render-backend=opengl|vulkan`; this overrides a valid ini
setting. Invalid/empty settings and duplicate CLI selections throw a diagnostic.
The filesystem's configuration rewrite preserves `RenderBackend`. Editor sources
and their settings UI need no changes.

Selecting Vulkan in the application is the explicit hardware opt-in. Tests and
default headless construction still require a CPU device. `KANS_VULKAN_REQUIRE_CPU=1`
overrides **every** construction policy before physical-device selection. Pin both
loader variables to lavapipe for smoke runs; the CPU gate alone does not select
between multiple installed software drivers. Debug application builds enable
Khronos and synchronization validation. Tests use an error sink that fails gtest.

`VulkanContext` requests GLFW's instance extensions and creates the surface before
choosing a graphics queue with presentation support. GLFW owns platform details
(X11/Wayland/Windows). Vulkan windows use `GLFW_NO_API`. The test-only private
`HeadlessSurface` option requests `VK_EXT_headless_surface` instead of a native
window and runs the same swapchain code. No editor is launched by tests.

`VulkanPresentation.cpp` implements both main and detached-window swapchains.
It retains the useful old swapchain choices (FIFO for VSync, mailbox then immediate
for VSync off, capabilities-based image count/current transform) and replaces the
unfinished retained implementation's ownership and synchronization. Extents are
clamped; all image views, framebuffers and per-image render-complete semaphores
are destroyed before the old swapchain. Recreation waits idle, preserving render
pass compatibility. Zero extent defers acquisition/recreation/capture. Acquisition
and presentation handle out-of-date/suboptimal results by scheduling recreation.
VSync changes also schedule recreation and propagate to detached windows.

A frame slot owns an acquire semaphore, command pool/buffer and fence. Rendering
waits for the acquired image, then signals its **per-image** presentation semaphore.
Reacquiring that image guarantees the previous presentation consumed that
semaphore. A slot fence alone does not guarantee presentation semaphore reuse is
safe; this is why completion semaphores are not per-slot. Presentation waits on
the corresponding completion semaphore.

Window RHI `EndFrame` no longer waits. `Application` calls `BeginWindowFrame`
before layer updates: it waits only for the reused slot, retires that slot's
command/resource/ImGui-image ownership and resets its command pool. Main-window
presentation submits a tail fence covering preceding scene, UI and platform work,
then advances the ring (`DeviceDesc::FramesInFlight`, default 2). Standalone window
commands use the current slot too, so preview/picking work cannot be reset while
pending. `WaitIdle` still retires everything. Headless RHI `EndFrame` retains its
synchronous readback contract. Native deletion queues drain after safe ownership
retirement. Scene uniforms and sprite vertices are fresh allocations per frame;
caller-owned upload buffers must still obey the existing no-overwrite contract.
Readback/picking, thumbnail generation, resize and capture may deliberately wait.

The pinned ImGui backend has no dynamic-rendering initialization API. A conventional
render pass is used **only for UI**; scene rendering remains dynamic. GLFW's Vulkan
platform integration supplies input/window callbacks. Engine renderer callbacks
own detached swapchains and use the pinned backend's per-viewport vertex/index
buffers. Fonts upload once on renderer creation. A CMake-generated Vulkan adapter removes
immutable font samplers from its descriptor layouts: otherwise user texture samplers
are ignored and RHI descriptor layouts are incompatible. The vendored submodule and OpenGL backend source
are unchanged; configuration checks that both adapter patch sites still exist.

ImGui texture IDs are native descriptor sets cached by texture and sampler description
in device-owned pools. Their layouts match ImGui's combined sampler binding. Texture
ownership includes the sampled view, sets and samplers; native frees are deferred.
Exports retain images through intervening `WaitIdle`, scene retirement and platform
draws. Main presentation transfers exports into the frame slot until its GPU fence
completes. Draw-data preparation also retains persistent IDs, uses the RHI access
tracker for every sampled mip, and inserts upload/attachment-to-sampling dependencies
before the render pass. There are no UI-issued texture barriers.

The editor's unchanged viewport and preview use OpenGL-style `(0,1)->(1,0)` UVs.
The Vulkan renderer adjusts **render-target exports** while copying ImGui vertices
and restores the draw lists immediately afterwards. Authored/uploaded icons retain
their UVs. RHI images, renderer coordinates, numerical readbacks and references do
not change. Offscreen ImGui construction (`CreateImGuiRenderer(Vulkan, nullptr)`)
uses the same shader/font/descriptor path for display-independent tests.

Surface selection explicitly requires BGRA8 or RGBA8 **UNORM** with
`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`. This describes how already encoded UI bytes
are presented; an SRGB attachment would encode those bytes again. The GL UI boundary
disables `GL_FRAMEBUFFER_SRGB`, and this path matches that behaviour. An unsupported
surface fails explicitly instead of silently changing UI colours. Solid colours
are checked byte-for-byte; authored SRGB textures still decode during sampling.

`RequestFrameCapture(window, path)` in `RHI/Backend.h` writes a binary PPM (P6 RGB8,
top-left rows) after the next successful main-window present. It works when requested
before or after UI rendering. Minimized/out-of-date frames keep the request pending.
GL reads the main default back buffer before swapping and restores context/readback
state. Vulkan copies the **acquired/rendered** swapchain image, with layout and host
visibility dependencies, before presenting it. Capture adds blocking readback and
file IO only when requested; ordinary frames allocate no capture objects.
Application CLI `--frame-capture=/tmp/editor.ppm --frame-capture-after=60` requests
capture after 60 non-minimized application frames (60 is the default). The path is
resolved relative to the startup working directory. Capture errors are explicit.
Only the main window is captured; detached windows have independent presentations.

Run a freshly linked editor **from your temporary project**, with its copied ini,
resources and mono layout:

```sh
cd /path/to/temporary/project
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
KANS_VULKAN_REQUIRE_CPU=1 VK_LOADER_LAYERS_DISABLE='~implicit~' \
/path/to/fresh/KansEditor --render-backend=vulkan \
  --frame-capture=/tmp/editor-vulkan.ppm --frame-capture-after=60

LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=llvmpipe GALLIUM_DRIVER=llvmpipe \
/path/to/fresh/KansEditor --render-backend=opengl \
  --frame-capture=/tmp/editor-opengl.ppm --frame-capture-after=60
```

A fresh Debug executable is linked at `/tmp/kans-phase4-editor/KansEditor` from all
21 unchanged editor translation units, compiled two at a time. Substitute that
path for `/path/to/fresh/KansEditor` above. It has not been launched. No resource
copy/post-build step or protected editor-output write is performed. Window icon,
events, input, resize and time already use GLFW and are backend-neutral. No active
editor engine path still requires native GL; retained `Platform/OpenGL` classes
remain legacy implementations. No Windows or compositor smoke run is claimed.


### Phase 4 final verification

Fresh `linux-gcc` Debug builds use `-j2`; CMake presets/cache settings are unchanged.
Final serial runs: CPU **408 passed / 0 skipped** (48.01 s); OpenGL **156 passed /
5 skipped** among 161 registered cases (164.52 s); Vulkan **142 passed / 0 skipped**
(103.30 s). The five GL WSI cases explicitly skip without an accessible display.
Vulkan WSI, resize, capture, minimized restoration and detached renderer callbacks
run on lavapipe `VK_EXT_headless_surface`. Offscreen UI additionally runs with
ordinary Vulkan image tracking. No test has both GPU labels. All twelve references
and existing assertions/scene bounds are unchanged, with zero unexpected Khronos or
synchronization validation errors.

The direct UI parity test compares every RGBA8 byte for opaque colours, alpha
blending and font coverage between real EGL/llvmpipe GL and offscreen lavapipe
Vulkan: **byte-identical**. This is synthetic renderer parity, not a claim that
an unlaunched editor screenshot or compositor/DPI configuration was tested.
The headless 24-frame retirement microbenchmark reports mean EndFrame times of
**0.169 / 0.199 / 0.218 microseconds** at slot counts **1 / 2 / 3**; slot wait,
retirement and pool reset average **107.5 / 78.5 / 76.3 microseconds**. These are
Debug/validation timings for 256x256 clears and released allocations, not editor
FPS. They substantiate moving completion out of EndFrame and bounding retained
work by the ring; two slots remain the default memory/latency choice.

All 21 editor translation units compile and link into `/tmp/kans-phase4-editor/KansEditor`
using their existing Debug command lines, two compilers at a time. The protected
editor binary/output, editor source/assets/resources, user files and reference
images are untouched. Changed-line `git clang-format` uses temporary Git metadata
under `/tmp`; `git diff --check` passes. No editor launch, real GPU, network,
commits or pushes.
