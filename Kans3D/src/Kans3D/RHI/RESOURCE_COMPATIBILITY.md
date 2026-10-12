# Runtime resource compatibility audit after phase 1c-4

Compared the deleted implementations at `57453b1` with their RHI replacements,
statement by statement, and traced the remaining engine/editor callers. This
record supplements the pass-state comparison from phase 1c-3. Protected editor
sources, resources and reference images are unchanged.

## File Texture2D constructor (OpenGLTexture.cpp)

| Old operation / implicit contract | Initial 1c-4 difference | Resolution and evidence |
| --- | --- | --- |
| Save the supplied path in `m_Path`; stb opens it against CWD. | The absolute-only asset importer threw on relative paths. | Resolve with `filesystem::absolute` at **every** file `Texture2D::Create`, preserving the original `GetPath()` spelling. `RealStartupLoadsRelativeResourcesFromChangedCwd` invokes real `EditorResources::Init` from a temporary CWD with relative INI directories and checks all 17 real resources. |
| `stbi_is_hdr`, then `stbi_loadf` for Radiance HDR; otherwise `stbi_load` for bytes. | The importer also returns floats for 16-bit LDR, incorrectly sending these through half-float HDR upload. | A private display decoder uses the same HDR-versus-byte decision as the old constructor. 16-bit LDR uses stb's high-byte truncation, **not** normalized float conversion/rounding. `SixteenBitLdrIsTruncatedToLegacyBytesWithoutMetadataTransforms` checks exact bytes including low-byte boundary cases. |
| stb accepts all its supported formats and ignores ICC/gAMA/EXIF import policy. | Importer supports fewer formats and validates color/metadata/raster payloads. | Display decoding enables the full legacy stb format set and does no import color/metadata transform. The asset importer remains strict and unchanged. Odd-width RGB PNM and nonstandard-gAMA 16-bit PNG regression tests check these differences. |
| `stbi_set_flip_vertically_on_load(1)` changes the shared decoder and flips rows. | Importer returns top-left data; RHI upload flips to the device origin. | Keep identical displayed orientation via explicit upload row conversion, with a **private** stb decoder. Do not retain the incidental global flip mutation. Startup byte oracle, asymmetric RGB/HDR tests and existing importer isolation tests check orientation and isolation. The GLFW icon uses stb before editor textures and never relied on that mutation. |
| Three channels select RGB8 or RGB16F; four select RGBA8 or RGBA16F. | Portable RHI has RGBA equivalents; RGB receives opaque alpha. | Preserve all RGB values with alpha exactly one/255. Tight transfer rows avoid legacy undefined odd-width unpack overreads. Explicit asymmetric 3x2 RGB and HDR tests check pixels/alpha. Real four-channel startup icons are compared byte for byte against stb. |
| HDR uploads float source into half storage, without sRGB conversion or tone mapping. | An sRGB request reached importer validation and rejected HDR. | Display decoder treats HDR as linear regardless of requested sRGB, retains values above one, converts to half, flips rows and hardware-generates mips. `HdrStaysLinearHalfPrecisionWithOpaqueAlphaAndFlippedRows` checks exact binary16-representable pixels and mip average. Product floats still stay full precision. |
| File dimensions come from the decoded image; `Width/Height/Format` do not override it. | Byte and half dimensions already follow the source. Explicit neutral sRGB support was added. | Source dimensions and byte/half classification are retained. Old enum names containing `SRGB` actually allocated linear textures; existing callers were translated to `RGBA8Unorm`. New explicit `SRGB`/`RGBA8Srgb` requests deliberately opt into sRGB for LDR; HDR remains linear. Editor resources keep their exact linear format. |
| Full immutable mip chain when requested; native mip generation; nearest/nearest-mip or linear/trilinear minification, nearest/linear magnification; repeat/edge/border wrapping. | Backend-neutral sampler and hardware mip commands replace texture parameters. | Preserve supported legacy modes, chain extents and device mip generation. Existing mip tests retain their assertions; real startup test checks linear/trilinear/edge parameters. Removed cubic-extension enum had mapped to linear mip filtering and has no remaining caller. |
| Failed decode leaves dimensions uninitialized, format zero, then asserts (or makes invalid GL calls without assertions). One/two-channel files likewise had no format case. | Decode errors threw at startup. | Missing, unreadable, empty and malformed files log an error and return an uploaded **2x2 opaque magenta/black checker**, preserving path and sampling/mip request. One/two-channel decoded sources now have defined RGBA expansion. Missing-resource startup and malformed/empty tests verify real fallback bytes and usable IDs. Allocation/device setup failures remain API errors, not missing-file recovery. |
| Destructor deletes native texture; IDs are GLuints; Bind changes native texture bindings. | RHI handles/device own resources, ID is uintptr_t, descriptors replace Bind. | Consumers use one shared device and exported ImGui ID. ID alias and frame retention are covered by existing real ImGui sampling and released-thumbnail tests. No editor caller invokes Bind. |

## Product, raw textures and other removed resources

