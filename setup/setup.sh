#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: ./setup/setup.sh" >&2
    exit 1
fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source /etc/os-release
if [[ "$ID" != ubuntu ]] || ! dpkg --compare-versions "$VERSION_ID" ge 22.04; then
    echo "ERROR: This setup script supports Ubuntu 22.04 and newer." >&2
    exit 1
fi

# miniaudio is already vendored; ALSA/PulseAudio packages provide Linux audio support.
packages=(build-essential cmake git pkg-config python3 gdb libboost-dev libopus-dev libasound2-dev libpulse-dev)
missing=()
for package in "${packages[@]}"; do
    if [[ "$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true)" != 'install ok installed' ]]; then
        missing+=("$package")
    fi
done

if (( ${#missing[@]} )); then
    elevate=()
    if (( EUID != 0 )); then
        command -v sudo >/dev/null || { echo "ERROR: sudo is required to install packages." >&2; exit 1; }
        elevate=(sudo)
    fi
    echo "Installing missing packages: ${missing[*]}"
    "${elevate[@]}" apt-get update
    "${elevate[@]}" apt-get install -y "${missing[@]}"
fi

for tool in g++ cmake git pkg-config python3 gdb; do
    command -v "$tool" >/dev/null || { echo "ERROR: Required tool is unavailable: $tool" >&2; exit 1; }
done
pkg-config --exists opus || { echo "ERROR: Opus development files are unavailable." >&2; exit 1; }

# Configure only: this generates the real compiler commands for IntelliSense.
cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build" -DCMAKE_BUILD_TYPE=Debug

python3 - "$PROJECT_DIR" "$(command -v g++)" <<'PYTHON'
import json
import pathlib
import re
import shutil
import sys

project = pathlib.Path(sys.argv[1])
editor = project / ".vscode"

def read_json(name, default):
    path = editor / name
    if not path.exists():
        return default
    text = path.read_text(encoding="utf-8-sig")
    text = re.sub(r'"(?:\\.|[^"\\])*"|//[^\r\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    text = re.sub(r'"(?:\\.|[^"\\])*"|,(?=\s*[}\]])',
                  lambda match: "" if match[0] == "," else match[0], text)
    result = json.loads(text)
    if not isinstance(result, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return result

def write_json(name, value):
    path = editor / name
    text = json.dumps(value, indent=4, ensure_ascii=False) + "\n"
    if path.exists():
        if path.read_text(encoding="utf-8") == text:
            return
        backup = path.with_name(path.name + ".setup-backup")
        if not backup.exists():
            shutil.copy2(path, backup)
    path.write_text(text, encoding="utf-8")

settings = read_json("settings.json", {})
properties = read_json("c_cpp_properties.json", {"configurations": [], "version": 4})
extensions = read_json("extensions.json", {"recommendations": []})
tasks = read_json("tasks.json", {"version": "2.0.0", "tasks": []})

configuration = {
    "name": "CMake", "compilerPath": sys.argv[2], "cStandard": "c17", "cppStandard": "c++17",
    "intelliSenseMode": "${default}",
    "configurationProvider": "ms-vscode.cmake-tools",
    "compileCommands": "${workspaceFolder}/build/compile_commands.json",
    "includePath": ["${workspaceFolder}/include", "/usr/include", "/usr/include/opus"],
    "defines": ["VOICE_DEBUG"],
}
properties["configurations"] = [configuration] + [c for c in properties.get("configurations", []) if c.get("name") != "CMake"]
properties["version"] = 4
cache = (project / "build" / "CMakeCache.txt").read_text(encoding="utf-8")
generator = next((line.split("=", 1)[1] for line in cache.splitlines()
                  if line.startswith("CMAKE_GENERATOR:INTERNAL=")), "Unix Makefiles")
settings.update({
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json",
    "cmake.configureOnOpen": True,
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "cmake.generator": generator,
    "cmake.cmakePath": shutil.which("cmake"),
})
configure = settings.setdefault("cmake.configureSettings", {})
for key in ("CMAKE_TOOLCHAIN_FILE", "VCPKG_INSTALLED_DIR", "VCPKG_TARGET_TRIPLET", "BOOST_INCLUDE_DIR"):
    configure.pop(key, None)
configure["CMAKE_BUILD_TYPE"] = "Debug"
configure["CMAKE_CXX_COMPILER"] = sys.argv[2]
for key in ("cmake.configureEnvironment", "cmake.buildEnvironment"):
    environment = settings.get(key, {})
    if "LIB" in environment or "LIBPATH" in environment:
        for name in ("INCLUDE", "LIB", "LIBPATH", "PATH"):
            environment.pop(name, None)
extensions["recommendations"] = list(dict.fromkeys(extensions.get("recommendations", []) + ["ms-vscode.cpptools", "ms-vscode.cmake-tools"]))
labels = {"voice: build Debug", "voice: build Release"}
tasks["tasks"] = [t for t in tasks.get("tasks", []) if t.get("label") not in labels]
for mode in ("Debug", "Release"):
    tasks["tasks"].append({
        "label": f"voice: build {mode}", "type": "process", "command": "bash",
        "args": ["${workspaceFolder}/build.sh", f"-{mode.lower()}"],
        "options": {"cwd": "${workspaceFolder}"}, "problemMatcher": ["$gcc"],
        "group": {"kind": "build", "isDefault": mode == "Debug"},
    })
tasks["version"] = "2.0.0"
editor.mkdir(exist_ok=True)
for name, value in (("settings.json", settings), ("c_cpp_properties.json", properties),
                    ("extensions.json", extensions), ("tasks.json", tasks)):
    write_json(name, value)
PYTHON

chmod +x "$PROJECT_DIR/build.sh" "$PROJECT_DIR/setup/setup.sh"
echo "Setup complete. Open this folder in VS Code and install its recommended extensions."
echo "Build with ./build.sh (Debug) or ./build.sh -release."
