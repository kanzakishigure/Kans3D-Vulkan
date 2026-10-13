# Frame structure (phase 2b)

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
are internal compatibility plumbing for its existing static API. Phase 2b adds
per-view features and extension points below; no new lighting, material multipass,
HDR or tonemapping is added.

## Frame passes and access declarations

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
Viewport model preview intent and its outlines **are** in the frame graph, contributed by editor features.

## Pipeline extension points and per-view features

`RenderPipeline::Inject(point, context)` dispatches the owning SceneRenderer's
`RenderFeatureSet`. Other pipelines call the same helper at the same semantic
boundaries, using their own graph targets. Forward's core is Clear, Renderer2D,
Skybox and the unchanged shared material batch. Feature passes retain their existing
names, attachment accesses, uniforms, states, stats and relative order.

| Extension point | Contract / reason | Current default feature |
| --- | --- | --- |
| `Immediate` | Once per `RenderView::ImmediateOrder` entry, before that entry's core draw; `context.Immediate` identifies the original entry. Needed because grids may be interleaved arbitrarily with sprites and skyboxes, including multiple grids. | `EditorGridFeature`, consuming only Grid entries |
| `BeforeOpaque` | Once after all immediate submissions and before material rendering. | None |
| `AfterTransparent` | Once after the opaque/mask/blend batch, before post-processing. | `MaterialPassFeature("Outline")` after `DebugGeometryFeature` (StencilShader / OutLineShader shells and DebugNormalShader draws, in original per-model order) |
| `BeforePostProcess` | Once after scene/debug geometry, at the future post-process input boundary. Forward currently has no post-process pass; default features add nothing here. | None |
| `Overlay` | Once after any post-processing, in final viewport color space. | `EditorSelectionFeature` (orange selection mask/composite), then `EditorPreviewFeature` (blue preview mask/composite) |

There is deliberately no `AfterOpaque` point yet: opaque/mask and blend share one
immutable recorded command list. Exposing a boundary inside that batch would imply
splitting recording and changing allocation/batching behavior. A future pipeline
with separate opaque/transparent passes can add that boundary; existing features
use the common points above. NPR outlines on scene geometry use
`AfterTransparent` through the engine `MaterialPassFeature`; final viewport annotations use `Overlay`.

`RenderFeature` has three callbacks:

- `Prepare(RenderView&, SceneRenderer&)`: optional, once before draw-list preparation;
  features may extract intent or contribute model draws to the shared material batch.
- `RunsAt(RenderExtensionPoint) const`: selects one or multiple points.
- `AddPasses(RenderExtensionPoint, const RenderFeatureContext&)`: receives the graph,
  immutable view, target `GraphUse` declarations, owning renderer and (at Immediate)
  the original submission. Add passes/resources with ordinary graph accesses;
  export outputs or mark real side effects when appropriate. CPU-only features may
  omit the renderer in their context. The context is borrowed during the callback;
  recorders capture the view/renderer or values, never that stack context.

`Target` lists loaded scene colors followed by loaded depth/stencil, all with
read/write attachment access. The editor outline features need SceneColor0 and the
final depth entry; future pipelines supply compatible targets. No interface uses a
concrete device backend. Device work is recorded with neutral RHI commands through
renderer helpers and graph callbacks.

Register with `renderer.GetFeatures().Register(name, unique_ptr<RenderFeature>,
enabled = true)`; use `SetEnabled(name, bool)` / `IsEnabled(name)` between frames.
Empty names, null instances, duplicate names and unknown toggle names are errors.
Each SceneRenderer owns its instances and flags; pipeline selection does not reset
them. Preparation and injection use registration order. Disabling skips **all**
callbacks, including Prepare and RunsAt; re-enabling restores the original slot.
Dispatch/toggling never constructs an active list, copies callbacks or allocates.
Registration is a one-time allocation, outside rendering.

Passes added by a feature are declared in intended execution order. Pipeline core
passes and feature additions use `RenderGraph::OrderAfter` (one replaceable scheduling
predecessor per pass) to preserve point and registration order even for disjoint
outputs. Unlike `DependsOn`, these ordering edges do not retain dead passes or their
textures. Graph resource producers/explicit prerequisites still control liveness;
contradictory ordering/resource dependencies are rejected as cycles. Ordering lives
in a scalar pass field, avoiding a new per-pass dependency-vector allocation.

