# Integrating GFX with realtime-voice

This is the handoff contract for future gfx_server/gfx.exe work. No gfx_2d files
are changed by this update.

Read [server API](server-api.md) for the remote protocol and
[client IPC](client-ipc.md) for the local process protocol. Both are version 2.

## Responsibilities and flow

~~~mermaid
sequenceDiagram
    participant VS as voice_server
    participant GS as gfx_server
    participant G as gfx.exe
    participant C as voice_client --ipc
    GS->>VS: hello(manager), create_group(game, teams)
    VS-->>GS: server_instance + group/channel mapping
    GS-->>G: voice configuration + gameplay player identity
    G->>C: connect
    C->>VS: hello(voice, pending intent)
    VS-->>C: assigned session_id
    C-->>G: session_ready
    G->>GS: player identity + assigned voice session_id
    GS->>VS: bind_player(authoritative team)
    VS-->>C: state(joined)
    C-->>G: connected
    Note over VS,C: Encoded audio travels directly over UDP
~~~

Only gfx_server creates/deletes its Games and assigns gameplay teams.
gfx.exe never gets a normal group-creation/listing command in its voice child.
Player-requested transmit switches go through gfx_server's gameplay checks first;
gfx.exe then passes the resolved channel_id to its child. The voice server still
validates that destination against current membership/team/phase.

All connections are unauthenticated in this trusted-testing version. A supplied
player identity or management role is not proof of identity.

## Complete two-team creation example

Open one management TCP connection, using four-byte big-endian length-prefixed
JSON frames. Send:

~~~json
{"v":2,"id":"manager-hello","op":"hello","role":"manager"}
~~~

Example response:

~~~json
{"v":2,"id":"manager-hello","ok":true,"data":{"session_id":1,"server_instance":"0123456789abcdef","role":"manager","state":"ready","binding_required":false,"manager_id":1,"resumed":false,"owned_group_count":0}}
~~~

Create the host's Game:

~~~json
{"v":2,"id":"create-1","op":"create_group","type":"game","teams":2,"name":"Friday match"}
~~~

Complete example response:

~~~json
{
  "v":2,
  "id":"create-1",
  "ok":true,
  "data":{
    "group_id":1,
    "name":"Friday match",
    "type":"game",
    "phase":"waiting",
    "owner":{"type":"gfx","manager_id":1},
    "all_channel_id":1,
    "team_channel_ids":[2,3],
    "connected_players":0,
    "channels":[
      {"channel_id":1,"name":"All","receive_members":0,"selected_transmitters":0},
      {"channel_id":2,"name":"Team 1","receive_members":0,"selected_transmitters":0},
      {"channel_id":3,"name":"Team 2","receive_members":0,"selected_transmitters":0}
    ],
    "participants":[]
  }
}
~~~

IDs are examples, not fixed constants. Persist them only in this GFX run's memory,
together with server_instance. A second Game gets independent IDs and routing.

## Five-group allocation belongs to GFX

Reserve a slot before sending an asynchronous create request. Count existing
Game groups PLUS pending reservations toward five. Release a reservation on
creation failure; release an allocation on group_deleted.

Waiting game lobbies, active matches, results, and lobby returns all retain one
allocation each. Team channels do not count separately. Manual groups do not count.

Use management status/list_groups to reconcile after missed UI events; follow
next_after_id pages. Update a map by group ID rather than incrementing once for
both a creation response and group_created event. The standalone voice server
deliberately permits more than five Game groups.

## Player connects and is bound

Start voice_client --ipc once per local GFX application run. Continuously drain
its stdout and stderr on owned GFX workers/event handling. Do not block the render
or gameplay loop while waiting for voice. These threading changes belong to the
future GFX integration, not this repository.

The child first emits:

~~~json
{"v":2,"event":"ready","data":{"protocol":2,"mutes":{"microphone":true,"all":true,"team":true}}}
~~~

gfx.exe sends a JSON LINE, not a TCP-length-prefixed frame:

~~~json
{"v":2,"id":"connect-17","op":"connect","host":"voice.example.com","port":8080,"name":"Pulkit","member_id":"player-17","server_instance":"0123456789abcdef","group_id":1,"channel_id":1}
~~~

