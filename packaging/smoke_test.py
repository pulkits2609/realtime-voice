"""Check an extracted package using only its binaries/examples and Python."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time


def smoke(archive):
    expected = archive.with_name(archive.name + ".sha256").read_text().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Archive checksum mismatch")
    # The OS temp directory is outside the checkout; spaces are intentional.
    with tempfile.TemporaryDirectory(prefix="realtime voice package ") as temporary:
        destination = Path(temporary)
        checkout = Path(__file__).resolve().parents[1]
        if destination == checkout or checkout in destination.parents:
            raise RuntimeError("Set the OS temporary directory outside the source checkout")
        shutil.unpack_archive(str(archive), destination)
        package, = destination.iterdir()
        print(f"Extraction directory: {package}", flush=True)
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        environment.pop("PYTHONHOME", None)
        environment.pop("LD_LIBRARY_PATH", None)
        windows = sys.platform == "win32"
        environment["PATH"] = str(Path(environment["SYSTEMROOT"]) / "System32") if windows else "/usr/bin:/bin"
        extension = ".exe" if windows else ""
        # Assert where every example was imported; do not consult checkout tools.
        probe = ("import pathlib,sys;sys.path.insert(0,'tools');"
                 "import mock_gfx,voice_api,background_voice;"
                 "expected=pathlib.Path('tools').resolve();"
                 "assert all(pathlib.Path(m.__file__).resolve().parent==expected "
                 "for m in (mock_gfx,voice_api,background_voice))")
        subprocess.run([sys.executable, "-E", "-c", probe], cwd=package, env=environment, check=True, timeout=10)
        with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            tcp.bind(("0.0.0.0", 0))
            port = tcp.getsockname()[1]
            udp.bind(("0.0.0.0", port))
        server = subprocess.Popen([str(package / ("voice_server" + extension)), str(port)],
            cwd=package, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        output, lines = [], queue.Queue()
        def read():
            for line in server.stdout:
                output.append(line)
                lines.put(line)
        reader = threading.Thread(target=read)
        reader.start()
        try:
            deadline = time.monotonic() + 8
            ready = False
            while time.monotonic() < deadline and not ready:
                if server.poll() is not None: break
                try: line = lines.get(timeout=.1)
                except queue.Empty: continue
                try:
                    event = json.loads(line)
                    ready = event.get("event") == "server_ready" and event["data"] == {
                        "port": port, "tcp": True, "udp": True}
                except ValueError: pass
            if not ready:
                raise RuntimeError(f"Packaged server failed readiness, port={port}, exit={server.poll()}:\n" + "".join(output))
            result = subprocess.run([sys.executable, "-E", "tools/mock_gfx.py", "--port", str(port),
                "--client", "voice_client" + extension, "--no-audio"], cwd=package, env=environment,
                capture_output=True, text=True, timeout=25)
            if result.returncode != 0: raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout.strip())
            print(f"Package smoke passed: {archive.name}; external extraction with spaces, packaged imports, restricted PATH")
        finally:
            try:
                if server.poll() is None:
                    try: server.stdin.write("shutdown\n"); server.stdin.flush()
                    except OSError: pass
                server.stdin.close()
                try: server.wait(timeout=8)
                except subprocess.TimeoutExpired: server.kill(); server.wait(); raise
            finally:
                reader.join(timeout=3)
                server.stdout.close()
                if reader.is_alive(): raise RuntimeError("Packaged server reader did not stop")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    smoke(parser.parse_args().archive.resolve())
