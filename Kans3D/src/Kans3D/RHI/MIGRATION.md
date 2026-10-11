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

## Transitional OpenGL interop

OpenGLInterop.h is backend-only. BufferName, TextureName, ProgramName,
UniformPoint and TextureUnit expose native names/points only here. BindUniform,
BindTexture and BindProgram serve immediate consumers. MeshVertexArray exposes
an immutable VAO/vertex/index compatibility view over RHI allocations;
TextureView/TextureResource bridge the existing editor Texture2D API.
BorrowTexture retains the legacy IBL owner while wrapping its existing native
image. CreateSceneFramebuffer/BindSceneFramebuffer/DeleteSceneFramebuffer share
RHI target attachments with immediate passes. SaveSamplers/RestoreSamplers keep
those passes' sampling state intact. AcquireDevice is the temporary context
construction path; SubmitUpload bridges loads outside scene frames.
RetireUnbound drains the RHI deletion queue after preview state restoration,
protecting caller-bound native objects until the next cache/frame retirement.

Every consumer site is marked RHI-MIGRATION. The inventory below is generated
from those markers; remove these sites and the interop API when the remaining
passes move to the RHI.

| Site | Purpose |
| --- | --- |
| [Renderer/MaterialPreviewRenderer.cpp:179](../Renderer/MaterialPreviewRenderer.cpp#L179) | retire RHI replacements after restoring native preview bindings. |
| [Renderer/MaterialPreviewRenderer.cpp:442](../Renderer/MaterialPreviewRenderer.cpp#L442) | immediate preview binds the RHI frame buffer through its compatibility shim. |
| [Renderer/MaterialPreviewRenderer.cpp:453](../Renderer/MaterialPreviewRenderer.cpp#L453) | immediate preview binds the RHI object buffer through its compatibility shim. |
| [Renderer/MaterialPreviewRenderer.cpp:464](../Renderer/MaterialPreviewRenderer.cpp#L464) | bind the RHI program and material buffers for the immediate preview. |
| [Renderer/MaterialPreviewRenderer.cpp:468](../Renderer/MaterialPreviewRenderer.cpp#L468) | native material texture bindings from the RHI group 1 resources. |
| [Renderer/MaterialPreviewRenderer.cpp:471](../Renderer/MaterialPreviewRenderer.cpp#L471) | preview geometry uses the RHI mesh VAO compatibility view. |
| [Renderer/Renderer2D.cpp:171](../Renderer/Renderer2D.cpp#L171) | batched texture views can own the context device; release them before context teardown. |
| [Renderer/Resource/GpuShader.cpp:8](../Renderer/Resource/GpuShader.cpp#L8) | standalone preview buffers obtain the current context's shared device. |
| [Renderer/Resource/GpuShader.cpp:19](../Renderer/Resource/GpuShader.cpp#L19) | compatibility binding for the immediate material preview and old clients. |
| [Renderer/Resource/GpuShader.cpp:21](../Renderer/Resource/GpuShader.cpp#L21) | native ID compatibility probe. |
| [Renderer/Resource/GpuShader.cpp:34](../Renderer/Resource/GpuShader.cpp#L34) | standalone shader clients obtain the current context's shared device. |
| [Renderer/Resource/GpuShader.cpp:100](../Renderer/Resource/GpuShader.cpp#L100) | native uniform points serve the immediate preview compatibility shim. |
| [Renderer/Resource/GpuShader.cpp:110](../Renderer/Resource/GpuShader.cpp#L110) | native texture units serve the immediate preview compatibility shim. |
| [Renderer/Resource/GpuShader.cpp:121](../Renderer/Resource/GpuShader.cpp#L121) | immediate material preview compatibility program binding. |
| [Renderer/Resource/GpuShader.cpp:123](../Renderer/Resource/GpuShader.cpp#L123) | native program probe for compatibility clients. |
| [Renderer/Resource/MeshFactory.cpp:28](../Renderer/Resource/MeshFactory.cpp#L28) | skybox/IBL geometry remains legacy; the static cube must not retain a context device lease. |
| [Renderer/Resource/RenderAssetCache.cpp:21](../Renderer/Resource/RenderAssetCache.cpp#L21) | preserve legacy standalone backend selection and context-error behavior. |
| [Renderer/Resource/RenderAssetCache.cpp:26](../Renderer/Resource/RenderAssetCache.cpp#L26) | standalone upload compatibility obtains the context device lease. |
| [Renderer/Resource/RenderAssetCache.cpp:54](../Renderer/Resource/RenderAssetCache.cpp#L54) | VAO view of RHI buffers for remaining immediate draws. |
| [Renderer/Resource/RenderAssetCache.cpp:233](../Renderer/Resource/RenderAssetCache.cpp#L233) | submit outside a scene frame for immediate preview consumers. |
| [Renderer/Resource/RenderAssetCache.cpp:235](../Renderer/Resource/RenderAssetCache.cpp#L235) | editor-compatible Texture2D view, owns the RHI image. |
| [Renderer/Resource/RenderAssetCache.cpp:241](../Renderer/Resource/RenderAssetCache.cpp#L241) | lazy standalone caches share the context's device with SceneRenderer. |
| [Renderer/Resource/RenderAssetCache.cpp:276](../Renderer/Resource/RenderAssetCache.cpp#L276) | unwrap the editor-compatible texture shim. |
| [Renderer/Resource/RenderAssetCache.cpp:433](../Renderer/Resource/RenderAssetCache.cpp#L433) | unwrap the Texture2D compatibility view. |
| [Renderer/Resource/RenderAssetCache.cpp:464](../Renderer/Resource/RenderAssetCache.cpp#L464) | material preview still draws immediately with OpenGL. |
| [Renderer/Resource/RenderAssetCache.cpp:472](../Renderer/Resource/RenderAssetCache.cpp#L472) | backend protects native caller bindings during preview restoration. |
| [Renderer/Resource/RenderAssetCache.h:17](../Renderer/Resource/RenderAssetCache.h#L17) | immutable VAO view for picking/outline and the material preview. |
| [Renderer/Resource/RenderAssetCache.h:81](../Renderer/Resource/RenderAssetCache.h#L81) | compatibility retirement after legacy preview state restoration. |
| [Renderer/Resource/SelectionTools.cpp:82](../Renderer/Resource/SelectionTools.cpp#L82) | picker and silhouette mask use the RHI mesh VAO compatibility view. |
| [Renderer/SceneRenderer.cpp:102](../Renderer/SceneRenderer.cpp#L102) | delete the attachment-only FBO used by remaining immediate passes. |
| [Renderer/SceneRenderer.cpp:118](../Renderer/SceneRenderer.cpp#L118) | auxiliary geometry still uses the immutable VAO view. |
| [Renderer/SceneRenderer.cpp:207](../Renderer/SceneRenderer.cpp#L207) | legacy auxiliary shaders use texture parameters, so restore their sampler bindings. |
| [Renderer/SceneRenderer.cpp:227](../Renderer/SceneRenderer.cpp#L227) | borrow legacy IBL precomputation outputs and retain their owners. |
| [Renderer/SceneRenderer.cpp:349](../Renderer/SceneRenderer.cpp#L349) | restore sampler state before legacy geometry/selection/Renderer2D. |
| [Renderer/SceneRenderer.cpp:351](../Renderer/SceneRenderer.cpp#L351) | resume immediate geometry/outline on the shared scene attachments. |
| [Renderer/SceneRenderer.cpp:402](../Renderer/SceneRenderer.cpp#L402) | acquire the device once alongside this context's scene renderer. |
| [Renderer/SceneRenderer.cpp:419](../Renderer/SceneRenderer.cpp#L419) | recreate the legacy attachment view on resize. |
| [Renderer/SceneRenderer.cpp:434](../Renderer/SceneRenderer.cpp#L434) | target attachments stay RHI-owned; only the legacy FBO is native. |
| [Renderer/SceneRenderer.cpp:642](../Renderer/SceneRenderer.cpp#L642) | preserve native raster state for the immediate grid/skybox passes. |
| [Renderer/SceneRenderer.cpp:649](../Renderer/SceneRenderer.cpp#L649) | grid and skybox draw immediately between BeginScene and EndScene. |
| [Renderer/SceneRenderer.cpp:683](../Renderer/SceneRenderer.cpp#L683) | restore native raster state after the remaining immediate geometry passes. |
| [Renderer/SceneRenderer.cpp:686](../Renderer/SceneRenderer.cpp#L686) | legacy geometry shares the target's alpha-preserving write mask. |
| [Renderer/SceneRenderer.cpp:711](../Renderer/SceneRenderer.cpp#L711) | release the shared legacy target after auxiliary passes. |
| [Renderer/SceneRenderer.h:177](../Renderer/SceneRenderer.h#L177) | attachments shared with legacy grid/skybox/geometry/outline passes. |

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