~~~json
{"v":2,"id":"connect-17","ok":true,"data":{"accepted":true,"state":"connecting","connection_id":1}}
~~~

After its remote hello completes, the child emits session_ready. Its data includes:

~~~json
{"session_id":2,"server_instance":"0123456789abcdef","state":"pending","binding_required":true,"group_id":0,"requested_group_id":1,"member_id":"player-17","transmit_channel_id":null,"receive_channels":[],"connection_id":1}
~~~

This block illustrates the binding fields; the actual event also includes mutes,
audio settings, and counters. This is NOT joined membership.

gfx.exe reports the new voice session ID to gfx_server using its existing gameplay
connection. gfx_server checks which actual gameplay player made the report and
binds that pending voice session:

~~~json
{"v":2,"id":"bind-17","op":"bind_player","group_id":1,"session_id":2,"member_id":"player-17","team":1}
~~~

Only after this does the server grant membership. The child initializes its audio
devices and emits connected. Waiting members transmit to All, with receive_channels
[1,2] for Team 1. All three mute settings remain true until the user changes them.

If voice connection, group creation, or device initialization fails, report the
problem and disable voice controls. Continue gameplay. An error in the voice child
must not shut down gfx.exe.

## Waiting-lobby team changes

GFX changes the authoritative voice team when gameplay team assignment changes:

~~~json
{"v":2,"id":"team-17","op":"set_team","group_id":1,"session_id":2,"team":2}
~~~

While waiting, transmit remains All. Receive eligibility becomes All + Team 2.
Incoming mute preferences and microphone mute are preserved. For the following
match-start example, set team back to 1 with the same operation.

A client cannot make itself a different team using its transmit command.

## Start match: force Team transmission

gfx_server sends:

~~~json
{"v":2,"id":"start-1","op":"set_phase","group_id":1,"phase":"playing"}
~~~

The voice server forces every member to their assigned Team. It broadcasts state;
the local child emits transmit_changed with transmit_channel_id 2 for player-17.
There is no need for gfx.exe to race a separate force-Team command.

Microphone, incoming All, and incoming Team mute settings are unchanged.

## Switch to All, still receive own Team

The player requests All through GFX's normal gameplay control path. After approval,
gfx.exe sends its child:

~~~json
{"v":2,"id":"all-17","op":"set_transmit","channel_id":1}
~~~

Its selected destination becomes 1, while receive_channels remains [1,2].
A teammate transmitting to Team 1 still reaches this player. Team 2 and other
Games cannot reach this player through normal routing. The sender receives no echo
of its own relayed packets.

A rejected switch leaves the previous selection intact.

## Independent mute example

These are local JSON-lines requests:

~~~json
{"v":2,"id":"mic-open","op":"set_microphone","muted":false}
{"v":2,"id":"hear-all","op":"set_incoming_all","muted":false}
{"v":2,"id":"mute-team","op":"set_incoming_team","muted":true}
~~~

Final preferences: microphone open, All audible, Team muted. Muting Team does not
mute All or close the microphone. Opening the microphone keeps it open across
transmit switches, forced switches, results, and reconnect commands in this child.

To listen to Team again:

~~~json
{"v":2,"id":"hear-team","op":"set_incoming_team","muted":false}
~~~

Old buffered Team audio is discarded. Fresh child processes always start muted.

## Death, results, and return to lobby

Death/respawn: send NO voice membership, phase, transmit, or mute operation.

Match completion/results: send NO deletion. Keep the same Game and channels.
Voice permissions remain in playing mode until GFX explicitly returns to waiting.

On return to the gameplay lobby:

~~~json
{"v":2,"id":"return-1","op":"set_phase","group_id":1,"phase":"waiting"}
~~~

Everyone selects All again; Team transmission is disabled. Existing mutes remain.
The next match reuses group 1 and channels 1/2/3.

## Guest departure and approved rejoin

gfx_server removes the departing guest:

~~~json
{"v":2,"id":"leave-17","op":"remove_player","group_id":1,"session_id":2}
~~~

The server stops forwarding voice, emits membership_removed, and closes that
guest's voice connection. Their child remains available for get_status/reconnect.

