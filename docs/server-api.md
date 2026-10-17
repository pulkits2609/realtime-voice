# Voice server API — protocol 2

This document describes the implemented TCP management and voice-control API.
For the child-process interface, see [client IPC](client-ipc.md).
For the game lifecycle and complete examples, see [GFX integration](gfx-integration.md).

## Build and start

From a source checkout: Windows runs setup/setup.bat once, then build.bat -release.
Ubuntu 22.04+ runs bash setup/setup.sh once, then bash build.sh -release.

~~~text
voice_server 8080
~~~

Source-build executables are build/voice_server.exe and build/voice_server.
In an extracted package, use .\voice_server.exe 8080 on Windows or
./voice_server 8080 on Ubuntu (the top-level bundled-library launcher).
The optional Python examples are included under tools; see the separate source
and package commands in [GFX integration](gfx-integration.md).
One process listens on the chosen IPv4 port for both TCP and UDP. Groups do not
allocate processes or extra ports. The server starts with no groups.

There is no disk persistence. Restarting the server destroys every session, group,
and channel. Its server_instance changes on every start.

For launchers/tests, stdout emits a flushed JSON event only after both transports
are bound and the TCP accept loop has started:

~~~json
{"v":2,"event":"server_ready","data":{"port":8080,"tcp":true,"udp":true}}
~~~

This is a process log, not a TCP frame. The preliminary Starting Voice Server
message is not readiness. Socket startup failures emit server_error to stderr
with code address_in_use or socket_error, native_code, and message, then exit 1.

## Trust model and ownership

This version is for trusted testing. Roles, names, supplied member_id values,
session/group/channel IDs, revisions, and server_instance are NOT authenticated.
There are no accounts, secrets, tickets, encryption, or secure UDP registration.
A process can claim the manager role if the management slot is free.

Exactly one logical manager may own the server, with at most one active TCP
management connection. A server-assigned manager_id owns its Games; session_id
identifies only the current socket. A different live manager receives manager_busy. Management creates
Game groups; the operator CLI creates Game or Lobby groups.

A manager can query the global summary but mutate/query detailed membership only
for its own groups. The operator can inspect or delete any group. Ordinary voice
sessions have no creation, deletion, team-assignment, or global-listing commands.

An unexpected TCP loss reserves ownership for ten seconds from the server's
observation of the disconnect. Groups, channels, participants, routing, phase,
transmit selection, and mutes remain intact. Voice continues. Only a resume with
the same manager_id and server_instance can reclaim that reserved ownership.
These public identifiers provide no authentication. Manually created groups are
unaffected by management expiry.

## TCP framing and envelopes

Every frame is a four-byte UNSIGNED BIG-ENDIAN byte length, followed by exactly
that many bytes of UTF-8 JSON. Length must be 1..65536. TCP reads may be partial;
do not assume one recv call equals one frame. JSON member ordering is irrelevant.

Normal requests:

~~~json
{"v":2,"id":"request-1","op":"status"}
~~~

Success and error:

~~~json
{"v":2,"id":"request-1","ok":true,"data":{}}
{"v":2,"id":"request-1","ok":false,"error":{"code":"group_missing","message":"The group no longer exists"}}
~~~

Asynchronous event:

~~~json
{"v":2,"event":"group_deleted","data":{"group_id":1,"reason":"operator_deleted"}}
~~~

Responses correlate by id, not position. Events may interleave with responses and
may precede the response to the operation that caused them. Use unique IDs for
outstanding requests; there is no replay/idempotency cache.

Each connection has one TCP writer and a bounded 128-frame outgoing queue. A slow
reader is disconnected rather than silently losing reliable control messages.

## Types and limits

