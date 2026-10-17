"""JSON-lines child-process driver used by tests and mock external applications."""
import json
import subprocess
import threading
import time


class BackgroundVoice:
    def __init__(self, executable):
        self.process = subprocess.Popen([str(executable), "--ipc"], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        self.condition = threading.Condition()
        self.responses, self.events, self.errors = {}, [], []
        self.counter = 0
        self.closed = False
        self.reader = threading.Thread(target=self._read)
        def drain_diagnostics():
            for _ in self.process.stderr:
                pass
        self.diagnostics = threading.Thread(target=drain_diagnostics)
        self.reader.start()
        self.diagnostics.start()

    def _read(self):
        try:
            for line in self.process.stdout:
                try:
                    message = json.loads(line)
                except ValueError:
                    self.errors.append(line)
                    continue
                with self.condition:
                    if "event" in message:
                        self.events.append(message)
                    else:
                        self.responses[message["id"]] = message
                    self.condition.notify_all()
        finally:
            with self.condition:
                self.closed = True
                self.condition.notify_all()

    def request(self, op, **fields):
        self.counter += 1
        identity = str(self.counter)
        self.process.stdin.write(json.dumps({"v": 2, "id": identity, "op": op, **fields}) + "\n")
        self.process.stdin.flush()
        deadline = time.monotonic() + 8
        with self.condition:
            while identity not in self.responses:
                if self.closed:
                    raise EOFError("Background voice client exited")
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

    def event(self, name, predicate=lambda data: True, timeout=8):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for index, event in enumerate(self.events):
                    if event["event"] == name and predicate(event["data"]):
                        return self.events.pop(index)["data"]
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(name)
                self.condition.wait(remaining)

    def close(self):
        if not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            code = self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        self.reader.join(timeout=3)
        self.diagnostics.join(timeout=3)
        self.process.stdout.close()
        self.process.stderr.close()
        if self.errors:
            raise AssertionError(f"Non-JSON stdout: {self.errors}")
        if code != 0:
            raise AssertionError(f"Background client exit code: {code}")

