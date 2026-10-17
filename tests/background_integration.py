"""Background IPC checks use simulated audio/device-free client sessions."""
import json
import queue
import socket
import subprocess
import threading
import time
import unittest
import server_integration as server
from background_voice import BackgroundVoice
from voice_api import VoiceAPI


class BackgroundTests(unittest.TestCase):
    setUp = server.ServerTests.setUp
    tearDown = server.ServerTests.tearDown
    peer = server.ServerTests.peer
    game = server.ServerTests.game

    def child(self):
        child = BackgroundVoice(server.BUILD / ("voice_client" + server.EXE))
        self.addCleanup(child.close)
        self.assertEqual(child.event("ready")["mutes"], {"microphone": True, "all": True, "team": True})
        return child

    def connect(self, child, group, member="gfx-player"):
        response = child.ok("connect", host="localhost", port=self.port, name=member, member_id=member,
            server_instance=self.info["server_instance"], group_id=group["group_id"],
            channel_id=group["all_channel_id"], audio=False)
        self.assertTrue(response["accepted"])
        ready = child.event("session_ready")
        self.manager.ok("bind_player", group_id=group["group_id"], session_id=ready["session_id"],
            member_id=member, team=1)
        return child.event("connected")

    def test_controls_lifecycle_and_reconnect(self):
        group = self.game()
        child = self.child()
        child.ok("set_microphone", muted=False)
        joined = self.connect(child, group)
        self.assertFalse(joined["mutes"]["microphone"])
        child.ok("set_incoming_all", muted=False)
        self.assertEqual(child.ok("get_status")["mutes"], {"microphone": False, "all": False, "team": True})
        child.ok("set_incoming_team", muted=False)
        self.manager.ok("set_phase", group_id=group["group_id"], phase="playing")
        forced = child.event("transmit_changed",
            lambda state: state["transmit_channel_id"] == group["team_channel_ids"][0])
        self.assertEqual(forced["mutes"], {"microphone": False, "all": False, "team": False})
        child.ok("set_transmit", channel_id=group["all_channel_id"])
        status = child.ok("get_status")
        self.assertEqual(status["transmit_channel_id"], group["all_channel_id"])
        self.assertEqual(status["receive_channels"], [group["all_channel_id"], group["team_channel_ids"][0]])
        self.manager.ok("set_team", group_id=group["group_id"], session_id=joined["session_id"], team=2)
        child.event("status", lambda state: state["team"] == 2)
        self.assertEqual(child.request("set_transmit", channel_id=group["team_channel_ids"][0])["error"]["code"], "invalid_channel")
        self.manager.ok("set_phase", group_id=group["group_id"], phase="waiting")
        child.event("status", lambda state: state["phase"] == "waiting")
        self.assertEqual(child.request("set_transmit", channel_id=group["team_channel_ids"][1])["error"]["code"], "team_disabled")
        child.ok("disconnect")
        self.assertEqual(child.ok("get_status")["mutes"], {"microphone": False, "all": False, "team": False})
        deadline = time.monotonic() + 3
        while self.manager.ok("group_details", group_id=group["group_id"])["connected_players"]:
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        second = self.connect(child, group)
        self.assertNotEqual(joined["session_id"], second["session_id"])
        self.assertEqual(second["mutes"], {"microphone": False, "all": False, "team": False})
        self.manager.ok("delete_group", group_id=group["group_id"])
        child.event("group_deleted")
        child.event("disconnected")
        self.assertEqual(child.ok("get_status")["state"], "disconnected")
        self.assertEqual(child.request("create_group", type="game", teams=2)["error"]["code"], "unknown_operation")
        child.ok("shutdown")

    def test_parsing_eof_and_muted_defaults(self):
        child = self.child()
        self.assertEqual(child.request("get_status", v=1)["error"]["code"], "incompatible_version")
        self.assertFalse(child.request("set_microphone", muted=1)["ok"])
        child.process.stdin.write("not-json\n" + ("x" * 9000) + "\n")
        child.process.stdin.flush()
        self.assertEqual(child.ok("get_status")["mutes"], {"microphone": True, "all": True, "team": True})
        child.ok("shutdown")

    def test_connected_eof_and_server_loss(self):
        group = self.game()
        child = self.child()
        self.connect(child, group)
        child.close()
        self.server.stdin.write("shutdown\n")
        self.server.stdin.flush()
        self.server.wait(timeout=8)

    def test_server_disconnect_does_not_exit_background(self):
        group = self.game()
        child = self.child()
        self.connect(child, group)
        self.server.stdin.write("shutdown\n")
        self.server.stdin.flush()
        self.server.wait(timeout=8)
        child.event("server_disconnected")
        child.event("disconnected")
        self.assertEqual(child.ok("get_status")["state"], "disconnected")
        child.ok("shutdown")

    def test_connect_failure_is_asynchronous(self):
        child = self.child()
        child.ok("connect", host="127.0.0.1", port=1, name="unavailable", member_id="p",
            server_instance=self.info["server_instance"], group_id=1, channel_id=1, audio=False)
        self.assertIn(child.event("error")["code"], ("connection_failed", "connection_error"))
        child.event("disconnected")
        self.assertEqual(child.ok("get_status")["state"], "disconnected")

    def test_eof_with_unread_stdout(self):
        child = subprocess.Popen([str(server.BUILD / ("voice_client" + server.EXE)), "--ipc"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            child.stdin.write(b"".join(json.dumps({"v": 2, "id": str(i), "op": "get_status"}).encode()+b"\n" for i in range(40)))
            child.stdin.flush()
            time.sleep(.2)
            child.stdin.close()
            self.assertEqual(child.wait(timeout=8), 0)
        finally:
            if child.poll() is None: child.kill(); child.wait()
            if not child.stdin.closed: child.stdin.close()
            child.stdout.close(); child.stderr.close()


if __name__ == "__main__":
    unittest.main()

