#!/usr/bin/env python3
"""Reproducible loopback public-API measurements; never qualifies dedicated LAN targets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import statistics
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        value = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
        return value.hexdigest()


def command(args):
    return subprocess.check_output(args, text=True).strip()


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)] if ordered else None


def describe(values):
    return {"samples": len(values), "mean": statistics.mean(values),
            "stdev": statistics.stdev(values) if len(values) > 1 else None,
            "min": min(values), "max": max(values),
            "p50": percentile(values, .5), "p95": percentile(values, .95),
            "p99": percentile(values, .99)}


def dataset(path, size, text=False):
    # SHA-256 counter stream: reproducible, bounded-memory, no repeated 64 KiB blocks.
    # Text dataset uses a documented repeated ASCII record.
    record = b"file_transfer benchmark: deterministic compressible text record.\n"
    with path.open("wb") as out:
        counter = 0
        while size:
            if text:
                block = record * 1024
            else:
                block = b"".join(hashlib.sha256(i.to_bytes(8, "big")).digest()
                                 for i in range(counter, counter + 2048))
                counter += 2048
            out.write(block[:size])
            size -= min(size, len(block))


def run_case(binary, root, source, expected, case, artifacts, name):
    root.mkdir()
    (root / "storage").mkdir()
    os.link(source, root / "source")
    if case["operation"] == "download":
        os.link(source, root / "storage/source")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    with (artifacts / (name + ".log")).open("w") as log:
        server = subprocess.Popen([str(binary), "server", str(root), str(port)],
                                  stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.monotonic() + 15
            while not (root / "ready").exists():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Server did not become ready")
                time.sleep(.01)
            began = time.monotonic()
            client = subprocess.run([str(binary), "client", str(root), str(port), case["operation"],
                                     str(case["clients"]), str(case["limit_bytes_per_second"]),
                                     case["compression"]], stdout=log, stderr=subprocess.STDOUT, timeout=660)
            elapsed = time.monotonic() - began
            client_report = json.loads((root / "client.json").read_text())
            transfers = client_report["transfers"]
            if len(transfers) != case["clients"]:
                raise RuntimeError("Missing transfer results")
            verification = []
            for i, transfer in enumerate(transfers):
                path = root / (f"storage/object-{i}" if case["operation"] == "upload" else f"output-{i}")
                actual = digest(path) if path.is_file() else None
                ok = (not transfer["error"] and path.is_file() and path.stat().st_size == case["file_bytes"]
                      and actual == expected and transfer["sha256"] == expected
                      and transfer["useful_payload_bytes"] == case["file_bytes"])
                verification.append({"index": i, "destination_sha256": actual, "verified": ok})
            # Download completion ACK may still be crossing the socket; stop() drains admitted work.
            server.communicate("stop\n", timeout=30)
            server_report = json.loads((root / "server.json").read_text())
            failed = client.returncode != 0 or server.returncode != 0 or not all(v["verified"] for v in verification)
            return {"case": case, "client": client_report, "server": server_report,
                    "source_sha256": expected, "verification": verification,
                    "client_process_seconds": elapsed, "passed": not failed,
                    "client_exit_code": client.returncode, "server_exit_code": server.returncode}
        finally:
            if server.poll() is None:
                server.kill()
                server.communicate(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-release"))
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--profile", choices=("smoke", "full"), default="smoke")
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--baseline", type=Path, help="Compare only an identical environment/workload manifest")
    parser.add_argument("--maximum-regression-percent", type=float, default=10)
    args = parser.parse_args()
    if args.repeats < 2 or args.warmups < 1:
        parser.error("At least one warmup and two measured repetitions are required")
    build, artifacts = args.build.resolve(), args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    binary = build / "bin/file_trans_api_benchmark"
    cache = (build / "CMakeCache.txt").read_text().splitlines()
    flags = [line for line in cache if not line.startswith(("//", "#")) and any(
        line.startswith(name) for name in ("CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS", "CMAKE_CXX_COMPILER:",
                                          "FILE_TRANS_ENABLE_", "ASIO_INCLUDE_DIR:"))]
    cpu = command(["sysctl", "-n", "machdep.cpu.brand_string"]) if platform.system() == "Darwin" else platform.processor()
    memory = int(command(["sysctl", "-n", "hw.memsize"])) if platform.system() == "Darwin" else os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")
    dependencies = {}
    for name, pin in json.loads((REPO / "cmake/dependency-revisions.json").read_text()).items():
        path = REPO.parent / name
        dependencies[name] = {"pin": pin, "actual": command(["git", "-C", str(path), "rev-parse", "HEAD"]),
                              "diff_sha256": hashlib.sha256(subprocess.check_output(["git", "-C", str(path), "diff", "HEAD"])).hexdigest()}
    linked_libraries = [line for line in cache if any(line.startswith(n + "_SYSTEM_LIBRARY:") for n in ("THREAD", "LOGGER", "NETWORK"))]
    dependency_builds = {}
    for line in linked_libraries:
        library = Path(line.split("=", 1)[1])
        parent = library.parent
        while parent != parent.parent and not (parent / "CMakeCache.txt").exists():
            parent = parent.parent
        if (parent / "CMakeCache.txt").exists():
            dependency_builds[line.split(":", 1)[0]] = [s for s in (parent / "CMakeCache.txt").read_text().splitlines()
                if s.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS", "CMAKE_CXX_COMPILER:"))]
    manifest = {"schema_version": 1, "os": platform.platform(), "architecture": platform.machine(),
                "cpu": cpu, "logical_cpus": os.cpu_count(), "physical_memory_bytes": memory,
                "link": "127.0.0.1 loopback; no physical link capacity; LAN qualification unavailable",
                "compiler": command([next(line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_CXX_COMPILER:")), "--version"]), "build_flags": flags,
                "dependency_builds": dependency_builds, "dependencies": dependencies,
                "profile": args.profile, "warmups": args.warmups, "repeats": args.repeats,
                "dataset": "SHA-256(big-endian uint64 counter) concatenation, counter starts at 0; text uses repeated documented ASCII record",
                "storage": "local temporary filesystem; dataset creation and independent digest verification outside API timing",
                "transport": "TCP", "encryption": False, "chunk_bytes": 262144,
                "rss_scope": "Separate server process and client driver process; driver holds N client instances. N=1 is per-client RSS.",
                "network_bytes_scope": "FTS1 application frames sent/received, excludes TCP/IP/link headers and dependency framing",
                "latency_scope": "API invocation through verified terminal outcome; progress intervals are callback spacing, not NFR-03 chunk processing latency"}
    source = {"revision": command(["git", "-C", str(REPO), "rev-parse", "HEAD"]),
              "tracked_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "-C", str(REPO), "diff", "HEAD"])).hexdigest(),
              "harness_sha256": digest(REPO / "benchmarks/public_api/transfer_benchmark.cpp"),
              "runner_sha256": digest(Path(__file__)), "binary_sha256": digest(binary)}
    cases = []
    sizes = [1048576, 16777216] if args.profile == "smoke" else [1048576, 67108864, 1_000_000_000]
    for size in sizes:
        for op in ("upload", "download"):
            cases.append({"operation": op, "file_bytes": size, "clients": 1, "limit_bytes_per_second": 0, "compression": "none", "dataset": "binary"})
    for op in ("upload", "download"):
        cases.append({"operation": op, "file_bytes": 1048576, "clients": 4 if args.profile == "smoke" else 100,
                      "limit_bytes_per_second": 0, "compression": "none", "dataset": "binary"})
    cases.append({"operation": "download", "file_bytes": 4194304, "clients": 4,
                  "limit_bytes_per_second": 1048576, "compression": "none", "dataset": "binary"})
    for op in ("upload", "download"):
        cases.append({"operation": op, "file_bytes": 8388608, "clients": 1,
                      "limit_bytes_per_second": 0, "compression": "adaptive", "dataset": "text"})
    report = {"manifest": manifest, "source": source, "runs": [], "summaries": [],
              "targets": {"NFR-01/PERF-001": "unmeasured: dedicated LAN required",
                          "NFR-02/PERF-002": "unmeasured: dedicated LAN required",
                          "NFR-03/PERF-010": "unmeasured: callback spacing is not chunk processing latency",
                          "NFR-06/PERF-023": "unmeasured: per-core CPU sampling unavailable",
                          "PERF-024": "pending" if args.profile == "full" else "unmeasured: smoke uses four clients"},
              "passed": False}
    def save():
        (artifacts / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    save()
    failed = False
    with tempfile.TemporaryDirectory(prefix="fts-benchmark-") as temporary:
        root = Path(temporary)
        datasets = {}
        for index, case in enumerate(cases):
            key = (case["file_bytes"], case["dataset"])
            if key not in datasets:
                path = root / f"dataset-{len(datasets)}"
                dataset(path, key[0], key[1] == "text")
                datasets[key] = path, digest(path)
            data, sha256 = datasets[key]
            measured = []
            for repetition in range(args.warmups + args.repeats):
                name = f"case-{index:02}-run-{repetition:02}"
                case_root = root / name
                try:
                    run = run_case(binary, case_root, data, sha256, case, artifacts, name)
                    run.update({"warmup": repetition < args.warmups, "name": name})
                    report["runs"].append(run)
                    failed |= not run["passed"]
                    if not run["warmup"]:
                        measured.append(run)
                    print(name, case, "PASS" if run["passed"] else "FAIL", flush=True)
                except Exception as error:
                    failed = True
                    report["runs"].append({"name": name, "case": case, "passed": False, "error": str(error)})
                    print(name, "FAIL", error, flush=True)
                finally:
                    shutil.rmtree(case_root, ignore_errors=True)
                    save()
            if len(measured) != args.repeats or not all(run["passed"] for run in measured):
                continue
            # Each transfer includes API entry hashing + verified completion; connection is outside.
            latencies = [t["elapsed_seconds"] for r in measured for t in r["client"]["transfers"]]
            throughput = [sum(t["useful_payload_bytes"] for t in r["client"]["transfers"]) /
                          (max(t["request_end_seconds"] for t in r["client"]["transfers"]) -
                           min(t["request_start_seconds"] for t in r["client"]["transfers"])) / 1_000_000 for r in measured]
            intervals = [v for r in measured for t in r["client"]["transfers"] for v in t["progress_intervals_ms"]]
            server_peaks = [r["server"]["peak_rss_bytes"] for r in measured]
            client_idle = [r["client"]["idle_rss_bytes"] for r in measured]
            report["summaries"].append({"case": case, "aggregate_payload_MB_per_second": describe(throughput),
                "transfer_latency_seconds": describe(latencies), "progress_interval_ms": describe(intervals) if intervals else None,
                "server_peak_rss_bytes": describe(server_peaks),
                "client_driver_peak_rss_bytes": describe([r["client"]["peak_rss_bytes"] for r in measured]),
                "client_driver_idle_rss_bytes": describe(client_idle),
                "application_frame_bytes": describe([sum(t["network_sent_bytes"] + t["network_received_bytes"] for t in r["client"]["transfers"]) for r in measured]),
                "NFR-04_local_observation": "within numeric budget" if max(server_peaks) < 100_000_000 + 1_000_000 * case["clients"] else "exceeds numeric budget",
                "NFR-05_local_observation": ("within numeric budget" if max(client_idle) < 50_000_000 else "exceeds numeric budget") if case["clients"] == 1 else "not per-client RSS"})
            save()
    if args.profile == "full":
        hundred = [r for r in report["runs"] if r["case"]["clients"] == 100]
        report["targets"]["PERF-024"] = "passed locally: 100 connected clients, all transfers verified" if hundred and all(r["passed"] for r in hundred) else "failed"
    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        if baseline.get("manifest") != manifest or not baseline.get("passed") or len(baseline.get("summaries", [])) != len(report["summaries"]):
            raise SystemExit("Baseline comparison refused: environment/workload mismatch, incomplete or failed baseline")
        comparison = []
        for old, new in zip(baseline["summaries"], report["summaries"]):
            if old["case"] != new["case"]:
                raise SystemExit("Baseline workloads differ")
            before, after = old["aggregate_payload_MB_per_second"]["mean"], new["aggregate_payload_MB_per_second"]["mean"]
            regression = 100 * (before - after) / before
            comparison.append({"case": new["case"], "regression_percent": regression})
            failed |= regression > args.maximum_regression_percent
        report["baseline_comparison"] = comparison
    report["passed"] = not failed and len(report["summaries"]) == len(cases) and bool(cases)
    save()
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