| Field | Type / limit |
| --- | --- |
| v | Integer, exactly 2; booleans and strings are rejected |
| id | Nonempty string, at most 64 UTF-8 bytes, no control characters |
| op, role | String, at most 64 bytes |
| name, member_id | Nonempty string, at most 64 UTF-8 bytes, no control characters |
| group_id, channel_id, session_id | Positive uint32 integer |
| manager_id | Positive uint32 logical owner, scoped to server_instance |
| team | 1..team count for Game; 0 for Lobby |
| server_instance | Server-generated 16-character hexadecimal string |
| mute fields | JSON booleans, never 0/1 or strings |
| connected players | At most 128 per group; pending unbound sessions are excluded |
| list page | At most 32 groups, ordered by group_id |

Unknown extra fields are ignored. Required fields and recognized values are
validated. There is no global five-group limit. GFX must enforce its own limit of
five live Game groups, including waiting lobbies, matches, results, and lobby
returns. Keep a map keyed by server_instance + group_id; do not count child
channels as additional allocations.

Group IDs and channel IDs have separate numeric namespaces. A channel ID may
numerically equal a group ID. Always use the correct field/operation. IDs are
not reused within one server instance. Scope every cached ID to server_instance.

## First request: hello

Manager:

~~~json
{"v":2,"id":"hello","op":"hello","role":"manager"}
~~~

~~~json
{"v":2,"id":"hello","ok":true,"data":{"session_id":1,"server_instance":"0123456789abcdef","role":"manager","state":"ready","binding_required":false,"manager_id":1,"resumed":false,"owned_group_count":0}}
~~~

Keep manager_id and server_instance in memory for the logical GFX server's run.
After unexpected TCP loss, open a new connection and send:

~~~json
{"v":2,"id":"resume","op":"hello","role":"manager","manager_id":1,"server_instance":"0123456789abcdef"}
~~~

~~~json
{"v":2,"id":"resume","ok":true,"data":{"session_id":9,"server_instance":"0123456789abcdef","role":"manager","state":"ready","binding_required":false,"manager_id":1,"resumed":true,"owned_group_count":2}}
~~~

manager_id is a positive uint32, monotonic and never reused in this instance.
Its namespace is separate from session IDs. Both resume fields are required;
omitting manager_id requests NEW ownership, which is rejected while reserved.
An already connected owner returns manager_busy even for a matching resume.
An unknown/expired ID returns manager_expired. A different instance returns
server_restarted. Invalid fields return invalid_request. No failed hello changes
the recovery deadline. If the old handler has not observed EOF yet, retry the
matching resume after manager_busy within the original deadline.

A successful resume cancels the old recovery deadline without changing group or
voice session IDs. No group_created/group_deleted event is emitted for retained
groups. Use list_groups pages, filter owner.manager_id, and fetch group_details
for current participants. Replace the caller's cached map with this state: operator
deletions during the gap are absent. There is no replay of missed events.

Voice connection, with an already known Game:

~~~json
{"v":2,"id":"hello","op":"hello","role":"voice","name":"Pulkit","member_id":"gfx-player-17","server_instance":"0123456789abcdef","group_id":1,"channel_id":1,"mutes":{"microphone":true,"all":true,"team":true}}
~~~

~~~json
{"v":2,"id":"hello","ok":true,"data":{"session_id":2,"server_instance":"0123456789abcdef","role":"voice","state":"pending","binding_required":true}}
~~~

This pending session has NO voice membership and cannot transmit or receive.
The manager must bind its server-assigned session_id to the intended member_id,
group, and authoritative team. See the handshake below.

For a manual group, binding_required is false. A voice client sends join next.
Manual voice hello may omit server_instance on an initial standalone connection;
managed hello and the background-client connect command require it.
A supplied old instance returns server_restarted before joining.

The requested channel must belong to the supplied group. For managed joins,
binding determines the initial selection: All while waiting, own Team while playing.

Missing version/old binary handshakes receive incompatible_version when a valid
bounded frame can be read. Invalid/oversized framing closes the connection.
Protocol 1 is not supported.

## Heartbeats

