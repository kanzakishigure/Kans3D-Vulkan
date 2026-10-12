# Render hardware interface

The core API is `RHI.h`, in `Kans::RenderHardware`. All engine rendering and
resources use it; native OpenGL is confined to `OpenGL/`.
See [MIGRATION.md](MIGRATION.md) for retirement and editor compatibility details.

`Backend.h` is the neutral application entry point: `SetBackend`,
`CreateWindowSurface`, `InitializeDevice(backend, window)`, `GetSharedDevice` and
`ShutdownDevice`. Initialization is explicit and idempotent for the same window;
all rendering consumers share its device. Backend window hints, context binding,
entry-point loading, presentation and VSync live in `OpenGL/`. Calls use the
context's owning thread. Headless conformance fixtures supply their own context.
The headless Vulkan foundation lives beside OpenGL in `Vulkan/`; see
[Vulkan foundation](Vulkan/README.md) for implementation and migration details.

`ImGuiRenderer.h` provides `CreateImGuiRenderer(backend, window)` with `NewFrame`,
`Render`, `RenderPlatformWindows` and `EndFrame`. Its OpenGL implementation owns
the current ImGui GLFW/OpenGL3 integration; ImGuiLayer contains no native GL.

## Public types

| Type | Responsibility |
| --- | --- |
| `Device`, `DeviceDesc`, `DeviceStats` | Create resources and command lists, cache pipelines, submit/retire frames, wait for GPU completion, query coordinates, buffer-binding alignment and ImGui texture IDs. Native allocation statistics include deferred allocations. |
| `Handle<T>`, `Resource` | Shared ownership; recorded and submitted commands retain their resources. Objects belong to one device. |
| `Buffer`, `BufferDesc` | Byte allocation with vertex/index/uniform/storage/copy usages and device-local/upload/readback memory intent. `Write` uploads host bytes; `Read` returns an owned copy of mapped readback bytes. |
| `Texture`, `TextureDesc`, `TextureSubresource` | 2D or cube allocation, format/usage/extent/mip count. Copies select a complete mip and face explicitly. |
| `Sampler`, `SamplerDesc` | Independent minification, magnification, mip filtering, addressing and LOD clamps. |
| `Shader` | Graphics SPIR-V modules and asset reflection supplied through `ShaderData`; exposes its required push-data size. |
| `BindGroupLayout`, `BindGroupLayoutDesc`, `BindGroupLayoutEntry` | Neutral group/slot/type contract. Uniform, storage, image, sampler and combined image/sampler bindings. |
| `BindGroup`, `BindGroupDesc`, `BindGroupEntry` | Immutable concrete bindings, including buffer ranges. Retain bound resources. |
| `Pipeline`, `PipelineDesc` | Immutable shader, layouts, vertex inputs, topology, rasterizer, depth/stencil, per-target blending/write masks and target formats. Weak, hash-indexed, collision-checked cache. |
| `VertexBinding`, `VertexAttribute`, `VertexBufferView`, `IndexType` | Input stride/divisor, attribute location/offset/type, buffer view, and index width. |
| `RasterizerState`, `DepthStencilState`, `StencilFaceState`, `BlendState` | Backend-neutral graphics state, including wireframe, bias, front/back stencil operations and separate color/alpha blend functions. |
| `CommandList` | One-shot recorded rendering, binding, drawing, copy and mip-generation operations; submission seals the list. Records own snapshots of push bytes. |
| `RenderingDesc`, `ColorAttachment`, `DepthAttachment` | Dynamic rendering scope, subresources, load/store actions, typed clears. |
| `Viewport`, `Scissor` | Target rectangle, depth bounds and clipping rectangle in the device's framebuffer coordinates. |
| `CoordinateConventions` | Clip-space Y direction, depth range, and framebuffer/copy-row origin. |

## Binding and shader translation

The initial backend supports groups 0 and 1 with arbitrary uint32 logical slots.
Shader reflection maps each logical group/slot to dense native uniform, storage
and texture points independently. This preserves published products with sparse
material bindings. Frame/Object reserve uniform points 0/1; material uniform
blocks start at 2. Engine IBL reserves texture units 0/1/2; material samplers
start at 3. The push emulation point 16 is excluded from uniform allocation.
Separate images/samplers keep their logical keys while pairs get free texture
units. Native stage/resource limits are enforced by shader compilation/linking. Frame data uses group 0; material data uses
group 1. Layout and group entries are canonicalized by slot. The backend checks
layouts against reflection and bound ranges against shader block sizes. `GetBufferBindingAlignment` exposes the uniform/storage
view offset alignment so callers can construct portable buffer ranges.

