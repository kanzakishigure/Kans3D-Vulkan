# Shader programs and material passes

`KansShaderProgram 1` remains the source format. Existing files keep their main
pass at top level (`vertex=...`, `fragment=...`), with the same defaults, hints,
texture fallbacks and parameter groups. A standalone compute program cannot have
additional material passes. Paths are relative to the manifest and remain inside
the asset root; quoted includes participate in input discovery for every stage.

Additional passes are ordered, named blocks. Keys have no surrounding whitespace;
blank lines and whole-line `#` comments are allowed. Names/tags are case-sensitive
ASCII letters, digits or underscores, at most 256 bytes. `Main` is reserved.
Names must be unique; several passes may share a tag and are drawn in declaration
order for each visible material draw. There are at most 32 extra passes.

```text
KansShaderProgram 1
vertex=Surface.vert
fragment=Surface.frag

default OutlineWidth = 0.08

pass Hull
vertex=Hull.vert
fragment=Hull.frag
tag=Outline
cull=front
depth_test=on
depth_compare=less_equal
depth_write=off
blend=off
color_write=rgb
endpass
```

Every block requires `vertex`, `fragment`, `tag` and `endpass`. Other keys are
optional. Duplicate/unknown fields, nested blocks and invalid values are errors.
Global defaults/hints/textures/groups belong outside blocks and describe the shared
main material interface. `OutlineWidth` is an ordinary uniform in that interface;
the engine does not reserve or interpret it.

| Key | Values | Default |
| --- | --- | --- |
| `cull` | `none`, `front`, `back` | `back` |
| `depth_test`, `depth_write` | `on`, `off` | `on` |
| `depth_compare` | `never`, `less`, `equal`, `less_equal`, `greater`, `not_equal`, `greater_equal`, `always` | `less` |
| `blend` | `on`, `off` | `off` |
| `src_color`, `src_alpha` | factors below | `one` |
| `dst_color`, `dst_alpha` | factors below | `zero` |
| `color_write` | unique letters from `rgba`, or `none` | `rgb` |

Factors: `zero`, `one`, `src_color`, `one_minus_src_color`, `src_alpha`,
`one_minus_src_alpha`, `dst_color`, `one_minus_dst_color`, `dst_alpha`,
`one_minus_dst_alpha`. Blend operations are additive. Extra passes use filled
triangles, the engine's front-face convention, and no stencil operations. They
own their state regardless of the main material's alpha mode/double-sided setting.
State applies to every scene colour attachment; writing RGB by default preserves
scene alpha. Only fragment location 0 is part of the material contract.

## Validation

Multi-pass programs must obey `MaterialShaderInterface` in every pass: engine mesh
vertex input locations/types, vertex+fragment stages, one vec4 fragment output at
location 0, engine set 0, material set 1, and no other sets. Each pass's vertex
inputs may use any subset of the fixed engine mesh layout.

Set 0 must have exactly the main pass's merged descriptor layout and the engine
contract. Set 1 may omit bindings or block members, but each retained descriptor
must match its main binding, kind, name, count and image dimension. Retained block
members must match names, scalar types, offsets, vector/matrix shapes, array counts
and strides, matrix strides and orientation, including nested structs. A subset
block may be smaller, never larger. Omitting a middle member requires explicit
GLSL offsets to preserve subsequent offsets. Changing a layout to pack a subset
more tightly is rejected. Main resource names/layouts across stages must agree.

Validation re-inspects bytecode at import, product encode/decode and GPU upload;
caller-supplied reflection is never trusted. Errors identify the pass and offending
set/binding. Single-pass generic shader products keep their prior validation rules.

## Product and runtime

KSHD v6 keeps the v5 prefix and appends a pass count, then each pass's length-prefixed
name/tag, ten little-endian uint32 state words (cull, depth test, compare, write,
blend, source/destination colour, source/destination alpha, colour mask), and a
length-prefixed nonrecursive v5 code-only KSHD record. Reflection is derived from
validated SPIR-V rather than serialized. Readers accept v1–v5 with no extra passes;
the importer version is 11 so existing sources rebuild. The committed
`test/data/multipass/single-pass-v5.kshd` was generated with the original v5 codec
before this change, not synthesized with the new encoder.

`GpuShader` owns the main program/cache and one program/state/cache per extra pass.
All share the main engine/material layouts and material bind group recipe.
`MaterialPassFeature(tag, point)` is an engine feature; it consumes the prepared
visible material order and records matching passes with engine frame/object data,
including transforms, normal matrices and main material render parameters. It
never walks all scene entities or cache contents. Excluded/unready draws stay out.
The current renderer has no model visibility flag or frustum culling; offscreen
submissions retain the established behavior.

Every SceneRenderer registers `MaterialOutline` for `Outline` at `AfterTransparent`,
after opaque/mask/blend depth and compatibility debug geometry, before post-process
and editor overlays. This boundary preserves Forward's one immutable material
batch. An inverted hull tests the populated depth, so its back shell does not
replace the surface interior or foreground occluders. Transparent compositing
policy can be revisited when Forward separates its batches. Without a matching
prepared draw, the feature creates no commands, uniforms, pipelines or graph pass.
Other tags (for example `ShadowCaster`) need an explicitly registered feature at
an appropriate point/target when that pipeline functionality exists.

The complete inverted-hull example lives in `test/data/multipass`, keeping shipped
Toon/ToneCharacter shaders and user materials/scenes unchanged. Its vertex stage
expands along world-space normals by the ordinary `OutlineWidth` parameter.

## Phase 2d: material colour output

The optional top-level `output_space=linear_hdr` declares scene-linear RGB;
`output_space=display` is the default when omitted, including all published v1–v6
products. Alpha is straight coverage in either space. The declaration applies to
main and additional passes. Standard declares `linear_hdr` and returns unclamped
radiance. Custom shaders which call `KansToneMap`, gamma encode, or deliberately
choose display colours should keep `display`. Toon is a minimal HDR example:
its manifest declares `linear_hdr` and its fragment returns linear colour.
ToneCharacter remains unchanged and display-referred, retaining its artistic
gamma curve. No user material or NPR project is migrated.

Importer version 12 rebuilds source shaders against the appended engine includes.
Product version 7 appends the output-space enum after the v6 pass records. Readers
retain v1–v6, defaulting their output space to display. Pass bytecode envelopes
remain bounded v5 records. The fixture `test/data/multipass/lights-prefix-v6.kshd`
was compiled and written by the original v6 codec before this change; its actual
SPIR-V contains the seven-member, 288-byte KansFrame prefix. CPU validation and
both backend draw tests use these bytes unchanged.

`KANS_LIGHT_COUNT` remains 4 for published source compatibility. New shaders use
`KansPointLightCount()`, `KansPointLightPosition(i)`, `KansPointLightRadiance(i)`
and `KansPointLightSpecular(i)` for up to `KANS_POINT_LIGHT_LIMIT` (16) lights.
`KansDirectionalToLight()`, `KansDirectionalRadiance()` and
`KansDirectionalAmbient()` expose the directional light; its stored direction
is the direction of light travel, so the to-light vector is its negative.
`KansExposure()` reads the camera's exposure multiplier. `KansShadePbr` consumes
these helpers and returns linear HDR. `KansToneMap` remains the legacy Reinhard
plus 1/2.2 gamma helper for display-referred custom shaders.
