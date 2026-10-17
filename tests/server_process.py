"""Owned test server with bounded readiness, diagnostics, and dual-port checks."""
from collections import deque
import errno
import json
from pathlib import Path
import queue
import socket
import subprocess
import sys
import threading
import time


def available_port():
    # Both transports bind the same IPv4 wildcard port in voice_server.
    for _ in range(20):
        with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            if sys.platform == "win32":
                tcp.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                udp.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            tcp.bind(("0.0.0.0", 0))
            port = tcp.getsockname()[1]
            try:
                udp.bind(("0.0.0.0", port))
                return port
            except OSError as error:
                if error.errno != errno.EADDRINUSE and getattr(error, "winerror", None) != 10048:
                    raise
    raise RuntimeError("Could not reserve a port available for both TCP and UDP")


class ServerProcess:
    def __init__(self, build, port=None, startup_timeout=8):
        self.executable = Path(build).resolve() / ("voice_server.exe" if sys.platform == "win32" else "voice_server")
        self.fixed_port, self.startup_timeout = port, startup_timeout
        self.port, self.process, self.reader = port, None, None
        self.lines = queue.Queue()
        self.logs = deque(maxlen=200)
        self.condition = threading.Condition()
        self.ready, self.address_in_use = False, False
        self.attempts = []

    def _read(self):
        try:
            for line in self.process.stdout:
                self.lines.put(line)
                with self.condition:
                    self.logs.append(line.rstrip())
                    try:
                        event = json.loads(line)
                        data = event.get("data", {})
                        if event.get("event") == "server_ready":
                            self.ready = data.get("port") == self.port and data.get("tcp") is True and data.get("udp") is True
                        if event.get("event") == "server_error":
                            self.address_in_use = data.get("code") == "address_in_use"
                    except (ValueError, AttributeError):
                        pass
                    self.condition.notify_all()
        finally:
            with self.condition:
                self.condition.notify_all()

    def diagnostics(self):
        code = None if self.process is None else self.process.poll()
        state = "not launched" if self.process is None else ("alive without readiness" if code is None else "exited early")
        with self.condition:
            logs = list(self.logs)
        return (f"Server {state}: executable={self.executable}, port={self.port}, exit_status={code}\n"
                + "\n".join(logs))

    def start(self):
        # Selection cannot reserve a port across CreateProcess/exec. Retry only
        # a structured address-in-use failure, once, with a newly checked port.
        for attempt in range(2):
            self.port = self.fixed_port if self.fixed_port is not None else available_port()
            self.ready = self.address_in_use = False
            self.logs.clear()
            deadline = time.monotonic() + self.startup_timeout
            try:
                self.process = subprocess.Popen([str(self.executable), str(self.port)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, bufsize=1)
                self.reader = threading.Thread(target=self._read)
                self.reader.start()
                with self.condition:
                    while not self.ready and self.process.poll() is None:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0: break
                        self.condition.wait(min(.05, remaining))
                if self.ready and self.process.poll() is None:
                    self.attempts.append({"port": self.port, "ready": True})
                    return self
                # Include the last stderr lines even if poll observed exit first.
                if self.process.poll() is not None: self.reader.join(timeout=1)
                detail = self.diagnostics()
                collision = self.address_in_use and self.process.poll() is not None
                self.attempts.append({"port": self.port, "ready": False, "diagnostics": detail})
                self.close()
                if collision and self.fixed_port is None and attempt == 0:
                    print("Retrying one confirmed address-in-use startup race:\n" + detail, file=sys.stderr)
                    continue
                raise RuntimeError(detail)
            except Exception as error:
                detail = self.diagnostics()
                self.close()
                message = str(error)
                if detail not in message: message += "\n" + detail
                raise RuntimeError(message) from error

    def close(self):
        if self.process is None: return
        failure = None
        try:
            if self.process.poll() is None and not self.process.stdin.closed:
                try:
                    self.process.stdin.write("shutdown\n")
                    self.process.stdin.flush()
                except (OSError, ValueError):
                    pass
            if not self.process.stdin.closed: self.process.stdin.close()
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                # This is exactly the Popen object owned by this fixture.
                self.process.kill()
                self.process.wait(timeout=3)
                failure = RuntimeError("Owned server did not stop:\n" + self.diagnostics())
        finally:
            if self.reader is not None:
                self.reader.join(timeout=3)
                if self.reader.is_alive(): failure = RuntimeError("Server output reader did not stop")
            self.process.stdout.close()
        if failure: raise failure