Shaders follow the existing shaderc/SPIR-V/spirv-cross path. Module validation
and reflection come from bytecode; supplied asset reflection is checked against
it. The backend does not call the material-only reflection validator, which
currently excludes push-data and storage resources. Only vertex and fragment
stages are accepted. Separate images and samplers are paired by spirv-cross;
each pair receives the lowest unused internal texture unit,
while its logical group/slot addresses stay independent. Unit limits are checked.

Push constants are one graphics range, at most 128 bytes, shared by both stages.
The backend translates the block into a uniform buffer at point 16 and repacks
from reflected source offsets to uniform-compatible offsets/strides. This
supports 32-bit scalars/vectors, matrices, structs and fixed one-dimensional
arrays, including mixed float/integer data. Stage declarations must agree about
the byte layout. Each draw orphans/uploads the emulation buffer so later push
updates do not overwrite earlier draws. Callers always supply the source
module's byte layout, with writes aligned to four bytes.

Arrays of bindings, texture arrays, multisampling, storage images, comparison sampling and compute
commands are outside this initial implementation. Core graphics features all
have equivalents in the planned DirectX backends.

## Frames, destruction and readback

`FramesInFlight` defaults to 2 and specifies the allowed upper bound. Both
current backends complete each frame synchronously, which is within any positive
bound; future implementations may keep that many frames pending. `BeginFrame`/`EndFrame` must
pair. Submission is allowed inside or outside a frame, always outside a rendering
scope. Standalone submissions retain resources and keep readbacks inaccessible
until `WaitIdle` or a later `EndFrame`; they do not implicitly start/end a frame.
`WaitIdle` can retire submitted work inside a frame without ending it.
Command replay establishes deterministic native state without per-submit
save/restore. ImGui sets its own draw state; its renderer boundary additionally
handles framebuffer bindings, colour masks, sRGB, raster discard, logic operations
and clip distances. Transfer-local guards preserve pixel-store/PBO bindings.
Native bindings alone confer no resource ownership; recorded commands and active
UI texture exports retain their resources.

A recorded list owns every referenced resource. Submission transfers retention
to the device until retirement, even when the caller releases the list. Safe
retirement releases those references and drains the native deletion queue only
after completion. Releasing an otherwise unused native resource during a frame
also queues its deletion. Unsubmitted lists continue owning their resources and
are not consumed by `WaitIdle`. Release all resource handles before destroying
the device, and destroy the device before its context.

Upload buffers can be written directly, but callers must not overwrite ranges
still used by recorded or in-flight work. Copy results are inaccessible through `Read` until
frame retirement or `WaitIdle`. Texture readback copies to a buffer, then maps
that buffer; no texture mapping or renderer-issued barrier exists. Copies are
full mip/face, tightly packed, with offsets aligned to the format's bytes per
pixel. Backends may internally repack native transfer pitches. RGBA16Float transfers IEEE binary16, RGBA32Float transfers native IEEE binary32 (16 bytes/pixel), RG8Unorm transfers two normalized bytes, R32Uint
transfers uint32, Depth32Float transfers float, and Depth24Stencil8 transfers
packed depth/stencil. Storage writes trigger automatic synchronization before
later binding use; transfer/readback paths insert the appropriate memory barriers.
An explicit backend can infer accesses from these same commands and track each
texture subresource independently.

`GetImGuiTextureID(texture, sampling)` returns a draw ID as `uintptr_t`.
Optional neutral sampling configures the ImGui view, while RHI draws use
independent samplers. Texture2D wraps an RHI image and supplies this description
through `Texture::GetImGuiTextureID()`. Exports during
`ImGuiRenderer::NewFrame` through `EndFrame` retain images across intervening
`WaitIdle`/scene frame retirement and all platform-window draws. After UI completion,
normal device retirement can destroy a released image.

## Coordinates

