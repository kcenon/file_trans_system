#!/usr/bin/env python3
"""Regenerate independent GCS V4 reference vectors using Python and OpenSSL.

The included RSA key is a public test fixture, never a service credential.
Specification: https://cloud.google.com/storage/docs/access-control/signing-urls-manually
Run from any directory; this does not use file_trans_system code.
"""
import hashlib
import json
from pathlib import Path
import subprocess
from urllib.parse import quote

root = Path(__file__).resolve().parent
account = json.loads((root / "service-account.json").read_text())
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
    signature = subprocess.check_output(["openssl", "dgst", "-sha256", "-sign", str(root / "test-key.pem")], input=message.encode())
    url = "https://storage.googleapis.com" + path + "?" + encoded + "&X-Goog-Signature=" + signature.hex()
    (root / (method.lower() + ".canonical")).write_text(canonical)
    (root / (method.lower() + ".url")).write_text(url)
    (root / (method.lower() + ".string-to-sign")).write_text(message)
print("Generated independent GET and PUT signing vectors.")
