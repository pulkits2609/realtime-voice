"""Small protocol-v2 peer for integration tests and the mock GFX application."""
import json
import socket
import struct
import threading
import time


class VoiceAPI:
    def __init__(self, host, port, heartbeat=True):
        self.socket = socket.create_connection((host, port), timeout=5)
        self.socket.settimeout(None)
        self.heartbeat = heartbeat
        self.condition = threading.Condition()
        self.write_lock = threading.Lock()
        self.responses = {}
        self.events = []
        self.counter = 0
        self.closed = False
        self.reader = threading.Thread(target=self._read, daemon=False)
        self.reader.start()

    def send(self, message):
        data = json.dumps(message, separators=(",", ":")).encode()
        if not 0 < len(data) <= 65536:
            raise ValueError("Invalid frame length")
        with self.write_lock:
            self.socket.sendall(struct.pack("!I", len(data)) + data)

    def _exact(self, count):
        result = bytearray()
        while len(result) < count:
            part = self.socket.recv(count - len(result))
            if not part:
                raise EOFError("Voice server disconnected")
            result.extend(part)
        return result

    def _read(self):
        try:
            while True:
                size, = struct.unpack("!I", self._exact(4))
                if not 0 < size <= 65536:
                    raise ValueError("Invalid frame size")
                message = json.loads(self._exact(size))
                if message.get("event") == "ping":
                    if self.heartbeat:
                        self.send({"v": 2, "op": "pong", "nonce": message["data"]["nonce"]})
                    continue
                with self.condition:
                    if "event" in message:
                        self.events.append(message)
                    else:
                        self.responses[message["id"]] = message
                    self.condition.notify_all()
        except (OSError, EOFError, ValueError):
            pass
        finally:
            with self.condition:
                self.closed = True
                self.condition.notify_all()

    def request(self, op, timeout=5, **fields):
        with self.condition:
            self.counter += 1
            identity = str(self.counter)
        self.send({"v": 2, "id": identity, "op": op, **fields})
        deadline = time.monotonic() + timeout
        with self.condition:
            while identity not in self.responses:
                if self.closed:
                    raise EOFError("Voice server disconnected")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(op)
                self.condition.wait(remaining)
            return self.responses.pop(identity)

    def ok(self, op, **fields):
        response = self.request(op, **fields)
        if not response["ok"]:
            raise RuntimeError(response["error"])
        return response["data"]

    def event(self, name, timeout=5):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for index, event in enumerate(self.events):
                    if event["event"] == name:
                        return self.events.pop(index)["data"]
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(name)
                self.condition.wait(remaining)

    def close(self):
        try:
            self.socket.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.socket.close()
        self.reader.join(timeout=5)
        if self.reader.is_alive():
            raise RuntimeError("Peer reader did not stop")

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

