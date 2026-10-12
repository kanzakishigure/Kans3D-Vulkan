# Resource-layer retirement (phase 1c-4)

All engine passes and resources now use `Kans::RenderHardware`: scene/material
rendering, grid, skybox and IBL, sprites, picking, selection/debug geometry,
material previews, thumbnails and editor icons. Engine shaders are embedded
validated SPIR-V products. No engine consumer issues native GL commands or
includes GL/glad headers outside `RHI/OpenGL/`.

`Backend.h` selects a neutral `Backend` and owns explicit shared-device
initialization/shutdown. `InitializeDevice(backend, window)` delegates context
binding and entry-point loading to the backend; window/context creation and hints,
presentation and VSync also dispatch through neutral functions. SceneRenderer,
RenderAssetCache, Renderer2D and Texture2D obtain the same initialized device.
Resources die before the shared device, which dies before the window context.
There is no lazy context lease, native resource borrowing or transitional bridge.
The old Vulkan framework remains in `Platform/Vulkan` and `Renderer/RHI/Vulkan`;
its context base and shader-layout metadata no longer depend on the deleted API.

Texture2D owns its device, RHI image, path and neutral sampling description.
Product uploads preserve authored mips, row origin, linear/sRGB and full floats.
Legacy editor files remain linear unless explicitly requested otherwise; hardware
mip generation, filtering, addressing and half-precision HDR files are preserved.
Sprite descriptors use this image and sampling description directly.
Runtime file paths resolve against CWD while GetPath preserves the supplied path.
Display decoding preserves legacy byte formats/metadata behavior and linear half
HDR; it is separate from strict asset importing. Missing/corrupt texture files
log and return a magenta checker. Startup fonts use a built-in fallback when
files cannot be read. The statement-level caller audit and regressions are in
[RESOURCE_COMPATIBILITY.md](RESOURCE_COMPATIBILITY.md).
`GetImGuiTextureID()` delegates to the device and returns `uintptr_t`.
UI-frame exports retain textures through main and secondary viewport rendering,
including intervening preview/picker retirement. UI-frame completion releases them.

Editor compatibility retained, without editor source edits:

- Empty `Platform/OpenGL/OpenGLShader.h` for EditorLayer's unused include.
- `RenderPipelineSpecification::render_scene` and no-op
  `Renderer::InitRenderPipline` for its obsolete pipeline setup.
- Deprecated `Texture::GetRenererID()` alias, returning the same `uintptr_t` ID.
- `Renderer::GetWhiteTexture()` for the sprite-creation menu, lazily RHI-owned.

ImGuiLayer uses `CreateImGuiRenderer(backend, window)`. The OpenGL implementation
owns GLFW/OpenGL3 integration, context restoration for secondary windows and
texture retention. Per-submit native snapshots and bound-object retirement scans
are removed. Replay establishes explicit state. The bundled ImGui renderer owns
program, VAO, buffers, texture/sampler, blend, depth/cull/stencil, viewport/scissor
and polygon mode. Its boundary guard handles only omissions: framebuffer bindings,
colour writes, sRGB, raster discard, logic operations, clip distances and the legacy enabled UI dithering default.
The real
ImGui pixel test and hostile-state snapshot test exercise these omissions and
restoration; transfer-local pixel-store guards remain for tight uploads/readback.

No tests were deleted. Legacy texture tests now call Texture2D and the backend
loader, retaining every mip/format/pixel/unpack/error assertion. Reference tests
remove unused shader-library setup; all 12 images and tolerances are unchanged.
Native state assertions in SceneSubmissionUsesStandardAndCustomShaders,
TargetAlpha, LegacyStencilOption, SurfacePicker,
PickerFindsMeshesAndOutlineBordersThem and the preview-state tests are ported to
actual ImGui draws at the surviving native boundary; scene/picker/outline pixel,
unprojection and statistics assertions remain. The reimport test retains immutable
program/descriptor snapshots, checks their bytes/native lifetime and UI binding
restoration, then releases them and requires deletion. The hostile-state test
still compares its full native snapshot and caller-buffer bytes, and checks the
same red preview pixel. New regressions are documented in README.md.

The engine no longer reads any shader under `KansEditor/Resources/Shaders/OpenGL`.
Migrated files now unused there include `GridShader.glsl`, `TextureShader.glsl`,
`StencilShader.glsl`, `OutLineShader.glsl`, `DebugNormalShader.glsl`,
`MeshEntityShader.glsl`, and `PBR/{HdrToCubeMap,genIrradiance,prefilter,brdf,skybox}.glsl`.
The other legacy shaders and their `Include/` files are also unused by the engine.
The entire user-owned folder is preserved.

Validation: configured with `cmake --preset linux-gcc`; engine and three test
executables built with `-j2`. All 21 editor object targets and editor units reused
by CPU/GPU tests compiled without editor edits, editor linking or launch.
Changed C++ lines passed `git clang-format`; final formatting and diff checks
are clean. CPU: 379/379 (40.01 s). Serial llvmpipe GPU: 130/130 (118.55 s),
including all 12 unchanged references and the bundled GL 4.3 run; no skips.
Protected sources/resources/assets and references are unchanged. No commits
or pushes were made.
