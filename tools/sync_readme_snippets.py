#!/usr/bin/env python3
"""Check or update the English/Korean README snippets from compiled sources."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--write", action="store_true", help="Update instead of checking")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
blocks = {}
for role in ("server", "client"):
    blocks[role] = "~~~cpp\n" + (root / f"examples/quick_start_{role}.cpp").read_text().rstrip() + "\n~~~"
status = (root / "docs/IMPLEMENTATION_STATUS.md").read_text()
blocks["roadmap"] = status.split("<!-- roadmap:start -->\n", 1)[1].split("\n<!-- roadmap:end -->", 1)[0]
blocks["roadmap"] = blocks["roadmap"].replace("](protocol/", "](docs/protocol/").replace("](validation/", "](docs/validation/")
failed = []
for filename in ("README.md", "README_KR.md"):
    path = root / filename
    original = path.read_text()
    updated = original
    for name, body in blocks.items():
        pattern = rf"(<!-- {name}:start -->\n).*?(<!-- {name}:end -->)"
        updated, count = re.subn(pattern, lambda match: match[1] + body + "\n" + match[2], updated, flags=re.S)
        if count != 1:
            raise SystemExit(f"{filename}: expected exactly one {name} marker pair")
    if updated != original:
        if args.write:
            path.write_text(updated)
        else:
            failed.append(filename)
if failed:
    raise SystemExit("Out-of-date generated blocks: " + ", ".join(failed) + "; run with --write")
print("English/Korean compiled snippets and roadmap are synchronized")
