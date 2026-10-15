#!/usr/bin/env python3
"""Generate ephemeral GCS signing fixtures using Python and OpenSSL.

Keys are generated in the build tree and never represent service credentials.
Specification: https://cloud.google.com/storage/docs/access-control/signing-urls-manually
Run from any directory; this does not use file_trans_system code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from urllib.parse import quote

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--openssl", default="openssl")
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
root.chmod(0o700)
key_file = root / "test-key.pem"
subprocess.run([args.openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
                "-out", str(key_file)], check=True, capture_output=True)
key_file.chmod(0o600)
account = {"type": "service_account", "project_id": "test-project",
           "private_key_id": "ephemeral-test-fixture", "private_key": key_file.read_text(),
           "client_email": "test-signer@test-project.iam.gserviceaccount.com"}
account_file = root / "service-account.json"
account_file.write_text(json.dumps(account, indent=2) + "\n")
account_file.chmod(0o600)
timestamp = "20250102T030405Z"
scope = "20250102/auto/storage/goog4_request"
object_name = "dir/café +%?.txt"
path = "/test-bucket/" + quote(object_name, safe="/~")

for method in ["GET", "PUT"]:
    headers = {"host": "storage.googleapis.com"}
    query = {}
    if method == "GET":
        query["response-content-type"] = "text/plain"
        query["response-content-disposition"] = 'attachment; filename="café.txt"'
    else:
        headers.update({"content-type": "text/plain; charset=UTF-8", "content-md5": "XUFAKrxLKna5cZ2REBfFkg==", "x-goog-meta-label": "Test Value"})
    signed = ";".join(sorted(headers))
    query.update({"X-Goog-Algorithm": "GOOG4-RSA-SHA256", "X-Goog-Credential": account["client_email"] + "/" + scope,
                  "X-Goog-Date": timestamp, "X-Goog-Expires": "3600", "X-Goog-SignedHeaders": signed})
    encoded = "&".join(quote(k, safe="") + "=" + quote(v, safe="") for k, v in sorted(query.items()))
    canonical_headers = "".join(k + ":" + headers[k] + "\n" for k in sorted(headers))
    canonical = "\n".join([method, path, encoded, canonical_headers, signed, "UNSIGNED-PAYLOAD"])
    message = "\n".join(["GOOG4-RSA-SHA256", timestamp, scope, hashlib.sha256(canonical.encode()).hexdigest()])
    # Fixed canonical messages still catch changes to the independent oracle.
    reference = Path(__file__).resolve().parent
    for suffix, actual in [("canonical", canonical), ("string-to-sign", message)]:
        expected = (reference / (method.lower() + "." + suffix)).read_text()
        if actual != expected:
            raise ValueError("Independent signing message differs from checked-in vector: " + suffix)
    signature = subprocess.check_output([args.openssl, "dgst", "-sha256", "-sign", str(key_file)], input=message.encode())
    url = "https://storage.googleapis.com" + path + "?" + encoded + "&X-Goog-Signature=" + signature.hex()
    (root / (method.lower() + ".canonical")).write_text(canonical)
    (root / (method.lower() + ".url")).write_text(url)
    (root / (method.lower() + ".string-to-sign")).write_text(message)
print("Generated ephemeral key and independent GET and PUT signing vectors.")
