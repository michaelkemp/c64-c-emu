#!/usr/bin/env bash
#
# Stages C64 (and, later, 1541) ROM dumps that the USER ALREADY OWNS into
# a gitignored roms/ directory. This script NEVER downloads anything --
# it only copies local files you point it at. See CLAUDE.md's license
# discipline section for why: Commodore's KERNAL/BASIC/Character ROMs
# (and the 1541's DOS ROM) are copyrighted, and no verified license
# permits this project to fetch or redistribute them itself.
#
# Usage:
#   scripts/stage_roms.sh --kernal /path/to/kernal.bin \
#                          --basic  /path/to/basic.bin \
#                          --chargen /path/to/chargen.bin
#
#   scripts/stage_roms.sh --1541 /path/to/dos1541.bin
#
# Expected real sizes (sanity-checked, not blindly trusted):
#   kernal.bin   8192 bytes
#   basic.bin    8192 bytes
#   chargen.bin  4096 bytes
#   dos1541.bin  16384 bytes
#
# Destination:
#   roms/c64/kernal.rom, roms/c64/basic.rom, roms/c64/chargen.rom
#   roms/1541/dos1541.rom
#
# Both roms/ and disk-drive/ are gitignored -- staged files never get
# committed.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_C64="$ROOT_DIR/roms/c64"
DEST_1541="$ROOT_DIR/roms/1541"

usage() {
    echo "Usage: $0 [--kernal PATH] [--basic PATH] [--chargen PATH] [--1541 PATH]" >&2
    echo "Copies your own already-owned ROM dumps into a gitignored roms/ dir." >&2
    echo "Never downloads anything. See this script's own header comment." >&2
    exit 1
}

stage_one() {
    local src="$1" dest_dir="$2" dest_name="$3" expected_size="$4"

    if [[ ! -f "$src" ]]; then
        echo "error: '$src' does not exist" >&2
        exit 1
    fi

    local actual_size
    actual_size="$(stat -c%s "$src" 2>/dev/null || stat -f%z "$src")"
    if [[ "$actual_size" != "$expected_size" ]]; then
        echo "warning: '$src' is $actual_size bytes, expected $expected_size for $dest_name -- staging anyway, but double-check this is the right dump" >&2
    fi

    mkdir -p "$dest_dir"
    cp -v "$src" "$dest_dir/$dest_name"
}

if [[ $# -eq 0 ]]; then
    usage
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --kernal)
            stage_one "$2" "$DEST_C64" "kernal.rom" 8192
            shift 2
            ;;
        --basic)
            stage_one "$2" "$DEST_C64" "basic.rom" 8192
            shift 2
            ;;
        --chargen)
            stage_one "$2" "$DEST_C64" "chargen.rom" 4096
            shift 2
            ;;
        --1541)
            stage_one "$2" "$DEST_1541" "dos1541.rom" 16384
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "error: unknown argument '$1'" >&2
            usage
            ;;
    esac
done

echo "Done. Staged files live under gitignored roms/ -- they are never committed."
