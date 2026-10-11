# Phase 1c-1

`SceneRenderer` obtains one shared device lease alongside the current GL context
and passes it to `RenderAssetCache`. Standalone compatibility upload functions
and lazy caches obtain the same lease. Resource owners declare the device before
its handles, so handles die first. Renderer2D releases retained batch texture
views at Shutdown, before destroying the context. The static skybox/IBL cube
stays on legacy allocations so it cannot retain a device across context
lifetimes. No editor sources change.

The cache owns RHI vertex/index buffers, texture mip chains (R8, RG8, RGBA8
linear/sRGB, RGBA32Float), shaders, independent samplers and immutable group-1
descriptors. Texture replacement invalidates cached descriptors; recorded lists
retain old snapshots. Shader wrappers own state-keyed pipelines, so shader
replacement releases old pipeline caches through RHI retirement. Lazy loading,
failure caching, version identity, Standard fallback, alpha state and eviction
remain unchanged. There are no cache-local retired-resource lists.

The scene owns two RGBA8 colours and Depth24Stencil8. Clears and material draws
use RHI rendering scopes. Legacy immediate passes use an attachment-only FBO
view. Output IDs come from Device::GetImGuiTextureID, with uintptr_t returned
without truncation (the editor's existing cast remains source-compatible).
Projection clip conversion and facing use the device coordinate query.

## Per-object data

Each material pass allocates fresh frame/object upload buffers. Object ranges
use GetBufferBindingAlignment and never overlap. Frame ranges are uploaded only
at debug-view transitions and shared until the next transition. Each draw owns
an immutable group 0 containing the frame/object ranges and the IBL bindings.
Command retention protects all ranges through retirement. Vulkan can allocate
these descriptors from a frame descriptor pool, bind the offsets/ranges directly
and release that pool with the frame; DX can use descriptor/root buffer views.
No dynamic-offset or push-data contract extension is required. Published
KansFrame/KansObject blocks, sets, slots and product versions remain intact.

Public RHI additions are RG8Unorm and RGBA32Float only. Sparse logical slots are
now mapped independently from native points. The neutral cache sampler setter
replaces native texture-filter mutations used by a regression test.

## Phase 1c-2

Engine shader sources live as readable inline 450 GLSL beside their users in
SelectionTools.cpp and MaterialPreviewRenderer.cpp. This keeps their C++/std140
layouts reviewable together and avoids filesystem/CWD dependencies. The small
InternalShader API caches validated/reflected ShaderData (and AssetError) by
name plus both source strings for the process, with synchronized first use.
It calls CompileEmbeddedShaderProgram: the same shaderc Vulkan-1.2 compiler,
SPIR-V validation, product encode/decode and reflection used by asset import.
Device shader creation is per device, never stored in the process cache.
Compile/backend errors return AssetError containing the compiler diagnostic and
shader name; consumers log it (stderr if logging has not been initialized) and
return an empty result. No assertion/crash is used for compilation failures.
Internal blocks use explicit set/binding and std140; asset reflection currently
excludes push constants, so its validation rules remain unchanged.

EntityPicker uses an R32Uint colour target, Depth32Float, immutable per-draw
uniform buffers, an RHI pipeline and a scissored rendering scope. It copies both
full targets through the existing texture-to-buffer contract, waits, and reads
only the picked words. Full-mip copies avoid adding a backend-specific readback
API; rasterization still covers only the cursor pixel. Picker signatures and
editor bottom-left pixel/world-position semantics are unchanged. Clip depth/Y,
scissor row and copy row conversion use the device coordinate query. The mesh
owns its device lease even when no legacy compatibility view is created.

SelectionOutline renders the selected silhouettes into an R8 mask, then
composites border/faint fill in an RHI scope loading scene colour 0. Scene alpha
and other colour attachments are preserved. Depth/stencil load without writes.
The standalone Draw API now accepts an explicit RenderingDesc target; its sole
direct caller outside SceneRenderer was a test. The editor's SceneRenderer
selection APIs need no shim or edits. Default standalone tools obtain their
device from their first uploaded mesh; the scene injects its shared device.

MaterialPreviewRenderer owns RHI colour/depth targets and checker/material
pipelines. RecordMaterialDraw shares engine/material bind-group construction,
pipeline state, mesh binding and drawing with the scene material pass. Both
resolve legacy IBL images/black fallbacks through GetMaterialEnvironment. Each
preview uses fresh immutable frame/object ranges. Standard/custom shader,
malformed recipe, missing/loading/failed asset, alpha/cull/rotation and Complete
semantics remain intact. Output uses Device::GetImGuiTextureID; Texture widens
to source-compatible uintptr_t. Render/RenderThumbnail signatures are unchanged;
no editor compatibility shim was needed. Thumbnail readback uses RHI buffers
and preserves tightly packed RGBA8, top row first, opaque alpha.

There are no new public RHI types or methods. Submit's contract now permits
standalone lists outside a frame; they retain resources/readback writes until
WaitIdle or EndFrame, without beginning or ending a caller frame. This is
covered by a neutral conformance test. Preview entry points retire standalone
work (including failed/nested fallbacks); picker waits for its readback.

Removed: GlProgram/native inline compilation, picker/outline/preview FBOs,
renderbuffers and empty VAOs, GpuShader/GpuUniformBuffer binding/ID shims and
native binding maps/constants, RenderAssetCache::BindMaterialTextures and
ReleaseRetiredResources, upload-frame bridging, special preview retirement and
sampler save/restore bridging. The mesh compatibility view remains solely for
legacy geometry and is created at that draw, not at upload.

## Transitional OpenGL interop

OpenGLInterop.h is backend-only. TextureView/TextureResource preserve the
editor Texture2D API. TextureResource unwraps RHI-owned views directly; native
editor Texture2D inputs retain their legacy owner inside a sampled view until
submission retirement. TextureSampling translates that input's existing filter
and address state to an independent RHI sampler. This bridge is needed only
until phase 1c-4 replaces editor inputs. IBL never uses it.

BufferName, TextureName, ProgramName, UniformPoint, TextureUnit and the new
PipelineVertexArrayName are native test probes (TextureName also supports the
Texture2D view). PipelineVertexArrayName lets existing mesh-layout tests inspect
an actual RHI-created pipeline VAO without retaining a mesh compatibility view.
BindTexture remains for immediate Texture2D clients and native regression tests.
AcquireDevice remains the transitional context construction path.

Removed in 1c-3: MeshVertexArray and its buffer/VAO adapters, BorrowTexture,
CreateSceneFramebuffer, BindSceneFramebuffer, DeleteSceneFramebuffer, the scene
attachment-only FBO, SceneRenderer native raster preservation and the static
legacy MeshFactory cube. Backend Submit state preservation remains unchanged.
The backend's private native sampled-image adapter now serves only old editor
Texture2D inputs; no cube or IBL ownership is borrowed.

Every remaining engine consumer site is marked RHI-MIGRATION. This table is
generated from the current markers; the remaining sites acquire context device
leases or preserve editor Texture2D APIs for phase 1c-4.

| Site | Purpose |
| --- | --- |
| [Renderer/Renderer2D.cpp:92](../Renderer/Renderer2D.cpp#L92) | standalone editor/particle clients acquire the current context device. |
| [Renderer/Renderer2D.cpp:176](../Renderer/Renderer2D.cpp#L176) | bridge editor Texture2D inputs through the existing texture view. |
| [Renderer/Resource/GpuShader.cpp:8](../Renderer/Resource/GpuShader.cpp#L8) | standalone buffers acquire a context device; cache/preview inject one. |
| [Renderer/Resource/GpuShader.cpp:30](../Renderer/Resource/GpuShader.cpp#L30) | standalone shader clients obtain the current context's shared device. |
| [Renderer/Resource/RenderAssetCache.cpp:21](../Renderer/Resource/RenderAssetCache.cpp#L21) | preserve legacy standalone backend selection and context-error behavior. |
| [Renderer/Resource/RenderAssetCache.cpp:26](../Renderer/Resource/RenderAssetCache.cpp#L26) | standalone upload compatibility obtains the context device lease. |
| [Renderer/Resource/RenderAssetCache.cpp:246](../Renderer/Resource/RenderAssetCache.cpp#L246) | editor-compatible Texture2D view, owns the RHI image. |
| [Renderer/Resource/RenderAssetCache.cpp:252](../Renderer/Resource/RenderAssetCache.cpp#L252) | lazy standalone caches share the context's device with SceneRenderer. |
| [Renderer/Resource/RenderAssetCache.cpp:287](../Renderer/Resource/RenderAssetCache.cpp#L287) | unwrap the editor-compatible texture shim. |
| [Renderer/Resource/RenderAssetCache.cpp:444](../Renderer/Resource/RenderAssetCache.cpp#L444) | unwrap the Texture2D compatibility view. |
| [Renderer/SceneRenderer.cpp:369](../Renderer/SceneRenderer.cpp#L369) | acquire the device once alongside this context's scene renderer. |

## Test adaptations

The existing frame upload tracer now observes COPY_WRITE_BUFFER and all aligned
offsets, rather than GL_UNIFORM_BUFFER offset zero. Its expected frame-transition
sequence and per-object count are unchanged. The mask/alpha equality test now
sets nearest filtering through SetTextureSampler instead of native image filter
edits, because independent RHI samplers own sampling state. Every pixel, alpha,
depth and tolerance assertion in that test is unchanged. Reference files and
reference-test code are untouched. The new conformance tests and their concrete
regressions are listed in README.md.

## Validation

Configured with `cmake --preset linux-gcc`; the engine and all three test
executables built with `-j2`. All 26 compilation targets containing editor
sources (22 editor units and four reused by CPU tests) passed without editing
editor sources, linking/launching the editor or writing its protected output.
`git clang-format` covered changed C++ lines; the entirely new interop header
was formatted in full. `git diff --check` passed.

The final CPU run passed 374/374 CTest entries with
`ctest -C Debug -LE OpenGL -j1 --timeout 300`. The final GPU run passed 106/106
entries serially with `LIBGL_ALWAYS_SOFTWARE=1`,
`MESA_LOADER_DRIVER_OVERRIDE=llvmpipe` and `GALLIUM_DRIVER=llvmpipe`, using
`ctest -C Debug -L OpenGL --timeout 300`. This includes all 12 reference images
with unchanged references/tolerances, the three new conformance regressions,
and the bundled GL 4.3 run that exercises successive context lifetimes.

## Phase 1c-2 test adaptations and regressions

Existing pixel/position/depth/reference/tolerance assertions are unchanged.
RenderAssetCacheOpenGLTests now uses test-local native binding/probe helpers
instead of removed GpuShader/GpuUniformBuffer methods. Those tests still inspect
the same native objects, bindings, bytes and deletion behavior. Mesh layout and
queued-resource lifetime tests explicitly create the now-lazy legacy VAO before
inspecting it. The direct outline test supplies an RHI-owned colour attachment
to its existing GL framebuffer: all border/fill and restored-FBO assertions stay
unchanged. The hostile-state test builds its dummy program through InternalShader
instead of the removed GlProgram; its complete state snapshot and rendered pixel
assertions stay unchanged. ReferenceImageTests, RendererReferenceOpenGLTests,
reference files and tolerances are untouched.

New tests and the concrete bugs they catch:

- InternalShader.ProducesValidatedReflectedSpirvAndReusesCpuProduct: missing
  validation/reflection, lost explicit sampler slot, or CPU cache not reused.
- InternalShader.CompilerFailureContainsDiagnosticAndCannotPoisonDifferentSource:
  swallowed compiler diagnostic or a name-only cache retaining a failed source.
- InternalShader.ProjectionAndPixelRowsRespectNonOpenGLCoordinates: hard-coded
  GL depth/Y/row conventions (exact matrix/row results).
- RHIConformance.StandaloneSubmissionRetainsReadbacksAndDoesNotStartAFrame:
  frame-only submission, premature readable data or an implicit frame left active.
- RHIConformance.PickerUsesBottomLeftRowsAndClearsResizedTargets: wrong row,
  stale ID/depth after resize, uint32 truncation or incorrect world coordinates.
  1e-6 allows float raster/unprojection rounding, well below a pixel spacing.
- RHIConformance.OutlinePreservesSceneAlphaAndDoesNotKeepOldSelectionOnResize:
  alpha/unrelated attachment writes, coverage outside selection or stale masks.
  Pixel checks are exact RGBA8.

## Phase 1c-2 validation

Configured with `cmake --preset linux-gcc` after adding sources. The engine and
all three test executables built with `-j2`. All 24 Debug compilation targets
containing editor sources (21 editor objects and three reused by CPU tests)
passed without editor source edits, linking/launching the editor, changing the
build configuration or writing its protected output directory.

Changed C++ lines were processed with `git clang-format`; entirely new files
were included as entirely changed files. Because the repository's .git is
read-only, the formatter used isolated temporary Git metadata with a read-only
object alternate; the real index/refs were untouched. A final formatter diff
reported no changes. `git diff --check` passed. The migration table matches all
23 remaining markers.

Final CPU run: 377/377 CTest entries with
`ctest -C Debug -LE OpenGL -j1 --timeout 300` (44.76 seconds).
Final GPU run: 109/109 entries serially with `LIBGL_ALWAYS_SOFTWARE=1`,
`MESA_LOADER_DRIVER_OVERRIDE=llvmpipe`, `GALLIUM_DRIVER=llvmpipe` and
`ctest -C Debug -L OpenGL --timeout 300` (84.88 seconds). This includes all
12 unchanged reference-image tests and the GL 4.3 extension-path bundled run.
Six new CTest entries account for the increase from 374 CPU / 106 GPU entries.
No references/tolerances were regenerated, and no commits or pushes were made.

## Phase 1c-3

SceneRenderer records HDR-to-cube capture, irradiance convolution, five specular
roughness mips and BRDF integration through RenderHardware. Each cube face/mip
uses ColorAttachment::Subresource; all outputs are normally owned RHI textures.
Sky and specular cubes retain their mip chains; irradiance and BRDF use level
zero. Lower unused specular levels are explicitly initialized to zero, matching
the prior software reference path. HDR decoding uses ParseTextureSource's
isolated decoder, FlipY and no generated input mips, then half-float quantization
to match the previous RGB16F upload. Outputs use RGBA16Float (BRDF stores RG).
EnvironmentPrecomputeSizes remains available; five specular levels require a
prefilter size of at least 16. Existing integration counts, step sizes and the
prefilter shader's 512-texel PDF constant are preserved for numerical equivalence.
Materials and previews bind these handles directly and resolve replacements on
each draw. Black fallbacks remain RHI-owned as before.

Engine GLSL 450 is embedded in Renderer/Resource/SceneShaders.h for IBL, grid,
skybox, stencil, outline and normal lines, and in Renderer2D.cpp for sprites.
All use InternalShader's cached validated/reflected SPIR-V products, explicit
set/binding and std140 blocks, with per-device shader objects. No engine pass
loads shader files or depends on CWD or editor resource copies. Scene passes use
a separate DeviceClip matrix after the original normal/shell calculations, so
clip depth/Y conversion cannot distort those calculations. Capture and BRDF
use DeviceTextureProjection: display clip conversion plus the framebuffer-origin
correction keeps sampled texture V aligned with stored row zero on GL, Vulkan
and DX conventions. This is an internal-shader helper, not an RHI extension.

Grid and skybox render inside RHI scopes loading the scene's attachments.
RenderScene preserves model submissions -> sprites -> grid -> skybox (material
and queued geometry execution remains in EndScene, as before). The effective
legacy editor state, measured at the immediate section, is the default for
both scene passes and Renderer2D: depth test/write on, LessEqual, blend off,
no culling, RGBA writes. Grid lines write {230,230,230,128} and depth; discarded
pixels leave the target untouched. The subsequent far-plane sky cannot cover
those lines. The target alpha still controls ImGui panel compositing.

ImmediatePassState (engine renderer API, not an RHI extension) makes standalone
variations explicit. The standalone grid/sprite references request straight-alpha
blending, including alpha; the overlay test additionally disables depth writes.
No native state is consulted to choose a pipeline. Existing editor calls retain
their signatures through default arguments.

Geometry uses explicit depth/stencil/cull state with RGB-only writes: stencil
shells exclude reference 1 with no stencil writes; inverted hulls cull front
faces. Normal lines use a portable LineList with two endpoints per indexed
triangle vertex; the shader reproduces the original .05 clip-space displacement.
No geometry shader RHI extension is needed. Triangle/draw statistics retain
their meanings. IBL now explicitly supplies a cleared depth24/stencil8 attachment
for every face/mip and BRDF scope, reproducing the original cleared depth24
renderbuffer and inherited depth testing/writes rather than dropping them.

### Phase 1c-3 ambient-state review

Compared a0d1a28 SubmitGrid/SubmitSkybox, Renderer2D BeginScene/EndScene/Flush,
DrawModelGeometry, EndScene, PrepareEnvironment, DrawGeometry/DrawMesh,
OpenGLRenderCommand dispatch, OpenGLRendererAPI draws, and framebuffer attachment/
resize code. The draw wrappers do not change raster state. Grid, skybox and
sprites inherit the measured editor state. Geometry explicitly resets depth,
blend, fill, cull and stencil for each draw, after EndScene selects RGB-only
writes; later geometry draws therefore do not inherit another mode's settings.
IBL disables culling, blending and stencil, retains the inherited depth/colour
state, and clears its depth renderbuffer before every draw. Enabling back-face
culling after legacy IBL was an ambient mutation; the migrated consumers choose
their own explicit cull modes.

All rows below have depth testing enabled, blending disabled, and filled
triangles (debug normals are lines). Immediate stencil state was inherited:
IBL left it disabled; before any IBL it was inert (Always, Keep, write mask zero)
from engine initialization. The new immediate pipelines disable stencil. Old
effective state and new RHI state match:

| Pass | Old depth compare/write | New depth compare/write | Old/new cull | Old/new colour mask | Old/new stencil |
| --- | --- | --- | --- | --- | --- |
| Renderer2D (editor) | LEQUAL/on | LessEqual/on | none | RGBA | inherited inert / disabled |
| Grid (editor) | LEQUAL/on | LessEqual/on | none | RGBA | inherited inert / disabled |
| Skybox (editor) | LEQUAL/on | LessEqual/on | none | RGBA | inherited inert / disabled |
| Stencil shell (StencilOutline) | LEQUAL/off | LessEqual/off | none | RGB | NotEqual 1, read 255, write 0, Keep |
| Outline shell (BackFaces) | LESS/off | Less/off | front | RGB | off |
| Debug normals (AllFaces) | LESS/off | Less/off | none | RGB | off |
| Other built-in geometry (AllFaces) | LESS/off | Less/off | none | RGB | off |
| HDR to cube | LEQUAL/on | LessEqual/on | none | RGBA (old target RGB) | off |
| Irradiance | LEQUAL/on | LessEqual/on | none | RGBA (old target RGB) | off |
| Specular prefilter, each mip | LEQUAL/on | LessEqual/on | none | RGBA (old target RGB) | off |
| BRDF LUT | LEQUAL/on | LessEqual/on | none | RGBA (old target RG) | off |

Renderer2D batches consecutive quads sharing one texture, flushing at a texture
change or 10000 quads. This preserves draw/blend order and has no descriptor slot
limit. Every draw owns immutable upload buffers and one combined texture/sampler
binding, supported by Vulkan descriptors and DX12 texture/sampler descriptors
without dynamic descriptor indexing or binding arrays. Vulkan can allocate these
sets from a frame pool; DX12 can allocate one SRV/sampler pair per batch.
DrawCalls counts actual issued batches, QuadCount counts all submitted quads,
and vertex/index totals retain their meanings. White quads use an owned RHI
texture; old editor Texture2D/SubTexture2D inputs use the marked view bridge.
Init has an optional device argument. SetTarget/ClearTarget allow explicit RHI
targets for standalone sprite/particle clients; SceneRenderer routes existing
BeginScene calls automatically. All existing draw/BeginScene/stat signatures
remain available. No public RHI extension was introduced.

SceneRenderer's environment/sky/LUT accessors now return RHI textures. No editor
source uses the changed accessor types. SceneRendererSpecification's old library
fields remain accepted as source-compatible shims; built-in passes now use
engine-owned shaders. Renderer::GetShaderLibrary remains available with an empty
library. The editor's grid enable option no longer tests legacy shader presence.
KansEditor/src requires no edits.

The following GLSL files are unused by engine passes, under
KansEditor/Resources/Shaders/OpenGL (all are retained; some reference fixtures
still load them into legacy libraries):

- PBR/HdrToCubeMap.glsl
- PBR/genIrradiance.glsl
- PBR/prefilter.glsl
- PBR/brdf.glsl
- PBR/skybox.glsl
- GridShader.glsl
- TextureShader.glsl
- StencilShader.glsl
- OutLineShader.glsl
- DebugNormalShader.glsl

### Phase 1c-3 test adaptations

ReferenceImageTests, reference images and tolerances are unchanged. Two calls in
RendererReferenceOpenGLTests now explicitly request blending, carrying the
standalone GL setup into the RHI pipeline; every assertion and tolerance stays
unchanged. Existing native material test draws use a test-local
NativeMeshArray holding an RHI-created pipeline and original mesh buffers. The
layout test still inspects actual native attribute enables/sizes and the index
binding; pixel and GL-error assertions remain unchanged. The queued-highlight
replacement test observes the old RHI vertex buffer's weak ownership instead of
the removed mesh VAO view: it still requires destruction on replacement before
EndScene and checks the same new/old silhouette pixels. The preview irradiance
replacement test uploads the same red/green values into RHI cube faces instead
of legacy cubes; every numeric pixel assertion and tolerance is unchanged.
DeferredSubmissionDoesNotQueryDriverRasterState still requires exactly zero
queries during both material and geometry submissions. Its draw count changes
from two to four because the now always-available built-ins execute both queued
geometry draws without Renderer::Init; previously a missing global library
silently skipped those draws. The statistic still counts actual issued draws.

### Phase 1c-3 new regressions

- ScenePassGL.EquirectangularCaptureKeepsEveryFaceAndBothAxes: swaps or mirrors
  any cube face. A direction-coded HDR independently checks three channels on
  all six faces, at centre and asymmetric interior positions. .02 accounts for
  RGBE truncation (<1/128), bilinear spherical interpolation and half rounding;
  an axis reversal changes the checked channels by more than .2.
- ScenePassGL.PrefilterRoughnessMipsActuallyBroadenTheDirectionalLobe: omitted
  roughness uniform or copying level zero into the chain. The +X red face mean
  must broaden across separately rendered levels, with conservative analytic
  gradient separations (.15, .05 and .03) rather than getter checks.
- ScenePassGL.BrdfLutMatchesIntegrationAtAsymmetricCoordinates: V flip, swapped
  NdotV/roughness or a lost channel. Four asymmetric samples compare both output
  channels with a double CPU integration oracle; .001 covers float accumulation
  and half-float rounding.
- ScenePassGL.ExplicitGridOverlayLeavesDepthAndBlendsAlphaWhileSkyboxStaysBehindSprites:
  ignoring explicitly requested overlay depth/blend state, wrong grid blend alpha or skybox overwriting foreground geometry.
  Depth24 is exact, grid/sprite/sky pixels use exact RGBA8 values.
- ScenePassGL.SpriteTextureChangesBeyond32SlotsPreserveBindingsAndBlendOrder:
  wrong texture descriptor, slot-limit overflow or reordering A/B/A batches.
  Forty distinct textures are checked pixel-for-pixel; overlapping translucent
  quads compare a CPU blend equation within one RGBA8 rounding unit.
- ScenePassGL.SpriteCapacityFlushKeepsTheLastQuadAndStatistics: overflow or lost
  final quad at the 10000-quad boundary, or overwriting submitted upload data.
  Exact pixels distinguish the final red quad from the previous blue one and
  verify quad/draw/vertex/index counts across both batches.
- MaterialSceneGL.DebugNormalLinesKeepIndexedEndpointsWithoutDepthOrAlphaWrites:
  missing endpoint expansion, incorrect stride/count, depth or alpha writes.
  Six exact white endpoint pixels and all depth words/alpha bytes are checked;
  destination alpha .25 distinguishes an accidentally enabled alpha mask.

- MaterialSceneGL.RuntimeGridPreservesLegacyOpaquePixelsAndOccludesSkyAroundModel
  replaces RuntimeGridRemainsVisibleOverSkyWithEngineOwnedShaders, whose blended
  expectation encoded an editor regression. It calls OnRenderRuntime with an
  actual asset-backed model and grid, then with a sky too. It requires exact
  {230,230,230,128} line pixels, depth less than the far plane and unchanged after
  sky drawing, an unobscured model, and exact sky background. Catches enabling
  grid blending, disabling grid depth writes, masking alpha or sky overwrites.
- ScenePassGL.DefaultSpritesKeepLegacyAlphaDepthWritesAndEqualDepthOrder:
  catches blended editor sprites, Less instead of LessEqual, or missing sprite
  depth writes. Two equal-depth quads must produce the second quad's unblended
  {0,255,0,128}, survive the sky, and store exact depth24 0x800000.

- InternalShader.TextureProjectionPreservesSampledRowsAcrossDeviceConventions:
  using display Y conversion for texture capture, which vertically flips cube
  faces and the BRDF LUT on top-origin devices. Independent viewport/origin math
  checks two asymmetric V values and depth conversion exactly for all eight
  combinations of clip Y, origin and depth range.

### Phase 1c-3 validation

Configured with `cmake --preset linux-gcc` after adding the shader/state headers
and GPU test source. The engine and all three test executables built with `-j2`.
All 24 Debug compilation targets containing editor sources (21 editor units and
three reused by CPU tests) passed without editor source edits, editor linking or
launching, resource-copy writes, build-configuration changes or output redirection.

Changed C++ lines were processed with `git clang-format`, including the entirely
new shader header and test file. Temporary Git metadata used the real repository
as a read-only object alternate; its index/refs were not modified. The final
formatter diff reported no changes. `git diff --check` also covered the new
files through that temporary index and passed. The regenerated inventory matches
all 11 remaining markers. Protected editor resources, user assets, reference
images and tolerances are unchanged; standalone reference call sites explicitly
request blending. No commits or pushes were made.

Final CPU run: 378/378 entries with
`ctest -C Debug -LE OpenGL -j1 --timeout 300` (41.86 seconds).
Final GPU run: 119/119 entries, serially with `LIBGL_ALWAYS_SOFTWARE=1`,
`MESA_LOADER_DRIVER_OVERRIDE=llvmpipe`, `GALLIUM_DRIVER=llvmpipe` and
`ctest -C Debug -L OpenGL --timeout 300` (113.98 seconds).
There were no skips. All 12 unchanged reference-image tests and the GL 4.3
extension-path bundled run passed. Ten new regressions comprise one CPU entry
and nine GPU entries (the review replaces one runtime test and adds one sprite
default-state test).
