# Tests

## OpenGL mipmap 回归测试

```sh
cmake --build --preset linux-gcc-debug --target OpenGLTextureTests RHIConformanceTests -j2
LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=llvmpipe GALLIUM_DRIVER=llvmpipe ctest --test-dir build/linux-gcc -C Debug -L OpenGL --timeout 300
```

`OpenGLTextureTests` 独立于 CPU 测试，实际创建 OpenGL 上下文并调用引擎的
`Texture2D::Create` 文件加载及后端的 `LoadOpenGLEntryPoints`。
Linux 需要 EGL 开发库和支持离屏 OpenGL 的驱动（Mesa llvmpipe 可用，无需 DISPLAY）；
其他平台使用 GLFW 隐藏窗口。上下文或函数入口不可用时测试失败，不静默跳过。

覆盖完整 mip 链尺寸、三线性缩小/线性放大参数、GL 错误、8×4 棋盘图最末级
1×1 像素的平均值、关闭 mipmap 时仅分配一级，以及全部 16 张编辑器图标的
非二次幂尺寸与最末级像素读回。

Linux 额外运行 `GL43ExtensionPath`，使用 Mesa 软件驱动并设置
`MESA_GL_VERSION_OVERRIDE=4.3`，防止驱动自动返回 4.5 而掩盖扩展入口加载遗漏。
测试输出 XML 中记录实际 renderer 和 version。曾复现 GL 4.3 路径没有加载
`glGenerateTextureMipmap`、函数指针为空的问题；修复位于 RHI/OpenGL 后端的 DSA 加载列表。

## 资源系统测试

`Kans3DTests` 为纯 CPU 测试，覆盖身份与 sidecar、Product 编解码、ProductStore
（含 fork 后强制退出的发布中断）、Project 构建/移动/删除、AssetManager 与导入器。

`OpenGLTextureTests` 还包含 `RenderAssetCacheGL`：网格上传、异步加载后的 GPU 资源
复用与替换，以及用内嵌 Standard Shader 离屏绘制并回读像素（法线、切线手性、UV、自发光、PBR 和调试视图）。

在 Linux 上 CTest 以 `LIBGL_ALWAYS_SOFTWARE=1`、`MESA_LOADER_DRIVER_OVERRIDE=llvmpipe`
串行运行全部 OpenGL 测试；在硬件驱动上并行运行 GL 测试曾导致 GPU 挂起。

```sh
ctest --test-dir build/linux-gcc -C Debug -LE OpenGL -j1 --timeout 300   # CPU
ctest --test-dir build/linux-gcc -C Debug -L OpenGL        # OpenGL（串行，llvmpipe）
```

`EditorStartupGL` calls the real `EditorResources::Init`, the font helper used by
`ImGuiLayer::OnAttach`, and the unedited content-browser thumbnail worker from a
temporary CWD with `ResoucesDir=Resources/` and `FontsDir=Resources/Fonts/`.
All 17 real icon/border textures are checked against independent stb bytes,
including row orientation, linear format and mip/sampler settings. Actual Roboto
and FontAwesome glyphs and an OpenGL3 text draw are checked. Missing resources
must log and yield an uploaded checker/built-in font instead of terminating.
Generated RGB PNM, 16-bit PNG with gAMA, HDR, empty and malformed files catch
channel padding, byte truncation, metadata policy, half precision and recovery
regressions. No test launches KansEditor or writes to its resources.

Phase 2c's sample and published-v5 input live in `data/multipass`; no shipped
material or pixel reference is changed. The new cases use the production importer,
KSHD codec, asset cache and render feature rather than a test-only pass parser or
outline renderer.

| New case (suite prefix omitted) | Concrete bug caught |
| --- | --- |
| ImportAndCodecPreserveNamesTagsStagesAndEveryStateField | Parse extra passes but drop modules, tags, declaration order or state fields while encoding/decoding |
| PublishedVersionFiveFixtureLoadsAsMainOnly | Reject a single-pass product actually encoded by the original v5 codec after bumping to v6 |
| ParserRejectsMalformedDuplicateNestedAndIncompletePasses | Silently accept duplicate state, unknown fields, unfinished/nested blocks, invalid paths or passes on compute programs |
| ImportRejectsChangedMaterialBindingsNamesTypesOffsetsAndSamplers | Accept incompatible set-1 block/member/sampler names, offsets, types, bindings or descriptor counts |
| StrictMaterialSubsetImportsButChangedEngineAndVertexContractsFail | Reject a correctly offset subset, allow incompatible engine vertex inputs or set-0 layouts (including an individually valid legacy frame layout differing from main) |
| DiscoveryIncludesExtraStagesAndTheirIncludesAndRequiresDeclaredInputs | Publish only main stages, omit hull includes, or allow an undeclared extra-stage input |
| DecoderRejectsTruncatedStateInvalidEnumsAndIncompatibleBytecode | Trust product metadata, read truncated pass fields/code, accept invalid state or load pass bytecode incompatible with main |
| InvertedHullSphereHasExteriorRingAndExactUnchangedInteriorAndDepth | Use back culling, omit depth testing, write depth, draw before opaque, lose material width, per-object transform/normal matrix or shared binding layout/group |
| ForegroundOpaqueOccludesHullAndUnTaggedMaterialsAddNoPass | Outline untagged or cached/unsubmitted models, or paint a hull over foreground depth |
| FeatureHonorsPreparedOrderAndUnreadyDrawsAtConfigurablePoint | Reintroduce pipeline-culled draws by scanning all draws/cache contents, or draw stale unready entries |
| ExtraPassReceivesEachObjectsMaterialRenderDataAndOnlyMatchingTagsRun | Bind a short/stale object block, lose alpha mode/cutoff/double-sided values, ignore pass tags, or fail explicitly registered tags |
| RuntimeUsesEveryPassStateAndCachesIndependentlyOfMainMaterial | Use main material depth/cull/blend/mask state or reuse its pipeline for an extra pass |

The sphere test compares every surface pixel and all packed depth/stencil bytes
exactly between enabled/disabled outline frames. Every changed colour pixel must
be black outside the red/green silhouettes with alpha unchanged. The two analytic
annuli occupy about 1318 pixels; the accepted 1200–1450 area and ±0.005 world-unit
silhouette allowance cover the 48×96 polygonal sphere tessellation and raster
coverage, not interior colour or depth errors. One sphere rotates 90 degrees so
using an identity normal matrix instead of per-object data fails. A separate graph
order assertion catches an outline-before-opaque mutation even if final colour
happens to match. Foreground and untagged pixels, and per-object RGBA8 uniform
outputs, use exact comparisons.

Existing test changes only account for v6's appended four-byte pass count: legacy
v1–v4 slices strip that word too; the group corruption test targets the same final
member eight bytes before the new word. Its truncation coverage expands by four
bytes. The unsupported-future-version input moves from 6 to 7 while keeping the
`UnsupportedVersion` assertion. No assertion or reference tolerance was weakened.
