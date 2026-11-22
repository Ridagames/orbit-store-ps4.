#!/usr/bin/env bash
# Piglet's VSH graphics path needs the process authorization used by the
# upstream OpenOrbis Piglet sample (samples/piglet/Makefile, pinned 6929baf).
# This is application metadata for our own homebrew, not a system patch.
set -euo pipefail
tool=$1
shift
authinfo=000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
[[ ${#authinfo} == 272 ]] || { echo "Invalid Piglet authorization length" >&2; exit 1; }
exec "$tool" "$@" -paid 0x3800000000000035 -authinfo "$authinfo"
