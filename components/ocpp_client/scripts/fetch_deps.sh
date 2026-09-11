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
    echo "==> $name already present — fetching $ref"
    git -C "$name" fetch --depth 1 origin "$ref" || git -C "$name" fetch --tags --depth 1 origin
    git -C "$name" checkout "$ref" || git -C "$name" checkout "v$ref" || true
  else
    echo "==> Cloning $name @ $ref"
    rm -rf "$name"
    # Prefer v-prefixed release tags (ArduinoJson/MicroOcpp).
    git clone --depth 1 --branch "v$ref" "$url" "$name" \
      || git clone --depth 1 --branch "$ref" "$url" "$name" \
      || { git clone --depth 1 "$url" "$name"; git -C "$name" checkout "v$ref" || git -C "$name" checkout "$ref"; }
  fi
}

fetch_repo MicroOcpp https://github.com/matth-x/MicroOcpp.git "$MO_REF"
fetch_repo ArduinoJson https://github.com/bblanchon/ArduinoJson.git "$AJ_REF"

# Drop huge trees we never compile (keeps clone smaller if re-copied).
rm -rf MicroOcpp/tests MicroOcpp/docs MicroOcpp/examples MicroOcpp/.github || true
rm -rf ArduinoJson/extras ArduinoJson/.github || true

echo "Done. Sibling layout:"
ls -la "$VENDOR"
test -f "$VENDOR/MicroOcpp/CMakeLists.txt"
test -f "$VENDOR/ArduinoJson/CMakeLists.txt"
echo "OK — enable ocpp_client in YAML and compile."