All four built-in features live in `Renderer/Features/Editor/EditorRenderFeatures.*`.
The preview feature's Prepare submits its **lit ghost** into the core material batch
at the same point as before, preserving mixed-material distance sorting and immutable
uniform allocation. Disabling it removes both that draw and its blue outline.
The orange selection composite still precedes the blue preview composite, and the
two masks still alias one stable R8 texture.

The one temporary compatibility policy is `RegisterDefaultEditorFeatures`, called
only in the **SceneRenderer constructor**. Move that call into editor view setup
later; no editor source changes are needed now. This policy intentionally applies
to runtime cameras too: `OnRenderRuntime` still displays the grid when
`EnableGridShader` is true, and uses the same enabled features as the editor camera.
Open policy question: should game views later register no editor features, while
play mode in an editor viewport retains them? No behavior change is made here.

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

Phase 2a validation on `asset-development`, `linux-gcc` Debug, builds with `-j2`:
CPU **391/391** (40.44 s); serial llvmpipe GPU **133/133** (123.55 s), including
the GL 4.3 extension bundle and all 12 unchanged references. The 13 new CPU and
three new GPU cases pass. All 21 editor object targets compile with unchanged
editor sources; only affected objects rebuild. No editor link/launch, real GPU,
protected-path changes, cache/configuration edits, commits or pushes. Changed-line
`git clang-format` and `git diff --check` are clean.

## Phase 2b regression additions

All pre-existing test bodies, pass-name expectations, SHA-256 values, the 12
reference files and their tolerances are unchanged. `test/CMakeLists.txt` adds a
separate CPU executable so allocation instrumentation cannot affect the other
CPU tests. The GPU test file only appends three cases and their helpers.

| New case | Concrete bug caught |
| --- | --- |
| RenderFeatureCpu.SamePointKeepsRegistrationOrderAndRestoresPositionAfterToggle | Name-sorted/reversed dispatch, or re-enabled features moved to the end; exact unsorted pass lists across enabled/disabled/re-enabled frames |
| RenderFeatureCpu.PointOrderSurvivesAnEarlierFeatureWaitingForItsProducer | Reliance on declaration-order ties lets a ready Overlay execute before a blocked BeforeOpaque feature; exact producer/feature execution order |
| RenderFeatureCpu.UnusedFeatureChainIsCulledAcrossLivePointBoundaries | Using liveness dependencies for point order retains dead feature outputs; exact surviving passes, empty resource accesses, no physical slots and zero factory calls |
| RenderFeatureCpu.LiveFeatureResourcesReuseStableFramesAndDisabledGap | Fresh pools per frame or clearing the cache on disable; two exact handles and exactly two allocation requests over 24 frames with a disabled gap |
| RenderFeatureCpu.StableDispatchAndTogglesAllocateNothingAndSkipDisabledCallbacks | Registry/callback copies, active-list allocation or registration rebuilt on toggle; exact zero heap allocations across 1,000 frames with all default empty editor features, and exact enabled/disabled callback counts |
| RenderFeatureCpu.RejectsInvalidRegistrationsAndUnknownToggleNames | Duplicate replacement, null/empty registrations, or typos silently ignored |
| RendererReferenceGL.FeaturePointsPreserveInterleavedImmediateAndMaterialOverlayOrder | Grids batched at the wrong point, core/immediate/material/overlay boundaries misplaced; exact full compiled pass list, seven marker recordings and one Prepare |
| RendererReferenceGL.DisabledEditorFeaturesSkipGhostPreparationPassesAndTransientResources | Disabled Prepare still submits the ghost; grid/debug/outline passes or transients survive disable; exact passes, draw/triangle/resource counts for three disabled frames and re-enable, and ghost stays Lit in Normals mode |
| RendererReferenceGL.FeatureRegistrationAndEnablingAreIsolatedBetweenSceneRenderers | Global feature instances or enable flags leak to another renderer; exact independent pass lists and callback counts |

Phase 2b validation on `asset-development`, `linux-gcc` Debug, builds with `-j2`:
CPU **397/397** (40.23 s); serial llvmpipe GPU **136/136** (132.73 s), including
all six new CPU cases, all three new GPU cases, the unchanged full-frame byte
oracle, all 12 references and the GL 4.3 extension bundle. Dispatch/toggling with
all default empty editor features makes zero measured heap allocations over
1,000 frames; feature transient handles/pool counts remain stable across 24
frames and a disabled gap. Existing full-frame mask reuse assertions also pass.
Changed-line `git clang-format` (including new files) and `git diff --check` are
clean. Editor sources/resources/assets, references and their tolerances, presets
and build configuration are unchanged. No editor launch, hardware GPU use,
commits or pushes.

