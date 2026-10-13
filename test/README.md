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


## Phase 3b test change audit

All GPU registration remains `RUN_SERIAL`. Vulkan fixtures pin both lavapipe
loader variables before creating an instance, always require CPU devices, and
install validation/synchronization validation sinks that fail gtest (including
teardown). GL remains llvmpipe, including the 4.3 extension bundle, whose filter
excludes Vulkan parameters. CPU tests create no GPU contexts.

`GpuTestContext` installs a caller-created headless Vulkan shared device so
renderer consumers use the same construction/validation policy. Its explicit
local-device factory remains available for conformance. No Vulkan renderer test
uses native GL calls; native UI/state assertions are conditional on OpenGL or
remain in dedicated native GL fixtures.

### Existing conformance: assertions unchanged

`RHIConformanceTests.cpp` removes all 31 phase-3b skip branches and constructs the
common shader on both backends. Every original assertion and coordinate helper
is unchanged. The 46 original cases are:

- `InternalShaderCreationKeepsTheCompilerDiagnostic`
- `StandaloneSubmissionRetainsReadbacksAndDoesNotStartAFrame`
- `PickerUsesBottomLeftRowsAndClearsResizedTargets`
- `OutlinePreservesSceneAlphaAndDoesNotKeepOldSelectionOnResize`
- `BufferUploadCopyReadbackRanges`
- `EveryMipAndCubeFaceRoundTrips`
- `OddWidthSingleChannelRows`
- `TriangleCoverageAndExactColor`
- `DepthTestWritesAndReadback`
- `DepthCompareAndBias`
- `SeparateColorAlphaBlendAndWriteMask`
- `BlendEquationsAndDisableTransition`
- `CullFrontBackAndWinding`
- `WireframeAndFillTransition`
- `ScissorAndViewportBounds`
- `PushConstantsPartialWritesAndRecordedValues`
- `TwoGroupsWithCollidingSlotsAndCombinedSamplers`
- `SeparateTextureAndSamplerBindings`
- `IntegerPickerTargetAndClear`
- `HalfFloatTargetPreservesHDRAndNegative`
- `PipelineCacheIncludesEveryStateField`
- `PipelineCacheIncludesVertexAndBindingLayouts`
- `ReleasedAllocationsSurviveUntilFrameRetires`
- `RecordedAndSubmittedResourcesAreRetained`
- `CoordinateQueryMatchesCoverageDepthAndRows`
- `IndexedVertexLayoutOffsetsAndBaseVertex`
- `AttachmentLoadAndClearIgnorePreviousMasks`
- `IndependentBlendStatesForMultipleTargets`
- `StencilReplaceCompareAndWriteMask`
- `SrgbTargetEncodingAndLinearTransition`
- `MipGenerationFiltersLevelZero`
- `StorageWritesFeedUniformsAndReadbackWithoutCallerBarriers`
- `PushConstantsRepackScalarArraysAndMixedTypes`
- `PushConstantsRepackNestedStructArraysAndMatrices`
- `PipelineMasksSurviveRenderingScopeClears`
- `OlderRetirementDoesNotExposePendingReadbackWrites`
- `PushConstantsRepackRowAndColumnMajorMatrixArrays`
- `FullFloatTargetPreservesValuesBelowHalfPrecision`
- `OddWidthTwoChannelMipsAndSampledComponents`
- `SparsePublishedSlotsAndRecordedPerDrawUniformRanges`
- `ClearOnlyFormatsPreserveTypedValuesAndLoad`
- `ClearOnlyDepthStencilMipReadback`
- `CubeMipAttachmentCopiesFollowSubmissionOrder`
- `PackedDepthStencilCopiesKeepMipBytesThroughDeviceLocalBuffers`
- `RepeatedFrameSlotsAndStandaloneWorkRetireReleasedResources`
- `OddWidthTwoChannelMipTransfers`

### Renderer fixtures: existing pixel/order/count assertions preserved

The lists below identify every existing case whose fixture or readback changed.
All are parameterized by `GpuTestBackend` (surface picking also keeps its
perspective/orthographic parameter). RHI readback replaces framebuffer/texture
name reads. Ordinary scene pixels use the coordinate query to normalize rows;
precomputed cube/BRDF texels keep their texture-projection convention and their
original numerical assertions. GL error checks remain GL checks; Vulkan uses
its validation-as-failure sink. Original raw state pollution/restoration and
ImGui assertions are retained on the GL branch.

