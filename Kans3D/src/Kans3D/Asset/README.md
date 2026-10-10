# 资源系统

源文件（Source）由编辑器导入为引擎格式的 Product；运行时只加载 Product，
从不读取源文件。

```text
编辑器 / 工具                                  运行时
assets/x.obj + x.obj.kmeta                    AssetID
  → Project::BuildAsset                         → AssetManager（Registry 查找、读取、解码）
      → Importer::DiscoverInputs                  → Ref<const MeshAsset/…>（不可变 CPU 数据）
      → 先构建被引用的源（纹理、Shader…）            → RenderAssetCache（上传 GPU，渲染线程）
      → Importer::Import → 暂存 Product              → SceneRenderer::SubmitModel
      → ProductStore::Publish（原子发布）
```

## 身份

- `SourceAssetID`：保存在源文件旁的 `.kmeta` 中，随文件移动而不变。
- `AssetID = SourceAssetID + LocalID`：一个源可产生多个 Product。导入器用对象键
  （如 `mesh:4:Body:...`）分配 LocalID，`.kmeta` 的 `local_ids` 记录全部键（包括
  已删除对象），所以重导入、调整顺序或丢失 `Cache/` 都不会改变 AssetID。
- 场景、材质等持久化引用只保存 AssetID。

## 导入与构建（`Importer/`、`Project/`）

- `ImporterRegistry` 按扩展名或 `.kmeta` 中指定的名称选择导入器。
- `DiscoverInputs` 报告导入还会读取的源文件（.mtl、glTF buffer、shader include）
  和引用的其他资源（模型纹理按路径，材质的 Shader/Texture 按 AssetID）。
  `Project` 先递归构建被引用的源，并检测循环。
- 构建键（`ImportFingerprint`）由源及输入文件内容、导入设置、导入器版本、目标、
  被引用 Product 的摘要和 LocalID 组成，不含路径。键未变且文件完整时跳过导入。
- 导入后重新校验输入文件；导入期间被修改则构建失败，不发布。
- 模型缺失纹理时给出警告并留空该材质槽；被用作法线/数据的纹理会自动获得非 sRGB
  导入设置（仅当该纹理尚无设置时）。
- 尚不支持的模型内容（动画、蒙皮、变形目标、内嵌纹理、额外 UV 通道）会被跳过并
  给出警告，静态几何仍然导入（蒙皮网格使用绑定姿态）。

## 模型材质 remap 与提取

- 模型设置 `scale` 为正数（默认 1）。`.kmeta` 的 `settings` 可保存
  `material.<slot key>: <Material AssetID>`；slot key 是完整稳定对象键（例如
  `material:7:Surface`，同名槽使用独立后缀），不是名称或数组索引。清除该设置恢复内嵌材质。
- remap 是声明的可选 Product 输入：先构建外部材质，纳入构建指纹与依赖关系。
  缺失、非 Material、未产生指定 Product 或构建失败时警告并使用内嵌材质。
  内嵌 Material 始终发布；KMSH v2 同时保存 bound Material 与原始槽身份，
  场景 Mesh override 优先，其次原始槽 override，再其次 bound Material override。
  KMSH v1 仍可读取，原始槽身份取其 Material；模型导入器版本为 11。
- `EditorAssetImportService::GetModelMaterialSlots(source)` 返回最后发布的槽：
  `Key`、`DisplayName`、`EmbeddedMaterial`、可选 `Remap`；尚未在 editor target
  导入的模型返回错误，已删除槽的保留 LocalID 不会出现在列表里。
  `SetModelMaterialRemap(source, slotKey, optionalMaterial)` 写入或清除设置。
- `ExtractModelMaterials(source, directory, slots = {})` 提取所选槽（空列表为全部），
  跳过已 remap 的槽。directory 相对 asset root 或为其内部的绝对目录，必须可被扫描。
  从已发布的内嵌材质及其实际 Shader 反射读取参数、纹理 ID 与 render state，
  用 `SerializeMaterialSource` 写入 `<ModelStem>_<MaterialName>.material`，
  文件名清理非法字符，冲突时加数字后缀，不覆盖源文件或已有 sidecar。
  注册每个新源及 `.kmeta` 后，一次提交全部 remap；失败保留原设置。
  返回 `ModelMaterialExtractionResult` 的 `Created`（含文件、源 ID、Material ID）、
  `Skipped`、`RemapsApplied` 和可选 `Error`；操作开始后的失败也报告已写文件，
  调用者须同时检查 variant 错误与 report.Error。结构参数或未绑定纹理暂不能转换为
  `.material` 的命名 schema，会报错而非丢失数据。