## Phase 2c material passes

The engine `Features/MaterialPassFeature.*` registers `MaterialOutline` by default,
for the `Outline` tag at `AfterTransparent`. It follows the compatibility debug
geometry and precedes post-processing and editor overlays. Opaque depth is already
populated; Forward's shared opaque/transparent command list and existing allocation
order are preserved. A view with no matching prepared material draw adds no graph
pass or GPU allocations. The feature records once, declaring the actual sampled
material images and environment textures, with loaded colour/depth attachments.
It uses only `context.View.MaterialOrder` and rejects stale unready entries; future
pipeline culling does not get bypassed by a second scene traversal. The existing
renderer still submits offscreen objects because it has no frustum/visibility flag.

See [shader syntax and product contract](../../Asset/Importer/ShaderREADME.md)
and the self-contained `test/data/multipass` inverted-hull sample. Shipped samples,
user content and the twelve pixel references remain untouched.

Phase 2c validation, `linux-gcc` Debug, builds with `-j2`: CPU **404/404**
(47.30 s); serial llvmpipe GPU **141/141** (160.33 s), including the unchanged
full-frame byte oracle, all twelve unchanged reference comparisons, and the
GL 4.3 extension bundle. Seven new CPU cases and five new GPU cases pass; their
individual mutation targets and exact/tessellation tolerances are recorded in
`test/README.md`. All **21/21** existing editor translation units pass
`-fsyntax-only` with their existing Debug compile flags, two at a time, without
linking into the protected editor output directory or launching the editor.
Changed-line `git clang-format` (using temporary Git metadata under `/tmp` because
this workspace's `.git` is read-only) and `git diff --check` are clean. Existing
codec test edits preserve assertions: v6 adds a four-byte count, so legacy slices
and final-group-member offsets shift by four; the unsupported future version
moves from 6 to 7. No reference, tolerance, shipped sample or user content changes.


## Phase 3b Vulkan execution

The same graph/view/material/feature recording now runs on headless Vulkan.
Command inference batches resource uses before native rendering scopes and
handles sampled/attachment transitions, native depth conventions and read-only
depth. The graph does not expose native barriers or descriptor offsets. Scene and
sprite owners retain warmed pipelines; immutable per-draw buffer ranges map to
shared native sets on Vulkan. Offscreen preview/thumbnail output is a neutral
image, independent of editor UI export. All scene, material-pass and feature
assertions run on both backends; all twelve original reference PNGs are preserved.
See the [backend design and measured validation](../../RHI/Vulkan/README.md#phase-3b-graphics-and-renderer)
and [test change audit](../../../../../test/README.md#phase-3b-test-change-audit).

## Phase 2d: lighting and ordered HDR/display rendering

The scene working image `SceneHDR` is RGBA16F. `SceneColor0` remains the RGBA8
output returned by GetOutput(0), GetRenderTarget and ImGui; SceneColor1 and D24S8
retain their old formats and roles. Picking remains independent of colour.

Forward owns the mandatory fullscreen **ACES fitted (Narkowicz) + IEC sRGB**
resolve, with exposure applied as a linear radiance multiplier before the curve.
It is a pipeline pass, rather than an optional editor RenderFeature. Consecutive
HDR materials share a working image and blend in linear HDR. Consecutive display
materials draw directly to the RGBA8 output, preserving their original byte
quantization and blend operations. Switching from display to HDR prefills the
working image with the exact algebraic inverse of the final curve and sRGB
transfer. Switching back resolves the HDR batch before drawing display materials.
The final HDR batch uses a separate recorded graph pass `Tone mapping (ACES
fitted + sRGB)`. Intermediate switches record the same fullscreen passes in the
material command list; both working/output image accesses are declared as
read/write attachments, and the final resolve declares its HDR sampled read.
RHI command tracking handles the internal sampling/attachment transitions.

This policy preserves the existing shared opaque/mask submission order and
back-to-front transparent order, even when output spaces interleave. Display
output bytecode is never rewritten or evaluated through another curve. Display
backgrounds cross an inverse/half-float/forward round trip only at HDR boundaries;
all 256 channel codes, mixed display blends, and exposures .001/.1/.8/4/100 are
checked for exact byte identity on each backend. The inverse is a compatibility
bridge for the already-composited display background, not an inverse applied to
custom material output. Mixed HDR/display blending uses that background's
virtual radiance; pure HDR batches retain values above one until their resolve.
The cost is two fullscreen conversions per HDR batch when custom shaders
interleave with it; all-Standard scenes use one prefill and one final resolve.

Resolve occurs before display geometry shells/material outline features at
AfterTransparent. BeforePostProcess features receive resolved viewport colour,
and Overlay selection/preview composites run afterward. Grid, sprite and skybox
submissions stay in their original immediate order on RGBA8, with their original
depth and alpha states; prefill preserves their bytes and later geometry retains
its original occlusion relationship. Selection/preview masks and composites also
retain their original code and space. Normals and base-colour debug views draw
directly to RGBA8. Wireframe deliberately retains its phase-one diagnostic
lighting (four legacy ambient contributions and Reinhard encode), independent of
the corrected lit rendering and camera exposure. It is never tone-mapped again.

MaterialPreviewRenderer and RenderThumbnail use the same RGBA16F working target,
ToneMappingPass, exposure and output-space policy. The checker is display colour
and prefills the HDR image before the material draw. Preview lighting is a single
explicit point light with diffuse radiance 40 and specular multiplier 1.
Legacy preview LightDiffuse[0] remains its unit NPR factor, preserving published
display shaders; the new PBR radiance is the explicit authoring light value 40.
MaterialPreviewOptions::Exposure defaults to .8 and lets editor callers match
scene exposure. Shader preview cache identity includes output-space metadata.

Point lights are collected in world space, sorted by squared camera distance,
then persistent entity UUID; the nearest 16 are uploaded. Nonfinite distances
sort last. The singular SceneInfo::pointLight remains a compatibility snapshot of
the first selected light, and direct legacy BeginScene callers can supply it.
Multiple directional components select the lowest UUID deterministically. PBR
uses Diffuse_Intensity as RGB radiance, inverse-square attenuation for points,
and Specular_Intensity as an RGB multiplier of the direct GGX specular lobe.
This separates diffuse and specular artistic controls cleanly, while preserving
the Fresnel energy split and leaving IBL independent. Directional ambient is
unattenuated albedo illumination multiplied by AO. Unused point slots are zero,
and new PBR loops stop at the uploaded count.

The std140 KansFrame prefix and offsets are unchanged:

| Member | Offset | Bytes |
| --- | ---: | ---: |
| ViewProjection | 0 | 64 |
| CameraPosition | 64 | 16 |
| LightPositions[4] | 80 | 64 |
| LightColors[4] (diffuse radiance) | 144 | 64 |
| LightDiffuse[4] | 208 | 64 |
| Time / DebugView / old padding | 272 / 276 / 280 | 4 / 4 / 8 |
| PointPositions[16] | 288 | 256 |
| PointRadiance[16] | 544 | 256 |
| PointSpecular[16] | 800 | 256 |
| DirectionalDirection (w: present) | 1056 | 16 |
| DirectionalRadiance | 1072 | 16 |
| DirectionalSpecular | 1088 | 16 |
| DirectionalAmbient | 1104 | 16 |
| LightingInfo (x: count, y: exposure) | 1120 | 16 |

Total: 1136 bytes. Existing six/seven-member frame products still validate; new
products require the full matching layout. Set 0/binding 0 binds the larger
uniform range, so old SPIR-V reads its unchanged prefix and ignores the tail.
Old four-light loops see four distinct selected lights with zero unused radiance.

Editor wiring uses the inherited Camera::SetExposure(float), const GetExposure()
and mutable GetExposure() on EditorCamera and
CameraComponent::SceneCamera. Scene rendering copies that camera value into
KansFrame and passes it to the resolve; there is no editor UI change. The resolve
uses an effective range .001–128 (nonfinite values use .8), keeping the display
background inverse representable with sufficient binary16 precision. Exposure
is a multiplier, not EV; an EV slider can pass pow(2, EV). Thumbnail callers wire
MaterialPreviewOptions::Exposure to the same camera value. Scene camera exposure
is persisted as an optional CameraComponent Exposure field; older scenes retain
the Camera default of .8.


New light components have defined defaults: point/directional diffuse and
specular intensities are RGB 1, ambient is RGB 0, and directional light travel is
(0,-1,0). Point ambient remains a legacy authoring field; corrected PBR direct
radiance comes only from diffuse intensity. The wireframe compatibility branch
is the sole deliberate retention of the old diagnostic lighting behavior.

Regression coverage and reference provenance are recorded in `test/README.md`
and `test/references/README.md`. The published v6 fixture is an actual pre-change
codec product, not a modified new product; see `test/data/multipass/README.md`.