#### Reference and feature cases

Per-test ownership replaces the shared suite context so backend parameters
cannot share device resources. `Read` normalizes PNGs to top-row-first through
`Origin`; the byte oracle normalizes to its original lower-left rows. Separate
filesystem ownership fixes a double shutdown when the entire 4.3 bundle runs.
The twelve PNGs remain unchanged. Only the five documented Vulkan tolerances
and two exact Vulkan attachment hashes differ; see the backend report.

- `StandardTexturedIblPointLight`
- `AlphaModesDepthSortingAndBackFaces`
- `EmissionTextureAndToneMapping`
- `ImportedToonMaterial`
- `DebugBaseColor`
- `DebugNormals`
- `DebugWireframe`
- `SelectionOutlineAndNumericSurfacePicker`
- `MaterialPreviewSphere`
- `Grid`
- `Skybox`
- `Renderer2DSprite`
- `FullRuntimeFrameIsByteIdenticalToPhaseOne`
- `FeaturePointsPreserveInterleavedImmediateAndMaterialOverlayOrder`
- `DisabledEditorFeaturesSkipGhostPreparationPassesAndTransientResources`
- `FeatureRegistrationAndEnablingAreIsolatedBetweenSceneRenderers`

#### Scene and immediate pass cases

Device construction is parameterized; production recording is unchanged.
Original face/axis, BRDF, HDR, sprite, depth, blend, binding-order, capacity and
graph-use checks retain their assertions. `Pixel` already used `DeviceRow`.

- `EquirectangularCaptureKeepsEveryFaceAndBothAxes`
- `PrefilterRoughnessMipsActuallyBroadenTheDirectionalLobe`
- `BrdfLutMatchesIntegrationAtAsymmetricCoordinates`
- `ExplicitGridOverlayLeavesDepthAndBlendsAlphaWhileSkyboxStaysBehindSprites`
- `SpriteTextureChangesBeyond32SlotsPreserveBindingsAndBlendOrder`
- `DefaultSpritesKeepLegacyAlphaDepthWritesAndEqualDepthOrder`
- `SpriteCapacityFlushKeepsTheLastQuadAndStatistics`
- `RuntimeWhiteSpriteDeclaresItsActualSampledFallback`
- `GraphExecutesTransientClearReadbackAndReusesStableViewport`

#### Asset/material bridge cases

Cache lifecycle/eviction assertions remain identical. Texture fallback reads
use RHI copies; the legacy first-uniform-float check uses a neutral GPU probe
and retains its exact 0.125 assertion. Shader-material rendering builds groups
from all reflected engine entries, including declared unused samplers that GL
previously optimized out. Remapped models and scene submission use neutral
readback. Picker/outline keeps GL ImGui/FBO restoration assertions on GL and
all pixel/picking assertions on both. `EditorSampleShadersLinkOnOpenGL` is
renamed `EditorSampleShaderProductsCreateOnBothBackends`: GL still cross-compiles
and links; Vulkan creates the same products directly from SPIR-V.

- `ResourcesAppearWhenLoadedAndInvalidIDsReturnNothing`
- `ReimportedAssetKeepsLastUploadUntilReplacementIsReady`
- `ModelInstancesResolveMeshesAndUnusedEntriesAreEvicted`
- `CustomSamplerFallbackBindsBlackAndNormalThroughPublishedProduct`
- `MissingFailedAndDebugCustomShadersUseStandardBindings`
- `ShaderMaterialDrawsWithItsPackedParametersAndTextures`
- `ShaderOutsideTheMaterialContractIsNotCompiled`
- `EditorSampleShaderProductsCreateOnBothBackends`
- `SceneSubmissionUsesStandardAndCustomShadersInLitDebugAndPreviewPasses`
- `ImportedRemappedModelDrawsAuthoredSlotAndPartOverrides`
- `PickerFindsMeshesAndOutlineBordersThem`

#### Standard material cases

