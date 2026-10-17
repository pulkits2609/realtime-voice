# Verification and manual checks

## Automated checks

Build first using build.bat -release or bash build.sh -release, then:

~~~text
ctest --test-dir build --output-on-failure
~~~

With Python available when CMake configures, seven suites are registered:

| Suite | Coverage |
| --- | --- |
| opus | Existing real-Opus encode/decode smoke test |
| voice_packet | Existing base serialization and truncated-packet checks |
| protocol | JSON types, limits, version errors |
| voice_groups | Valid/invalid teams, six managed Games, manual Lobby, routing, mutes, ownership, deletion, stale revisions, versioned UDP |
| client_audio | Real Opus streams mixed sample-for-sample, timed PLC/loss cap, independent inbound mutes, stale recipient context, muted capture |
| server_integration | Actual TCP/UDP routing and lifecycle, one manager, heartbeat timeout/recovery, standalone CLI and EOF |
| background_integration | Actual child IPC, parsing/limits, controls, reconnect, group deletion, EOF/backpressure, server failure/restart, full mock GFX lifecycle |

Without Python, only the five C++ suites are registered. That is not a complete
integration run. Install/use Python and reconfigure to run the others.

Individual integration commands:

~~~text
python tests/server_integration.py build
python tests/background_integration.py build
~~~

On Ubuntu substitute python3. Tests launch only their own local servers/clients,
choose ephemeral ports, and use device-free clients. They require no internet,
microphone, headphones, or GFX installation. Some deliberately wait ten seconds to
test the real management heartbeat deadline.

The integration helper sends synthetic encoded payload bytes for relay checks.
The C++ audio suite uses real Opus encoding/decoding and the actual client mixing
code. Neither is an objective/perceptual speech-quality assessment.

## Completed build verification

### Startup diagnostics

The shared tests/server_process.py fixture checks the chosen wildcard IPv4 port
for BOTH TCP and UDP. There is still a release-to-child-bind allocation race.
Only a structured address_in_use failure from an exited server permits one retry
with a new dual-checked port; other failures and test assertions are never retried.
Intentional collision tests exercise this path and print its diagnostics.

Each startup waits at most eight seconds for the flushed server_ready event emitted
after both binds and TCP accept startup. The fixture then performs one control
hello. Failure reports the executable, port, exit status, bounded combined output,
and whether the process exited early or stayed alive without readiness. Cleanup
is registered before startup, including when unittest setUp fails, and stops only
that fixture's Popen process, closes pipes, and joins its reader.

The original intermittent Windows ConnectionRefusedError was not reproduced with
evidence proving its cause. TCP-only port selection, missing readiness, and missing
setUp-failure cleanup were confirmed fixture defects; UDP collisions are a tested
possible cause, not a claimed explanation of the original failures.

To repeat complete integration suites without retrying a failed assertion:

~~~text
ctest --test-dir build -R "server_integration|background_integration" --repeat until-fail:5 --output-on-failure
~~~

until-fail repeats successful tests and stops that test on its first failure.
For focused readiness/failure checks: python tests/server_integration.py build StartupTests.

This update was built and the registered suites run on Windows x64 / MSVC and
Ubuntu 22.04 / GCC 11. The Ubuntu tests ran in an isolated WSL-hosted environment.
This does not establish native audio-device behavior on every Ubuntu desktop.

The audit follow-up verification completed:

| Check | Result |
| --- | --- |
| Windows x64 / VS 2022 Release | All seven CTest suites passed |
| Windows complete integration repetitions | Five runs of server_integration and five of background_integration, all passed; stop on first failure |
| Windows focused startup repetitions | Twenty rounds of the five StartupTests methods: 100 methods passed, including intentional failures and cleanup |
| Ubuntu 22.04 / GCC 11 Release | All seven suites passed |
| Ubuntu static-C++-runtime packaging build | All seven suites passed |
| Windows and Ubuntu extracted archives | Both packaged mocks passed, including resume/reconciliation; spaces in external extraction paths, packaged import origins checked |

The local v2.0.0-audit archives and checksums were rebuilt. Windows extraction
used a System32-only PATH. Linux extraction used the bundled-library launchers.
No release was published. These results do not establish the cause of the original
intermittent Windows startup failure or replace hardware/real-network testing.

## Hardware listening checklist

