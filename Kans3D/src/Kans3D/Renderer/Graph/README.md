# Phase 2a frame structure

`RenderView` owns the camera and engine-coordinate matrices, viewport, the same
last directional/point light as phase one, model/material IDs and transforms,
material render state and squared-distance sort keys, opaque/mask/blend indices,
sprites, explicit grid/sky state, environment handles, debug mode, selection and
preview intent. No queued draw borrows a GPU mesh. `RenderScene::Extract`,
`ExtractModelPart`, `ExtractModelDraws` and `ExtractDrawLists` make no device calls. Scene extraction
can run with no initialized device. Asset preparation stays on the render thread;
it supplies ready metadata to extraction, and draw consumption resolves resources.
Unloaded model/mesh parts are skipped; missing shaders/materials retain Standard
fallback behavior. Phase one has no model visibility flag or frustum culling:
offscreen parts stay submitted. Opaque and mask retain their **combined** stable
submission order; only blends sort far-to-near, with stable distance ties. Separate
filtered lists do not change that combined execution order.

`RenderGraph` imports textures or declares description-based transient textures,
adds passes with access declarations, supports explicit `DependsOn`, exports
outputs, and marks side effects such as readbacks. `Compile` is CPU-only and
throws descriptive errors for invalid handles, duplicate uses, incompatible
format/usage, uninitialized reads and cycles (even in dead passes). Reads depend
on their previous writer, or the first producer for a transient read declared
before its producer. Pure writes discard earlier contents; read/write accesses
represent attachment Load, blending and depth/stencil testing. RAW, WAW and WAR
edges order hazards; culling follows value producers, explicit prerequisites,
exports and side effects, rather than keeping unused overwrite chains. Declaration
order breaks topological ties. An imported write is not automatically a side effect:
export the output or explicitly mark its output/readback pass.

`Allocate` applies the compiled alias plan to a `TransientTexturePool`; its injected
texture factory lets CPU tests count real allocation requests. `Execute` allocates,
records command lists and submits in compiled order. `AddRecordedPass` also accepts
an immutable RHI command list. The Forward material pass uses it because asynchronous
material texture resolution happens while recording: recording once during graph
construction declares the exact images retained by commands, without a second
per-draw material-group resolution/allocation. Renderer2D batches also use it:
recording supplies the actual sampled image, including the internal white fallback
for untextured sprites. Sprite batching/order/stats and the external Renderer2D API
stay unchanged. Grid, sky, geometry and outline passes record during execution. Asset uploads and lazy fallback initialization remain RHI operations
outside graph scheduling; they precede consumers on the same device queue.

`ExecuteImportedPrefix` preserves the old observable `BeginScene` clear: Forward's
`Begin` adds/executes just Clear before returning the target to direct RHI callers.
The full graph is then extended and recompiled at EndScene. Its access sequence
retains Clear, and execution skips the submitted prefix; prefix execution never
acquires/empties the transient pool. Dependencies that would reorder that prefix
are rejected. This preserves existing custom target clears between BeginScene and
EndScene, including the debug-normal alpha test. Direct RHI operations remain
backend-tracked, independently of the graph's declarations. A future backend may
defer frame submissions to use the final compiled plan before GPU dispatch.

`RenderPipeline::Begin` describes the compatibility prefix;
`Build(graph, view, renderer)` describes the remainder of a frame. The registry
provides `Register` and `Create`, starts with `Forward`, and rejects unknown or
duplicate names. `SceneRenderer::SetPipeline` validates the name before changing
it. All existing SceneRenderer entry points remain source-compatible. No editor
source shim is required. Renderer2D's added submission sink and command-list routing
are internal compatibility plumbing for its existing static API. No extension
point mechanism, new lighting, material multipass, HDR or tonemapping is added.

## Forward passes and access declarations

The actual phase-one order differs from the planning summary: sprites, grid and
skybox submitted immediately, before the deferred material and geometry draws in
EndScene. This order is preserved, including arbitrary SubmitGrid/SubmitSkybox and
external Renderer2D flush order.

