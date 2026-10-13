#!/usr/bin/env python3
"""Explicitly credentialed, read-only GCS signed-URL verification."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--test-binary", type=Path, required=True)
parser.add_argument("--artifacts", type=Path, required=True)
args = parser.parse_args()
args.artifacts.mkdir(parents=True, exist_ok=True)
required = ("GCS_SERVICE_ACCOUNT_JSON", "GCS_SIGNING_TEST_BUCKET", "GCS_SIGNING_TEST_OBJECT")
missing = [name for name in required if not os.getenv(name)]
summary = {"environment": "live GCS", "status": "unavailable" if missing else "requested", "missing": missing}
report = args.artifacts / "gcs-tests.xml"
if not missing:
    with tempfile.TemporaryDirectory(prefix="fts-gcs-") as directory:
        credential = Path(directory) / "service-account.json"
        credential.write_text(os.environ["GCS_SERVICE_ACCOUNT_JSON"])
        credential.chmod(0o600)
        env = dict(os.environ, GCS_SIGNING_TEST_SERVICE_ACCOUNT_FILE=str(credential))
        with (args.artifacts / "gcs-tests.log").open("w") as output:
            run = subprocess.run([str(args.test_binary.resolve()), "--gtest_filter=GcsSignedUrlIntegrationTest.*",
                                  "--gtest_output=xml:" + str(report.resolve())],
                                 stdout=output, stderr=subprocess.STDOUT, env=env, timeout=90)
        cases = ET.parse(report).getroot().findall(".//testcase")
        skipped = sum(case.find("skipped") is not None for case in cases)
        summary.update(tests=len(cases), skipped=skipped, exit_code=run.returncode)
        summary["status"] = "passed" if len(cases) == 1 and not skipped and run.returncode == 0 else "failed"
(args.artifacts / "gcs-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary))
raise SystemExit(0 if summary["status"] == "passed" else 1)
