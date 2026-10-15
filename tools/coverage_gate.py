#!/usr/bin/env python3
"""Merge Clang profiles and enforce the PRD line-coverage gate over project code."""
import argparse
import json
import re
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--llvm-cov", default="llvm-cov")
    parser.add_argument("--llvm-profdata", default="llvm-profdata")
    parser.add_argument("--minimum", type=float, default=80.0)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    profiles = sorted(build.glob("profiles/*.profraw"))
    if not profiles:
        raise SystemExit("No executed coverage profiles; refusing an empty report")
    merged = output / "coverage.profdata"
    subprocess.run([args.llvm_profdata, "merge", "-sparse", *map(str, profiles), "-o", str(merged)], check=True)
    library = build / "libfile_transfer.a"
    if not library.exists():
        raise SystemExit("Expected the complete instrumented production archive")
    # The archive includes every production translation unit, including objects
    # that a test executable's linker never selected. Extra binaries contribute
    # instantiated header-only project code. No project source exclusion regex.
    objects = [p for p in sorted((build / "bin").iterdir())
               if p.is_file() and p.name.startswith("file_trans_") and p.suffix == ""]
    command = [args.llvm_cov, "export", str(library), "-instr-profile=" + str(merged)]
    for binary in objects:
        command.extend(["-object", str(binary)])
    exported_run = subprocess.run([*command, "-summary-only"], capture_output=True, text=True, check=True)
    exported = json.loads(exported_run.stdout)
    # LLVM's debug report writes to a shared stream from worker threads and can
    # corrupt the heap (llvm/llvm-project#208182). Keep mismatch diagnostics,
    # but generate this report with one worker; the coverage scope is unchanged.
    diagnostics = subprocess.run([args.llvm_cov, "report", *command[2:], "-dump", "-num-threads=1"],
                                 capture_output=True)
    # Human-readable LLVM diagnostics can contain non-UTF-8 bytes. Preserve
    # them exactly and escape only for display; the JSON export stays strict.
    (output / "coverage-diagnostics.raw").write_bytes(diagnostics.stderr)
    (output / "coverage-report.raw").write_bytes(diagnostics.stdout)
    diagnostic_text = diagnostics.stderr.decode("utf-8", errors="backslashreplace")
    (output / "coverage-report.txt").write_text(diagnostics.stdout.decode("utf-8", errors="backslashreplace"))
    (output / "coverage-diagnostics.log").write_text(diagnostic_text)
    if diagnostics.returncode:
        print(diagnostic_text)
        diagnostics.check_returncode()
    # Unemitted inline definitions have hash zero and correctly contribute zero
    # execution. Any mismatch with a real CFG hash indicates stale/bad profiles.
    bad_profiles = [line for line in diagnostic_text.splitlines()
                    if "hash-mismatch:" in line and not line.endswith(" = 0x0")]
    inventory = sorted(p.resolve() for folder in ("src", "include")
                       for p in (root / folder).rglob("*") if p.suffix in (".cpp", ".h", ".hpp"))
    mapped = {}
    for item in exported["data"][0]["files"]:
        path = Path(item["filename"]).resolve()
        if path in inventory:
            mapped[path] = item["summary"]["lines"]
    # This one translation unit contains only the umbrella include and an empty
    # namespace. Keep it visible in inventory with zero executable lines.
    empty_unit = root / "src/file_transfer.cpp"
    content = re.sub(r"/\*.*?\*/|//[^\n]*", "", empty_unit.read_text(), flags=re.S)
    content = re.sub(r"^\s*#include[^\n]*", "", content, flags=re.M)
    no_executable_cpp = {empty_unit} if re.fullmatch(r"\s*namespace\s+kcenon::file_transfer\s*\{\s*\}\s*", content) else set()
    missing = [str(p.relative_to(root)) for p in inventory
               if p.suffix == ".cpp" and p not in mapped and p not in no_executable_cpp]
    rows = []
    for path in inventory:
        lines = mapped.get(path, {"count": 0, "covered": 0, "percent": 0})
        rows.append({"path": str(path.relative_to(root)), **lines,
                     "mapping": "emitted" if path in mapped else "no executable mapping in this build"})
    total = sum(row["count"] for row in rows)
    covered = sum(row["covered"] for row in rows)
    percent = 100 * covered / total if total else 0
    passed = bool(total) and not missing and not bad_profiles and percent >= args.minimum
    report = {
        "denominator": "All emitted executable lines in src/ and include/, across the complete production archive and all test binaries; no project path exclusions",
        "configuration": "Clang source coverage; encryption/LZ4 enabled; QUIC dependency path disabled",
        "minimum_percent": args.minimum, "covered_lines": covered, "executable_lines": total,
        "line_coverage_percent": percent, "passed": passed,
        "missing_production_translation_units": missing, "files": rows,
        "profiles": len(profiles), "invalid_profile_records": bad_profiles,
    }
    (output / "coverage-summary.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "source-inventory.txt").write_text("\n".join(row["path"] for row in rows) + "\n")
    # Also retain a standard LCOV artifact, filtered by an explicit allowlist of
    # the whole project inventory instead of broad regexes that hide sources.
    lcov = subprocess.run([*command, "-format=lcov"], capture_output=True, text=True, check=True).stdout
    retained = []
    for record in lcov.split("end_of_record"):
        source = next((line[3:] for line in record.splitlines() if line.startswith("SF:")), None)
        if source and Path(source).resolve() in inventory:
            retained.append(record.strip() + "\nend_of_record\n")
    (output / "coverage.info").write_text("".join(retained))
    print(f"{covered}/{total} project executable lines: {percent:.2f}% (required {args.minimum:.2f}%)")
    if missing:
        print("Missing production mappings:", ", ".join(missing))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
