#!/usr/bin/env bash
# Build QDSSD + QDSSD SC Input for Akai MPC: native host tests, ARM64 + ARM32
# cross-compile, MPC touch UI, validation and ready-to-copy plugin folders.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD="$ROOT/mpc/build"
PKG="$ROOT/dist/mpc/r3d0xStyle - VST - QDSSD"
PKG_SC="$ROOT/dist/mpc/r3d0xStyle - VST - QDSSD SC Input"
IMAGE="qdssd-mpc-cross"
CXX="${CXX:-g++}"
PY="${PYTHON:-python3}"
VERSION="1.1.2"

command -v docker >/dev/null || { echo "docker is required" >&2; exit 1; }
command -v "$PY" >/dev/null || { echo "python3 is required" >&2; exit 1; }

CFLAGS_COMMON="-std=c++17 -O2 -fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden"
INCLUDES="-I $ROOT/mpc/framework -I $ROOT/src"

echo "== 1/7  native builds + host tests"
mkdir -p "$BUILD/native"
"$CXX" $CFLAGS_COMMON $INCLUDES -Wl,--version-script="$ROOT/mpc/tools/exports.map" \
    -o "$BUILD/native/qdssd.so" "$ROOT/src/QDssdMpc.cpp"
"$CXX" $CFLAGS_COMMON $INCLUDES -Wl,--version-script="$ROOT/mpc/tools/exports.map" \
    -o "$BUILD/native/qdssd-sc-input.so" "$ROOT/src/QDssdScInput.cpp"

"$CXX" -std=c++17 -O2 -I "$ROOT/src" -o "$BUILD/spec-test" "$ROOT/mpc/tools/spec-test.cpp"
"$BUILD/spec-test"
"$CXX" -std=c++17 -O2 -I "$ROOT/src" -o "$BUILD/bus-test" "$ROOT/mpc/tools/bus-test.cpp"
"$BUILD/bus-test"
"$CXX" -std=c++17 -O2 -I "$ROOT/src" -o "$BUILD/duck-test" "$ROOT/mpc/tools/duck-test.cpp"
"$BUILD/duck-test"
"$CXX" -std=c++17 -O2 -I "$ROOT/src" -o "$BUILD/chain-test" "$ROOT/mpc/tools/chain-test.cpp"
"$BUILD/chain-test"

"$CXX" -std=c++17 -O2 -o "$BUILD/host_sim" "$ROOT/mpc/tools/host-sim/host_sim.cpp"
"$BUILD/host_sim" "$BUILD/native/qdssd.so" test --seconds 3 --wav "$BUILD/qdssd-test.wav"
"$BUILD/host_sim" "$BUILD/native/qdssd-sc-input.so" test --seconds 2
"$BUILD/host_sim" "$BUILD/native/qdssd.so" dump > "$ROOT/mpc/params.json"
"$BUILD/host_sim" "$BUILD/native/qdssd-sc-input.so" dump > "$ROOT/mpc/scinput-params.json"

echo "== 2/7  generate the MPC touch UI skins"
"$PY" "$ROOT/mpc/tools/gen_ui.py" \
    --params "$ROOT/mpc/params.json" --ui "$ROOT/mpc/ui.json" \
    --assets-dir "$ROOT/mpc/assets" --out "$BUILD/Plugin Skins" --kit-cache "$BUILD"
"$PY" "$ROOT/mpc/tools/gen_ui.py" \
    --params "$ROOT/mpc/scinput-params.json" --ui "$ROOT/mpc/scinput-ui.json" \
    --assets-dir "$ROOT/mpc/assets" --out "$BUILD/Plugin Skins SC" --kit-cache "$BUILD"
cp "$ROOT/mpc/assets/qdssd-bg.png" "$BUILD/Plugin Skins/"
cp "$ROOT/mpc/assets/qdssd-bg.png" "$BUILD/Plugin Skins SC/"

echo "== 3/7  build the cross-compiler image"
docker build -q -t "$IMAGE" -f "$ROOT/docker/Dockerfile.mpc-cross" "$ROOT"

echo "== 4/7  cross-compile ARM64 and ARM32"
mkdir -p "$BUILD/cross/arm64" "$BUILD/cross/arm32"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$ROOT":/work/src -w /work/src "$IMAGE" bash -lc '
set -euo pipefail
CFLAGS="-std=c++17 -O2 -fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden \
 -ffunction-sections -fdata-sections -Wall -Wextra -I mpc/framework -I src"
LDFLAGS="-Wl,--gc-sections -Wl,-s -Wl,--version-script=mpc/tools/exports.map \
 -static-libstdc++ -static-libgcc -Wl,--as-needed"