The server sends a challenge immediately after hello and approximately every two
seconds. Until acknowledged, it may repeat the same nonce:

~~~json
{"v":2,"event":"ping","data":{"nonce":1}}
~~~

Answer with this special notification; it has no request id and gets no response:

~~~json
{"v":2,"op":"pong","nonce":1}
~~~

Only a valid outstanding nonce refreshes the heartbeat deadline. Duplicate/stale
pongs and ordinary requests do not. The first heartbeat deadline begins when hello
is accepted; each valid outstanding pong starts a new ten-second deadline.
Expiry closes TCP and destroys that manager's groups immediately, with NO second
recovery grace. A short pause can recover on the same socket before this deadline.

Unexpected TCP loss detected BEFORE heartbeat expiry starts the separate recovery
deadline described above. Once disconnected, failed resume attempts do not refresh
it. Successful resume starts the new socket's heartbeat budget; stale callbacks,
pongs on old sockets, and cancelled ownership deadlines cannot affect it.

Recovery expiry deletes the owner's Games exactly once, disconnects their voice
members, and releases the slot. Explicit delete_group, operator deletion, and
server shutdown remain immediate. A server restart destroys all state and changes
server_instance; old resume IDs return server_restarted, never recreate groups.

The background voice client handles these heartbeats itself. The future gfx_server
must independently service manager heartbeats while gameplay continues.

## Management operations

All rows below also require v, id, and op. Counts and IDs are integers.

| op | Additional fields | data |
| --- | --- | --- |
| create_group | type: "game", teams: 2..4, optional name | Full group details and channel mapping |
| list_groups | Optional after_id: uint32, default 0 | Summary page, totals, next_after_id |
| status | Same optional after_id | Same result as list_groups |
| group_details | group_id | Full details of an owned group |
| bind_player | group_id, session_id, member_id, team | Bound voice session status |
| set_team | group_id, session_id, team | session_id |
| set_phase | group_id, phase: "waiting" or "playing" | Updated group details |
| remove_player | group_id, session_id | session_id |
| delete_group | group_id | group_id |
| delete_channel | Not supported | delete_group_required |

A list result contains server_instance, groups, gfx_game_groups, total_groups,
and next_after_id. If next_after_id is non-null, request the next page using it.
gfx_game_groups counts manager-owned Game groups, not manually created groups.

Full group details include group_id, name, type, phase, owner, all_channel_id,
team_channel_ids, connected_players, channels, and participants.
owner is {"type":"manual"} or {"type":"gfx","manager_id":1}.
The old owner.session_id field is replaced by owner.manager_id; ownership no
longer changes when a manager's TCP session changes.

Each channel has channel_id, name, receive_members, and selected_transmitters.
receive_members counts eligible subscriptions, even if locally muted; while
waiting, Team subscriptions exist but nobody may transmit to Team.
selected_transmitters counts selected destinations, even with microphone muted.
connected_players counts each joined session ONCE, not once per subscription.

Each participant has session_id, name, member_id, team, and transmit_channel_id.

## Binding and membership

1. The manager creates a Game and passes server_instance/group/channel mappings
   through GFX to the player's local background client.
2. The child connects and emits session_ready with its server-assigned session_id.
3. gfx.exe passes that session_id and the known player identity to gfx_server.
4. gfx_server sends bind_player. The server verifies the pending intent's
   group/member_id and the manager's ownership.
5. The client receives state, initializes its devices, and emits connected.
6. An audio-device failure emits an error and disconnects that voice session.

bind_player refuses an active duplicate member_id in the same group. For an
approved rejoin, remove the previous membership (or observe its TCP removal)
before binding the replacement session. No 60-second game-rejoin policy lives here.

set_team is authoritative. A waiting player stays on All. During play, someone
already transmitting to Team follows their new team; someone transmitting to All
stays on All. Receive membership changes to All + the new team.

