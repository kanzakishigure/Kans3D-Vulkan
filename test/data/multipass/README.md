# Multi-pass fixture

`Outline.shaderprog` is a deliberately unlit inverted-hull sample. `Tint` and
`OutlineWidth` belong to the main material block and serve both passes. The hull
uses the same engine transform/normal matrix, expands in world space, culls front
faces, tests existing scene depth, and leaves depth/alpha unchanged. Tests compile
these actual files through the production importer and codec.

`single-pass-v5.kshd` was encoded by the unmodified phase-2b library's KSHD v5
encoder before implementing v6 (not by downgrading a new product). It contains
`TestMaterialVertex`/`TestMaterialFragment` from `MaterialShaderTestUtilities.h`,
with `Strength=1` and `Tint=(1,.5,.25,1)` defaults. It is a compatibility input,
independent of the pixel reference images. Do not regenerate it with v6.
