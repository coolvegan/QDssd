#!/usr/bin/env bash
# Install/update QDSSD and QDSSD SC Input on a connected Akai MPC drive.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/dist/mpc/r3d0xStyle - VST - QDSSD"
SRC_SC="$ROOT/dist/mpc/r3d0xStyle - VST - QDSSD SC Input"
[ -d "$SRC" ] && [ -d "$SRC_SC" ] || { echo "missing packages - run ./scripts/build-mpc.sh first" >&2; exit 1; }

DEV="$(blkid -L 'MPC User' 2>/dev/null || true)"
[ -n "$DEV" ] || DEV="$(blkid -L 'MPC Content' 2>/dev/null || true)"
[ -n "$DEV" ] || { echo "MPC drive not found (labels: MPC User / MPC Content)" >&2; exit 1; }

MNT="$(findmnt -n -o TARGET "$DEV" 2>/dev/null || true)"
MOUNTED_BY_US=0
if [ -z "$MNT" ]; then
    udisksctl mount -b "$DEV" >/dev/null
    MNT="$(findmnt -n -o TARGET "$DEV")"
    MOUNTED_BY_US=1
fi

DEST="$MNT/VST2 Plugins"
mkdir -p "$DEST"
for name in "r3d0xStyle - VST - QDSSD" "r3d0xStyle - VST - QDSSD SC Input"; do
    rm -rf "$DEST/$name"
    cp -a "$ROOT/dist/mpc/$name" "$DEST/"
    echo "installed: $DEST/$name"
done
sync

if [ "$MOUNTED_BY_US" = "1" ]; then
    udisksctl unmount -b "$DEV" >/dev/null
    echo "unmounted $DEV (safe to disconnect)"
fi
