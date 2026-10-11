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

OpenGLInterop.h is backend-only. BufferName and TextureName serve the
remaining mesh/Texture2D compatibility views; BindTexture serves immediate
Texture2D clients. ProgramName, UniformPoint and TextureUnit are native probes
used only by regression tests, so the tests can continue checking actual GL
bindings and object deletion without binding methods on GpuShader. MeshVertexArray
is now lazy and used only by legacy geometry; RHI mesh upload/picking/outline/
preview never create a native VAO. TextureView/TextureResource preserve the
editor Texture2D API. BorrowTexture retains legacy IBL owners while wrapping
precomputation images. CreateSceneFramebuffer/BindSceneFramebuffer/
DeleteSceneFramebuffer share RHI attachments with grid/skybox/legacy geometry.
AcquireDevice remains the transitional context construction path.

BindUniform, BindProgram, SubmitUpload, RetireUnbound and SaveSamplers/
RestoreSamplers are removed. Native replay state preservation and safe idle
retirement now belong to the backend, including indexed blend/masks, stencil,
scissor, buffer ranges, descriptors, clip enables and pixel stores.

Every remaining engine consumer site is marked RHI-MIGRATION. The inventory below is generated
from those markers; remove these sites and the interop API when the remaining
passes move to the RHI.

| Site | Purpose |
| --- | --- |
| [Renderer/Renderer2D.cpp:171](../Renderer/Renderer2D.cpp#L171) | batched texture views can own the context device; release them before context teardown. |
| [Renderer/Resource/GpuShader.cpp:8](../Renderer/Resource/GpuShader.cpp#L8) | standalone buffers acquire a context device; cache/preview inject one. |
| [Renderer/Resource/GpuShader.cpp:30](../Renderer/Resource/GpuShader.cpp#L30) | standalone shader clients obtain the current context's shared device. |
| [Renderer/Resource/MeshFactory.cpp:28](../Renderer/Resource/MeshFactory.cpp#L28) | skybox/IBL geometry remains legacy; the static cube must not retain a context device lease. |
| [Renderer/Resource/RenderAssetCache.cpp:21](../Renderer/Resource/RenderAssetCache.cpp#L21) | preserve legacy standalone backend selection and context-error behavior. |
| [Renderer/Resource/RenderAssetCache.cpp:26](../Renderer/Resource/RenderAssetCache.cpp#L26) | standalone upload compatibility obtains the context device lease. |
| [Renderer/Resource/RenderAssetCache.cpp:232](../Renderer/Resource/RenderAssetCache.cpp#L232) | editor-compatible Texture2D view, owns the RHI image. |
| [Renderer/Resource/RenderAssetCache.cpp:238](../Renderer/Resource/RenderAssetCache.cpp#L238) | lazy standalone caches share the context's device with SceneRenderer. |
| [Renderer/Resource/RenderAssetCache.cpp:273](../Renderer/Resource/RenderAssetCache.cpp#L273) | unwrap the editor-compatible texture shim. |
| [Renderer/Resource/RenderAssetCache.cpp:430](../Renderer/Resource/RenderAssetCache.cpp#L430) | unwrap the Texture2D compatibility view. |
| [Renderer/Resource/RenderAssetCache.h:18](../Renderer/Resource/RenderAssetCache.h#L18) | lazy VAO compatibility view only for legacy geometry passes. |
| [Renderer/SceneRenderer.cpp:104](../Renderer/SceneRenderer.cpp#L104) | delete the attachment-only FBO used by remaining immediate passes. |
| [Renderer/SceneRenderer.cpp:120](../Renderer/SceneRenderer.cpp#L120) | only legacy geometry creates/uses the mesh VAO compatibility view. |
| [Renderer/SceneRenderer.cpp:275](../Renderer/SceneRenderer.cpp#L275) | borrow legacy IBL precomputation outputs and retain their owners. |
| [Renderer/SceneRenderer.cpp:350](../Renderer/SceneRenderer.cpp#L350) | acquire the device once alongside this context's scene renderer. |
| [Renderer/SceneRenderer.cpp:369](../Renderer/SceneRenderer.cpp#L369) | recreate the legacy attachment view on resize. |
| [Renderer/SceneRenderer.cpp:384](../Renderer/SceneRenderer.cpp#L384) | target attachments stay RHI-owned; only the legacy FBO is native. |
| [Renderer/SceneRenderer.cpp:592](../Renderer/SceneRenderer.cpp#L592) | preserve native raster state for the immediate grid/skybox passes. |
| [Renderer/SceneRenderer.cpp:599](../Renderer/SceneRenderer.cpp#L599) | grid and skybox draw immediately between BeginScene and EndScene. |
| [Renderer/SceneRenderer.cpp:633](../Renderer/SceneRenderer.cpp#L633) | restore native raster state after the remaining immediate geometry passes. |
| [Renderer/SceneRenderer.cpp:636](../Renderer/SceneRenderer.cpp#L636) | legacy geometry shares the target's alpha-preserving write mask. |
| [Renderer/SceneRenderer.cpp:669](../Renderer/SceneRenderer.cpp#L669) | release the shared legacy target after auxiliary passes. |
| [Renderer/SceneRenderer.h:184](../Renderer/SceneRenderer.h#L184) | attachments shared with legacy grid/skybox/geometry passes. |

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
