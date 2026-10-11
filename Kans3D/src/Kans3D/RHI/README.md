# Render hardware interface

The core API is `RHI.h`, in `Kans::RenderHardware`. The namespace avoids the
existing `Kans::RHI` class. The asset cache, scene target and material pass use this interface as of phase 1c-1. Remaining immediate passes use the temporary backend-only `OpenGLInterop.h` (see [MIGRATION.md](MIGRATION.md)).
`OpenGL/OpenGLDevice.h` is the backend construction entry point, separate from
the neutral core header. Creating that device requires a current, loaded 4.3
core context; all calls use its owning thread.

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

`FramesInFlight` defaults to 2 and specifies the allowed upper bound. This
backend completes each frame synchronously, which is within any positive bound;
future backends may keep that many frames pending. `BeginFrame`/`EndFrame` must
pair. Submission is allowed only inside a frame and outside a rendering scope.
`WaitIdle` can retire submitted work inside a frame without ending it.

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

`GetImGuiTextureID` returns the texture's native draw ID as `uintptr_t`. Its
lifetime follows the texture's frame-retirement contract.

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
`GpuTestContext::CreateDevice` knows backend construction. Every GPU test is
labelled `OpenGL`, serial, and runs with llvmpipe. Shader compilation uses
`MaterialShaderTestUtilities.h`, as the material tests do.

| Test (prefix `RHIConformance.`) | Regression caught |
| --- | --- |
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
