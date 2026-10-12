#!/usr/bin/env python3
"""Build the compatible ecosystem revisions without replacing existing work.

Usage: python3 tools/bootstrap_dependencies.py [--workspace PATH] [--jobs N]
Requires Git, CMake, Ninja, a C++20 compiler, and development OpenSSL/LZ4/libcurl.
The pinned network client requires the checked-in native socket close patch.
"""
import argparse
import json
import subprocess
from pathlib import Path


def run(args):
    print("+ " + " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=repo.parent)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    workspace = args.workspace.resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    revisions = json.loads((repo / "cmake/dependency-revisions.json").read_text())
    for name, revision in revisions.items():
        destination = workspace / name
        if not destination.exists():
            run(["git", "init", "-q", destination])
            run(["git", "-C", destination, "remote", "add", "origin", f"https://github.com/kcenon/{name}.git"])
            run(["git", "-C", destination, "fetch", "--depth=1", "origin", revision])
            run(["git", "-C", destination, "checkout", "--detach", revision])
        current = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
        if current != revision:
            raise SystemExit(f"{destination} has another revision; use a separate --workspace")
        if name == "network_system":
            patch = repo / "cmake/patches/network-system-client-close.patch"
            applied = subprocess.run(["git", "-C", str(destination), "apply", "--reverse", "--check", str(patch)], capture_output=True)
            if applied.returncode:
                run(["git", "-C", destination, "apply", "--check", patch])
                run(["git", "-C", destination, "apply", patch])

    definitions = {
        "thread_system": ["-DBUILD_TESTING=OFF", "-DTHREAD_BUILD_INTEGRATION_TESTS=OFF", "-DBUILD_DOCUMENTATION=OFF", "-DBUILD_WITH_LOGGER_SYSTEM=OFF"],
        "logger_system": ["-DBUILD_TESTS=OFF", "-DBUILD_SAMPLES=OFF", "-DLOGGER_BUILD_INTEGRATION_TESTS=OFF", "-DNO_VCPKG=ON"],
        "network_system": ["-DBUILD_TESTS=OFF", "-DBUILD_SAMPLES=OFF", "-DNETWORK_BUILD_INTEGRATION_TESTS=OFF", "-DBUILD_MESSAGING_BRIDGE=OFF", "-DBUILD_WITH_CONTAINER_SYSTEM=OFF", "-DBUILD_WITH_LOGGER_SYSTEM=OFF"],
    }
    for name, flags in definitions.items():
        source = workspace / name
        run(["cmake", "-S", source, "-B", source / "build", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", *flags])
        target = {"thread_system": "ThreadSystem", "logger_system": "LoggerSystem", "network_system": "NetworkSystem"}[name]
        run(["cmake", "--build", source / "build", "--target", target, "--parallel", args.jobs])
    print("Dependencies built. Configure the project using their include and library paths.")
    print("When they are siblings of this checkout, FileTransDependencies.cmake finds them automatically.")


if __name__ == "__main__":
    main()
