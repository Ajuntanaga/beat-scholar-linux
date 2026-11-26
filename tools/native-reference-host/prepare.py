"""Patch a new, pinned JUCE checkout for the isolated raw-parameter diagnostic."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent
juce = Path(sys.argv[1]).resolve(strict=True)
record = json.loads((root / "upstream-check.json").read_text())
actual = subprocess.check_output(["git", "-C", str(juce), "rev-parse", "HEAD"], text=True).strip()
if actual != record["revision"]:
    raise SystemExit("Unexpected JUCE revision; refusing to patch")
for entry in record["files"]:
    data = (juce / "modules" / entry["file"]).read_bytes()
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise SystemExit("Unexpected JUCE header: " + entry["file"])
patch = str(root / "raw-parameters.patch")
subprocess.run(["git", "-C", str(juce), "apply", "--check", patch], check=True)
subprocess.run(["git", "-C", str(juce), "apply", patch], check=True)
print("Patched the isolated diagnostic checkout only")
