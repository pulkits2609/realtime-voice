# Packaging a release

Release users extract the archive and run the applications. They do not run setup.

The maintainer needs Python 3.10 or newer. The packaging script builds Release, runs the existing tests, and excludes test executables and debug symbols. Generated files go into `dist/`, which is ignored by Git.

Build, package.py, and smoke_test.py commands in this guide run from a SOURCE
CHECKOUT. The extracted-package example commands have their own section below;
archive users do not need the maintainer scripts or a build directory.

The examples below use `v2.0.0` as a prospective release version. Protocol 2 is
incompatible with the published v1.0.0 applications; use a new version when
publishing. Packaging does not create a Git tag, push, or publish a release.

## Windows x64

Run the usual project setup once if build tools are missing, then:

```powershell
python packaging/package.py v2.0.0
```

The script initializes x64 Visual Studio tools and calls `build.bat -release`. It scans executable and DLL imports recursively and copies required Opus and Microsoft runtime DLLs from their package/redistributable locations.

Output: `realtime-voice-v2.0.0-windows-x64.zip` and its `.sha256` file.

The ZIP includes the client, server, required DLLs, API documentation, usage instructions, and license notices, including nlohmann/json. Windows 10/11 x64 supplies its OS libraries and audio drivers. Microsoft runtime DLLs retain Microsoft's redistribution terms; they are copied from Visual Studio's redistributable directory, not System32.

## Ubuntu x86_64

Build on **Ubuntu 22.04**, the oldest supported version. Newer hosts can introduce newer glibc requirements even when other libraries are bundled.

The script bundles Opus, ALSA, PulseAudio client libraries, ALSA PulseAudio plugins, their linked dependencies, and ALSA configuration. It links the application's C++ runtime statically. It does not bundle glibc, a sound server, or device drivers.

Ubuntu still supplies its normal audio service and actual devices. A headless machine can run the server; the client needs an audio environment.

Use an isolated Ubuntu 22.04 build environment. With Docker installed, run this from the project root in Bash:

```bash
mkdir -p dist
docker run --rm --platform linux/amd64 \
    --mount "type=bind,source=$PWD,target=/source,readonly" \
    --mount "type=bind,source=$PWD/dist,target=/output" \
    ubuntu:22.04 bash -c '
        set -eu
        printf "%s\n" \
            "deb-src http://archive.ubuntu.com/ubuntu jammy main universe" \
            "deb-src http://archive.ubuntu.com/ubuntu jammy-updates main universe" \
            "deb-src http://security.ubuntu.com/ubuntu jammy-security main universe" \
            > /etc/apt/sources.list.d/voice-sources.list
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get upgrade -y
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build pkg-config python3 ca-certificates \
            libboost-dev libopus-dev libasound2 libasound2-plugins libpulse0 \
            libasound2-data
        mkdir /work
        tar --exclude=.git --exclude=build --exclude=dist \
            -C /source -cf - . | tar -C /work -xf -
        cd /work
        python3 packaging/package.py v2.0.0 --output /output
    '
```

All installations and upgrades above happen inside the disposable container. The project mount is read-only; only the output mount is writable.

On a prepared Ubuntu 22.04 build machine:

```bash
python3 packaging/package.py v2.0.0
```

Enable source repositories (`deb-src`). The script downloads exact source packages matching the shared libraries. If an old installed library's source version is unavailable, update that library in the build environment and retry.

Output:

- `realtime-voice-v2.0.0-ubuntu-x86_64.tar.gz`
- `realtime-voice-v2.0.0-ubuntu-x86_64-library-sources.tar.gz`
- A `.sha256` file for each archive.

Publish the library sources alongside the Ubuntu binary and preserve license notices. Users do not need that source archive to run the applications.

Users run the top-level `voice_client` and `voice_server` launchers, which locate bundled libraries using an absolute path. Extraction directories with spaces are supported.

## Optional integration examples

Both archives include tools/mock_gfx.py, tools/voice_api.py, and
tools/background_voice.py with their relative layout intact. Python 3.10+ is
required only for these scripts, not for normal standalone use.

From the extracted Windows directory, start .\voice_server.exe 8080, then:

```powershell
python tools/mock_gfx.py --port 8080 --client voice_client.exe --no-audio
```

From the extracted Ubuntu directory, start ./voice_server 8080, then:

```bash
python3 tools/mock_gfx.py --port 8080 --client ./voice_client --no-audio
```

Source checkouts instead use --client build/voice_client.exe or
--client ./build/voice_client. See [the integration contract](../docs/gfx-integration.md).

After building an archive, verify its checksum and run its packaged mock from an
external temporary extraction directory containing spaces:

```powershell
python packaging/smoke_test.py dist/realtime-voice-v2.0.0-windows-x64.zip
```

```bash
python3 packaging/smoke_test.py dist/realtime-voice-v2.0.0-ubuntu-x86_64.tar.gz
```

The smoke test checks imports come from the extracted tools directory, strips
Python environment overrides, restricts Windows PATH to System32, and uses the
Ubuntu bundled-library launchers. It runs device-free; hardware checks remain.

## Before publishing

1. Extract both application archives and run them from those folders.
2. Test microphone capture, playback, multiple participants, and Ctrl+C on target systems.
3. Verify Ubuntu 22.04 and a newer supported Ubuntu installation.
4. Keep library sources available alongside the Ubuntu download.
5. Review and commit the packaging/license changes.
6. Push the commit and create the chosen version tag yourself.
7. Create one GitHub release with both application archives, the library sources, and all checksums.

GitHub's automatic source archives contain source code, not runnable applications. Attach the generated files in `dist/` as release assets.

For another release, change `v2.0.0` in the commands. Re-running a version replaces that version's generated archives locally.

