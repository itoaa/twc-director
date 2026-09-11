#!/usr/bin/env bash
# Fetch MicroOCPP + ArduinoJson as sibling ESP-IDF components under vendor/.
# Required only when building with ocpp_client.enabled: true.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR="$ROOT/vendor"
MO_REF="${MICROOCPP_REF:-1.2.0}"
AJ_REF="${ARDUINOJSON_REF:-6.21.5}"

mkdir -p "$VENDOR"
cd "$VENDOR"

fetch_repo() {
  local name="$1" url="$2" ref="$3"
  if [[ -f "$name/CMakeLists.txt" ]]; then
    echo "==> $name already present — updating to $ref"
    git -C "$name" fetch --depth 1 origin "v$ref" 2>/dev/null \
      || git -C "$name" fetch --depth 1 origin "$ref" 2>/dev/null \
      || git -C "$name" fetch --tags --depth 1 origin || true
    git -C "$name" checkout "v$ref" 2>/dev/null || git -C "$name" checkout "$ref"
  else
    echo "==> Cloning $name @ $ref"
    rm -rf "$name"
    # Prefer v-prefixed release tags (ArduinoJson / MicroOcpp).
    git clone --depth 1 --branch "v$ref" "$url" "$name" \
      || git clone --depth 1 --branch "$ref" "$url" "$name" \
      || { git clone --depth 1 "$url" "$name"; git -C "$name" checkout "v$ref" || git -C "$name" checkout "$ref"; }
  fi
}

fetch_repo MicroOcpp https://github.com/matth-x/MicroOcpp.git "$MO_REF"
fetch_repo ArduinoJson https://github.com/bblanchon/ArduinoJson.git "$AJ_REF"

# Drop huge trees we never compile.
rm -rf MicroOcpp/tests MicroOcpp/docs MicroOcpp/examples MicroOcpp/.github || true
rm -rf ArduinoJson/extras ArduinoJson/.github || true

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