GFX alone applies its 60-second rejoin policy. If approved, connect the child again,
wait for its NEW session_ready ID, then bind_player with the restored team.
Do not reuse the old voice session ID. If an old membership is still connected,
remove it before binding its replacement.

## Host departure / lobby disband

gfx_server sends:

~~~json
{"v":2,"id":"disband-1","op":"delete_group","group_id":1}
~~~

Every child channel is destroyed, remaining voice participants are disconnected,
and manager/clients receive group_deleted. The GFX allocation can be released.
There is no pause/resume or individual Game-child deletion.

An operator's delete command has the same voice effect and reason
operator_deleted. GFX disables voice for that game; it must NOT interpret this
as a request to stop the gameplay match.

## Management heartbeat failure and server restart

Manager heartbeat:

~~~json
{"v":2,"event":"ping","data":{"nonce":7}}
{"v":2,"op":"pong","nonce":7}
~~~

Service it independently of gameplay work. The heartbeat deadline begins at hello
acceptance and refreshes only on a valid outstanding pong. Ten seconds of silence
closes the socket and deletes its Games; no additional grace is added.

Unexpected TCP loss BEFORE heartbeat expiry instead starts ten seconds of recovery
from when the server observes the loss. The logical ownership slot stays reserved;
existing groups, channels, player sessions, team/phase state, selections, and mute
choices survive and voice continues. A new unrelated manager receives manager_busy.

Reconnect with the public, in-memory registration from the initial hello:

~~~json
{"v":2,"id":"resume-1","op":"hello","role":"manager","manager_id":1,"server_instance":"0123456789abcdef"}
~~~

~~~json
{"v":2,"id":"resume-1","ok":true,"data":{"session_id":9,"server_instance":"0123456789abcdef","role":"manager","state":"ready","binding_required":false,"manager_id":1,"resumed":true,"owned_group_count":1}}
~~~

session_id changes; manager_id and owned IDs remain. Fetch list_groups pages,
filter owner.manager_id, then group_details for each retained group. Reconcile
the GFX allocation map, including operator deletions during the gap. Retained
groups do not emit fake creation/deletion events, and missed events are not replayed.
The helper demonstrates hello_manager(), reconnect_manager(), and owned_groups().

Failed resume attempts never extend the deadline. A matching owner can briefly get
manager_busy until the old handler observes EOF; the helper retries only this
handoff race for at most one second. Expiry deletes only that owner's groups and
returns manager_expired on later resume. Manual groups survive. Explicit host
departure/delete_group and operator deletion remain immediate, including during
the grace period. manager_id is an ownership identifier, NOT a secret or authentication.

If voice_server stops/restarts, everything it hosted disappears. Existing children
emit server_disconnected and disconnected; their IPC processes can remain alive.
The new server_instance differs, and an old connect configuration returns
server_restarted instead of attaching to newly reused numeric IDs.

GFX marks the affected existing game sessions' voice unavailable. Do not silently
create replacement groups or reconnect them to some other lobby. Gameplay continues.

## Run the mock external application

Start voice_server first, with no other manager connected, then:

~~~text
python tools/mock_gfx.py --host 127.0.0.1 --port 8080 --client build/voice_client.exe --no-audio
~~~

On Ubuntu use build/voice_client without .exe. Remove --no-audio only for a manual
device test. The mock creates a Game, launches the child, binds its session, changes
mutes, starts a match, switches transmit, returns to waiting, deletes the group,
resumes management without replacing the group/player session, reconciles owned
groups, and shuts down the child.

The implementation is in tools/mock_gfx.py, tools/voice_api.py, and
tools/background_voice.py. They demonstrate actual working protocols, not a planned
GFX implementation. No audio samples pass through these Python control pipes.

## Hand this back to the gfx_2d chat

Provide these three documents and tools/mock_gfx.py. Ask that integration to:
implement logical manager registration/resume/heartbeat, reserve at most five Games,
follow the session_ready -> bind_player -> connected handshake, maintain the
lifecycle above, and drive one background voice child per gfx.exe.

Preserve mute choices in GFX memory if it replaces its child. Scope asynchronous
events by connection_id and server_instance. Voice failures disable voice controls
and notify users/hosts, without ending gameplay.

