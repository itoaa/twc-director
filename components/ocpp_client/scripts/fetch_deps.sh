#!/usr/bin/env bash
# Ensure MicroOCPP + ArduinoJson under vendor/ (git submodules preferred).
# Required when building with ocpp_client.enabled: true.
# ESPHome external_components often clone without --recursive; CI and
# __init__.py fallback call this script so HA Device Builder still works.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR="$ROOT/vendor"
REPO_ROOT="$(cd "$ROOT/../.." && pwd)"
MO_REF="${MICROOCPP_REF:-1.2.0}"
AJ_REF="${ARDUINOJSON_REF:-6.21.5}"

mkdir -p "$VENDOR"
cd "$VENDOR"

try_submodule() {
  local path="$1"
  if [[ -f "$REPO_ROOT/.gitmodules" ]] && command -v git >/dev/null 2>&1; then
    if git -C "$REPO_ROOT" submodule update --init --depth 1 -- "$path" 2>/dev/null; then
      return 0
    fi
  fi
  return 1
}

fetch_repo() {
  local name="$1" url="$2" ref="$3"
  local relpath="components/ocpp_client/vendor/$name"

  if [[ -f "$name/CMakeLists.txt" ]]; then
    echo "==> $name already present"
    return 0
  fi

  echo "==> Ensuring $name @ $ref"
  if try_submodule "$relpath" && [[ -f "$name/CMakeLists.txt" ]]; then
    echo "==> $name from git submodule"
    # Detached pin (submodule gitlink); optionally move to requested tag.
    git -C "$name" fetch --tags --depth 1 origin "v$ref" 2>/dev/null \
      || git -C "$name" fetch --tags --depth 1 origin "$ref" 2>/dev/null || true
    git -C "$name" checkout "v$ref" 2>/dev/null || git -C "$name" checkout "$ref" 2>/dev/null || true
    return 0
  fi

  echo "==> Cloning $name @ $ref (submodule unavailable)"
  rm -rf "$name"
  git clone --depth 1 --branch "v$ref" "$url" "$name" \
    || git clone --depth 1 --branch "$ref" "$url" "$name" \
    || { git clone --depth 1 "$url" "$name"; git -C "$name" checkout "v$ref" || git -C "$name" checkout "$ref"; }

  # Drop huge trees only for plain clones (not submodule checkouts).
  if [[ "$name" == "MicroOcpp" ]]; then
    rm -rf MicroOcpp/tests MicroOcpp/docs MicroOcpp/examples MicroOcpp/.github || true
  elif [[ "$name" == "ArduinoJson" ]]; then
    rm -rf ArduinoJson/extras ArduinoJson/.github || true
  fi
}

fetch_repo MicroOcpp https://github.com/matth-x/MicroOcpp.git "$MO_REF"
fetch_repo ArduinoJson https://github.com/bblanchon/ArduinoJson.git "$AJ_REF"

# Keep ArduinoJson headers PRIVATE so they do not override ESPHome's ArduinoJson
# (public INCLUDE_DIRS would break esphome/components/json).
MO_CMAKE="$VENDOR/MicroOcpp/CMakeLists.txt"
if [[ -f "$MO_CMAKE" ]]; then
  python3 - "$MO_CMAKE" <<'PY'
import re, sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text()
pat = re.compile(
    r'idf_component_register\(SRCS \$\{MO_SRC\}\s+'
    r'INCLUDE_DIRS "\./src" "\.\./ArduinoJson/src"\s+'
    r'PRIV_REQUIRES spiffs\s*\)',
    re.M,
)
new = (
    'idf_component_register(SRCS ${MO_SRC}\n'
    '            INCLUDE_DIRS "./src"\n'
    '            PRIV_INCLUDE_DIRS "../ArduinoJson/src"\n'
    '            PRIV_REQUIRES spiffs\n'
    '            )'
)
text2, n = pat.subn(new, text, count=1)
if n:
    p.write_text(text2)
    print("Patched MicroOcpp CMakeLists: ArduinoJson is PRIV_INCLUDE_DIRS")
elif "PRIV_INCLUDE_DIRS" in text and "ArduinoJson" in text:
    print("MicroOcpp CMakeLists already uses PRIV_INCLUDE_DIRS for ArduinoJson")
else:
    print("WARNING: MicroOcpp CMakeLists format unexpected; ArduinoJson may leak includes")
PY
fi

echo "Done. Sibling layout:"
ls -la "$VENDOR"
test -f "$VENDOR/MicroOcpp/CMakeLists.txt"
test -f "$VENDOR/ArduinoJson/CMakeLists.txt"
echo "OK — enable ocpp_client in YAML and compile."
