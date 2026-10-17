# Background voice_client IPC — protocol 2

Launch the built client as a child process:

~~~text
voice_client --ipc
~~~

On Windows use build/voice_client.exe; on Ubuntu use build/voice_client.
Connect its stdin, stdout, and stderr to separate pipes. Do not merge stderr into
stdout. No interactive prompts are used. The child owns its TCP/UDP connections,
audio devices, Opus encoder/decoders, jitter buffers, and mixing.

For the remote manager API, see [server API](server-api.md).
For complete lifecycle examples, see [GFX integration](gfx-integration.md).

## Framing and process lifetime

stdin: one UTF-8 JSON request per LF-terminated line, at most 8192 bytes excluding
the line ending. CRLF is accepted. A partial line is buffered only up to this limit;
an oversized line is discarded through its newline and returns input_too_large.
An unterminated final line is discarded when EOF shuts down the process.

stdout: one UTF-8 JSON response/event per line, exclusively. Diagnostic logs go
to stderr, including Debug logs. Always continuously drain BOTH output streams.

Requests, successes, errors, and events use the same version-2 envelopes as the
remote API. IDs are nonempty strings of at most 64 bytes, without control characters.
IDs starting with __ are reserved for internal control messages. Use a unique ID
for each outstanding request. Responses correlate by id; event order may interleave.

The outgoing event queue is bounded to 128 messages. A parent that does not read
stdout cannot stall voice indefinitely: the child uses nonblocking output, detects
stdin EOF, and stops on output overflow. Exit code 2 reports output overflow.
Shutdown uses a bounded best-effort 250 ms final-output drain, so an unresponsive
parent may miss final events. Exit code 1 indicates background setup failure;
normal shutdown/EOF returns 0.

Closing stdin means the parent has left: the child stops capture/playback, joins
owned workers, closes sockets, and exits. Closing stdout also ends the IPC session.
A voice connection failure normally emits errors and leaves the IPC process alive.

## Fresh defaults and remembered choices

Every fresh process starts with microphone, incoming All, and incoming Team muted.
These three booleans are independent; true means muted.

Settings live only in memory. Disconnect/reconnect in the SAME child process,
transmit switches, server-forced changes, team changes, and match transitions retain
the user's choices. Disconnect always stops audio, even if remembered microphone
mute is false.

If GFX deliberately replaces its child process during the same GFX run, GFX must
remember the three choices and reapply them to the new child. A new child always
starts muted; it cannot infer another process's previous preferences.

Incoming mute changes invalidate buffered audio. Unmuting waits for fresh accepted
audio; it does not play a backlog from the muted route.
Audio in the affected direction stays gated until pending mute acknowledgements
arrive, including a rapid mute/unmute pair. Incoming mute changes leave microphone
transmission independent.

## Commands

Every request requires v:2, id, and op.

| op | Additional fields | Response |
| --- | --- | --- |
| connect | host, port, name, member_id, server_instance, group_id, channel_id; optional team, audio | accepted/connecting and connection_id |
| disconnect | None | Disconnected status; process stays alive |
| set_transmit | channel_id | Server-acknowledged voice state, or error |
| set_microphone | muted: boolean | Server-acknowledged state, or remembered preferences while disconnected/pending |
| set_incoming_all | muted: boolean | Same |
| set_incoming_team | muted: boolean | Same |
| set_mutes | microphone, all, team: booleans | Set all three explicitly |
| get_status | None | Current local status |
| shutdown | None | Empty success, followed by process exit |

host is a nonempty IPv4 address/domain string up to 253 bytes. port is an integer
1..65535. name/member_id are nonempty strings up to 64 UTF-8 bytes. IDs are uint32
numbers. server_instance is the hexadecimal instance obtained from management.
team defaults to 0 and is used for MANUAL joins; GFX teams come from bind_player.

audio defaults to true. Set audio:false only for device-free integration tests:
networking, decoding, buffering, and mixing still run, but devices are not opened
and no microphone audio is generated. Actual audio never passes through IPC.

Connect is asynchronous:

~~~json
{"v":2,"id":"connect-1","op":"connect","host":"127.0.0.1","port":8080,"name":"Pulkit","member_id":"player-17","server_instance":"0123456789abcdef","group_id":1,"channel_id":1}
~~~

