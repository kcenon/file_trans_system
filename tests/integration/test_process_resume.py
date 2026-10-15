#!/usr/bin/env python3
"""Crash both endpoints, then inspect actual FTS1 chunk IDs and verify bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time


def wait_for(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("Deadline exceeded")


def free_port():
    with socket.socket() as connection:
        connection.bind(("127.0.0.1", 0))
        return connection.getsockname()[1]


def start_server(binary, root, port, mode, log, *, retry_port_conflicts=False):
    # An unused port selected by the parent can be claimed before the child
    # binds it. Retry only that initial fixture allocation, never a restart:
    # restart recovery must retain the exact endpoint in its checkpoint.
    attempts = 5 if retry_port_conflicts else 1
    for attempt in range(attempts):
        (root / "server-ready").unlink(missing_ok=True)
        log.flush()
        offset = log.tell()
        process = subprocess.Popen([binary, "server", str(root), str(port), "new", "0", mode],
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(lambda: (root / "server-ready").exists() or process.poll() is not None)
        except BaseException:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            raise
        if process.poll() is None:
            return process, port
        output = Path(log.name).read_bytes()[offset:].decode("utf-8", errors="replace")
        if "address already in use" not in output.lower() or attempt + 1 == attempts:
            raise AssertionError(f"Server failed to start on port {port}: {output}")
        port = free_port()
    raise AssertionError("Initial server port allocation exhausted")


class Proxy:
    def __init__(self, target):
        self.target = target
        self.trace = []
        self.errors = []
        self.workers = []
        self.stop = threading.Event()
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.thread = threading.Thread(target=self.accept)
        self.thread.start()

    def accept(self):
        while not self.stop.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                server = socket.create_connection(("127.0.0.1", self.target), timeout=2)
                server.settimeout(None)
            except OSError as error:
                self.errors.append(str(error))
                client.close()
                continue
            for source, target, direction in ((client, server, "upload"), (server, client, "download")):
                worker = threading.Thread(target=self.relay, args=(source, target, direction))
                self.workers.append(worker)
                worker.start()

    def relay(self, source, target, direction):
        pending = bytearray()
        try:
            while data := source.recv(65536):
                pending.extend(data)
                while len(pending) >= 9:
                    magic, kind, length = struct.unpack(">IBI", pending[:9])
                    if magic != 0x46545331 or length > 16 * 1024 * 1024:
                        raise AssertionError("Malformed FTS1 frame on transport")
                    if len(pending) < length + 13:
                        break
                    payload = pending[9:9 + length]
                    if kind == 0x20:
                        self.trace.append({"direction": direction, "id": payload[:16].hex(),
                                           "chunk": struct.unpack(">Q", payload[16:24])[0]})
                    del pending[:length + 13]
                target.sendall(data)
        except OSError:
            pass  # Process death intentionally closes a connection midstream.
        except Exception as error:
            self.errors.append(str(error))
        finally:
            for connection in (source, target):
                try:
                    connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            source.close()

    def close(self):
        self.stop.set()
        self.listener.close()
        self.thread.join(2)
        for worker in self.workers:
            worker.join(2)
            assert not worker.is_alive(), "Proxy worker did not stop"


def scenario(binary, direction, encrypted, artifacts):
    with tempfile.TemporaryDirectory(prefix="fts-process-resume-") as directory:
        root = Path(directory)
        source = bytes(range(251)) * 2083
        (root / "source").write_bytes(source)
        (root / "server").mkdir()
        if direction == "download":
            (root / "server/data").write_bytes(source)
        port = free_port()
        proxy = Proxy(port)
        mode = "encrypted" if encrypted else "plain"
        log_path = artifacts / f"186-process-{direction}-{mode}.log"
        server = None
        with log_path.open("w") as log:
            try:
                server, port = start_server(binary, root, port, mode, log, retry_port_conflicts=True)
                proxy.target = port
                first = subprocess.run([binary, direction, str(root), str(proxy.port), "new", "2", mode],
                                       stdout=log, stderr=subprocess.STDOUT, timeout=15)
                assert first.returncode == 41, f"Crash checkpoint not reached: {first.returncode}"
                transfer_id = (root / "interrupted-id").read_text()
                record = json.loads((root / "client-state" / f"{transfer_id}.json").read_text())
                assert record["transferred_bytes"] == 2 * 65536
                assert (root / "client-state" / f"{transfer_id}.json").stat().st_size < 1024 * 1024
                server.kill()  # No graceful server cleanup; filesystem recovery is required.
                server.wait(timeout=5)
                wait_for(lambda: all(not worker.is_alive() for worker in proxy.workers))
                before = len(proxy.trace)
                server, _ = start_server(binary, root, port, mode, log)
                resumed = subprocess.run([binary, direction, str(root), str(proxy.port), transfer_id, "0", mode],
                                         stdout=log, stderr=subprocess.STDOUT, timeout=15)
                assert resumed.returncode == 0, f"Resumed process failed: {resumed.returncode}"
                wait_for(lambda: all(not worker.is_alive() for worker in proxy.workers))
                report = json.loads((root / "result.json").read_text())
                expected_hash = hashlib.sha256(source).hexdigest()
                destination = root / ("server/data" if direction == "upload" else "output")
                assert destination.read_bytes() == source
                assert report["sha256"] == expected_hash and report["bytes"] == len(source)
                assert report["first_progress_ms"] < 1000, report
                actual = [item["chunk"] for item in proxy.trace[before:] if item["direction"] == direction]
                expected = list(range(2, (len(source) + 65535) // 65536))
                assert actual == expected, (actual, expected)
                assert not list((root / "client-state").glob("*.json"))
                assert not proxy.errors, proxy.errors
                report.update(direction=direction, encryption=mode, resumed_chunk_ids=actual,
                              total_chunks=(len(source) + 65535) // 65536, original_confirmed_chunks=2,
                              client_exit_at_checkpoint=first.returncode, server_termination="SIGKILL")
                (artifacts / f"186-process-{direction}-{mode}.json").write_text(json.dumps(report, indent=2) + "\n")
            finally:
                if server and server.poll() is None:
                    server.kill()
                    server.wait(timeout=5)
                proxy.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--crypto", action="store_true")
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    for direction in ("upload", "download"):
        scenario(args.binary, direction, False, args.artifacts)
        if args.crypto:
            scenario(args.binary, direction, True, args.artifacts)
    print("Process restart, wire chunk IDs, exact bytes, hashes, and resume timing passed")


if __name__ == "__main__":
    main()