for arch in arm64 arm32; do
  if [ "$arch" = arm64 ]; then
    CXXC=aarch64-linux-gnu-g++; ARCHFLAGS="-march=armv8-a"
  else
    CXXC=arm-linux-gnueabihf-g++; ARCHFLAGS="-mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard"
  fi
  $CXXC $CFLAGS $ARCHFLAGS $LDFLAGS -o mpc/build/cross/$arch/qdssd-$arch.so src/QDssdMpc.cpp
  $CXXC $CFLAGS $ARCHFLAGS $LDFLAGS -o mpc/build/cross/$arch/qdssd-sc-input-$arch.so src/QDssdScInput.cpp
done
'

echo "== 5/7  package"
rm -rf "$PKG" "$PKG_SC"
mkdir -p "$PKG/VST" "$PKG/Plugin Skins" "$PKG_SC/VST" "$PKG_SC/Plugin Skins"
cp "$ROOT/mpc/plugin.json" "$ROOT/mpc/version.xml" "$PKG/"
cp "$ROOT/mpc/scinput-plugin.json" "$PKG_SC/plugin.json"
cp "$ROOT/mpc/scinput-version.xml" "$PKG_SC/version.xml"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt" 2>/dev/null || true
cp "$ROOT/LICENSE" "$PKG_SC/LICENSE.txt" 2>/dev/null || true
cp "$BUILD/cross/arm64/qdssd-arm64.so" "$BUILD/cross/arm32/qdssd-arm32.so" "$PKG/VST/"
cp "$BUILD/cross/arm64/qdssd-sc-input-arm64.so" "$BUILD/cross/arm32/qdssd-sc-input-arm32.so" "$PKG_SC/VST/"
cp "$BUILD/Plugin Skins/"* "$PKG/Plugin Skins/"
cp "$BUILD/Plugin Skins SC/"* "$PKG_SC/Plugin Skins/"

cat > "$PKG/INSTALL.txt" <<'EOF'
QDSSD for Akai MPC - installation
=================================

This package contains TWO plugins:

    r3d0xStyle - VST - QDSSD            (the processor, on the beat/bus)
    r3d0xStyle - VST - QDSSD SC Input   (the vocal key sender)

Copy BOTH folders into

    VST2 Plugins

at the root of a USB drive, SD card or the MPC's internal drive.

On the MPC enable:
    Preferences -> Enhancements (Beta) -> Enable Enhancements
    Preferences -> Enhancements (Beta) -> External Plugin Support

Restart or rescan. Both appear under External Plugins.

Workflow:
    vocal track : QDSSD SC Input, To Bus 1
    beat track  : QDSSD, Mode Vocal (or Both), From Bus 1

The sender track must not be muted. The buses are QDSSD-specific.
EOF

cat > "$PKG_SC/INSTALL.txt" <<'EOF'
QDSSD SC Input - installation
=============================

Companion plugin for QDSSD. Put it on the track that should duck the beat
(usually the vocal), choose To Bus 1-8, and select the same bus as From Bus
in QDSSD.

Copy this whole folder into <drive>/VST2 Plugins/ and enable External Plugin
Support in Preferences -> Enhancements (Beta). The audio passes through
unchanged.
EOF

echo "== 6/7  validate"
"$PY" "$ROOT/mpc/tools/validate.py" "$PKG" --params "$ROOT/mpc/params.json"
"$PY" "$ROOT/mpc/tools/validate.py" "$PKG_SC" --params "$ROOT/mpc/scinput-params.json"

echo "== 7/7  zip"
"$PY" - "$ROOT" "$VERSION" <<'PY'
import os, shutil, sys
root, version = sys.argv[1], sys.argv[2]
mpc = os.path.join(root, "dist", "mpc")
archive = os.path.join(root, "dist", "r3d0xStyle - VST - QDSSD-" + version)
names = ["r3d0xStyle - VST - QDSSD", "r3d0xStyle - VST - QDSSD SC Input"]
tmp = archive + "-tmp"
shutil.rmtree(tmp, ignore_errors=True)
os.makedirs(tmp)
for name in names:
    shutil.copytree(os.path.join(mpc, name), os.path.join(tmp, name))
shutil.make_archive(archive, "zip", tmp)
shutil.rmtree(tmp)
print("wrote " + archive + ".zip")
PY

echo
echo "MPC plugin folders:"
echo "  $PKG"
echo "  $PKG_SC"
file "$PKG/VST/qdssd-arm64.so" "$PKG_SC/VST/qdssd-sc-input-arm32.so"