~~~json
{"v":2,"id":"connect-1","ok":true,"data":{"accepted":true,"state":"connecting","connection_id":1}}
~~~

Acceptance means that connection work started, not that voice is usable.
Wait for session_ready, have GFX bind the player if needed, then wait for connected.
For a manually created group the child automatically issues its manual join.

Only one connect may be active. Explicit disconnect permits a new connect.
No automatic group creation, automatic reconnect, or silent instance replacement
takes place. Failed connect does not exit the background process.

Changes to mutes while disconnected/pending are accepted locally with
{"mutes":{...},"applied":false}; they are applied when membership becomes available.
Connected controls await the remote acknowledgement. At most 128 remote requests
are kept outstanding; overload returns too_many_requests/control_unavailable.

## Status and connection IDs

The state is connecting, pending, joined, or disconnected.

Before hello finishes, status may only contain state, mutes, and counters.
After session_ready it includes session_id, server_instance, requested_group_id,
member_id, group_id (0 while unbound/disconnected), transmit_channel_id (null when
not joined), receive_channels, audio_enabled, and audio_initialized.
Joined status also contains phase, group_type, team, all_channel_id, team_channel_id,
send_revision, receive_revision, peers, decoded_frames, and mixed_frames.

The selected transmit channel is NOT a receive subscription. A player transmitting
to All still has All + own Team in receive_channels.

An unexpected manager TCP loss does not disconnect this voice client during the
ten-second management recovery grace. A successful manager resume leaves these
session IDs, routes, selections, and mute settings unchanged. If recovery/heartbeat
expires, the group is deleted and this client reports group_deleted/disconnected.

get_status adds connection_id. Each accepted connect increments this local number.
All connection-originated events carry connection_id inside data. It scopes events
even before a server session ID exists. Ignore stale events from another local
connection or server instance. Request responses remain correlated by request id.

mutes in local status are the current remembered preferences. A pending server
response may acknowledge an earlier requested snapshot if commands were pipelined.
Process correlated responses and subsequent status events accordingly.

## Events

| event | data |
| --- | --- |
| ready | protocol:2, fresh mutes; emitted once at process startup |
| session_ready | Pending transport status, session_id/server_instance/connection_id; forward binding information to GFX |
| connected | Joined status after required device initialization succeeds |
| transmit_changed | Status with the new selected channel, including server-forced changes |
| mute_changed | Current microphone/all/team preferences |
| status | Updated membership/routing state |
| group_deleted | group_id, reason, connection_id |
| membership_removed | group_id, connection_id |
| server_disconnected | message, connection_id |
| error | code, message, request_id of connect, connection_id |
| disconnected | Final status after cleanup |

mute_changed generated by a purely local disconnected command has no connection_id.
Connected request responses can also be followed by a mute_changed notification;
treat the booleans as current state rather than counting notifications.

Example independent controls:

~~~json
{"v":2,"id":"mic","op":"set_microphone","muted":false}
{"v":2,"id":"all","op":"set_incoming_all","muted":false}
{"v":2,"id":"team","op":"set_incoming_team","muted":true}
~~~

This opens the microphone, listens to All, and keeps Team muted. Changing the
transmit channel does not change any of those choices.

## Errors and failure handling

Command validation errors are responses with ok:false and an error code/message.
Malformed JSON/invalid IDs may produce id:null. Unknown operations return
unknown_operation; creating/deleting/listing groups is not part of this interface.

Remote errors (group_missing, invalid_channel, team_disabled, server_restarted,
etc.) are preserved. Local errors include invalid_request, incompatible_version,
input_too_large, duplicate_id, already_connected, not_connected, too_many_requests,
control_unavailable, disconnected, and client_error.

Asynchronous connect/device errors use event:error, for example:
connection_failed, connection_error, codec_failed, audio_device_failed,
group_missing, or server_restarted. Connect errors include the initiating request_id.
Pending controls receive disconnected if their connection ends before acknowledgement.

GFX should disable unavailable voice controls and continue gameplay. Failure of this
child must not terminate gfx.exe or its game session.

## Python driver and mock

tools/background_voice.py demonstrates reading JSON responses/events separately
from draining diagnostics. tools/mock_gfx.py combines it with tools/voice_api.py
to exercise the exact remote-manager/child-binding handshake.

These Python tools are examples/tests, not an implementation of gfx_2d.