- 上述 service API 仅允许 owner thread 且没有运行中的构建，使用 `EditorAssetTarget`。
  修改或提取只保存配置，调用者随后 `Start(modelSource, options)` 构建；会先导入新材质。
  修改独立 `.material` 后构建该材质即可影响所有引用它的模型。

## Product 存储（`Storage/ProductStore`）

- 每次发布把某个源在某个目标下的完整 Product 集合写入新的 `generations/` 目录，
  再原子替换 `catalog.yaml`。失败或取消时保留上一次成功的集合；进程在任何时刻
  被杀死都只会看到旧集合或新集合之一。
- 发布前用运行时 Loader 解码每个暂存 Product 进行校验。
- catalog 只是缓存：无法读取时从空开始，重新导入即可恢复，身份不受影响。
- 进程内仍被 `ProductRecord` 引用的旧 generation 不会被回收。

## 运行时（`Manager/`、`Runtime/`、`Loader/`、`Product/`）

- `AssetManager`：按 AssetID 缓存 CPU 资源，同一 ID 同时只加载一次。`Load<T>` 阻塞，
  `Request<T>` 交给工作线程并可每帧轮询。`SetRegistry` 只驱逐内容变化或被删除的
  资源，已取得的 `Ref` 仍然有效。
- Product 格式：KMSH（网格）、KTEX（纹理）、KSHD（SPIR-V Shader）为带版本、
  显式字节序的二进制；材质与模型为带 magic/version 的 YAML。
- 材质纹理语义使用引擎的 `MaterialTextureSemantic`，与 Assimp 版本无关。

## 渲染（`Renderer/Resource/RenderAssetCache`、`SceneRenderer`）

- `RenderAssetCache` 在渲染线程把 CPU 资源转换为 GPU 网格、材质和纹理；新版本
  加载完成前继续使用旧资源；长时间未使用的资源会被释放。
- `ProductModelComponent` 只保存 Model 的 AssetID 和材质替换。所有材质都通过
  SPIR-V 材质 Shader 路径绘制；Shader 未指定、加载中或失败时使用引擎 Standard。
  均可叠加描边、模板、拾取和调试视图；调试视图用 Standard 绘制。

## 材质与材质 Shader（`Importer/MaterialSource`、`Runtime/MaterialShaderInterface`）

- 材质只有一个模型：Shader + 命名参数 + 命名纹理 + render state。`.material`（YAML v3，兼容 v2）的
  `shader` 为空时选择引擎内嵌 Standard（保留 ID `ffffffffffffffff:0000000000000001`），
  无需项目文件。Standard 和项目 Shader 使用同一编译、SPIR-V 反射、约定校验及
  GL 交叉编译流程；`DescribeMaterial(nullptr)` 在 CPU 上提供其真实反射 schema、默认值和 hint。
- `render: {alpha_mode: mask, alpha_cutoff: 0.5, double_sided: true}` 适用于所有 Shader。
  `alpha_mode` 为 `opaque`（默认）、`mask` 或 `blend`；cutoff 默认 0.5、有限且非负，双面默认 false。
  glTF alphaMode/alphaCutoff、doubleSided 直接映射，未指定 doubleSided 时为单面；
  非 glTF 模型默认双面，保留旧模型头发、裙摆等单层表面的内侧；`.material` 仍默认单面并使用显式值。
  其他模型 0 < opacity < 1 使用 blend；非 glTF 的 opacity ≤ 0 或非有限值
  视为不可靠，恢复 opaque / alpha=1 并给出包含材质名的导入警告。
  场景先画 opaque/mask，再按每个 part 的世界包围盒中心到相机的距离从远到近画 blend（含拖放预览）。
  blend 测试深度但不写深度，颜色使用 SRC_ALPHA / ONE_MINUS_SRC_ALPHA；清屏设置 alpha=1，材质绘制禁止写 alpha，
  包括自定义 Shader 的 opaque/mask 输出。
  双面材质不剔除背面，Standard 翻转背面法线。
- 参数是 set 1 uniform block 的成员，纹理是 set 1 sampler2D；导入后写入 KMAT v4
  绑定配方。Standard 提供 `BaseColor`、`Metallic`、`Roughness`、`EmissiveColor`、
  `EmissiveIntensity`、`NormalStrength`、`OcclusionStrength`、`UVTiling`、`UVOffset`、`VertexColorStrength`，
  以及 `BaseColorMap`、`NormalMap`、`MetallicRoughnessMap`（G 粗糙度 / B 金属度）、
  `OcclusionMap`（R）、`EmissiveMap`。UV 先缩放再偏移；法线使用顶点切线及其手性，
  切线退化时使用导数 TBN。自发光颜色乘强度，有贴图时再乘贴图。