| Legacy behavior / callers traced | Replacement / audit result |
| --- | --- |
| `OpenGLProductTexture::UploadProduct`: validate 2D data/capabilities; R8/RG8/RGBA8 linear or sRGB/full-float; authored mips; canonical float endianness; TopLeft row reversal; tight unpack guard; linear/repeat defaults; errors returned as AssetError. | `CreateFromProduct` preserves each behavior with RHI copy buffers and transfer-local guards. Existing malformed-product, odd-width/native-float, authored-mip/sRGB/unpack and ImGui tests remain. There is no file parsing in this entry point. |
| Content-browser thumbnails run `ThumbnailCache::Decode` on a worker and upload decoded RGBA8 Linear via CreateFromProduct. Their HDR path tone maps explicitly. Browser paths come from Project's canonical absolute AssetDirectory. Missing/unsupported source returns nullptr; panel falls back to EditorResources file icon. | This caller remains unedited. `RealThumbnailWorkerUploadsPixelsFromAbsoluteBrowserPaths` compiles and invokes the actual worker from the changed CWD and compares all uploaded icon bytes. No relative path enters the strict importer here; missing thumbnail behavior is already explicit and unchanged. |
| Material thumbnail/editor inspector uploads Texture Products, or obtains RHI-owned previews through RenderAssetCache; failures are returned as variants/cache fallback. | No runtime file-loader call or shader-file dependency was introduced. Existing product/cache/material-preview/reimport tests cover the format, mip, descriptor, lifetime and preview behavior. |
| Raw OpenGLTexture2D: byte uploads, one level, sampler parameters; historical `*_SRGB` names allocate linear. Float cases take float input but assert a byte-sized payload, which was inconsistent. Null data warns and leaves uninitialized storage. | Actual remaining raw callers are 1x1 RGBA8 white/default-sprite pixels; bytes, alpha and sampling are identical. Neutral format spelling fixes the misleading SRGB name. New RHI raw float inputs use the format's actual storage bytes; GenerateMips/SRGB now work explicitly. No active caller depends on the old inconsistent float/null/cubic behavior. |
| TextureCube, FrameBuffer attachment ownership/bind/resize, vertex/index/uniform buffers, VertexArray layouts, Shader/ShaderLibrary file parsing/uniform setters. | All actual render consumers migrated before deletion: RHI attachments/subresources, explicit viewport/pipeline layouts, uint32 indices, immutable descriptors/push data and embedded validated shaders. No live editor call uses these deleted APIs. Native binding side effects are no longer a contract; surviving ImGui boundary has its measured guard. Existing conformance, mesh-product, scene/picking/outline/preview and all 12 reference tests preserve their behavioral checks. Old cube and raw-VAO helpers have no remaining caller. |
| Renderer startup creates global white/black/quad, empty shader library and inert legacy pipeline pass objects; GL API Init enables blend/depth/stencil/cull/seamless/MSAA and sets less-equal/stencil mask zero. | Actual scene/sprite passes use explicit RHI states (already compared in phase 1c-3). White texture remains a lazy RHI shim for the editor sprite menu; unused black/quad/library/pass allocation is gone. GetWhiteTexture sampling/bytes and shared ownership are exercised by existing tests. InitRenderPipline and unused OpenGLShader include remain shims. No resource shader file is required at startup. |
| OpenGLRHI binds GLFW context, applies VSync, loads glad and GL4.3 DSA extension entry points. | Neutral InitializeDevice/window factory delegates to backend; one explicit shared device. Existing device reuse and GL4.3 extension bundle cover the behavior. No second consumer-created device remains. |

## Startup icon and fonts

`Application` initializes FileSystem and window/device before ImGui startup;
`ImGuiLayer::OnAttach` loads Roboto Regular/Bold at 16 px and merges FontAwesome
at 14 px (same glyph range, pixel snap and 16 px advance). `EditorLayer::OnAttach`
then invokes `EditorResources::Init`. The new test invokes these resource loaders
without creating Application or launching the editor, using the actual files.

The GLFW window icon still loads `Resources/Icon/Kanslogo.png` relative to CWD,
requests four channels and keeps the OS default when decoding fails. Added an
error log for that already safe fallback; decoding/orientation/order are unchanged.

Font reading previously went through `AddFontFromFileTTF`, whose missing-file
branch asserts. `LoadEditorFonts`, now used by the real ImGuiLayer, reads before
calling `AddFontFromMemoryTTF`, preserving names, sizes, ownership and merge
settings. Missing/unreadable/empty text font files log and use ImGui's built-in
font at 16 px; missing optional FontAwesome logs and skips merging. Existing valid
fonts have identical atlas construction. Startup test checks both actual font
names, text/icon glyphs and a real OpenGL3 text draw. Missing-resource test builds
and draws the fallback atlas. Malformed nonempty TTF validation is still delegated
to ImGui, as before; this change does not introduce a separate font parser.

No tests were deleted or weakened. Seven new EditorStartupGL cases name their
concrete regression in comments; generated images and temporary INI files live
only under the temporary test directory. The real browser worker is compiled
from protected sources without modifying them.

Validation: `cmake --preset linux-gcc`, Debug builds with `-j2`, changed-line
`git clang-format` and diff checks clean. CPU 379/379 (40.01 s); serial llvmpipe
GPU 130/130 (118.55 s), including all 12 unchanged references, the seven new
startup regressions and the GL 4.3 bundle. All 21 editor object targets compile;
no editor link/launch or protected-path edits.
