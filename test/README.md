# Tests

## OpenGL mipmap 回归测试

```sh
cmake --build --preset linux-gcc-debug --target OpenGLTextureTests KansEditor -j2
ctest --test-dir build/linux-gcc -C Debug -R '^OpenGLTextureMipmap\.' --output-on-failure
```

`OpenGLTextureTests` 独立于 CPU 测试，实际创建 OpenGL 上下文并调用引擎的
`OpenGLTexture2D` 文件加载构造函数和 `OpenGLRHI::EnsureDSAFunctionsLoaded`。
Linux 需要 EGL 开发库和支持离屏 OpenGL 的驱动（Mesa llvmpipe 可用，无需 DISPLAY）；
其他平台使用 GLFW 隐藏窗口。上下文或函数入口不可用时测试失败，不静默跳过。

覆盖完整 mip 链尺寸、三线性缩小/线性放大参数、GL 错误、8×4 棋盘图最末级
1×1 像素的平均值、关闭 mipmap 时仅分配一级，以及全部 16 张编辑器图标的
非二次幂尺寸与最末级像素读回。

Linux 额外运行 `GL43ExtensionPath`，使用 Mesa 软件驱动并设置
`MESA_GL_VERSION_OVERRIDE=4.3`，防止驱动自动返回 4.5 而掩盖扩展入口加载遗漏。
测试输出 XML 中记录实际 renderer 和 version。曾复现 GL 4.3 路径没有加载
`glGenerateTextureMipmap`、函数指针为空的问题；修复位于 OpenGLRHI 的 DSA 加载列表。

## Core and model benchmarks

项目平滑法线测试：`MeshUtilsNormals` 直接调用 `MeshUtils::SmoothNormal` 和 `SmoothNormalHash`，验证同位置顶点获得一致的归一化法线和不同位置保持独立。原 Assimp 测试仅作为解析库测试保留。

```sh
cmake --build build/linux-gcc --config Debug --target Kans3DTests SmoothNormalsBenchmark -j2
ctest --test-dir build/linux-gcc -C Debug -R MeshUtilsNormals --output-on-failure
build/linux-gcc/test/Debug/SmoothNormalsBenchmark
```

基准直接测量上述两个 Utils 函数，使用每位置三个拆分顶点及不同输入法线。输入生成与输出校验不计时，输出分配包含在耗时内。
每组预热一次、采样五次，报告中位数、最近秩 p95 和正确性。旧算法为平方复杂度，限定 3000/12000 顶点；Hash 版额外测试 48000/192000/768000 顶点。
出现错误结果时仍输出耗时和 correct=false，进程最终返回 1；错误算法的耗时不能作为等价实现的性能对比。没有性能通过阈值。

模型加载基准（需要 OpenGL 窗口/上下文，独立于 CTest 运行）：

```sh
cmake --build build/linux-gcc --config Debug --target ModelLoadBenchmark -j2
build/linux-gcc/test/Debug/ModelLoadBenchmark
```

该基准测量 `AssimpMeshImporter` 的 CPU 解析、GPU 资源终结和 fence 等待耗时，输出 3 次采样。

现有模型测试直接调用 Assimp，不覆盖引擎导入器；引擎导入器与 GPU fence 的验证依赖 GL 上下文，未纳入无窗口的 `Kans3DTests`。资源版本的读写依赖管理仍待 `TaskGraph` 实现。