Native immediate draws, frame/object bindings, RGBA32F readback and cube
updates are replaced by the same neutral shader/layout/binding draw path.
`SetTextureSampler` preserves the prior nearest/repeat sampling setup. The
camera uses `DeviceProjection`. All float tolerances (including 1e-6 and 2e-6),
exact values and comparisons are unchanged. The derivative +V assertion caught
the actual Vulkan normal-map orientation bug; it was fixed, not loosened.

- `PackingFailureLogsOnceDrawsStandardAndRetriesEitherChangedSource`
- `UnassignedNormalIsGeometricAndUnassignedEmissionIsDark`
- `VertexColorIsOptInAndStrengthOneAppliesIt`
- `TangentDirectionHandednessAndDerivativeFallbackOrientNormalMap`
- `UvTilingRepeatsAndOffsetIsAppliedAfterScaling`
- `EmissiveFactorAndMapMultiplyBeforeToneMapping`
- `StandardFactorsShadeWithSceneLightsAndGltfPackedChannels`
- `OcclusionRedChannelAndStrengthOnlyAttenuateIbl`

#### Material scene cases

RHI readback replaces texture-name reads. Picking rows use `DeviceRow`,
packed D24 is converted to normalized float for the existing depth assertions,
and indexed normal-line pixel offsets now use the coordinate query. GL-only
ImGui mask/stencil restoration assertions stay on the GL branch. Material,
alpha, stencil ring, shell, normal, sorting, occlusion and reload assertions
remain unchanged.

- `RuntimeGridPreservesLegacyOpaquePixelsAndOccludesSkyAroundModel`
- `HighlightUsesReimportedMeshAfterQueuedGpuResourceIsDestroyed`
- `TargetAlphaIsOpaqueEvenWhenProjectShadersReturnFractionalAlpha`
- `LegacyStencilOptionDrawsARingOutsideTheModelAndClearsPreviousReferences`
- `LegacyOutlineCullsFrontFacesAndKeepsTheInflatedBackShell`
- `DebugNormalLinesKeepIndexedEndpointsWithoutDepthOrAlphaWrites`
- `BlendSortsAcrossModelsAndPreviewByTransformedBoundsAfterOpaque`
- `BlendTestsOpaqueDepthAndLeavesUncoveredDepthClear`
- `BlendDistanceUsesTheCameraAndEachPartsBoundsWithinOneModel`
- `StandardMaskMultipliesTextureFactorAndVertexAlphaAndRetainsCutoffEquality`
- `FailedCustomShaderAndDebugFallbackKeepBlendAndDoubleSidedState`
- `DoubleSidedBackFacesFlipNormalsAndDoNotDisableCullingForTheNextPart`

#### KSHD v6 material-pass cases

The fixture selects either shared device. Full image/depth readbacks normalize
row order before existing asymmetric pixel checks. Ring/interior/depth, feature
order/culling, tag dispatch, per-object values, every extra-pass state and cache
assertions all remain unchanged; no new tolerance is used.

- `InvertedHullSphereHasExteriorRingAndExactUnchangedInteriorAndDepth`
- `ForegroundOpaqueOccludesHullAndUnTaggedMaterialsAddNoPass`
- `FeatureHonorsPreparedOrderAndUnreadyDrawsAtConfigurablePoint`
- `ExtraPassReceivesEachObjectsMaterialRenderDataAndOnlyMatchingTagsRun`
- `RuntimeUsesEveryPassStateAndCachesIndependentlyOfMainMaterial`

#### Offscreen material preview cases

`RenderImage` exposes a neutral image rather than requiring an ImGui export.
Pixel reads use RHI copies and `DeviceRow`; all original pixel/tolerance,
completion, reload, blend, cutoff, shape, lighting and thumbnail assertions stay
unchanged. UI-only preview/export/lifetime checks remain native GL below.

- `UnsavedValuesAndShaderChangesReachPixelsInLitMode`
- `BlendCompositesOverBothCheckerColorsAndKeepsOpaqueAlpha`
- `MaskCutoffChangesCoverageAndRetainsEquality`
- `YawPitchAndDoubleSidedControlPlaneVisibility`
- `ProceduralTangentsMapNormalAlongIncreasingTextureV`
- `StandardRemainsLitAndUsesSceneIrradianceChanges`
- `FailedCustomShaderFallsBackWithRenderStateAndIncompleteResult`
- `ThumbnailWaitsForTextureThenReadsTopRowFirstAfterReimport`
- `PreviewBetweenSceneSubmissionAndDrawPreservesQueueModeAndStats`
- `CustomOpaqueOutputAlphaCannotMakeThePreviewTransparent`
- `ThumbnailRejectsFailedTextureUploadDespiteAnOlderGpuTexture`

