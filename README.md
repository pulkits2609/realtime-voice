# realtime-voice

A command-line voice chat project written in C++17, using Boost.Asio for networking, Opus for audio compression, and miniaudio for microphone capture and playback.

I built this as a student project to understand what happens between speaking into a microphone and hearing that audio on another computer. Over roughly four weeks, it grew from basic socket communication into a working voice pipeline with multiple participants, connection management, jitter buffering, packet loss recovery, and speaker mixing.

The project is intentionally small enough to read and understand. My priority has been getting the fundamentals right and keeping the code maintainable.

## What it does

- Captures microphone audio and sends it as Opus packets.
- Relays voice through a central server.
- Supports multiple connected participants and mixes simultaneous speakers on each client.
- Uses TCP for joining, participant updates, heartbeats, and disconnects.
- Uses UDP for voice packets.
- Accepts an IPv4 address or a domain name when starting a client.
- Keeps separate decoder and jitter-buffer state for each remote participant.
- Schedules playback at 20 ms intervals.
- Uses Opus packet loss concealment when a frame misses its playback deadline.
- Recovers from large packet gaps without playing a long backlog of stale audio.
- Handles Ctrl+C and cleans up connections, audio devices, and worker threads.

There is no graphical interface. You start the server and clients from a terminal, and connected clients begin transmitting microphone audio automatically.

## Current status

The Windows build and local voice pipeline have been tested, including multiple speakers, disconnects, packet loss recovery, domain resolution, and server restarts.

Build and setup scripts are also provided for Ubuntu 22.04 and newer. The latest version still needs a full Ubuntu build and audio test, along with testing between devices over the public internet.

