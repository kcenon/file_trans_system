#!/usr/bin/env python3
"""Configure/build with the compatible sibling dependencies; used by every CI job."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", default="build")
parser.add_argument("--sanitizer", choices=("", "ASAN", "TSAN", "UBSAN"), default="")
parser.add_argument("--coverage", action="store_true")
parser.add_argument("--examples", action="store_true")
parser.add_argument("--encryption", choices=("ON", "OFF"), default="ON")
parser.add_argument("--dependency-build")
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
dependency_build = args.dependency_build or ("build-instrumented" if args.sanitizer else "build")
asio = repo.parent / "network_system" / dependency_build / "_deps/network_system_asio-src/asio/include"
flags = ["-DCMAKE_BUILD_TYPE=Debug", "-DFILE_TRANS_BUILD_TESTS=ON",
         "-DFILE_TRANS_BUILD_INTEGRATION_TESTS=ON", "-DFILE_TRANS_BUILD_EXAMPLES=" + ("ON" if args.examples else "OFF"),
         "-DFILE_TRANS_ENABLE_QUIC=OFF", "-DFILE_TRANS_ENABLE_LZ4=ON",
         "-DFILE_TRANS_ENABLE_ENCRYPTION=" + args.encryption,
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
for name, variable, library in (
    ("thread_system", "THREAD_SYSTEM_LIBRARY", "lib/libThreadSystem.a"),
    ("logger_system", "LOGGER_SYSTEM_LIBRARY", "lib/libLoggerSystem.a"),
    ("network_system", "NETWORK_SYSTEM_LIBRARY", "libNetworkSystem.a"),
):
    flags.append("-D" + variable + "=" + str(repo.parent / name / dependency_build / library))
if asio.exists():
    flags.append("-DASIO_INCLUDE_DIR=" + str(asio))
if args.sanitizer:
    flags.append("-DFILE_TRANS_ENABLE_" + args.sanitizer + "=ON")
if args.coverage:
    flags.append("-DFILE_TRANS_ENABLE_COVERAGE=ON")
subprocess.run(["cmake", "-S", str(repo), "-B", args.build, "-G", "Ninja", *flags], check=True)
subprocess.run(["cmake", "--build", args.build, "--parallel", os.getenv("FTS_BUILD_JOBS", "4")], check=True)