Surface picker case `UnprojectsNearestDepthAtPixelCentreAndClearsPreviousHit`
now runs on both backends for both projections. Every ID/world-coordinate
assertion is unchanged; GL hostile-depth/ImGui restoration checks remain on GL.

The following seven existing cases only move to native GL fixture names, so
Vulkan does not claim an editor integration implemented before phase 4. Their
original native assertions are preserved:

- `MaterialSceneNativeGL.DeferredSubmissionDoesNotQueryDriverRasterState`
- `MaterialSceneNativeGL.FrameUniformsUploadOnlyAtDebugViewTransitionsIncludingLitPreview`
- `MaterialSceneNativeGL.ImGuiRestoresDepthCullBlendFunctionsAndWireframeModeAfterPreviewOutline`
- `MaterialPreviewNativeGL.ImGuiRestoresHostileNativeStateIncludingBindingsAndBufferRanges`
- `MaterialPreviewNativeGL.PublishedShaderReloadAndCacheEvictionDoNotRetainGpuPointers`
- `MaterialPreviewNativeGL.PreviewReplacementsAreReleasedWithoutBeginFrame`
- `MaterialPreviewNativeGL.ReimportRetainsRecordedProgramsTexturesAndUniformBuffersUntilNextFrame`

`RenderAssetCacheGL.UploadMeshCreatesIndexedVertexArrayWithEngineLayout` remains
GL-specific because its assertions query VAO state. Neutral vertex/index layout,
base-vertex, instancing and actual pixels are exercised by conformance on both
backends. Editor texture/startup/native UI fixtures remain unchanged GL fixtures.

### CPU expectations for implemented Vulkan uploads

Two exact error-code checks change because Vulkan upload is now implemented.
With Vulkan selected but **no initialized device**, both must return
`InvalidArgument`, as GL does, rather than claiming an unimplemented backend.
The `None` backend still returns `NotImplemented`; invalid input validation and
all other assertions remain unchanged. No CPU test creates Vulkan or performs
source IO as a result of this change.

- `MeshProductUpload.RejectsAbsentContextAndUnsupportedBackends` becomes
  `RejectsAbsentDeviceAndUnselectedBackend`.
- `TextureProductUpload.RejectsAbsentContextAndUnimplementedBackendWithoutSourceIo`
  becomes `RejectsAbsentDeviceAndUnselectedBackendWithoutSourceIo`.

### New tests and concrete mutation targets

Every new case runs on both backends. Vulkan-specific diagnostics check native
creation counters without adding native API to the neutral interface; elapsed
time is reported, never asserted.

| Case | Plausible bug that fails it |
| --- | --- |
| RHIConformance.HeadlessContextInstallsTheChosenSharedDeviceAndRejectsReplacement | Renderer keeps a GL shared device for a Vulkan fixture; live shared device replaced; null device accepted |
| RHIConformance.InstancedTriangleStripUsesBindingDivisorAndFirstInstance | Per-instance binding treated as per-vertex; firstInstance ignored; triangle strip mapped to a list and half a tile remains clear |
| RHIConformance.PointTopologyAndFirstVertexSelectOnlyTheRequestedPoint | Point list mapped to a line/triangle topology; firstVertex ignored and the wrong pixel is drawn |
| RHIConformance.RenderedTextureSamplingAndReplacementKeepRecordedDescriptors | Missing attachment-to-sampled layout transition (validation); wrong sparse binding 19; group-0 set cache ignores texture replacement or mutates an older recorded set |
| RHIConformance.SampledReadOnlyDepthAttachmentKeepsDepthAndPassesDepthTest | Wrong sampled depth aspect/layout; missing render-to-sample transition; writable attachment layout used for sampling; disabled depth test allows the blue failing draw; attachment Store omitted from write tracking (sync validation) |
| RHIConformance.DepthWritesAndAttachedSamplingWithinOneScopeAreOrdered | Clear/write/read-only uses planned as a single incompatible native scope; layout barrier emitted inside dynamic rendering; resumed viewport/scissor/attachment content lost |
| RHIConformance.CachedPipelinesRenderTheirOwnTargetFormats | Description cache omits the target format and returns an LDR pipeline for a full-float HDR draw; HDR gets clamped/rounded |
| MaterialSceneGL.PerDrawGroupsReusePoolsAndPipelinesAfterWarmup | Pipeline created per draw; pool created per group; native sets not shared across object ranges; retired sets leak and pool count grows |
| ScenePassGL.WarmFramesKeepSpriteGridAndSkyPipelinesAfterRetirement | Weak device cache is mistaken for renderer ownership and every immediate pipeline is recreated after frame retirement |

