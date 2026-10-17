"""Minimal external application exercising management + background voice IPC."""
import argparse
from pathlib import Path
from background_voice import BackgroundVoice
from voice_api import VoiceAPI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--no-audio", action="store_true", help="Use device-free integration mode")
    parser.add_argument("--manager-id", type=int, help="Resume an existing logical manager")
    parser.add_argument("--server-instance", help="Required with --manager-id")
    args = parser.parse_args()
    if (args.manager_id is None) != (args.server_instance is None):
        parser.error("Supply --manager-id and --server-instance together")
    with VoiceAPI(args.host, args.port) as manager:
        resume = None if args.manager_id is None else {
            "manager_id": args.manager_id, "server_instance": args.server_instance}
        hello = manager.hello_manager(resume)
        if manager.ok("status")["gfx_game_groups"] >= 5:
            raise RuntimeError("GFX's five-group allocation limit has been reached")
        group = manager.ok("create_group", type="game", teams=2, name="Mock game")
        child = BackgroundVoice(args.client.resolve())
        deleted = False
        try:
            child.event("ready")
            child.ok("connect", host=args.host, port=args.port, name="Mock player", member_id="mock-player",
                server_instance=hello["server_instance"], group_id=group["group_id"],
                channel_id=group["all_channel_id"], audio=not args.no_audio)
            session = child.event("session_ready")
            manager.ok("bind_player", group_id=group["group_id"], session_id=session["session_id"],
                member_id="mock-player", team=1)
            child.event("connected")
            child.ok("set_microphone", muted=False)
            child.ok("set_incoming_all", muted=False)
            child.ok("set_incoming_team", muted=False)
            manager.ok("set_phase", group_id=group["group_id"], phase="playing")
            child.event("transmit_changed",
                lambda state: state["transmit_channel_id"] == group["team_channel_ids"][0])
            child.ok("set_transmit", channel_id=group["all_channel_id"])
            status = child.ok("get_status")
            assert status["receive_channels"] == [group["all_channel_id"], group["team_channel_ids"][0]]
            assert status["mutes"] == {"microphone": False, "all": False, "team": False}
            resumed = manager.reconnect_manager()
            retained = next(item for item in manager.owned_groups() if item["group_id"] == group["group_id"])
            assert resumed["manager_id"] == hello["manager_id"] and resumed["resumed"]
            assert retained["all_channel_id"] == group["all_channel_id"]
            assert retained["team_channel_ids"] == group["team_channel_ids"]
            assert retained["participants"][0]["session_id"] == session["session_id"]
            assert child.ok("get_status")["mutes"] == status["mutes"]
            # Death/respawn and match completion require no voice operation.
            manager.ok("set_phase", group_id=group["group_id"], phase="waiting")
            child.event("status", lambda state: state["phase"] == "waiting")
            manager.ok("delete_group", group_id=group["group_id"])
            deleted = True
            child.event("group_deleted")
            child.event("disconnected", lambda state: state["session_id"] == session["session_id"])
            child.ok("shutdown")
            print("Mock GFX lifecycle passed")
        finally:
            child.close()
            if not deleted:
                manager.request("delete_group", group_id=group["group_id"])


if __name__ == "__main__":
    main()

