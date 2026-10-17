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

This update was built and the registered suites run on Windows x64 / MSVC and
Ubuntu 22.04 / GCC 11. The Ubuntu tests ran in an isolated WSL-hosted environment.
This does not establish native audio-device behavior on every Ubuntu desktop.

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

Use build/voice_client on Ubuntu. The automated suite runs this mock too.

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
- Close the manager socket. Its groups must disappear immediately; manual groups
  remain. An open but nonresponding manager times out after ten seconds.
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

