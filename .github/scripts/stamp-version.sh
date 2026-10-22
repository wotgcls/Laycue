#!/usr/bin/env bash
# Writes a release tag ("0.2.0" or "0.2.0-rc1") into buildspec.json as the plugin version, so the installed plugins
# know which version they are and "Check for updates" can compare them with the newest release.
set -euo pipefail

tag="${1:?usage: stamp-version.sh <tag>}"
if [[ ! "${tag}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-(beta|rc)[0-9]*)?$ ]]; then
  echo "Not a release tag, version left as it is: ${tag}"
  exit 0
fi

python_bin="$(command -v python3 || command -v python)"
"${python_bin}" - "${tag}" <<'PY'
import json
import sys

tag = sys.argv[1]
with open("buildspec.json", encoding="utf-8") as f:
    spec = json.load(f)
spec["version"] = tag
with open("buildspec.json", "w", encoding="utf-8") as f:
    json.dump(spec, f, indent=4)
    f.write("\n")
print(f"Plugin version set to {tag}")
PY