Use separate computers and headphones. Avoid multiple live microphones feeding
the same room's speakers.

1. Start a fresh server, create a Lobby, and join two foreground clients with the
   printed group/channel IDs. Confirm all three mutes are true initially.
2. Open one microphone with mic on. Unmute All on the receiver with all on.
   Confirm speech is audible only after these explicit choices.
3. Create a manual Game with two teams. Join at least two Team-1 players and one
   Team-2 player, supplying explicit team numbers. Start playing with the operator
   phase command.
4. Confirm Team-1 audio reaches its teammate even when that teammate transmits All.
   Confirm Team 2 cannot hear Team 1. Verify All reaches everyone in the Game.
5. Create a second Game and confirm no audio crosses between Games.
6. Test incoming All and Team mute separately, with the microphone left open.
   Check that unmuting does not play queued speech from the muted period.
7. Change teams and phases while talking. Verify receive routing, forced selection,
   and unchanged mute settings. Short rebuffer gaps during context changes are
   expected; stale audio in the new context is not.
8. Test two simultaneous speakers for mixing/clipping, then disconnect a speaker.
9. Test default-device initialization failure (missing/inaccessible microphone or
   playback device). Confirm audio_device_failed is reported without killing the
   background IPC process or another application.
10. Test Ctrl+C with an empty prompt and with a partially typed command. Restart
    the server immediately on the same TCP/UDP port.

## Manager and lifecycle checklist

Run tools/mock_gfx.py against a server without another manager:

~~~text
python tools/mock_gfx.py --port 8080 --client build/voice_client.exe --no-audio
~~~

This is the source-checkout command. Use python3 and build/voice_client on Ubuntu.
The automated suite runs this mock too. Extracted archives use their own tools
directory and top-level client; see [package example commands](gfx-integration.md#run-the-mock-external-application).

Maintainers can check an archive and checksum from the SOURCE CHECKOUT with:

~~~text
python packaging/smoke_test.py dist/realtime-voice-v2.0.0-audit-windows-x64.zip
python3 packaging/smoke_test.py dist/realtime-voice-v2.0.0-audit-ubuntu-x86_64.tar.gz
~~~

This extracts outside the checkout to an OS temporary directory containing spaces,
checks module import paths, and runs ONLY the packaged examples/binaries. Python
ignores PYTHONPATH/PYTHONHOME; Windows PATH is restricted to System32. Ubuntu uses
the package's top-level launchers and bundled libraries. Neither smoke test opens
audio devices or establishes microphone/headphone quality.

For manual inspection:

- Read the operator's group/channel details. One player subscribed to All and Team
  counts once in connected_players. Receive-membership counts include muted users;
  selected-transmitter counts include muted microphones.
- Leave the Game in playing mode through results. Its IDs and voice stay allocated.
- Return it to waiting. IDs remain, All is selected, Team sending is disabled.
- Remove a guest through management. Their old socket/UDP packets must not keep
  listening or sending. Reconnect with a new session ID and approve binding again.
- Delete a managed group from the operator CLI. Both management and clients must
  receive deletion. GFX should later disable voice without ending gameplay.
- Close the manager socket. Voice and group/session IDs must survive for ten seconds.
  Resume with manager_id/server_instance and reconcile owned groups. Check that an
  unrelated manager is rejected during grace. Without resume, only the departed
  manager's groups expire. Heartbeat silence on an open socket expires after ten
  seconds without adding a second grace. Explicit deletion remains immediate.
- Restart voice_server. Old server_instance must fail; do not auto-recreate an
  existing game session's voice group.
- Close the parent's stdin while connected, pending binding, and pending a TCP
  handshake. The child should stop cleanly. Keep draining stdout and stderr during
  normal operation.

## Network checks still requiring real deployments

Test between machines on different networks, including home NAT and mobile/Wi-Fi.
Allow the chosen server port for BOTH TCP and UDP. Confirm UDP loss/reordering,
network interruption, laptop sleep/wake, and long-call behavior.

A changed UDP endpoint needs a new approved voice connection; automatic NAT
rebinding is not implemented. Authentication, encrypted transport, and public
abuse protection are explicitly deferred. Keep these deployments trusted.

No gameplay/GFX threading or integration tests are claimed here: gfx_2d was not
modified. Follow the handoff contract before testing the integrated game.