| Pass | Reads | Writes |
| --- | --- | --- |
| Clear | — | SceneColor0, SceneColor1, SceneDepthStencil; same .02/.02/.02/1, depth 1, stencil 0 |
| Renderer2D sprites / recorded batches | sprite textures; both loaded colors and depth/stencil | both scene colors and depth/stencil (conservative attachment declaration) |
| Grid | loaded colors and depth/stencil | both colors and depth/stencil, with unchanged ImmediatePassState |
| Skybox | supplied cube map; loaded colors and depth/stencil | both colors and depth/stencil, with unchanged ImmediatePassState |
| Materials (opaque/mask, blend) | actual material images and IBL maps/fallbacks; loaded colors and depth/stencil | both colors and depth/stencil, same stencil intent, debug mode, transparency states and immutable uniform ranges |
| Legacy geometry (queued StencilShader / OutLineShader / DebugNormalShader draws) | loaded colors and depth/stencil | both colors and depth/stencil; same per-model geometry order and states |
| Selection mask | model meshes (buffers remain RHI-managed) | transient R8 mask, cleared to zero |
| Selection composite | mask, SceneColor0 and loaded depth/stencil | SceneColor0; depth/stencil conservatively read/write, actual pipeline leaves it unchanged |
| Preview mask | preview meshes | transient R8 mask, cleared to zero |
| Preview composite | mask, SceneColor0 and loaded depth/stencil | SceneColor0; same conservative depth/stencil declaration |

The preview model's **lit material draw** stays in the material pass; its blue
outline follows the orange selection composite. Outputs are imported, unchanged
RGBA8Unorm/RGBA8Unorm/Depth24Stencil8 images, exported for the editor. Only material
and legacy geometry draws contribute to SceneRenderStats; sprite stats retain
their separate reset/batching semantics. Selection preserves alpha.

Picking/SurfacePicker, independent MaterialPreviewRenderer/thumbnails, and IBL
precomputation stay outside the frame graph: they are synchronous on-demand
operations with existing ownership/readback contracts, not frame dependencies.
Viewport model preview intent and its outlines **are** inside Forward.

## Pool and future explicit backend

For each live transient, compilation computes its first/last execution index.
Exported transients extend through end-of-graph output consumption. A physical
slot can be reused only with a strictly earlier lifetime end and an identical
width, height, format, dimension, mip count and usage. Resources used in the same
pass cannot alias. Selection and preview masks have disjoint lifetimes and share
one R8 image. Pool acquisition matches each slot to at most one existing image;
stable descriptions allocate nothing. Frames without transients retain the cache
(e.g. deselect/reselect at the same viewport). Incompatible unused allocations leave
the pool instead of accumulating on resize. Commands retain replaced images until
RHI retirement. Reuse submits to the same ordered RHI queue: backend state tracking
also covers preceding submissions/frames. Do not execute a pool concurrently on
multiple devices or queues; use a pool per renderer/device.

`Resources()` publishes description, import status, physical slot and the ordered
`{Pass, ExecutionIndex, Access}` sequence for **every** logical resource (culled
resources have no accesses). `Texture(id)` resolves its physical RHI image after
allocation. A future Vulkan backend can inspect sequences before submission,
merge sequences sharing a physical slot, and plan texture layout transitions,
read/write visibility and alias boundaries for the whole frame. Declarations are
currently whole-texture accesses, conservatively covering all faces/mips;
subresource precision can be added without changing the ownership model. Concrete
shader, attachment, copy and subresource state still comes from RHI commands. The
graph emits no barriers, backend names/types, layouts or queue-family operations.

## Regression tests

Existing test bodies/assertions and all 12 reference files/tolerances are unchanged.
The two existing GPU source files only gain new cases (and required includes).
`test/CMakeLists.txt` registers the new CPU files. There are no adaptations of
checked behavior and no weakened comparisons.