`GetCoordinateConventions()` returns `ClipY`, `Depth`, and `Origin`. The current
backend reports upward clip Y, [-1, 1] clip depth, and a lower-left framebuffer
origin. Viewports, scissors and texture-copy row zero use `Origin`. A later
renderer migration must derive projection and picking conversions from this
query rather than spreading backend conditionals across callers. The conformance
fixture likewise centralizes native depth conversion and clip-to-copy row
conversion, and derives facing expectations from the queried clip Y direction.

## Conformance tests and concrete regressions

`test/RHIConformanceTests.cpp` contains the parameterized suite. Only
`GpuTestContext::CreateDevice` knows backend construction. GPU tests are
labelled `OpenGL` or `Vulkan`, run serially, and use llvmpipe or lavapipe.
Vulkan enables validation and synchronization validation, with errors failing
gtest; drawing cases explicitly skip with “phase 3b”. Shader compilation uses
`MaterialShaderTestUtilities.h`, as the material tests do.

| Test (prefix `RHIConformance.`) | Regression caught |
| --- | --- |
| StandaloneSubmissionRetainsReadbacksAndDoesNotStartAFrame | Submission wrongly requires BeginFrame, implicitly exposes pending readbacks, or leaves an implicit frame active. Exact copied uint32 values. |
| PickerUsesBottomLeftRowsAndClearsResizedTargets | Editor Y inversion, stale ID/depth after resize, truncated uint32 IDs, incorrect unprojection, or picker ending the caller frame. World coordinates allow 1e-6 for float raster/unprojection rounding. |
| OutlinePreservesSceneAlphaAndDoesNotKeepOldSelectionOnResize | Wrong selection coverage, alpha overwritten by fill/border, unrelated scene attachments cleared, or stale selection mask after resize. Exact RGBA8 bytes. |
| BufferUploadCopyReadbackRanges | Dropped upload patches, wrong copy offsets/lengths, or readback exposed before completion. |
| EveryMipAndCubeFaceRoundTrips | Aliased/swapped mips or cube faces, wrong copy extents/offsets or row reversal. All uploads precede all readbacks. |
| OddWidthSingleChannelRows | Incorrect tight-row alignment for R8. |
| TriangleCoverageAndExactColor | Missing draw, wrong viewport/topology or shader output. Checks covered and uncovered pixels. |
| DepthTestWritesAndReadback | Ignored depth test, unintended depth writes, or wrong stored depth values. |
| DepthCompareAndBias | LessEqual treated as Less, or missing/reversed depth bias. Separate regions check both. |
| SeparateColorAlphaBlendAndWriteMask | Wrong RGB/alpha factors or ignored per-channel mask. |
| BlendEquationsAndDisableTransition | Reverse subtraction mapped incorrectly or blending left enabled. |
| CullFrontBackAndWinding | Inverted winding/front-face mapping, ignored cull choice or stale cull enable state. |
| WireframeAndFillTransition | Filled wireframe interior, absent edges or stale line mode. |
| ScissorAndViewportBounds | Wrong rectangle origin, dimensions, intersection or exclusive upper bounds. |
| PushConstantsPartialWritesAndRecordedValues | Lost untouched bytes, pointer capture instead of a snapshot, or later constants overwriting earlier draws. |
| TwoGroupsWithCollidingSlotsAndCombinedSamplers | Group 0/1 slot collisions, ignored uniform-buffer range offsets, wrong texture unit, or ignored nearest filtering. |
| SeparateTextureAndSamplerBindings | Sampler bound at the image's logical slot instead of its own, wrong pairing, filtering or repeat addressing. |
| IntegerPickerTargetAndClear | Signed/float clear or transfer path truncating integer IDs above 2^24. |
| FullFloatTargetPreservesValuesBelowHalfPrecision | Half precision substituted for full float, normalization/clamping, wrong transfer width or component order; exact binary float comparisons. |
| OddWidthTwoChannelMipsAndSampledComponents | RG collapsed to R/RGBA, row padding or wrong mip extents; independent shader sampling checks components so matching transfer bugs cannot cancel. |
| SparsePublishedSlotsAndRecordedPerDrawUniformRanges | Slots over 65535 rejected/truncated, group collisions, range offsets dropped, later groups replacing earlier recorded draws, or missing command ownership; exact RGBA8 pixels. |
| HalfFloatTargetPreservesHDRAndNegative | Normalized allocation, clamping, wrong half transfer type or channel stride. |
| PipelineCacheIncludesEveryStateField | Omitted raster/depth/stencil/blend/topology/push-range/format fields, or failure to reuse equal descriptions. |
| PipelineCacheIncludesVertexAndBindingLayouts | Omitted vertex stride/divisor/location/offset/type, shader identity or logical binding layout; handle identity used instead of equivalent layout content. |
| ReleasedAllocationsSurviveUntilFrameRetires | Immediate native deletion despite pending frame retirement, or a deletion-queue leak. |
| RecordedAndSubmittedResourcesAreRetained | Borrowed command-resource pointers, device failing to retain a released submitted list, or retention leaking after completion. |
| CoordinateQueryMatchesCoverageDepthAndRows | Reported Y/origin/depth conventions disagreeing with actual rasterization or copy/scissor coordinates. |
| IndexedVertexLayoutOffsetsAndBaseVertex | Wrong buffer view/attribute/index offsets, stride, normalized color format, index width, first index or base vertex. |
| AttachmentLoadAndClearIgnorePreviousMasks | Clear respecting a previous scissor/write mask or Load unexpectedly clearing prior content. |
| IndependentBlendStatesForMultipleTargets | Applying target 0's blend/mask to target 1 or wrong fragment-output attachment routing. |
| StencilReplaceCompareAndWriteMask | Ignored stencil replace/compare or write mask. |
| SrgbTargetEncodingAndLinearTransition | Wrong sRGB encoding or sRGB conversion staying enabled for linear targets. |
| MipGenerationFiltersLevelZero | Missing generation, wrong mip or incorrect reduction pixels. |
| StorageWritesFeedUniformsAndReadbackWithoutCallerBarriers | Wrong storage binding/write visibility when reused as uniform data or copied for readback. |
| PushConstantsRepackScalarArraysAndMixedTypes | Source push strides confused with uniform strides or naive float-only flattening. |
| PushConstantsRepackNestedStructArraysAndMatrices | Incorrect struct-array/member/matrix strides and offsets when repacking nested push data. |
| PipelineMasksSurviveRenderingScopeClears | Clear operations silently overriding an already bound pipeline's color/depth masks in the next rendering scope. |
| OlderRetirementDoesNotExposePendingReadbackWrites | An older retirement incorrectly permitting reads while newer copies are still pending, or canceled lists leaving readback permanently blocked. |
| PushConstantsRepackRowAndColumnMajorMatrixArrays | Wrong array/matrix strides or lost row/column-major matrix order in code generation. |

