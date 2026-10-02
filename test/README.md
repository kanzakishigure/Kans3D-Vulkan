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

