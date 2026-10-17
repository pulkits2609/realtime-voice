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
from unittest.mock import patch
from server_process import ServerProcess, available_port

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from voice_api import VoiceAPI

BUILD = Path("build").resolve()
if len(sys.argv) > 1 and Path(sys.argv[1]).is_dir():
    BUILD = Path(sys.argv.pop(1)).resolve()
EXE = ".exe" if sys.platform == "win32" else ""


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.peers, self.udp = [], []
        self.server_process = ServerProcess(BUILD)
        # unittest does not call tearDown when setUp raises.
        self.addCleanup(ServerTests.tearDown, self)
        self.server_process.start()
        self.port, self.server = self.server_process.port, self.server_process.process
        self.lines = self.server_process.lines
        try:
            self.manager = self.peer()
            self.info = self.manager.hello_manager()
        except Exception as error:
            raise RuntimeError(f"Control hello failed after readiness: {error}\n"
                + self.server_process.diagnostics()) from error

    def peer(self, heartbeat=True):
        peer = VoiceAPI("127.0.0.1", self.port, heartbeat)
        self.peers.append(peer)
        return peer

    def tearDown(self):
        failures = []
        for peer in self.peers:
            try: peer.close()
            except Exception as error: failures.append(error)
        for udp in self.udp:
            udp.close()
        self.server_process.close()
        if failures: raise failures[0]

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
        valid = self.packet(a, ua)
        self.assertIsNotNone(self.received(ub))
        self.assertIsNotNone(self.received(uc))
        ua.sendto(valid + b"x" * 2048, ("127.0.0.1", self.port))
        self.assertIsNone(self.received(ub))
        self.assertIsNone(self.received(uc))
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
        a.ok("set_transmit", channel_id=first["all_channel_id"])
        self.packet(a, ua)
        self.assertIsNotNone(self.received(ub))
        self.assertIsNotNone(self.received(uc))
        a.ok("set_transmit", channel_id=first["team_channel_ids"][0])
        b.ok("set_mutes", microphone=False, all=False, team=False)
        a.ok("set_mutes", microphone=True, all=False, team=False)
        self.packet(a, ua)
        self.assertIsNone(self.received(ub))
        a.ok("set_mutes", microphone=False, all=False, team=False)
        self.packet(a, ua, channel=first["team_channel_ids"][1])
        self.assertIsNone(self.received(uc))
        rogue = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.packet(a, rogue)
            self.assertIsNone(self.received(ub))
        finally:
            rogue.close()
        b.ok("set_mutes", microphone=False, all=False, team=True)
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
        self.manager.ok("set_phase", group_id=group["group_id"], phase="playing")
        a, ua = self.join(group, "a", 1)
        b, ub = self.join(group, "b", 1)
        c, uc = self.join(group, "c", 2)
        b.ok("set_transmit", channel_id=group["all_channel_id"])
        for peer, udp in ((a, ua), (b, ub), (c, uc)):
            self.packet(peer, udp, b"")
        time.sleep(.05)  # Allow the relay to consume endpoint-registration datagrams.
        before = a.ok("status")
        self.manager.close()
        time.sleep(.1)  # Exercise actual traffic while management is disconnected.
        unrelated = self.peer()
        self.assertEqual(unrelated.request("hello", role="manager")["error"]["code"], "manager_busy")
        self.packet(a, ua)
        self.assertIsNotNone(self.received(ub))
        self.assertIsNone(self.received(uc))
        a.ok("set_transmit", channel_id=group["all_channel_id"])
        self.packet(a, ua)
        self.assertIsNotNone(self.received(ub)); self.assertIsNotNone(self.received(uc))
        new = self.peer()
        resumed = new.hello_manager(self.info)
        self.assertTrue(resumed["resumed"])
        self.assertNotEqual(resumed["session_id"], self.info["session_id"])
        self.assertEqual(resumed["manager_id"], self.info["manager_id"])
        retained = new.owned_groups()[0]
        for key in ("group_id", "all_channel_id", "team_channel_ids", "phase"):
            self.assertEqual(retained[key], group[key] if key != "phase" else "playing")
        current = a.ok("status")
        for key in ("session_id", "team", "mutes", "receive_revision"):
            self.assertEqual(current[key], before[key])
        with new.condition:
            self.assertFalse(any(e["event"] in ("group_created", "group_deleted") for e in new.events))
        started = time.monotonic()
        new.ok("delete_group", group_id=group["group_id"])
        a.event("group_deleted", timeout=2)
        self.assertLess(time.monotonic() - started, 2)

    def test_disconnect_grace_expires_only_owned_groups(self):
        self.server.stdin.write("create lobby grace-manual\n"); self.server.stdin.flush()
        while True:
            line = self.lines.get(timeout=5)
            try:
                response = json.loads(line)
                if response.get("ok") and response["data"].get("name") == "grace-manual": break
            except ValueError: pass
        manual = response["data"]
        player, udp = self.join(self.game(), "expiry", 1)
        started = time.monotonic()
        self.manager.close()
        player.event("group_deleted", timeout=12)
        self.assertGreater(time.monotonic() - started, 9)
        self.assertLess(time.monotonic() - started, 12)
        expired = self.peer()
        self.assertEqual(expired.request("hello", role="manager", manager_id=self.info["manager_id"],
            server_instance=self.info["server_instance"])["error"]["code"], "manager_expired")
        fresh = self.peer(); fresh.hello_manager()
        self.assertEqual(fresh.ok("status")["gfx_game_groups"], 0)
        self.assertEqual(fresh.ok("status")["groups"][0]["group_id"], manual["group_id"])

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
        self.assertLess(time.monotonic() - started, 12)  # No second ten-second grace.
        new = self.peer()
        new.ok("hello", role="manager")
        status = new.ok("status")
        self.assertEqual(status["gfx_game_groups"], 0)
        self.assertTrue(any(g["group_id"] == manual["group_id"] for g in status["groups"]))

    def test_incompatible_version(self):
        peer = self.peer()
        result = peer.request("hello", role="manager", v=1)
        self.assertEqual(result["error"]["code"], "incompatible_version")

    def test_short_heartbeat_interruption_keeps_groups(self):
        group = self.game()
        self.manager.heartbeat = False
        time.sleep(3)
        self.manager.heartbeat = True
        time.sleep(2.5)
        self.assertEqual(self.manager.ok("group_details", group_id=group["group_id"])["group_id"], group["group_id"])

    def test_standalone_client_manual_game_and_eof(self):
        self.server.stdin.write("create game 2 standalone\n")
        self.server.stdin.flush()
        group = None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            line = self.lines.get(timeout=5)
            try:
                message = json.loads(line)
                if message.get("ok") and message["data"].get("name") == "standalone":
                    group = message["data"]
                    break
            except ValueError:
                pass
        self.assertIsNotNone(group)
        client = subprocess.Popen([str(BUILD / ("voice_client" + EXE)), "localhost", str(self.port),
            "standalone", str(group["group_id"]), str(group["all_channel_id"]), "1", "--no-audio"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        events = queue.Queue()
        reader = threading.Thread(target=lambda: [events.put(line) for line in client.stderr])
        reader.start()
        try:
            deadline = time.monotonic() + 5
            joined = False
            while time.monotonic() < deadline:
                line = events.get(timeout=5)
                try:
                    event = json.loads(line)
                    if event.get("event") == "connected":
                        self.assertEqual(event["data"]["mutes"], {"microphone": True, "all": True, "team": True})
                        joined = True
                        break
                except ValueError:
                    pass
            self.assertTrue(joined)
            client.stdin.write("mic on\nall on\nteam on\n")
            client.stdin.flush()
            deadline = time.monotonic() + 5
            changed = False
            while time.monotonic() < deadline:
                line = events.get(timeout=5)
                try:
                    event = json.loads(line)
                    if event.get("event") == "status" and event["data"]["mutes"] == {"microphone": False, "all": False, "team": False}:
                        changed = True
                        break
                except ValueError:
                    pass
            self.assertTrue(changed)
            client.stdin.close()
            self.assertEqual(client.wait(timeout=8), 0)
        finally:
            if client.poll() is None: client.kill(); client.wait()
            if not client.stdin.closed: client.stdin.close()
            reader.join(timeout=5)
            client.stderr.close()
            client.stdout.close()


class StartupTests(unittest.TestCase):
    def test_ready_binds_both_transports(self):
        runner = ServerProcess(BUILD)
        self.addCleanup(runner.close)
        runner.start()
        with VoiceAPI("127.0.0.1", runner.port) as peer:
            self.assertEqual(peer.hello_manager()["state"], "ready")
        self.assertTrue(runner.ready)
        self.assertEqual(len(runner.attempts), 1)

    def test_occupied_ports_report_failure_and_cleanup(self):
        for transport in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
            with self.subTest(transport=transport), socket.socket(socket.AF_INET, transport) as occupied:
                if sys.platform == "win32":
                    occupied.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                occupied.bind(("0.0.0.0", 0))
                if transport == socket.SOCK_STREAM: occupied.listen()
                runner = ServerProcess(BUILD, port=occupied.getsockname()[1])
                self.addCleanup(runner.close)
                with self.assertRaisesRegex(RuntimeError, "exited early") as failure:
                    runner.start()
                # Windows reports WSAEACCES for a TCP socket blocked by an
                # exclusive owner. Do not classify/retry that as address-in-use.
                code = "socket_error" if sys.platform == "win32" and transport == socket.SOCK_STREAM else "address_in_use"
                for expected in (str(runner.executable), str(runner.port), code, "exit_status=1"):
                    self.assertIn(expected, str(failure.exception))
                self.assertIsNotNone(runner.process.poll())
                self.assertFalse(runner.reader.is_alive())
                self.assertTrue(runner.process.stdin.closed and runner.process.stdout.closed)

    def test_only_identified_allocation_race_is_retried(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
            occupied.bind(("0.0.0.0", 0))
            blocked, free = occupied.getsockname()[1], available_port()
            runner = ServerProcess(BUILD)
            self.addCleanup(runner.close)
            with patch("server_process.available_port", side_effect=[blocked, free]): runner.start()
            self.assertEqual([item["port"] for item in runner.attempts], [blocked, free])
            self.assertIn("address_in_use", runner.attempts[0]["diagnostics"])

    def test_alive_without_readiness_is_not_retried(self):
        runner = ServerProcess(BUILD, startup_timeout=.2)
        self.addCleanup(runner.close)
        real_popen = subprocess.Popen
        def stalled(arguments, **options):
            return real_popen([sys.executable, "-c", "import sys; sys.stdin.read()"], **options)
        with patch("server_process.subprocess.Popen", side_effect=stalled):
            with self.assertRaisesRegex(RuntimeError, "alive without readiness"):
                runner.start()
        self.assertEqual(len(runner.attempts), 1)
        self.assertIsNotNone(runner.process.poll())
        self.assertFalse(runner.reader.is_alive())

    def test_setup_failure_runs_registered_cleanup(self):
        class FailingSetup(unittest.TestCase):
            def setUp(inner):
                inner.runner = ServerProcess(BUILD)
                inner.addCleanup(inner.runner.close)
                inner.runner.start()
                raise RuntimeError("Intentional failure after startup")
            def runTest(inner): pass
        case, result = FailingSetup(), unittest.TestResult()
        case.run(result)
        self.assertEqual(len(result.errors), 1)
        self.assertIsNotNone(case.runner.process.poll())
        self.assertFalse(case.runner.reader.is_alive())


if __name__ == "__main__":
    unittest.main()