- `.shaderprog` 用 `default <名字> = <数值...>` 和 `hint <名字> = toggle | color |
  range <min> <max>` 声明默认值和编辑控件；未声明的参数为 0。用
  `texture <sampler名字> = white | black | normal` 声明未指定或未加载纹理的替代值
  （存入 KSHD v4；未声明时为白色）。Standard 的法线替代值为精确平面法线，
  其余（含自发光贴图）为白色。Standard 默认自发光颜色为 0，不增加光照。
  `VertexColorStrength` 默认 0，保持旧模型外观；设为 1 时使用顶点颜色。
- 模型材质映射为 Standard，包含 Assimp 的自发光因子；有自发光贴图且导入因子为黑色时使用白色因子。旧 `.material` 的
  `base_color`、`metallic`、`roughness` 和旧纹理槽名在读取时迁移，保存使用新名字。
  KMAT v1–v3 和 KSHD v1–v3 仍可读取；缺少 render state 的旧产品/源保持 opaque、单面。
  旧无配方材质在渲染缓存中转换为 Standard。
  导入器版本升级后项目会自动重导入，不把内建 Shader 当作项目依赖。
- 材质 Shader 是 `.shaderprog`（顶点 + 片元，Vulkan GLSL 450）：set 0 是引擎数据，
  用 `#include <Kans3D/Engine.glsl>` 声明（`u_Frame`：ViewProjection、相机位置、
  4 个点光源的位置、PBR 辐射度 `LightColors` 与漫反射强度 `LightDiffuse`、Time、DebugView；`u_Object`：Transform、NormalMatrix、
  `MaterialRender`（x: alpha mode 0/1/2，y: cutoff，z: double_sided）；IBL 的
  `u_IrradianceMap`、`u_PrefilterMap`、`u_BrdfLUT`）；顶点输入用
  `<Kans3D/VertexInput.glsl>`；`<Kans3D/Lighting.glsl>` 提供引擎 PBR 光照
  `KansShadePbr` 与 `KansToneMap`。片元在 location 0 输出 vec4。不符合约定的
  Shader 在导入材质时报错。
- 新 object block 为 144 bytes，旧 128-byte block 仍可校验。
  片元包含 `<Kans3D/Alpha.glsl>`，用 `KansAlphaClip(alpha)` 做 mask discard、
  `KansOutputAlpha(alpha)` 输出 blend 的 alpha 或 opaque/mask 的 1。Standard 的 alpha 为
  BaseColor.a × BaseColorMap.a × 顶点颜色项，debug 模式也遵循 clip/alpha。
  自定义 Shader 应调用这些 helper；现有不使用它们的 Toon/ToneCharacter 保持原来的着色。
- OpenGL 下由 spirv-cross 把 SPIR-V 转成 GLSL 4.30（`GpuShader`），set/binding
  展平为 GL 绑定点；材质参数打包进 uniform buffer。
- 示例：`KansEditor/assets/shaders/Toon/Toon.shaderprog`（卡通着色 + Ramp + 边缘光）；
  `ToneCharacter/ToneCharacter.shaderprog` 是旧版原神风格 `ToneCharactorShader`
  的移植（Diffuse + `_Light` + `_Ramp` 贴图，默认参数同旧 `MaterialUtils`）。

## 编辑器

- `EditorAssetImportService` 在后台线程运行一次构建；完成后编辑器更新运行时
  Registry。
- 把模型文件拖到视口会创建带 Model 组件的实体（必要时先导入）；属性面板可选择
  Model 或拖入源文件，并设置材质替换。
- 内容浏览器（`KansEditor/src/Panels/ContentBrowser*`）：重命名、移动（拖到文件夹）、
  复制/剪切/粘贴、复制副本、删除都经过项目，源资产随 `.kmeta` 一起移动，ID 不变；
  复制得到新 ID 并沿用原导入设置；删除前列出引用它的资产。卡片显示导入状态，
  预览栏显示 Source ID、导入器、产物和引用关系。
- 内容浏览器右键「Create Material」新建材质，双击 `.material` 打开材质编辑器：
  选择 Shader（或引擎 Standard）、编辑参数与纹理，保存后自动导入并在场景中生效。

## 尚未实现

骨骼/动画 Product、内嵌纹理导入、透明材质、材质节点编辑器、平台目标的 Product
变体、Pak/发布、文件监视自动重导入、Vulkan 渲染后端。