This version is suitable for learning and controlled testing. Before running an unrestricted public server, the project still needs authentication, encrypted transport, and resource limits. See [Known limitations](#known-limitations).

## How it works

Each client maintains a TCP connection and a UDP socket to the server.

TCP handles the session:

- Joining with a display name.
- Receiving a server-assigned client ID.
- Receiving the current participant list.
- Learning when participants join or leave.
- Exchanging heartbeats and detecting lost connections.

UDP carries the audio:

```text
Microphone
    |
    v
Capture buffer -> 20 ms PCM frame -> Opus encoder
    |
    v
UDP packet -> Server -> Other connected clients
                           |
                           v
                  Per-speaker jitter buffer
                           |
                           v
                  Opus decoder / loss concealment
                           |
                           v
                  Mix remote speakers -> Playback
```

The server forwards encoded packets without decoding or mixing them. Each receiving client does its own decoding and mixing. A sender does not receive its own voice packets back from the server.

Both TCP and UDP use the port passed to the server at startup.

## Audio settings

The current settings are fixed in the code.

| Setting | Value |
| --- | --- |
| Sample rate | 48,000 Hz |
| Channels | Mono |
| PCM format | 32-bit floating point |
| Codec | Opus |
| Opus application | VOIP |
| Target bitrate | 32 kbps |
| Voice frame | 960 samples / 20 ms |
| Jitter startup delay | 60 ms |
| Per-speaker jitter capacity | 8 packets |
| Consecutive loss concealment limit | 5 frames before rebuffering |
| Playback PCM buffer limit | 1,920 samples / 40 ms |

The 20 ms frame duration is not an end-to-end latency measurement. Actual latency also includes capture buffering, encoding, the network, jitter buffering, and the audio device.

## Requirements

### Windows

- A 64-bit Windows system.
- Visual Studio C++ tools and a Windows SDK.
- CMake 3.24 or newer for the Windows scripts.
- Ninja.
- Git.
- Boost.Asio headers.
- Opus, using the `x64-windows` vcpkg triplet.

The setup script checks for these and installs missing components.

### Ubuntu

The setup script supports Ubuntu 22.04 and newer and checks for:

- `build-essential`
- `cmake`
- `git`
- `pkg-config`
- `python3`
- `gdb`
- `libboost-dev`
- `libopus-dev`
- `libasound2-dev`
- `libpulse-dev`

miniaudio is included in the repository, so it does not need a separate download.

The client needs a working microphone and playback device. It uses the system's default audio devices.

## Getting started

Clone the repository:

```bash
git clone https://github.com/pulkits2609/realtime-voice.git
cd realtime-voice
```

If Git is not installed yet, you can download and extract the repository ZIP instead.

### Windows setup

From PowerShell, in the project root:

```powershell
.\setup\setup.bat
```

The script:

1. Checks Git, CMake, Ninja, and the Visual Studio C++ toolchain.
2. Installs missing build tools and the Windows SDK when needed.
3. Reuses an existing vcpkg installation or creates one under:
   `%LOCALAPPDATA%\realtime-voice\vcpkg`
4. Checks Boost and installs missing Boost.Asio or Opus dependencies.
5. Configures a Debug build.
6. Generates the VS Code settings and build tasks.

Installing Visual Studio components can take some time and may require administrator approval. If an installer requests a restart, restart Windows and rerun setup.

Open a new terminal after setup so it picks up the saved environment variables.

Build the project:

```powershell
.\build.bat
```

The build script initializes the x64 Visual Studio compiler environment automatically. You can also run it from an x64 Native Tools Command Prompt.

### Ubuntu setup

From the project root:

```bash
bash setup/setup.sh
```

The script installs missing packages using `apt`, configures a Debug build, and generates the VS Code settings and build tasks. Package installation may ask for your sudo password.

Build the project:

```bash
bash build.sh
```

Setup configures the project; the build script compiles the executables.

## Build options

Debug is the default. Release must be requested explicitly.

### Windows

```powershell
# Debug
.\build.bat

# Explicit Debug
.\build.bat -debug

# Release
.\build.bat -release

# Clean and rebuild as Debug
.\build.bat -clean

# Clean and rebuild as Release
.\build.bat -clean -release
```

### Ubuntu

```bash
# Debug
bash build.sh

# Explicit Debug
bash build.sh -debug

# Release
bash build.sh -release

# Clean and rebuild as Debug
bash build.sh -clean

# Clean and rebuild as Release
bash build.sh -clean -release
```

Flags can appear in either order. For example, `-release -clean` and `-clean -release` do the same thing.

Passing both `-debug` and `-release` is rejected. Unknown arguments are also rejected before the script starts cleaning or building.

Both configurations use the same `build/` directory. Switching configuration rebuilds in that directory; it does not create separate Debug and Release folders.

`-clean` removes the previous build directory before rebuilding.

Debug enables additional diagnostic logging. Release is more appropriate for normal voice testing.

## Running the project

Start the server first, then start the clients in separate terminals.

The port must be between `1` and `65535`. The following examples use `8080`.

### Windows

Server:

```powershell
.\build\voice_server.exe 8080
```

First client:

```powershell
.\build\voice_client.exe 127.0.0.1 8080 pulkit
```

Second client, in another terminal:

```powershell
.\build\voice_client.exe 127.0.0.1 8080 rover
```

### Ubuntu

Server:

```bash
./build/voice_server 8080
```

First client:

```bash
./build/voice_client 127.0.0.1 8080 pulkit
```

Second client, in another terminal:

```bash
./build/voice_client 127.0.0.1 8080 rover
```

Client arguments follow this order:

```text
voice_client <server_address> <server_port> <client_name>
```

Names can contain spaces if quoted:

```powershell
.\build\voice_client.exe 127.0.0.1 8080 "Pulkit Sharma"
```

Use a name between 1 and 64 bytes. Names are display labels; they are not authentication credentials.

Press **Ctrl+C** to stop either program. The server releases its sockets during shutdown so the same port can be used again.

For a useful voice test, run the clients on separate computers and wear headphones. Multiple microphone/playback clients on one machine can create feedback.

## Connecting across a network

### On the same LAN

Run the server on one computer. On the other computers, replace `127.0.0.1` with the server computer's LAN IPv4 address.

For example, if the server is at `192.168.1.50`:

```powershell
.\build\voice_client.exe 192.168.1.50 8080 rover
```

`127.0.0.1` always points to the computer running the client.

Allow the selected port through the server's firewall for **both TCP and UDP**.

### Using a domain name

The client resolves domain names to IPv4 addresses:

```powershell
.\build\voice_client.exe voice.example.com 8080 pulkit
```

Replace `voice.example.com` with your own domain. It must resolve to a reachable server IPv4 address.

UDP uses the address selected by the TCP connection, so both transports communicate with the same server.

### Relay and NAT behavior

Clients connect outward to the central server. The server learns each client's UDP endpoint from its outgoing packets and relays audio to that endpoint.

Clients send an empty UDP keepalive every two seconds to help maintain their NAT mapping. These keepalives are not forwarded as audio.

The design does not require clients to establish direct connections to each other. It does require a server that every client can reach over both TCP and UDP.

If the server is behind a router, its selected port needs forwarding for both protocols. A server behind carrier-grade NAT may not be directly reachable through ordinary router port forwarding.

Actual behavior across different routers and internet connections still needs testing. The project does not implement STUN, TURN, or automatic router configuration.

## VS Code support

The setup scripts create or update these files under `.vscode/`:

```text
settings.json
c_cpp_properties.json
extensions.json
tasks.json
```

They configure the compiler, include paths, CMake integration, and the generated `build/compile_commands.json`.

Recommended extensions:

- Microsoft C/C++
- CMake Tools

Build tasks:

- `voice: build Debug`
- `voice: build Release`

When an existing editor file is changed, setup creates a `.setup-backup` copy if one does not already exist. Unrelated configurations and tasks are retained.

These files contain machine-specific paths and are ignored by Git.

## Tests

The build creates two standalone test executables.

### Windows

```powershell
.\build\opus_test.exe
.\build\voice_packet_test.exe
```

### Ubuntu

```bash
./build/opus_test
./build/voice_packet_test
```

`opus_test` generates a tone, encodes it with Opus, and decodes it again. It checks codec initialization and reports encoding or decoding failures.

`voice_packet_test` checks the serialized voice packet layout, payload round-trip, client ID, sequence number, and rejection of truncated packets.

The Opus test prints more information in Debug builds. A successful Release run may produce no output. Both tests return a nonzero exit code on failure.

These are focused checks, not a complete networking or audio-quality test suite.

Additional local Windows validation has covered simultaneous speakers, playback deadlines, packet loss concealment, sequence wraparound, large gaps, disconnect cleanup, Ctrl+C, and immediate server restarts. The temporary integration fixtures used for that validation are not included in this repository.

## Project structure

```text
realtime-voice/
├── client/
│   └── main.cpp
├── server/
│   └── main.cpp
├── include/
│   ├── Audio/
│   ├── Codec/
│   ├── Common/
│   ├── Network/
│   └── third_party/
│       └── miniaudio.h
├── src/
│   ├── Audio/
│   ├── Codec/
│   ├── Network/
│   └── Tests/
├── setup/
│   ├── setup.bat
│   └── setup.sh
├── CMakeLists.txt
├── build.bat
├── build.sh
└── README.md
```

- **Audio:** device capture, playback, and PCM buffering.
- **Codec:** Opus encoder and decoder wrappers.
- **Network:** TCP session management, UDP transport, packet serialization, jitter buffering, and client/server processing.
- **Common:** debug logging.
- **Tests:** codec and voice-packet checks.

If you want to follow the implementation, start with `client/main.cpp` and `Client::Run()` in `src/Network/client.cpp`. Then look at `ProcessReceivedVoice()` and `ProcessPlayback()` for the receiving audio pipeline.

On the server side, start with `Server::Run()` and `RelayVoicePacket()` in `src/Network/server.cpp`.

## Troubleshooting

### Boost headers are not found

Run setup again. If you installed Boost yourself, `BOOST_INCLUDEDIR` should point to the directory containing `boost/asio.hpp`.

### CMake cannot find Opus on Windows

Check that `VCPKG_ROOT` points to the intended vcpkg installation and that `opus:x64-windows` is installed there.

Run setup again, then try:

```powershell
.\build.bat -clean
```

### CMake reports a generator mismatch

An existing build directory may have been configured with a different generator. Rebuild with `-clean` to create a fresh configuration.

### The client connects, but there is no sound

Check:

- Both clients have working default microphone and playback devices.
- The operating system allows microphone access.
- The server's port is open for UDP as well as TCP.
- Both clients are using the same server address and port.
- You are using the server's network address when testing across computers.

A successful TCP connection does not prove that UDP traffic is getting through.

### Audio echoes or feeds back

Use headphones. The client continuously captures its microphone, and this version does not include acoustic echo cancellation.

### The server cannot bind its port

Check whether another process is already using the selected TCP or UDP port. Stop the previous server with Ctrl+C or choose a different port.

### Ubuntu cannot initialize an audio device

Make sure you are running in an environment with usable microphone and playback devices. A headless machine, container, or WSL environment may not have the same audio access as a normal desktop session.

## Known limitations

This first version has a working voice pipeline, but there are several things left to address:

- **No authentication or transport encryption.** Control messages and voice packets are unencrypted.
- **UDP registration needs stronger session verification.** The first UDP endpoint is associated using a client ID without a secret session token. Endpoint checks after registration do not secure that initial step.
- **No explicit connection caps or comprehensive rate limits.** Some control queues are unbounded.
- **IPv4 only.**
- **Fixed audio and jitter settings.** There is no adaptive jitter buffer or automatic bitrate adjustment.
- **No mute control, push-to-talk, device selector, echo cancellation, or noise suppression.**
- **No automatic reconnect.** Restart the client after losing its server connection.
- **No rooms.** Participants connected to a server share the same voice session.
- **No published capacity or latency benchmarks.**

The server's outgoing traffic increases as more participants join because each incoming voice stream is forwarded separately to the other registered clients.

For now, keep testing within a trusted group. Public deployment needs the security and resource-control work above.

## What I learned

The biggest lesson from this project was that sending audio packets is only one part of voice communication.

Packets can arrive late, out of order, or not at all. A client can disappear without sending a disconnect message. Audio devices consume samples on their own schedule. Threads need clear ownership, and shutdown needs just as much attention as startup.

Working through those problems helped me understand buffering, sequence numbers, deadlines, thread synchronization, and connection lifecycle much better than a basic socket example would have.

There are still rough edges and learning comments in the code. I have kept the project readable so I can continue improving it and other students can follow how the pieces fit together.

## Contributing

Bug reports, explanations, and small improvements are welcome.

For a useful issue, include:

- Your operating system.
- Whether you used Debug or Release.
- The commands you ran.
- Relevant terminal output.
- Whether the problem happened locally, over a LAN, or over the internet.

For code changes, explain the problem being fixed and how you tested it. Focused changes are easier to understand and review.

## Dependencies and credits

This project uses:

- **Boost.Asio** for networking.
- **Opus** for voice encoding, decoding, and packet loss concealment.
- **miniaudio** for audio capture and playback.
- **CMake** for building the project.
- **vcpkg** for Windows dependency management.

The networking protocol, session handling, buffering, and client-side mixing are implemented in this repository.

## License

A license for the project's own code has not been added yet. The third-party dependencies retain their respective licenses.

A project license needs to be chosen before this repository is presented as licensed open-source software.