#!/usr/bin/env python3
"""Run full local suites and retain explicit executed/failed/skipped counts."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--artifacts", type=Path, required=True)
args = parser.parse_args()
build, artifacts = args.build.resolve(), args.artifacts.resolve()
artifacts.mkdir(parents=True, exist_ok=True)
# Refuse stale XML after a crash or timeout.
summaries = []
failed = False
for name in ("unit", "integration"):
    report = artifacts / (name + ".xml")
    report.unlink(missing_ok=True)
    with (artifacts / (name + ".log")).open("w") as output:
        try:
            run = subprocess.run([str(build / "bin" / ("file_trans_" + name + "_tests")),
                                  "--gtest_output=xml:" + str(report)],
                                 stdout=output, stderr=subprocess.STDOUT, timeout=300)
            code = run.returncode
        except subprocess.TimeoutExpired:
            code = 124
    cases = ET.parse(report).getroot().findall(".//testcase") if report.exists() else []
    summary = {"suite": name, "tests": len(cases), "exit_code": code,
               "skipped": sum(case.find("skipped") is not None for case in cases),
               "failed": sum(case.find("failure") is not None for case in cases)}
    summaries.append(summary)
    failed |= bool(code or not cases or summary["failed"])
    print(json.dumps(summary), flush=True)
with (artifacts / "process-resume.log").open("w") as output:
    run = subprocess.run(["ctest", "--test-dir", str(build), "-R", "^ProcessRestartResume$",
                          "--output-on-failure", "--timeout", "90"], stdout=output, stderr=subprocess.STDOUT, timeout=100)
summaries.append({"suite": "process-restart-resume", "exit_code": run.returncode})
failed |= bool(run.returncode)
(artifacts / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")
raise SystemExit(1 if failed else 0)
