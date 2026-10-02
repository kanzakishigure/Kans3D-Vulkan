# Content browser icons

Generated with the built-in imagegen tool, using `FBX.png` as a style reference.
The original FBX and folder icons are retained. `EditorResources::GetFileIcon`
maps case-insensitive extensions to these textures; unknown or missing extensions
use `File.png`. These icons indicate file categories, not importer support.

Model formats now have separate icons: `.obj` → `OBJ.png`, `.gltf` →
`GLTF.png`, `.glb` → `GLB.png`, `.blend` → `Blend.png`, `.dae` → `DAE.png`,
and `.mesh` → `Mesh.png`. FBX keeps its existing dedicated icon.

## Model format prompts

Generated with the built-in imagegen tool, using `Model.png` as the edit reference.
Each format uses this prompt with the substitutions below:

> Edit the reference into one distinct {name} file-format icon. Preserve the centered white folded document frame, charcoal inset, bottom white label band, proportions and transparent exterior. Replace the two cubes with {symbol}. Replace MODEL with exact uppercase label {uppercase name}. Large bold white geometric pictogram, readable at 48 pixels. Clean smooth antialiased edges, uniform charcoal inset, no distressed edges, no stray marks, no hatching, no shadows or holes. Match the monochrome icon family. High resolution square image.

| name | symbol |
| --- | --- |
| OBJ | a single bold wireframe cube |
| GLTF | three connected scene-graph nodes with a small cube above them |
| GLB | a solid isometric cube enclosed in a simple package outline |
| Blend | a stylized white Blender swirl with circular center and three outward arms |
| DAE | two interlocking outlined triangles |
| Mesh | a triangular polygon mesh with six large vertices |

## Final generation prompts

For Model, Scene, Material, Script, Audio and File, the prompt template was:

> Create one sibling asset-browser {name} icon matching reference folded document silhouette and white stencil style. Central pictogram: {symbol}. Bottom white band with clearly readable dark charcoal exact label {label}. Square canvas, centered document occupies 75% width and 90% height. White document outline and white pictogram, solid charcoal (#44443f) inset and lettering. Clean restrained rough edges only, NO hatching, NO diagonal stripes, NO texture outside document, NO shadows. Transparent outside document only. Simple bold shapes readable at 48px.

| File | name | symbol | label |
| --- | --- | --- | --- |
| Model.png | Model | two isometric cubes | MODEL |
| Scene.png | Scene | a landscape with a small cube and horizon | SCENE |
| Material.png | Material | a shaded sphere | MAT |
| Script.png | Script | bold angle brackets and slash | CODE |
| Audio.png | Audio | a musical note | AUDIO |
| File.png | File | three horizontal document lines | FILE |

For `Shader.png`, the original icon was replaced using the built-in imagegen tool with the previous shader icon as the edit reference:

> Edit the supplied asset-browser icon. Preserve its square canvas, centered white folded-document frame, upper-right fold, charcoal inset, white bottom label band, proportions and monochrome style. Replace the lightning bolt entirely with a single large upright equilateral triangle representing a rendered graphics primitive. Divide the triangle into three clearly visible triangular shading regions meeting at its center: white, light gray, and medium gray, with a clean white outer edge. Flat, bold, geometric and legible at 48px; no lightning, no sphere, no pyramid perspective. Replace GLSL with the exact word SHADER, bold dark charcoal lettering centered within the bottom white band with comfortable margins. Keep the inset uniformly charcoal, no holes or stray marks, no glow, no extra decoration. Preserve transparent background outside the document.

For `Image.png`:

> Create one sibling IMAGE file icon. Match reference white folded document silhouette, white mountain and sun pictogram on SOLID charcoal inset, bottom white band with bold dark charcoal exact label IMG. Square canvas centered document 75% width 90% height. Clean solid flat colors, NO stripes, NO hatching, NO distressed texture, NO extra marks. Transparent only outside document. Bold shapes legible at 48px.
