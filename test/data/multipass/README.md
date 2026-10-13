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

`lights-prefix-v6.kshd` was compiled and encoded by the unmodified phase-2c
library's KSHD v6 codec before the phase-2d changes. It contains a seven-member,
288-byte KansFrame, reads the original ViewProjection/Object uniforms and sums
the four original LightColors slots divided by 16. It has no output-space field.
Its SHA-256 is `f6b75acf5ff5af5fad2574e37775431fa163ce114dc5bf258376ca3f1c4ec303`.
Do not recreate it by truncating a v7 product. CPU decoding/reflection and real
OpenGL/Vulkan rendering verify that these exact published bytes remain usable,
with display output and four distinct lights in the preserved frame prefix.