Pixel and transfer assertions are exact, except sRGB conversion, which allows
one encoded byte of error for implementation conversion precision. Depth
expectations use gtest's float-ULP comparison for binary-exact test depths.

`OpenGLRHILifetimeTests.cpp` adds a native texture/ImGui-ID probe: the texture
must actually exist after release during the frame, then cease to exist after
retirement. This independently detects early deletion even if allocation
counters incorrectly still claim that the texture is alive.

Engine-owned shaders use [InternalShader.h](../Renderer/Resource/InternalShader.h):
`GetInternalShaderData(name, vertex, fragment)` caches CPU products and failures by
all three arguments for the process; `CreateInternalShader(device, ...)` uploads
a product to an RHI shader or returns `AssetError`. It reuses the asset importer's
shaderc compilation, SPIR-V validation, product round trip and reflection. Inline
450 sources beside selection/preview rendering require no working-directory
lookup. Explicit uniform blocks use the same neutral group/slot contract as
material shaders; no material-only convention validation is imposed on internal
programs. Sources must use the categories supported by asset reflection (uniform
blocks/combined samplers), which currently excludes push-data and storage.
`test/InternalShaderTests.cpp` checks descriptor reflection/cache reuse, diagnostic
propagation and source-sensitive failure caching, and projection/row conversions
for downward-Y/zero-to-one/upper-left conventions.

`test/EditorTextureOpenGLTests.cpp` drives the real ImGui renderer on llvmpipe:

