"""Real TCP/UDP tests; no microphone or audio device is required."""
import json
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from voice_api import VoiceAPI

BUILD = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path("build").resolve()
sys.argv = sys.argv[:1]
EXE = ".exe" if sys.platform == "win32" else ""


class ServerTests(unittest.TestCase):
    def setUp(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        self.port = listener.getsockname()[1]
        listener.close()
        self.lines = queue.Queue()
        self.server = subprocess.Popen([str(BUILD / ("voice_server" + EXE)), str(self.port)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.reader = threading.Thread(target=lambda: [self.lines.put(line) for line in self.server.stdout])
        self.reader.start()
        self.peers, self.udp = [], []
        deadline = time.monotonic() + 5
        while True:
            try:
                self.manager = self.peer()
                self.info = self.manager.ok("hello", role="manager")
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.03)

    def peer(self, heartbeat=True):
        peer = VoiceAPI("127.0.0.1", self.port, heartbeat)
        self.peers.append(peer)
        return peer

    def tearDown(self):
        for peer in self.peers:
            peer.close()
        for udp in self.udp:
            udp.close()
        if self.server.poll() is None:
            self.server.stdin.write("shutdown\n")
            self.server.stdin.flush()
        try:
            self.server.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.server.kill()
            self.server.wait()
            self.fail("Server failed to shut down")
        self.server.stdin.close()
        self.reader.join(timeout=5)
        self.server.stdout.close()

    def game(self, teams=2):
        return self.manager.ok("create_group", type="game", teams=teams)

    def join(self, group, member, team):
        peer = self.peer()
        hello = peer.ok("hello", role="voice", name=member, member_id=member,
            server_instance=self.info["server_instance"], group_id=group["group_id"],
            channel_id=group["all_channel_id"])
        self.manager.ok("bind_player", group_id=group["group_id"],
            session_id=hello["session_id"], member_id=member, team=team)
        peer.ok("set_mutes", microphone=False, all=False, team=False)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("127.0.0.1", 0))
        udp.settimeout(.15)
        self.udp.append(udp)
        self.packet(peer, udp, b"")
        return peer, udp

    def packet(self, peer, udp, payload=b"voice", **overrides):
        state = peer.ok("status")
        fields = dict(group=state["group_id"], channel=state["transmit_channel_id"],
            sender_revision=state["send_revision"], receiver_revision=0,
            instance=int(state["server_instance"], 16), sender=state["session_id"])
        fields.update(overrides)
        packet = struct.pack("<2sBBIIIIQ", b"RV", 2, 0, fields["group"], fields["channel"],
            fields["sender_revision"], fields["receiver_revision"], fields["instance"])
        packet += struct.pack("<BIII", 2, len(payload), fields["sender"], 1) + payload
        udp.sendto(packet, ("127.0.0.1", self.port))
        return packet

    def received(self, udp):
        try:
            return udp.recv(1024)
        except socket.timeout:
            return None

    def test_routing_lifecycle(self):
        for teams in (0, 1, 5):
            self.assertFalse(self.manager.request("create_group", type="game", teams=teams)["ok"])
        first, second = self.game(), self.game()
        a, ua = self.join(first, "a", 1)
        b, ub = self.join(first, "b", 1)
        c, uc = self.join(first, "c", 2)
        d, ud = self.join(second, "d", 1)
        self.assertEqual(a.request("list_groups")["error"]["code"], "forbidden")
        self.assertEqual(a.request("set_transmit", channel_id=first["team_channel_ids"][0])["error"]["code"], "team_disabled")
        self.packet(a, ua)
        self.assertIsNotNone(self.received(ub))
        self.assertIsNotNone(self.received(uc))
        self.assertIsNone(self.received(ua))
        self.assertIsNone(self.received(ud))
        self.manager.ok("set_phase", group_id=first["group_id"], phase="playing")
        b.ok("set_transmit", channel_id=first["all_channel_id"])
        self.packet(a, ua)
        self.assertIsNotNone(self.received(ub))
        self.assertIsNone(self.received(uc))
        b.ok("set_mutes", microphone=False, all=True, team=False)
        self.packet(a, ua)
        self.assertIsNotNone(self.received(ub))
        b.ok("set_mutes", microphone=False, all=False, team=True)
        self.packet(a, ua)
        self.assertIsNone(self.received(ub))
        a.ok("set_mutes", microphone=True, all=False, team=False)
        self.packet(a, ua)
        self.assertIsNone(self.received(ub))
        a.ok("set_mutes", microphone=False, all=False, team=False)
        self.packet(a, ua, channel=first["team_channel_ids"][1])
        self.assertIsNone(self.received(uc))
        rogue = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.packet(a, rogue)
            self.assertIsNone(self.received(uc))
        finally:
            rogue.close()
        self.manager.ok("set_team", group_id=first["group_id"], session_id=b.ok("status")["session_id"], team=2)
        self.manager.ok("set_phase", group_id=first["group_id"], phase="waiting")
        self.assertEqual(a.ok("status")["transmit_channel_id"], first["all_channel_id"])
        self.assertTrue(b.ok("status")["mutes"]["team"])
        self.assertEqual(self.manager.request("delete_channel", channel_id=first["all_channel_id"])["error"]["code"], "delete_group_required")
        self.manager.ok("remove_player", group_id=first["group_id"], session_id=b.ok("status")["session_id"])
        b.event("membership_removed")
        self.manager.ok("delete_group", group_id=first["group_id"])
        a.event("group_deleted")
        self.assertEqual(d.ok("status")["group_id"], second["group_id"])

    def test_single_manager_and_disconnect(self):
        other = self.peer()
        self.assertEqual(other.request("hello", role="manager")["error"]["code"], "manager_busy")
        group = self.game()
        player, udp = self.join(group, "managed", 1)
        self.manager.close()
        player.event("group_deleted", timeout=2)
        new = self.peer()
        new.ok("hello", role="manager")
        self.assertEqual(new.ok("status")["gfx_game_groups"], 0)

    def test_heartbeat_timeout_preserves_manual_groups(self):
        self.server.stdin.write("create lobby manual\n")
        self.server.stdin.flush()
        manual = None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            line = self.lines.get(timeout=5)
            try:
                result = json.loads(line)
                if result.get("ok") and result["data"].get("type") == "lobby":
                    manual = result["data"]
                    break
            except ValueError:
                pass
        self.assertIsNotNone(manual)
        group = self.game()
        player, udp = self.join(group, "managed", 1)
        self.manager.heartbeat = False
        started = time.monotonic()
        player.event("group_deleted", timeout=13)
        self.assertGreater(time.monotonic() - started, 7)
        new = self.peer()
        new.ok("hello", role="manager")
        status = new.ok("status")
        self.assertEqual(status["gfx_game_groups"], 0)
        self.assertTrue(any(g["group_id"] == manual["group_id"] for g in status["groups"]))

    def test_incompatible_version(self):
        peer = self.peer()
        result = peer.request("hello", role="manager", v=1)
        self.assertEqual(result["error"]["code"], "incompatible_version")


if __name__ == "__main__":
    unittest.main()

