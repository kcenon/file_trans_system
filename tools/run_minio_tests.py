#!/usr/bin/env python3
"""Own a pinned local MinIO process, create a signed test bucket, and run real S3 tests."""
import argparse
import datetime
import hashlib
import hmac
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request
import xml.etree.ElementTree as ET

REVISION = "07c3a429bfed433e49018cb0f78a52145d4bedeb"
ACCESS, SECRET, BUCKET = "fts-local-test", "fts-local-test-password", "fts-validation"


def port():
    with socket.socket() as connection:
        connection.bind(("127.0.0.1", 0))
        return connection.getsockname()[1]


def create_bucket(endpoint):
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    day = date[:8]
    host = endpoint.removeprefix("http://")
    digest = hashlib.sha256(b"").hexdigest()
    canonical_headers = f"host:{host}\nx-amz-content-sha256:{digest}\nx-amz-date:{date}\n"
    headers = "host;x-amz-content-sha256;x-amz-date"
    canonical = f"PUT\n/{BUCKET}\n\n{canonical_headers}\n{headers}\n{digest}"
    scope = f"{day}/us-east-1/s3/aws4_request"
    signing = f"AWS4-HMAC-SHA256\n{date}\n{scope}\n{hashlib.sha256(canonical.encode()).hexdigest()}"
    key = ("AWS4" + SECRET).encode()
    for item in (day, "us-east-1", "s3", "aws4_request"):
        key = hmac.new(key, item.encode(), hashlib.sha256).digest()
    signature = hmac.new(key, signing.encode(), hashlib.sha256).hexdigest()
    request = urllib.request.Request(endpoint + "/" + BUCKET, data=b"", method="PUT", headers={
        "x-amz-content-sha256": digest, "x-amz-date": date,
        "Authorization": f"AWS4-HMAC-SHA256 Credential={ACCESS}/{scope}, SignedHeaders={headers}, Signature={signature}",
    })
    with urllib.request.urlopen(request, timeout=5) as response:
        if response.status not in (200, 204):
            raise RuntimeError(f"Bucket creation failed: {response.status}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--test-binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fts-minio-") as temporary:
        work = Path(temporary)
        binary = args.binary.resolve() if args.binary else work / "minio"
        if not args.binary:
            source = work / "source"
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(["git", "-C", str(source), "fetch", "--depth=1",
                            "https://github.com/minio/minio.git", REVISION], check=True)
            subprocess.run(["git", "-C", str(source), "checkout", "--detach", "FETCH_HEAD"], check=True)
            subprocess.run(["go", "build", "-trimpath", "-o", str(binary), "."], cwd=source, check=True)
        endpoint = f"http://127.0.0.1:{port()}"
        environment = dict(os.environ, MINIO_ROOT_USER=ACCESS, MINIO_ROOT_PASSWORD=SECRET,
                           MINIO_ENDPOINT=endpoint, MINIO_ACCESS_KEY=ACCESS,
                           MINIO_SECRET_KEY=SECRET, MINIO_BUCKET=BUCKET)
        with (artifacts / "minio-server.log").open("w") as log:
            server = subprocess.Popen([str(binary), "server", str(work / "data"),
                                       "--address", endpoint.removeprefix("http://"),
                                       "--console-address", f"127.0.0.1:{port()}"],
                                      stdout=log, stderr=subprocess.STDOUT, env=environment)
            try:
                deadline = time.monotonic() + 30
                while True:
                    if server.poll() is not None:
                        raise RuntimeError("MinIO exited before readiness")
                    try:
                        with urllib.request.urlopen(endpoint + "/minio/health/live", timeout=1) as response:
                            if response.status == 200:
                                break
                    except OSError:
                        pass
                    if time.monotonic() >= deadline:
                        raise RuntimeError("MinIO readiness deadline exceeded")
                    time.sleep(0.05)
                create_bucket(endpoint)
                report = artifacts / "minio-tests.xml"
                with (artifacts / "minio-tests.log").open("w") as log_tests:
                    result = subprocess.run([str(args.test_binary.resolve()), "--gtest_filter=MinIOIntegrationTest.*",
                                             "--gtest_output=xml:" + str(report)], env=environment,
                                            stdout=log_tests, stderr=subprocess.STDOUT, timeout=180)
                root = ET.parse(report).getroot()
                cases = root.findall(".//testcase")
                skipped = sum(case.find("skipped") is not None for case in cases)
                summary = {"minio_revision": REVISION, "endpoint": endpoint,
                           "tests": len(cases), "skipped": skipped, "exit_code": result.returncode,
                           "binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
                (artifacts / "minio-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
                print(json.dumps(summary))
                if result.returncode or not cases or skipped:
                    raise SystemExit("MinIO suite failed, was empty, or skipped tests")
            finally:
                server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == "__main__":
    main()
