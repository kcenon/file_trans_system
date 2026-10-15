#!/usr/bin/env python3
"""Temporarily break transfer results and require the persistence regressions to fail.

Run only against an otherwise idle local build. The original source is restored
in finally, and the real executable is rebuilt before returning.
"""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--artifacts", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = root / "src/client/file_transfer_client.cpp"
build, artifacts = args.build.resolve(), args.artifacts.resolve()
artifacts.mkdir(parents=True, exist_ok=True)
original = source.read_text()
mutations = [
    ("noop-upload",
     "state->launch([state, ctx, response, handle_id]() mutable {",
     "state->launch([state, ctx, response, handle_id]() mutable {\n        state->finish_upload(ctx, true); return;",
     "FileUploadIntegrationTest.UploadSmallFile:FileListIntegrationTest.ListFilesDefault:ConcurrentStressTest.NoDataCorruptionUnderConcurrency"),
    ("empty-listing", "    return files;\n}", "    return std::vector<file_info>{};\n}",
     "FileListIntegrationTest.ListFilesDefault:FileListIntegrationTest.ListFilesWithOptions:FileListIntegrationTest.ListFilesWithPagination"),
    ("missing-download-output", "    if (!failure) ctx->output_committed = true;",
     "    if (!failure) { std::error_code ec; std::filesystem::remove(ctx->local_path, ec); ctx->output_committed = true; }",
     "FileDownloadIntegrationTest.DownloadToValidPath:FileDownloadIntegrationTest.DownloadWithOptions:ConcurrentStressTest.NoDataCorruptionUnderConcurrency"),
]
results = []


def rebuild(label):
    with (artifacts / (label + "-build.log")).open("w") as log:
        subprocess.run(["cmake", "--build", str(build), "--target", "file_trans_integration_tests",
                        "--parallel", "4"], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)


try:
    for name, before, after, selection in mutations:
        if name == "noop-upload":
            # Upload uses a const response capture and a differently named
            # lambda; assert its unique site before applying the mutation.
            before = "state->launch([state, ctx, response]() mutable {"
            after = before + "\n        state->finish_upload(ctx, true); return;"
        if original.count(before) != 1:
            raise RuntimeError(f"Mutation anchor must match once: {name}")
        source.write_text(original.replace(before, after, 1))
        rebuild(name)
        report = artifacts / (name + ".xml")
        with (artifacts / (name + ".log")).open("w") as log:
            run = subprocess.run([str(build / "bin/file_trans_integration_tests"),
                                  "--gtest_filter=" + selection, "--gtest_output=xml:" + str(report)],
                                 stdout=log, stderr=subprocess.STDOUT, timeout=60)
        cases = ET.parse(report).getroot().findall(".//testcase")
        failed = [case.attrib["classname"] + "." + case.attrib["name"]
                  for case in cases if case.find("failure") is not None]
        if run.returncode != 1 or len(failed) != len(selection.split(":")):
            raise RuntimeError(f"{name} was not rejected by every selected test: {failed}")
        results.append({"mutation": name, "exit_code": run.returncode, "failed_tests": failed})
        print(name, "rejected by", len(failed), "tests", flush=True)
finally:
    source.write_text(original)
    rebuild("restored")
(artifacts / "mutation-results.json").write_text(json.dumps(results, indent=2) + "\n")