remove_player immediately removes membership and closes that voice connection.
A client cannot restore it using queued UDP or a normal join request.

## Voice-session operations

| op | Additional fields | Result |
| --- | --- | --- |
| join | group_id, channel_id, team | Join the manual group named in hello |
| status | None | Own routing state and same-group peer metadata |
| set_transmit | channel_id | Select All or own Team; Team requires playing |
| set_mutes | microphone, all, team: booleans | Apply independent mute states |

Voice status contains session_id, server_instance, state, group_id, group_type,
phase, team, all_channel_id, team_channel_id (null in Lobby), transmit_channel_id,
receive_channels, send_revision, receive_revision, mutes, and peers.
Each peer includes session_id, name, team, and send_revision.

All status receive_channels describe eligibility, independently of selected
transmit destination and mute preferences. Lobby has only its shared All-like route.

set_phase to playing forces every member to their assigned Team. Returning to
waiting forces All and disables Team transmission. Neither changes mute settings.
Repeating the current phase is a no-op. Match completion alone and death/respawn
require NO voice request; the group remains allocated.

## Routing and stale audio

All audio reaches eligible members of the same group, except its sender.
Team audio reaches only that sender's teammates, even if they are transmitting
to All themselves. The server validates the packet against CURRENT membership,
team, phase, transmit selection, and revisions. A claimed target alone is insufficient.

A sender revision changes when its microphone, transmit context, or membership
changes. A receiver revision changes when its receive context or incoming mutes
change. Peer metadata lets receivers discard an old sender revision too.

The client flushes affected capture/jitter/playback state and rejects old contexts.
Transitions can briefly rebuffer; this does not change the other route's mute setting.
Already submitted hardware audio cannot be recalled.

## UDP wire format

Maximum datagram: 1024 bytes. All integers below are LITTLE-ENDIAN.
The 28-byte routing wrapper precedes the existing 13-byte voice packet header.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 2 | ASCII RV |
| 2 | 1 | Protocol version 2 |
| 3 | 1 | Reserved, must be 0 |
| 4 | 4 | group_id |
| 8 | 4 | channel_id |
| 12 | 4 | Sender send_revision |
| 16 | 4 | Recipient receive_revision; clients send 0 |
| 20 | 8 | Numeric server instance, encoded from hexadecimal server_instance |
| 28 | 1 | Voice packet type 2 |
| 29 | 4 | Opus payload byte count |
| 33 | 4 | Sender session_id |
| 37 | 4 | Sequence number |
| 41 | 0..983 | One 960-sample / 20 ms Opus frame, or empty keepalive |

The server stamps the recipient revision separately for each recipient. It does
not decode or mix Opus. Empty voice packets register/maintain the UDP endpoint
and are never relayed. The endpoint is pinned for that TCP session; network/NAT
endpoint changes need a new approved connection. This pinning is not authentication.

## Events and errors

Management events: group_created, group_updated, group_deleted.
group_created carries group_id/type; group_updated carries group_id; query details
when needed. group_deleted carries group_id/reason. Treat events and creation
responses idempotently in your group map.

Voice events: state, group_deleted, membership_removed, and ping.
Deletion reasons include manager_deleted, operator_deleted, manager_timeout.
A group deletion or membership removal is followed by TCP closure. The child
converts transport loss into local server_disconnected/disconnected events.

Errors include invalid_request, incompatible_version, hello_required, manager_busy,
group_missing, invalid_group, invalid_channel, invalid_team, invalid_team_count,
binding_required, binding_mismatch, member_in_use, session_missing, already_joined,
not_joined, forbidden, not_owner, wrong_role, wrong_group_type, invalid_phase,
team_disabled, group_full, ids_exhausted, delete_group_required, unknown_operation,
server_restarted, manager_expired, and connection_error. Framing/heartbeat/send timeouts can instead
close TCP. Invalid requests do not grant membership or change a valid selection.

