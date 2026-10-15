#!/usr/bin/env python3
"""Generate a localhost-only test certificate and private key in the build tree."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--openssl", default="openssl")
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
root.chmod(0o700)
subprocess.run([
    args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
    "-keyout", str(root / "server-key.pem"), "-out", str(root / "server-cert.pem"),
], check=True, capture_output=True)
(root / "server-key.pem").chmod(0o600)
print("Generated ephemeral localhost QUIC identity.")
