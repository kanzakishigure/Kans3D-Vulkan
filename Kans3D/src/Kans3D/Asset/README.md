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
- `ProductModelComponent` 只保存 Model 的 AssetID 和材质替换。没有 Shader 的材质
  使用 `ProductPBR.glsl` 绘制（场景光源 + IBL），有 Shader 的材质用其自身程序绘制；
  均可叠加描边、模板和法线调试。

## 材质与材质 Shader（`Importer/MaterialSource`、`Runtime/MaterialShaderInterface`）

- `.material` 源文件（YAML v2）按名字填写参数和纹理：`shader` 为空时是引擎 PBR
  材质（`base_color`、`metallic`、`roughness`，纹理槽 `base_color`、`normal`、
  `metallic_roughness`、`occlusion`、`emissive`）；指定 Shader 时，参数是其 set 1
  uniform block 的成员，纹理是其 set 1 的 sampler2D。导入时根据 Shader 反射解析
  成按索引的绑定配方并写入 KMAT。未填写的参数取 Shader 声明的默认值（`.shaderprog`
  中的 `default <名字> = <数值...>` 行，存入 Shader Product），没有默认值则为 0；
  未指定的 sampler 读白色。
- 材质 Shader 是 `.shaderprog`（顶点 + 片元，Vulkan GLSL 450）：set 0 是引擎数据，
  用 `#include <Kans3D/Engine.glsl>` 声明（`u_Frame`：ViewProjection、相机位置、
  4 个点光源的位置、PBR 辐射度 `LightColors` 与漫反射强度 `LightDiffuse`、Time；`u_Object`：Transform、NormalMatrix；IBL 的
  `u_IrradianceMap`、`u_PrefilterMap`、`u_BrdfLUT`）；顶点输入用
  `<Kans3D/VertexInput.glsl>`；`<Kans3D/Lighting.glsl>` 提供引擎 PBR 光照
  `KansShadePbr` 与 `KansToneMap`。片元在 location 0 输出 vec4。不符合约定的
  Shader 在导入材质时报错。
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
- 内容浏览器右键「Create Material」新建材质，双击 `.material` 打开材质编辑器：
  选择 Shader（或引擎 PBR）、编辑参数与纹理，保存后自动导入并在场景中生效。

## 尚未实现

骨骼/动画 Product、内嵌纹理导入、透明材质、材质节点编辑器、平台目标的 Product
变体、Pak/发布、文件监视自动重导入、Vulkan 渲染后端。
