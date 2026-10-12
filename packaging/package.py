#!/usr/bin/env python3
"""Build local release archives. Python is needed by the maintainer, not users."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
PROGRAMS = ("voice_client", "voice_server")
BOOST_LICENSE = "https://www.boost.org/LICENSE_1_0.txt"


def command(arguments):
    return arguments if isinstance(arguments, str) else list(map(str, arguments))


def run(arguments, **options):
    arguments = command(arguments)
    print("+", arguments if isinstance(arguments, str) else " ".join(arguments), flush=True)
    subprocess.run(arguments, check=True, **options)


def output(arguments, **options):
    return subprocess.check_output(command(arguments), text=True, **options)


def write(path, text):
    path.write_text(text, encoding="utf-8", newline="\n")


def common_files(stage, version, target):
    shutil.copy2(PROJECT / "LICENSE", stage / "LICENSE")
    shutil.copy2(PROJECT / "README.md", stage / "README.md")
    (stage / "packaging").mkdir()
    shutil.copy2(PROJECT / "packaging/README.md", stage / "packaging/README.md")
    notices = stage / "licenses"
    notices.mkdir()
    with urllib.request.urlopen(BOOST_LICENSE, timeout=30) as response:
        (notices / "Boost.txt").write_bytes(response.read())
    miniaudio = (PROJECT / "include/third_party/miniaudio.h").read_text(encoding="utf-8")
    license_start = miniaudio.rindex("This software is available as a choice")
    write(notices / "miniaudio.txt", miniaudio[license_start:miniaudio.index("*/", license_start)])
    write(stage / "BUILD.txt", f"realtime-voice {version}\nTarget: {target}\n"
          "Release build; test executables and debug symbols are excluded.\n"
          "See README.md for behavior and security limitations.\n")
    return notices


def checksum(archive):
    digest = hashlib.sha256()
    with archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    write(archive.with_name(archive.name + ".sha256"), f"{digest.hexdigest()}  {archive.name}\n")


def windows_package(stage):
    import winreg

    environment = {key.upper(): value for key, value in os.environ.items()}
    for name in ("VCPKG_ROOT", "BOOST_INCLUDEDIR"):
        if not environment.get(name):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
                try:
                    environment[name] = winreg.QueryValueEx(key, name)[0]
                except FileNotFoundError:
                    pass
    previous_cache = PROJECT / "build/CMakeCache.txt"
    if not environment.get("VCPKG_ROOT") and previous_cache.exists():
        opus = re.search(r"^Opus_DIR:[^=]+=(.+)$", previous_cache.read_text(encoding="utf-8"), re.M)
        if opus:
            candidate = Path(opus[1]).parents[3]
            if (candidate / "scripts/buildsystems/vcpkg.cmake").is_file():
                environment["VCPKG_ROOT"] = str(candidate)
    vswhere = Path(environment["PROGRAMFILES(X86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = Path(output([vswhere, "-latest", "-products", "*", "-requires",
                                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                "-property", "installationPath"]).strip())
    if not installation.is_dir():
        raise RuntimeError("Visual Studio C++ tools not found. Run setup/setup.bat first.")
    devcmd = installation / "Common7/Tools/VsDevCmd.bat"
    saved_vcpkg = environment.get("VCPKG_ROOT")
    native_command = f'call "{devcmd}" -arch=x64 -host_arch=x64 >nul && set'
    native = output(environment["COMSPEC"] + ' /d /s /c "' + native_command + '"', env=environment)
    for line in native.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            environment[key.upper()] = value
    if saved_vcpkg:
        environment["VCPKG_ROOT"] = saved_vcpkg
    build_command = f'call "{PROJECT / "build.bat"}" -release'
    run(environment["COMSPEC"] + ' /d /s /c "' + build_command + '"', env=environment)
    build = PROJECT / "build"
    for test in ("opus_test", "voice_packet_test"):
        run([build / (test + ".exe")], env=environment)

    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    # Use the package CMake actually linked, even when another vcpkg is installed.
    installed = Path(re.search(r"^Opus_DIR:[^=]+=(.+)$", cache, re.M)[1]).parents[1]
    runtime = Path(environment["VCTOOLSREDISTDIR"]) / "x64/Microsoft.VC143.CRT"
    search = (installed / "bin", runtime)
    dumpbin = shutil.which("dumpbin.exe", path=environment["PATH"])
    if not dumpbin or not runtime.is_dir():
        raise RuntimeError("MSVC dependency scanner or redistributable DLL directory not found.")

    pending = []
    for name in PROGRAMS:
        destination = stage / (name + ".exe")
        shutil.copy2(build / destination.name, destination)
        pending.append(destination)
    scanned = set()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in scanned:
            continue
        scanned.add(binary.name.lower())
        imports = output([dumpbin, "/nologo", "/dependents", binary], env=environment)
        for name in re.findall(r"^\s+([\w.-]+\.dll)\s*$", imports, re.M | re.I):
            source = next((directory / name for directory in search if (directory / name).is_file()), None)
            if source:
                destination = stage / name
                if not destination.exists():
                    shutil.copy2(source, destination)
                    pending.append(destination)
            elif not (name.lower().startswith(("api-ms-", "ext-ms-")) or
                      (Path(environment["SYSTEMROOT"]) / "System32" / name).is_file()):
                raise RuntimeError(f"Unresolved DLL: {name}")
    shutil.copy2(installed / "share/opus/copyright", stage / "licenses/Opus.txt")
    write(stage / "licenses/Microsoft-runtime.txt",
          "The bundled Microsoft Visual C++ runtime DLLs are Microsoft Distributable Code,\n"
          "not covered by this project's MIT license.\n"
          "https://visualstudio.microsoft.com/license-terms/\n"
          "https://learn.microsoft.com/visualstudio/releases/2022/redistribution\n"
          "https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files\n")
    write(stage / "RUN.txt", "Windows x64 (Windows 10/11).\n"
          "Extract the entire ZIP before running; keep all DLLs beside the executables.\n"
          "In PowerShell, from this folder:\n"
          "  .\\voice_server.exe 8080\n"
          "In a second terminal:\n"
          "  .\\voice_client.exe 127.0.0.1 8080 your_name\n"
          "For another computer, replace 127.0.0.1 with the server's address.\n"
          "Allow the server port through its firewall for both TCP and UDP.\n"
          "Use headphones. Stop with Ctrl+C.\n")
    # Ensure both applications load without the developer's PATH.
    clean_environment = environment.copy()
    clean_environment["PATH"] = str(Path(environment["SYSTEMROOT"]) / "System32")
    for name in PROGRAMS:
        result = subprocess.run([str(stage / (name + ".exe"))], cwd=stage, env=clean_environment,
                                capture_output=True, text=True, timeout=10)
        if result.returncode != 1 or "Usage:" not in result.stderr:
            raise RuntimeError(f"Packaged {name} failed its startup check: {result.stderr}")


def linux_package(stage, source_stage):
    release = dict(line.split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line)
    if release.get("ID", "").strip('"') != "ubuntu" or release.get("VERSION_ID", "").strip('"') != "22.04":
        raise RuntimeError("Build this release on Ubuntu 22.04 to preserve the oldest supported glibc baseline.")
    build = PROJECT / "build/release-linux"
    run(["cmake", "-S", PROJECT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++"])
    run(["cmake", "--build", build, "--parallel", str(min(os.cpu_count() or 2, 4))])
    for test in ("opus_test", "voice_packet_test"):
        run([build / test])

    (stage / "bin").mkdir()
    (stage / "lib/alsa-lib").mkdir(parents=True)
    libraries = Path("/usr/lib/x86_64-linux-gnu")
    seeds = []
    for name in PROGRAMS:
        destination = stage / "bin" / name
        shutil.copy2(build / name, destination)
        run(["strip", "--strip-unneeded", destination])
        seeds.append(build / name)
        launcher = stage / name
        write(launcher, '#!/bin/sh\nset -eu\n'
              'APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
              'export LD_LIBRARY_PATH="$APP_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
              'export ALSA_CONFIG_DIR="$APP_DIR/share/alsa"\n'
              'export ALSA_CONFIG_PATH="$ALSA_CONFIG_DIR/alsa.conf"\n'
              'export ALSA_PLUGIN_DIR="$APP_DIR/lib/alsa-lib"\n'
              f'exec "$APP_DIR/bin/{name}" "$@"\n')
        launcher.chmod(0o755)
    seeds += [libraries / "libasound.so.2", libraries / "libpulse.so.0"]
    for kind in ("pcm", "ctl"):
        plugin = libraries / "alsa-lib" / f"libasound_module_{kind}_pulse.so"
        shutil.copy2(plugin, stage / "lib/alsa-lib" / plugin.name)
        seeds.append(plugin)
    shutil.copytree("/usr/share/alsa", stage / "share/alsa", symlinks=False)

    # ldd covers normal linking. Seeds above also cover miniaudio's dlopen backends.
    system_libraries = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2",
                        "librt.so.1", "libresolv.so.2", "libutil.so.1"}
    dependencies = set(seeds[2:])
    for binary in seeds:
        listing = output(["ldd", binary])
        if "not found" in listing:
            raise RuntimeError(f"Missing build-host dependency:\n{listing}")
        for name, filename in re.findall(r"^\s*(\S+) => (/\S+)", listing, re.M):
            if name not in system_libraries:
                dependencies.add(Path(filename))
    for library in sorted(dependencies):
        if library.parent.name != "alsa-lib":
            shutil.copy2(library, stage / "lib" / library.name)

    packages = {"libasound2-data", "libstdc++-11-dev", "libgcc-11-dev"}
    for library in dependencies:
        for candidate in (library, library.resolve()):
            try:
                owner = output(["dpkg-query", "-S", candidate], stderr=subprocess.DEVNULL)
                packages.add(owner.split(": ", 1)[0].splitlines()[0])
                break
            except subprocess.CalledProcessError:
                continue
        else:
            raise RuntimeError(f"Cannot identify the package owning {library}")
    sources = set()
    manifest = []
    for package in sorted(packages):
        name, version, source_name, source_version = output([
            "dpkg-query", "-W", "-f=${Package}\t${Version}\t${source:Package}\t${source:Version}", package
        ]).split("\t")
        shutil.copy2(Path("/usr/share/doc") / name / "copyright", stage / "licenses" / (name + ".txt"))
        manifest.append(f"{name}\t{version}\t{source_name}\t{source_version}")
        # GCC static runtime is covered by the GCC Runtime Library Exception.
        if not name.startswith(("libstdc++-11-dev", "libgcc-11-dev")):
            sources.add(f"{source_name}={source_version}")
    shutil.copytree("/usr/share/common-licenses", stage / "licenses/common-licenses")
    write(stage / "DEPENDENCIES.tsv", "Package\tVersion\tSource\tSource version\n" + "\n".join(manifest) + "\n")
    source_stage.mkdir()
    for source in sorted(sources):
        run(["apt-get", "source", "--download-only", source], cwd=source_stage)
    write(stage / "licenses/SOURCES.txt",
          "Bundled libraries retain their original licenses; see the package notices here.\n"
          "The matching Ubuntu source packages (.dsc, upstream tarballs, Debian patches)\n"
          "are in the accompanying ubuntu-x86_64-library-sources.tar.gz release asset.\n"
          "Keep that asset available alongside this binary download when redistributing.\n"
          "Shared libraries can be replaced in lib/; they are not embedded in the application.\n")
    write(stage / "RUN.txt", "Ubuntu 22.04+ x86_64.\n"
          "Extract the entire archive. From the extracted folder:\n"
          "  ./voice_server 8080\n"
          "In a second terminal:\n"
          "  ./voice_client 127.0.0.1 8080 your_name\n"
          "Use these launchers, not the executables inside bin/.\n"
          "No compiler, Python, Boost, or Opus installation is needed to run this package.\n"
          "The host supplies glibc, its audio service/drivers, and actual audio devices.\n"
          "Use headphones. Allow the server port for both TCP and UDP. Stop with Ctrl+C.\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Release version, for example v1.0.0")
    parser.add_argument("--output", type=Path, default=PROJECT / "dist")
    args = parser.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?", args.version):
        parser.error("Use a version such as v1.0.0.")
    windows = os.name == "nt"
    if platform.machine().lower() not in ("amd64", "x86_64"):
        parser.error("These packages target x86_64 only.")
    if not windows and platform.system() != "Linux":
        parser.error("Run on Windows x64 or Ubuntu 22.04 x86_64.")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    target = "windows-x64" if windows else "ubuntu-x86_64"
    name = f"realtime-voice-{args.version}-{target}"
    with tempfile.TemporaryDirectory(prefix="voice-package-") as temporary:
        workspace = Path(temporary)
        stage = workspace / name
        stage.mkdir()
        common_files(stage, args.version, target)
        if windows:
            windows_package(stage)
            archive = args.output / (name + ".zip")
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
                for file in sorted(stage.rglob("*")):
                    if file.is_file():
                        zipped.write(file, file.relative_to(workspace))
        else:
            source_stage = workspace / "library-sources"
            linux_package(stage, source_stage)
            archive = args.output / (name + ".tar.gz")
            with tarfile.open(archive, "w:gz") as tar:
                tar.add(stage, arcname=name)
            source_archive = args.output / (name + "-library-sources.tar.gz")
            with tarfile.open(source_archive, "w:gz") as tar:
                tar.add(source_stage, arcname="library-sources")
            checksum(source_archive)
        checksum(archive)
        print(f"Created {archive}", flush=True)


if __name__ == "__main__":
    main()

