# 	Kans3D-Vulkan(Branch of Kans3D)

<p align="center">
  <a href="https://github.com/kanzakishigure/Kans3D-Vulkan">
    <img src="./KansEditor/Resources/Icon/Kans3D_logo.png" width="400" height="200" alt="Kans3D logo">
  </a>
</p>


The **Kans3D** is a Engine for personal learning 

## Continuous build status

| Target platform |               Status               |
| :-------------: | :--------------------------------: |
|     Windows     | <font color=#45b787>passing</font> |
|      Linux      | <font color=#45b787>passing</font> |

## Requirements

- Git, CMake 3.21 or newer, Python 3.9 or newer (for the setup script)
- **Windows 10/11:** Visual Studio 2022 with the C++ desktop workload, Vulkan SDK
- **Linux:** GCC, Ninja, and the Vulkan, shaderc, glslang, SPIRV-Cross, SPIRV-Tools, Assimp and X11/Wayland development packages
- **.NET SDK 10** (optional): enables C# scripting; without it the engine builds with scripting disabled

The setup script checks all of these and installs what is missing.

## Build Kans3D

Clone with submodules, then run the setup script from the repository root:

```
git clone --recursive https://github.com/kanzakishigure/Kans3D-Vulkan.git
cd Kans3D-Vulkan
python script/Setup.py          # Windows: script\Setup.bat   Linux: ./script/Setup.sh
```

In a terminal the script opens an interactive screen: it lists every dependency, lets you choose which missing ones to install (Space toggles), installs them (winget on Windows; pacman, apt or dnf on Linux), initializes the submodules and configures CMake. It uses [Textual](https://textual.textualize.io/) from the pure-Python wheels in `script/third_party`, so nothing has to be installed with pip first.

| Option | Effect |
| :----- | :----- |
| `--no-tui` | Plain text mode instead of the interactive screen |
| `--check` | Only report; exit code 1 if a required dependency is missing |
| `--tests` | Also check test dependencies (GoogleTest, software Vulkan) |
| `--build` | Build KansEditor (Debug) after configuring |
| `--yes` | Install without asking (plain text mode, for automation) |
| `--preset <name>` | CMake configure preset (default `windows-msvc` / `linux-gcc`) |

To build manually after configuring:

```
cmake --build build/windows-msvc --config Debug --target KansEditor   # or build/linux-gcc
```

On Windows, the Vulkan SDK installer must include the shader toolchain components below. Open a new terminal after installing so `VULKAN_SDK` is set.

<p align="center">
  <a href="https://vulkan.lunarg.com/doc/sdk">
    <img src="./KansEditor/Resources/ReadMeResource/VulkanOption.png"  alt="Install option">
  </a>
</p>

## Documentation

For documentation, will coming soon.

## Extra

Because of the Kans3D is still in developing ,The master branch is the only stable branch,if you switch to another branch may cause crash.
