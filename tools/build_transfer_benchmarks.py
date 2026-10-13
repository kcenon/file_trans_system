#!/usr/bin/env python3
"""Build the public API measurement harness with Release dependencies."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build-release"))
parser.add_argument("--dependency-build", default="build-release")
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
flags = ["-DCMAKE_BUILD_TYPE=Release", "-DFILE_TRANS_BUILD_TRANSFER_BENCHMARKS=ON",
         "-DFILE_TRANS_BUILD_BENCHMARKS=OFF", "-DFILE_TRANS_BUILD_TESTS=OFF",
         "-DFILE_TRANS_BUILD_EXAMPLES=OFF", "-DFILE_TRANS_ENABLE_QUIC=OFF",
         "-DFILE_TRANS_ENABLE_LZ4=ON", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
for name, variable, library in (
    ("thread_system", "THREAD_SYSTEM_LIBRARY", "lib/libThreadSystem.a"),
    ("logger_system", "LOGGER_SYSTEM_LIBRARY", "lib/libLoggerSystem.a"),
    ("network_system", "NETWORK_SYSTEM_LIBRARY", "libNetworkSystem.a"),
):
    path = repo.parent / name / args.dependency_build / library
    if not path.exists():
        raise SystemExit(f"Missing {path}; run bootstrap_dependencies.py --build-directory {args.dependency_build} --build-type Release")
    flags.append("-D" + variable + "=" + str(path))
flags.append("-DASIO_INCLUDE_DIR=" + str(repo.parent / "network_system" / args.dependency_build / "_deps/network_system_asio-src/asio/include"))
subprocess.run(["cmake", "-S", str(repo), "-B", str(args.build), "-G", "Ninja", *flags], check=True)
subprocess.run(["cmake", "--build", str(args.build), "--target", "file_trans_api_benchmark", "--parallel", "6"], check=True)