| New case | Concrete bug caught |
| --- | --- |
| RenderGraphCpu.DependenciesReorderConsumersAndPublishAccessSequence | Execute declaration order instead of producer order; publish wrong resource access indices/types |
| RenderGraphCpu.CullsDeadChainsAndOverwritesButRetainsReadbackAndItsProducer | Keep dead/overwritten imported writes, or cull a side-effect readback's producer |
| RenderGraphCpu.WriteAfterReadMustFollowReaderEvenWhenWriterIsOtherwiseReady | Overwrite initial imported contents while a delayed reader still needs them |
| RenderGraphCpu.ReadModifyWriteKeepsPreviousContentsAndOrdersMultipleReaders | Treat Load/blend as discard; retain only one of several WAR dependencies |
| RenderGraphCpu.AliasesOnlyStrictlyDisjointLifetimesAndCompatibleDescriptions | Alias input/output used in the same pass, fail to reuse a disjoint lifetime, or alias different formats; checks actual factory count/handles |
| RenderGraphCpu.ExportedTextureCannotAliasBeforeOutputConsumption | Reuse a final output as later scratch storage |
| RenderGraphCpu.PoolReusesFramesAndReallocatesSizeFormatUsageMipAndDimension | Allocate each frame or omit any compatibility field; 20 stable frames plus an empty-frame gap, factory counts and actual handle comparisons |
| RenderGraphCpu.RejectsCyclesEvenInDeadPassesAndExplainsInvalidReadsAndUsage | Hide cycles during culling, accept uninitialized reads/exports, missing resources, duplicate declarations or incompatible texture usage |
| RenderGraphCpu.ReimportingOneTexturePreservesLogicalIdentity | Import one physical image under two logical identities and lose hazards |
| RenderViewCpu.StableOpaqueMaskAndBackToFrontBlendUseTransformedBoundsAndCamera | Reorder mask versus opaque; unstable blend ties; near-first sort or distance from the transform origin; lose render state or retain stale lists |
| RenderViewCpu.ExtractsReadyPartsComposesTransformsAndKeepsOffscreenModels | Draw unloaded parts; add unintended frustum culling; reverse transforms; lose part/authored-material overrides or debug/stencil intent |
| RenderViewCpu.SceneSnapshotKeepsHierarchyLightsSpriteGridAndShellIntentWithoutDevice | Make device calls during extraction; lose world transforms, camera/light/state data, shell flags or immediate order; lose the packed override spans |
| RenderViewCpu.PipelineRegistryProvidesForwardAndRejectsUnknownAndDuplicateNames | Silently choose a different pipeline or permit invalid registrations |
| RendererReferenceGL.FullRuntimeFrameIsByteIdenticalToPhaseOne | Change the real shared runtime/editor frame order, shell/depth/stencil state, sprite alpha, sky/grid behavior, outline/preview order, stats or mask reuse, including deselection/reselection at a stable viewport |
| ScenePassGL.RuntimeWhiteSpriteDeclaresItsActualSampledFallback | Omit the internal white texture from whole-frame access planning, or lose runtime sprite draw/clear/alpha/stats behavior; exact covered/uncovered pixels and independent access checks |
| ScenePassGL.GraphExecutesTransientClearReadbackAndReusesStableViewport | Drop readbacks/recording/submission; issue wrong clear; replay the prefix, discard its access history or empty the pool at BeginScene; recreate physical textures every frame or fail to resize |

The full-frame SHA-256 expectations were captured from the saved **phase-one**
static library with original SceneRenderer/RenderScene headers, through neutral RHI
readback under serial llvmpipe CTest. They cover every byte of both RGBA8 targets
and packed D24S8, with zero tolerance. Its camera has a valid viewport and looks
slightly down so the grid has area; sprites/models overlap and sky remains visible.
The old library oracle lives only under `/tmp`; no reference assets were written.
All other CPU assertions are exact (float keys use float-ULP comparisons for exact
representable distances). The transient GPU clear uses exact normalized RGBA8
rounding (64,128,191,255), readback of every pixel, 12 stable frames then four
resized frames. Existing justified reference tolerances are untouched.

Validation on `asset-development`, `linux-gcc` Debug, builds with `-j2`:
CPU **391/391** (40.44 s); serial llvmpipe GPU **133/133** (123.55 s), including
the GL 4.3 extension bundle and all 12 unchanged references. The 13 new CPU and
three new GPU cases pass. All 21 editor object targets compile with unchanged
editor sources; only affected objects rebuild. No editor link/launch, real GPU,
protected-path changes, cache/configuration edits, commits or pushes. Changed-line
`git clang-format` and `git diff --check` are clean.
