These 128×128 RGBA8 PNGs baseline the current OpenGL renderer on Mesa llvmpipe.
Rows are stored top first. Material shaders encode their LDR output explicitly;
the framebuffer does not perform an additional sRGB conversion.

The scenes live in `../RendererReferenceOpenGLTests.cpp`. They publish real mesh,
model, texture and material Products into an AssetManager and render through
SceneRenderer/RenderAssetCache. Toon is imported from the editor's shaderprog
with ShaderImporter, including its declared defaults. The preview uses
MaterialPreviewRenderer::RenderThumbnail. No scene shader reads time.

| Reference | Regression behaviours |
| --- | --- |
| standard-ibl-point | Perspective depth, asymmetric sRGB albedo, authored mips, tangent normal map, IBL and point lighting, roughness prefilter sampling |
| alpha-depth-sort-backfaces | Opaque depth, textured mask cutoff, near-before-far submissions sorted across models, overlapping blends without depth writes, double-sided and culled back faces |
| emission | Textured emission multiplication and HDR-to-LDR tone mapping |
| toon-imported | Imported shader defaults, material blocks, sampler binding, normal matrix, cel threshold and rim lighting |
| debug-base-color | sRGB decoding/encoding and tiled/offset UV orientation |
| debug-normals | Tangent basis, normal texture orientation and object normal transform |
| debug-wireframe | Polygon raster mode, triangle coverage and restoration |
| selection-outline | Off-centre silhouette mask, screen-space border and fill composition; associated picker checks IDs and pixel-centre world positions numerically |
| preview-sphere | Real preview sphere/tangents, fixed yaw/pitch, IBL, point light, checker background and top-first thumbnail readback |
| grid | Perspective XZ geometry, line discard/coverage and alpha composition |
| skybox | Cubemap face and view orientation, HDR sampling and LDR conversion |
| renderer2d-sprite | Real batched Renderer2D, texture unit selection, tiling, asymmetric UVs, transform, tint and straight-alpha blending |

Lit/IBL/tone-mapped references allow a maximum channel difference of 2/255;
Toon, debug views, grid and sprite allow 1/255. These budgets cover floating point
filtering and final byte rounding. All comparisons allow **zero** pixels beyond
their channel budget, including alpha, edges and background; there are no masks,
coverage exclusions or auto-selected tolerances.

The generated 8×4 RGBE environment has unequal coloured lobes. The production
precompute shaders and integration sample counts are unchanged; only offscreen
output sizes are selected: cubemap 32, irradiance 8, prefilter 32 (five roughness
levels), BRDF LUT 32. Editor defaults remain 512/512/128/512. A tiny input HDR alone
would not reduce the renderer's fixed precompute output cost. The optional
`SceneRendererSpecification::AuxiliaryShaders` provides the same resource
shaders without requiring legacy Renderer/window initialization; the existing
`GeometryShaders` override retains its original geometry-only scope.

Build and compare (GL tests must always run serially on software rendering):

```sh
cmake --preset linux-gcc
cmake --build build/linux-gcc --config Debug -j2
cd build/linux-gcc
LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=llvmpipe GALLIUM_DRIVER=llvmpipe \
  ctest -C Debug -L OpenGL --timeout 300
```

To explicitly regenerate only these references:

```sh
cd build/linux-gcc
KANS_UPDATE_REFERENCES=1 LIBGL_ALWAYS_SOFTWARE=1 \
MESA_LOADER_DRIVER_OVERRIDE=llvmpipe GALLIUM_DRIVER=llvmpipe \
  test/Debug/OpenGLTextureTests --gtest_filter='RendererReferenceGL.*'
```

Review regenerated PNGs before accepting them. Without the update flag a missing
reference fails. Mismatches write `<name>.actual.png` and `<name>.diff.png` in
`build/linux-gcc/test/reference-output/`; the failure reports paths, differing
pixel count/fraction and maximum channel difference. The diff amplifies absolute
channel differences by eight and makes alpha-only changes visible in red.

Validation on this machine: the 12-case file initially ran in 2.885 seconds,
including context setup, IBL precomputation and shader compilation. Each CTest
case runs in its own process, so the CTest label includes repeated setup cost.
The CPU suite passed 374 cases; the OpenGL label passed 69 entries (including
the GL 4.3 extension-path entry, which runs the whole GPU binary again).

To prove the comparisons detect rendering failures, both engine vertex shader
compile paths were temporarily changed to call the original entry point and
then execute `gl_Position.y = -gl_Position.y`. All twelve comparisons failed
with unchanged PNGs and tolerances. This also perturbs environment capture; the
mutation was in renderer compilation, not in the comparator or image readback.

| Reference | Pixels outside channel tolerance (of 16,384) | Maximum channel difference |
| --- | ---: | ---: |
| standard-ibl-point | 4398 | 220 |
| alpha-depth-sort-backfaces | 5691 | 210 |
| emission | 4490 | 220 |
| toon-imported | 5056 | 240 |
| debug-base-color | 4069 | 204 |
| debug-normals | 4401 | 250 |
| debug-wireframe | 3151 | 229 |
| selection-outline | 1456 | 199 |
| preview-sphere | 6665 | 120 |
| grid | 404 | 112 |
| skybox | 16384 | 6 |
| renderer2d-sprite | 3769 | 95 |

A separate mutation disabled blending for the transparent draws in
SceneRenderer. The alpha reference failed with 3,034 pixels outside tolerance
(18.52%) and maximum channel difference 117. Both mutations were restored.
Diagnostic PNGs, logs and the Y-flip patch are retained locally under
`build/linux-gcc/test/reference-output/y-flip/` and `blend-disabled/`.

Final validation after restoring the faults and separating the auxiliary shader
option: 374/374 CPU cases (41.67 s) and 69/69 serial OpenGL CTest entries
(64.89 s), including GL 4.3, passed. The `-j2` build succeeded with no warnings
from the changed code. Final validation logs are in the same local reference
output directory. Editor build output was redirected locally to
`build/linux-gcc/phase0-editor-build/` to protect the existing user project;
this override is only in this build cache, not a repository CMake default.
