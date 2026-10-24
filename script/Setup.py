#!/usr/bin/env python3
"""Checks and installs the Kans3D build dependencies, then configures CMake.

    python script/Setup.py            interactive (Textual) in a terminal, plain text otherwise
    python script/Setup.py --no-tui   plain text: check, offer to install what is missing, configure
    python script/Setup.py --check    report only; exit code 1 if anything required is missing
    python script/Setup.py --yes      install without asking (plain text)
    python script/Setup.py --build    also build the editor (Debug)
    python script/Setup.py --tests    include test dependencies (GoogleTest, software Vulkan)

Linux: Arch (pacman), Debian/Ubuntu (apt), Fedora (dnf). Windows: winget.
Needs only the standard library; the interactive mode imports Textual from the pure-Python wheels
committed in script/third_party (no pip, no network, no virtual environment).
"""
import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
THIRD_PARTY = Path(__file__).resolve().parent / "third_party"
REQUIRED_CMAKE = (3, 21)
REQUIRED_DOTNET_MAJOR = 10


def run(command, **kwargs):
    """Returns (exit code, combined output); a missing executable is exit code 127."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, **kwargs)
        return result.returncode, result.stdout + result.stderr
    except (OSError, ValueError) as error:
        return 127, str(error)


def version_of(text):
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
    return tuple(int(part or 0) for part in match.groups()) if match else None


class Dependency:
    """A named check plus the package that provides it on each package manager."""

    def __init__(self, name, check, packages, required=True, tests_only=False, note=""):
        self.name, self.check, self.packages = name, check, packages
        self.required, self.tests_only, self.note = required, tests_only, note


# ---------------------------------------------------------------- checks

def has_program(name):
    return lambda: (shutil.which(name) is not None, shutil.which(name) or "not found")


def check_cmake():
    code, out = run(["cmake", "--version"])
    found = version_of(out) if code == 0 else None
    if not found:
        return False, "not found"
    return found[:2] >= REQUIRED_CMAKE, ".".join(map(str, found)) + f" (need >= {REQUIRED_CMAKE[0]}.{REQUIRED_CMAKE[1]})"


def check_dotnet():
    code, out = run(["dotnet", "--list-sdks"])
    if code != 0:
        return False, "not found"
    majors = [int(line.split(".")[0]) for line in out.splitlines() if line[:1].isdigit()]
    ok = any(major >= REQUIRED_DOTNET_MAJOR for major in majors)
    return ok, ", ".join(line.split(" ")[0] for line in out.splitlines() if line.strip()) or "no SDK"


def check_pkgconfig(module):
    def check():
        if shutil.which("pkg-config") is None:
            return False, "pkg-config not found"
        code, out = run(["pkg-config", "--modversion", module])
        return code == 0, out.strip() if code == 0 else "not found"
    return check


def check_library(*names):
    """Looks for a library file in the usual Linux locations (no pkg-config file for some packages)."""
    def check():
        roots = ["/usr/lib", "/usr/lib64", "/usr/local/lib", "/usr/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu"]
        for root in roots:
            for name in names:
                if Path(root, name).exists():
                    return True, str(Path(root, name))
        return False, "not found"
    return check


def check_header(relative):
    def check():
        for root in ["/usr/include", "/usr/local/include"]:
            if Path(root, relative).exists():
                return True, str(Path(root, relative))
        return False, "not found"
    return check


def check_lavapipe():
    for root in ["/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d"]:
        for icd in sorted(Path(root).glob("lvp_icd*.json")):
            return True, str(icd)
    return False, "lvp_icd.json not found"


def check_submodules():
    code, out = run(["git", "submodule", "status", "--recursive"], cwd=ROOT)
    if code != 0:
        return False, "git submodule status failed"
    missing = [line.split()[1] for line in out.splitlines() if line.startswith("-")]
    return not missing, "initialized" if not missing else "missing: " + ", ".join(missing)


def check_visual_studio():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                   "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.exists():
        return False, "Visual Studio Installer not found"
    code, out = run([str(vswhere), "-latest", "-version", "[17.0,18.0)", "-products", "*",
                     "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "displayName"])
    return code == 0 and bool(out.strip()), out.strip() or "VS 2022 with C++ tools not found"


def check_vulkan_sdk():
    sdk = os.environ.get("VULKAN_SDK")
    if not sdk or not Path(sdk).exists():
        return False, "VULKAN_SDK is not set (a new terminal is needed after installing)"
    return True, sdk


# ---------------------------------------------------------------- dependency tables

def linux_dependencies():
    return [
        Dependency("Git", has_program("git"), {"pacman": "git", "apt": "git", "dnf": "git"}),
        Dependency("CMake", check_cmake, {"pacman": "cmake", "apt": "cmake", "dnf": "cmake"}),
        Dependency("Ninja", has_program("ninja"), {"pacman": "ninja", "apt": "ninja-build", "dnf": "ninja-build"}),
        Dependency("GCC (g++)", has_program("g++"), {"pacman": "gcc", "apt": "g++", "dnf": "gcc-c++"}),
        Dependency("pkg-config", has_program("pkg-config"), {"pacman": "pkgconf", "apt": "pkg-config", "dnf": "pkgconf-pkg-config"}),
        Dependency("Vulkan loader and headers", check_pkgconfig("vulkan"),
                   {"pacman": "vulkan-icd-loader vulkan-headers", "apt": "libvulkan-dev", "dnf": "vulkan-loader-devel vulkan-headers"}),
        Dependency("shaderc", check_library("libshaderc_combined.a"),
                   {"pacman": "shaderc", "apt": "libshaderc-dev", "dnf": "libshaderc-devel"},
                   note="Debian/Ubuntu may lack libshaderc_combined.a; use the LunarG Vulkan SDK there"),
        Dependency("glslang", check_library("libglslang.so", "libglslang.a"),
                   {"pacman": "glslang", "apt": "glslang-dev", "dnf": "glslang-devel"}),
        Dependency("SPIRV-Cross", check_library("libspirv-cross-glsl.a", "libspirv-cross-glsl.so"),
                   {"pacman": "spirv-cross", "apt": "libspirv-cross-c-shared-dev spirv-cross", "dnf": "spirv-cross-devel"}),
        Dependency("SPIRV-Tools", check_library("libSPIRV-Tools.so", "libSPIRV-Tools.a"),
                   {"pacman": "spirv-tools", "apt": "spirv-tools", "dnf": "spirv-tools-devel"}),
        Dependency("Assimp", check_header("assimp/Importer.hpp"),
                   {"pacman": "assimp", "apt": "libassimp-dev", "dnf": "assimp-devel"}),
        Dependency("X11 (GLFW)", check_pkgconfig("x11"),
                   {"pacman": "libx11 libxrandr libxinerama libxcursor libxi",
                    "apt": "libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev",
                    "dnf": "libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel"}),
        Dependency("Wayland (GLFW)", check_pkgconfig("wayland-client"),
                   {"pacman": "wayland wayland-protocols libxkbcommon",
                    "apt": "libwayland-dev wayland-protocols libxkbcommon-dev",
                    "dnf": "wayland-devel wayland-protocols-devel libxkbcommon-devel"}),
        Dependency(".NET SDK 10 (C# scripting)", check_dotnet,
                   {"pacman": "dotnet-sdk", "apt": "dotnet-sdk-10.0", "dnf": "dotnet-sdk-10.0"}, required=False,
                   note="without it the engine builds with C# scripting disabled"),
        Dependency("GoogleTest", check_header("gtest/gtest.h"),
                   {"pacman": "gtest", "apt": "libgtest-dev", "dnf": "gtest-devel"}, tests_only=True),
        Dependency("Software Vulkan (lavapipe)", check_lavapipe,
                   {"pacman": "vulkan-swrast", "apt": "mesa-vulkan-drivers", "dnf": "mesa-vulkan-drivers"}, tests_only=True,
                   note="GPU tests run on lavapipe; never on the real GPU by default"),
    ]


def windows_dependencies():
    return [
        Dependency("Git", has_program("git"), {"winget": "Git.Git"}),
        Dependency("CMake", check_cmake, {"winget": "Kitware.CMake"}),
        Dependency("Visual Studio 2022 (C++)", check_visual_studio,
                   {"winget": "Microsoft.VisualStudio.2022.Community --override \"--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive\""}),
        Dependency("Vulkan SDK", check_vulkan_sdk, {"winget": "KhronosGroup.VulkanSDK"}),
        Dependency(".NET SDK 10 (C# scripting)", check_dotnet, {"winget": "Microsoft.DotNet.SDK.10"}, required=False,
                   note="without it the engine builds with C# scripting disabled"),
    ]


# ---------------------------------------------------------------- package managers

def package_manager():
    if platform.system() == "Windows":
        return "winget" if shutil.which("winget") else None
    os_release = {}
    try:
        for line in Path("/etc/os-release").read_text().splitlines():
            key, _, value = line.partition("=")
            os_release[key] = value.strip('"')
    except OSError:
        pass
    family = " ".join([os_release.get("ID", ""), os_release.get("ID_LIKE", "")])
    for name, ids in (("pacman", ("arch",)), ("apt", ("debian", "ubuntu")), ("dnf", ("fedora", "rhel"))):
        if any(i in family.split() for i in ids) and shutil.which(name):
            return name
    return None


def install_commands(manager, packages):
    if manager == "winget":
        return [f"winget install --exact --accept-package-agreements --accept-source-agreements --id {p}" for p in packages]
    joined = " ".join(packages)
    return {
        "pacman": [f"sudo pacman -S --needed {joined}"],
        "apt": ["sudo apt-get update", f"sudo apt-get install -y {joined}"],
        "dnf": [f"sudo dnf install -y {joined}"],
    }[manager]


def ask(question, assume_yes):
    if assume_yes:
        return True
    try:
        return input(f"{question} [y/N] ").strip().lower() in ("y", "yes")
    except EOFError:
        return False


# ---------------------------------------------------------------- main

def load_tui():
    """Imports the Textual front end from the vendored wheels, or returns None (plain mode)."""
    if sys.version_info < (3, 9):
        return None
    sys.path[:0] = [str(wheel) for wheel in sorted(THIRD_PARTY.glob("*.whl"))]
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    try:
        import SetupTui
        return SetupTui
    except ImportError as error:
        print(f"Interactive mode unavailable ({error}); using plain mode.\n")
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="report only, install nothing")
    parser.add_argument("--yes", action="store_true", help="install without asking")
    parser.add_argument("--tests", action="store_true", help="include test dependencies")
    parser.add_argument("--build", action="store_true", help="build KansEditor (Debug) after configuring")
    parser.add_argument("--preset", help="CMake configure preset (default: linux-gcc / windows-msvc)")
    parser.add_argument("--no-tui", action="store_true", help="plain text mode even in a terminal")
    args = parser.parse_args()

    interactive = sys.stdin.isatty() and sys.stdout.isatty()
    if interactive and not (args.check or args.yes or args.no_tui):
        tui = load_tui()
        if tui:
            tui.run(tests=args.tests, build=args.build, preset=args.preset)
            return

    windows = platform.system() == "Windows"
    preset = args.preset or ("windows-msvc" if windows else "linux-gcc")
    dependencies = windows_dependencies() if windows else linux_dependencies()
    dependencies = [d for d in dependencies if args.tests or not d.tests_only]
    manager = package_manager()

    print(f"Kans3D setup — {platform.system()} {platform.machine()}, package manager: {manager or 'none found'}\n")
    missing = []
    for dependency in dependencies:
        ok, detail = dependency.check()
        mark = "OK  " if ok else ("MISS" if dependency.required else "opt ")
        print(f"  [{mark}] {dependency.name:<30} {detail}")
        if not ok:
            missing.append(dependency)
            if dependency.note:
                print(f"         {dependency.note}")

    submodules_ok, detail = check_submodules()
    print(f"  [{'OK  ' if submodules_ok else 'MISS'}] {'Git submodules':<30} {detail}")

    if args.check:
        required_missing = [d for d in missing if d.required] or ([] if submodules_ok else ["submodules"])
        sys.exit(1 if required_missing else 0)

    if missing:
        if manager is None:
            print("\nNo supported package manager found; install the packages above manually.")
        else:
            packages = []
            for dependency in missing:
                package = dependency.packages.get(manager)
                if package and package not in packages:
                    packages.extend([package] if manager == "winget" else package.split())
            commands = install_commands(manager, packages)
            print("\nInstall commands:")
            for command in commands:
                print(f"  {command}")
            if ask("Run them now?", args.yes):
                for command in commands:
                    if subprocess.call(command, shell=True) != 0:
                        print(f"Failed: {command}")
                        sys.exit(1)
                if windows:
                    print("\nOpen a new terminal so PATH and VULKAN_SDK changes take effect, then run Setup again.")
                    sys.exit(0)

    still_missing = [d.name for d in dependencies if d.required and not d.check()[0]]
    if still_missing:
        print("\nStill missing: " + ", ".join(still_missing) + ". Configure skipped.")
        sys.exit(1)

    if not submodules_ok:
        print("\nInitializing git submodules...")
        if subprocess.call(["git", "submodule", "update", "--init", "--recursive"], cwd=ROOT) != 0:
            sys.exit(1)

    print(f"\nConfiguring: cmake --preset {preset}")
    if subprocess.call(["cmake", "--preset", preset], cwd=ROOT) != 0:
        sys.exit(1)
    if args.build:
        jobs = os.cpu_count() or 2
        print(f"\nBuilding KansEditor (Debug, {jobs} jobs)")
        if subprocess.call(["cmake", "--build", f"build/{preset}", "--config", "Debug", "--target", "KansEditor",
                            "--parallel", str(jobs)], cwd=ROOT) != 0:
            sys.exit(1)
    print("\nDone.")


if __name__ == "__main__":
    main()