| Test (prefix `EditorTextureGL.`) | Concrete regression caught |
| --- | --- |
| ConsumersReuseTheInitializedDeviceAndItsResources | Duplicate device creation by repeated initialization or a lazy resource consumer; the shared device also copies an uploaded wrapper image and checks exact bytes. |
| ImGuiSamplesLinearAndSrgbProductsThroughTheExportedId | Lost colour space, mismatched/truncated ID or incomplete native view; real sampled pixels compare linear bytes exactly and IEC sRGB decoding within one byte. |
| ReleasedThumbnailSurvivesInterveningRetirementUntilImGuiDraws | Texture replacement after AddImage releases its allocation before draw-list execution; checks native lifetime, exact sampled RGB and actual deletion after UI completion. |
| RealImGuiDrawIgnoresTheLastRhiTargetMaskSrgbAndWireframe | UI inherits offscreen target, colour mask or sRGB conversion, or fails to set filled rasterization/dithering; exact UI RGB, every offscreen-target RGBA byte and draw-time dither state are checked. |

Existing native-state tests now exercise actual ImGui rendering. Their complete
native-state/byte assertions survive at this renderer boundary; engine pass
pixel/depth/unprojection/statistics assertions and reference tolerances remain.
No test is deleted. Tests of the retired native texture class are ported to
Texture2D with the same checks, including the GL 4.3 extension loader run.

`TextureProductUpload.DeprecatedIdAliasKeepsPointerWidth` uses a CPU texture with
a descriptor-sized ID containing a nonzero high word. Exact pointer-width
comparison catches restoring the old uint32 narrowing in the deprecated alias;
it does not depend on OpenGL's inherently 32-bit texture names.

`test/EditorStartupOpenGLTests.cpp` exercises the actual editor resource/font
startup loaders from a changed CWD and relative INI directories, compares every
icon/border base-level byte and sampler/mip setting, checks actual merged font
glyphs and draws text through OpenGL3. It also invokes the unedited browser
thumbnail worker and covers missing/corrupt file fallback, odd-width RGB,
legacy 16-bit LDR byte truncation/metadata handling and linear half HDR.
See [RESOURCE_COMPATIBILITY.md](RESOURCE_COMPATIBILITY.md) for the operation-by-operation
comparison with the deleted implementations. Runtime display decoding has its
own private stb instance; strict asset importing remains independent.

## Phase 3a Vulkan validation

The [Vulkan foundation notes](Vulkan/README.md) describe structure, legacy removals,
state/graph batching, synchronous frame fences, deferred destruction, coordinates,
CPU/lavapipe selection and every new test's concrete mutation target. The nine
existing cases needing only foundation features run on Vulkan unchanged. Six new
neutral cases run on both backends; two additional Vulkan safety cases verify
selection and error delivery. All 31 existing drawing/pipeline cases stay registered
and explicitly skip on Vulkan with “phase 3b”. Public signatures are unchanged;
the existing D24S8 transfer word's bit packing is now stated explicitly.

`linux-gcc` Debug, `-j2` builds; CPU **404/404** (57.34 s), serial llvmpipe OpenGL
**147/147**, no skips (141.94 s), serial lavapipe Vulkan **17 passed, 31 explicitly
skipped, 0 failures** across 48 registrations (3.26 s). The existing 141 OpenGL
registrations and all references/assertions are preserved; six neutral additions
account for the increase. Khronos validation and synchronization validation report
no errors. Vulkan CTest registration pins both loader driver variables, disables
third-party implicit layers and sets RUN_SERIAL for every case. An initial run
correctly failed on loader errors from the installed LSFG implicit layer; isolating
implicit layers resolved those errors without filtering validation messages.

Changed-line `git clang-format` (with temporary Git metadata under `/tmp`) and
`git diff --check` are clean. No cache/preset changes, editor launch/link, hardware
GPU use, protected-path edits, reference changes, commits or pushes.

Final review also preserves neutral scissors with negative/out-of-bounds origins
by clipping to active attachment bounds before native recording. The new clear/load
case checks this on both backends. After that correction the full Vulkan suite
passes again (17 active/31 skipped), and the affected OpenGL case passes its
additional serial recheck. A lavapipe-only standalone smoke check also verifies
neutral shared-device initialization, all buffer usage flags, sampler creation,
coordinate queries, byte copies and zero live allocations after retirement.