For range selection and all remaining raster/stencil/blend/push fields, the
original conformance mutation targets and exact assertions remain in force.
Per-backend reference limits, strict byte-oracle rationale and the measured
12.023 -> approximately 5.1 microsecond group-0 decision are recorded in
[the backend report](../Kans3D/src/Kans3D/RHI/Vulkan/README.md#phase-3b-graphics-and-renderer).

Final phase-3b validation: CPU **404 pass / 0 skip**; OpenGL **156 pass / 0 skip**
(including the complete software GL 4.3 bundle); Vulkan **131 pass / 0 skip**
with **zero validation/synchronization errors**. The Vulkan label includes
53 conformance, two safety and 76 renderer registrations. Both backends run
all twelve unchanged PNG references. Software GPU suites are serial; builds
use `-j2`, and the prescribed CPU suite uses `-j1`. No editor launch, protected
path/reference changes, CMake cache/preset changes, network, commits or pushes.

The final precompute-program reuse correction also passes all 19 affected
environment/reference cases on each backend, serially. All 21 editor source
translation units pass fresh syntax-only Debug checks (26 commands including
test-target variants), two at a time, without linking or editing editor outputs.
An audit of all 46 original conformance bodies finds no change beyond removing
skips and formatting. Changed-line `git clang-format` and `git diff --check` are
clean; original untracked user content is preserved.

## Phase 4 window and UI tests

Existing conformance assertions, reference images and scene bounds are unchanged.
New test sources are additive. All Vulkan processes pin both loader variables to
lavapipe, refuse unintended device types, enable Khronos/synchronization validation
and route every error to gtest. Window tests prefer `VK_EXT_headless_surface`; if
unavailable they use hidden GLFW windows, otherwise explicitly skip. OpenGL WSI
requires an accessible display; existing EGL/llvmpipe UI coverage does not.

| New test | Concrete plausible regression caught |
| --- | --- |
| `BackendSelection.DefaultsToOpenGLAndCommandLineOverridesTheIni` | Startup ignores the ini or CLI override, or defaults to Vulkan on the user's machine. |
| `BackendSelection.RejectsInvalidEmptyAndDuplicateSelections` | Typo/empty backend values silently fall back; conflicting CLI flags are accepted. |
| `BackendSelection.CaptureArgumentsReachApplicationStartupAndRejectMalformedSchedules` | Entry-point arguments never reach Application, capture count silently accepts trailing text/overflow, or capture scheduling lacks a path. |
| `BackendSelection.FilesystemStartupRewritePreservesTheBackendSetting` | Computing the startup root hash drops a BOM/whitespace backend key and silently returns to OpenGL on the next startup. Exercises the real filesystem rewrite in a temporary project. |
| `VulkanSafety.CpuForcingOverridesTheEditorsExplicitHardwareOptIn` | CPU-forcing environment policy loses to application hardware opt-in; uses synthetic device types, no hardware enumeration. |
| `WindowPresentation.ResizeVSyncAndCaptureReadTheCurrentImageWithUnencodedUIColours/{OpenGL,Vulkan}` | Recreation leaks or keeps stale views, presentation misses rendering completion (sync validation), SRGB surface attachment shifts UI colours, or capture reads another swapchain image/BGRA order/wrong rows. Twelve distinct frames/resizes/VSync changes; requests alternate before/after UI rendering. |
| `WindowPresentation.ReleasedTextureSurvivesUIEndAndInterveningIdleUntilPresent/{OpenGL,Vulkan}` | Exported texture/descriptor is released before Vulkan main presentation, or upload-to-UI sampling layout is missing. Real captured pixels and weak lifetime before/after GPU completion. GL keeps its existing completion boundary. |
| `WindowPresentation.RenderTargetExportPreservesTheEditorsOpenGLUVConvention/{OpenGL,Vulkan}` | Viewport and material preview become vertically mirrored with the editor's unchanged UVs. Distinct native upper/lower rows are normalized from device coordinates. |
| `WindowPresentation.MinimizedSurfaceDefersCaptureUntilTheRestoredFrame/{OpenGL,Vulkan}` | Acquiring/recreating at zero extent, writing an old frame while minimized, or losing a pending capture/VSync change. Requires headless surface for deterministic zero extent. |
| `WindowPresentation.HeadlessPlatformWindowsShareTextureIDsAcrossResizeAndPresentation/{OpenGL,Vulkan}` | Detached windows use incompatible pipelines, free shared descriptors early, keep stale views on resize, or leave renderer callbacks pointing at destroyed state. Drives the production renderer callbacks with independent headless surfaces; GLFW-specific behavior still needs a compositor smoke run. |
| `VulkanImGui.DescriptorsRetainReleasedImagesAndTrackUploadAndAttachmentLayouts` | Native IDs use incompatible layouts/views/samplers, or textures die across intermediate WaitIdle; exact linear/attachment pixels and existing one-byte SRGB decode allowance. |
| `VulkanImGui.FontsAndUnencodedUIColoursRenderToTheExpectedRows` | Missing font upload, encoded UI attachment, inverted rows, or native scene state bleeding into ImGui. |
| `VulkanImGui.RenderTargetUVAdaptationRestoresDrawListsAndPreservesUploadedIcons` | Render-target adjustment mirrors authored icons, mutates persistent draw lists, or ignores nearest sampling. Exact independent upper/lower pixels; redraw of the same data. |
| `VulkanImGuiParity.OpaqueAlphaAndFontPixelsMatchTheOpenGLUI` | UI colour/blend/font-coverage/origin differs between the actual GL and Vulkan backends. Renders both serially with pinned llvmpipe/lavapipe and compares every RGBA8 byte exactly. |
| `VulkanWindowFrames.SlotReuseRetiresOnlyCompletedResourcesAtBoundsOneTwoAndThree` | EndFrame still retires synchronously, ring slots reset pools before completion, resources retire too early or leak. 24 frames each at bounds 1/2/3 using production asynchronous retirement; weak lifetimes, live allocation count and validation. Timings are reported without load-dependent thresholds. |

Tests never launch KansEditor. A freshly compiled/linked editor may be produced in
`/tmp/kans-phase4-editor` from the unchanged editor source commands, two translation
units at a time, without executing the protected editor target's post-build resource
copies or modifying its existing executable/project directory.


Final phase-4 label counts: CPU 408 passed; OpenGL 156 passed + 5 explicit
WSI skips (161 registered); Vulkan 142 passed, including headless swapchains,
offscreen UI and exact cross-backend UI parity. Zero unexpected validation errors.
All original renderer references and assertions pass unchanged.


## Phase 2d regression additions

The approved default lighting/HDR change is documented in
`Kans3D/src/Kans3D/Renderer/Graph/README.md`. Reference provenance, the deliberate
new full-frame oracle and the four measured HDR-only Vulkan bounds are in
`references/README.md`. The comparator still allows zero outlier pixels.

Eight new CPU cases use authored values/legacy bytes rather than rendering
snapshots:

| Case | Concrete plausible bug caught |
| --- | --- |
| LightingHdr.NewlyCreatedLightComponentsHaveDefinedRadianceAndSpecularDefaults | Newly consumed specular/ambient fields remain uninitialized |
| LightingHdr.DistinctDiffuseRadianceAndZeroUnusedSlotsPreserveLegacyPrefix | One light copied into every slot, ambient used as radiance, stale unused slots or moved prefix offsets |
| LightingHdr.ExtractSelectsNearestSixteenWithUuidTiesAndWorldPositions | Last-light-only collection, local positions, unstable ties, wrong camera or ignored limit |
| LightingHdr.DirectionalPackingNormalizesTravelDirectionAndSeparatesAmbient | Directional upload dropped, wrong normalization or ambient mixed into radiance |
| LightingHdr.ExtractionSelectsDirectionalUuidAndClearsRemovedLights | Iteration-order selection or removed lights retained in subsequent frames |
| LightingHdr.OutputSpaceDefaultsAndCodecRetainExplicitHdrAndRejectInvalidFields | Existing manifests default to HDR, explicit declaration dropped by codec or invalid/duplicate declarations accepted |
| LightingHdr.PublishedVersionSixFramePrefixLoadsUnmodified | Real v6 products rejected or frame prefix interpretation changed |
| SceneProductModelTest.CameraExposurePersistsAndLegacyScenesUseDefault | Exposure not saved/restored or older scenes lose the .8 default |

Ten new GPU cases run on **both** backends through production import/products,
RHI material binding, SceneRenderer or MaterialPreviewRenderer:

| RendererReferenceGL case | Concrete plausible bug caught |
| --- | --- |
| HdrValuesAboveOneAndExposureHaveAnalyticFilmicBytes | RGBA8 working target clamps 4 before the curve; exposure ignored, HDR curve skipped or applied twice. Exact half readback plus analytic bytes at three exposures |
| PointPbrUsesDistinctDiffuseRadianceCountAndSpecularScale | Four copies, ambient radiance, specular multiplier ignored or fifth light lost. Known-distance .5 Lambert/GGX surface with black environment and analytic bytes |
| PbrLightCountIgnoresPoisonedUnusedSlots | Fixed-size shader loop ignores count and lights from deliberately bright unused slots |
| AppendedCameraExposureAndLightCountAreBoundToShaders | CPU-only upload tests pass while actual appended uniform offsets misbind |
| DirectionalPbrReachesShadersWithAmbientAndSpecular | Directional shader contribution absent/reversed, ambient attenuation or specular scale wrong. Analytic isolated direct+ambient expectation |
| PublishedV6ProductsBindPrefixAndFourDistinctLights | Old product loads but falls back or misbinds the expanded uniform buffer. Unmodified v6 bytes, four-slot sum and exposure-independent display bytes |
| DisplayMaterialsAndBlendsRemainByteExactAcrossHdrTransitions | Legacy display material tone-mapped twice, display blends moved or inverse prefill changes background bytes. Exhaustive 256 codes across five exposures, exact whole images |
| DebugViewsBypassExposureAndPostPassExactly | Base colour, normals or wireframe routed through the curve. Exact whole images at .1/4 exposure and no post pass |
| PreviewUsesSceneHdrCurveAndExplicitExposure | Thumbnail remains on the old path, ignores exposure or curves display output. Analytic HDR and exact display bytes |
| PreviewCacheTracksOutputSpaceEvenWithIdenticalBytecode | Unsaved preview cache ignores output-space-only edits and uses stale target/resolve behavior |

The six HDR reference cases additionally capture actual RGBA16F values. Five
without subsequent selection composition check every RGB pixel against the
analytic post curve within one byte. Selection retains its strict full reference
and independent numeric picking assertions. The replaced
`FullRuntimeFrameIsByteIdenticalToPhaseTwoD` keeps exact backend attachment hashes,
feature/pass order, depth/stencil and transient-alias checks. Existing shader
unit expectations now explicitly test linear Standard output; their numeric
precision is unchanged. Legacy ambient-only material-state fixtures now author
intended diffuse intensity explicitly.


Phase 2d final validation after the clean rebuild: CPU **416 passed / 0 failed**
(52.73 s); serial llvmpipe OpenGL **167 passed / 5 skipped / 0 failed**, 172
selected entries (214.19 s), including the GL 4.3 extension bundle. The five
existing OpenGL window-presentation skips lack an accessible X11/Wayland surface.
Serial lavapipe Vulkan: **153 passed / 0 skipped / 0 failed** (136.50 s),
including all headless window-presentation cases.
Changed-line `git clang-format` and `git diff --check` are clean. GPU validation
logs and HDR audit artifacts are retained under
`build/linux-gcc/test/reference-output/phase2d/`.
