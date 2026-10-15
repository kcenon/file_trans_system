#!/usr/bin/env python3
"""Execute the compiled README programs and independently verify persisted files."""
import argparse
import hashlib
from pathlib import Path
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--artifacts", type=Path, required=True)
args = parser.parse_args()
build, artifacts = args.build.resolve(), args.artifacts.resolve()
artifacts.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="fts-quick-start-") as temporary:
    root = Path(temporary)
    data = bytes(range(256)) * 4097
    (root / "source").write_bytes(data)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with (artifacts / "server.log").open("w") as log:
        server = subprocess.Popen([str(build / "bin/quick_start_server"), str(root / "storage"), str(port)],
                                  stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.monotonic() + 10
            while "READY;" not in (artifacts / "server.log").read_text():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Server readiness failed")
                time.sleep(.01)
            client = subprocess.run([str(build / "bin/quick_start_client"), str(port), str(root / "source"), str(root / "output")],
                                    capture_output=True, text=True, timeout=150)
            (artifacts / "client.log").write_text(client.stdout + client.stderr)
            if client.returncode or "Verified upload:" not in client.stdout:
                raise RuntimeError("Quick Start did not report verified completion")
            for path in (root / "storage/quick-start.bin", root / "output"):
                if path.read_bytes() != data:
                    raise RuntimeError("Quick Start persisted different bytes")
            # A missing source must fail without printing successful completion.
            failure = subprocess.run([str(build / "bin/quick_start_client"), str(port), str(root / "missing"), str(root / "unused")],
                                     capture_output=True, text=True, timeout=15)
            (artifacts / "expected-failure.log").write_text(failure.stdout + failure.stderr)
            if not failure.returncode or "Verified upload:" in failure.stdout:
                raise RuntimeError("Missing-source case falsely reported success")
            server.communicate("\n", timeout=15)
            if server.returncode:
                raise RuntimeError("Server shutdown failed")
            (artifacts / "result.json").write_text(
                '{"passed":true,"bytes":' + str(len(data)) + ',"sha256":"' + hashlib.sha256(data).hexdigest() +
                '","missing_source_rejected":true}\n')
        finally:
            if server.poll() is None:
                server.kill()
                server.communicate(timeout=5)
print("Compiled Quick Start upload/download bytes and SHA-256 verified; failure reporting verified")
