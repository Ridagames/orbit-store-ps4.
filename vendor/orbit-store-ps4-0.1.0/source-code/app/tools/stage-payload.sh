#!/usr/bin/env bash
# Orbit Store TV app - stages Orbit's payload for the package, as orbit/orbit_store.elf
# with its release version in orbit/version.txt, and the pinned elfldr as orbit/elfldr.elf
# (both read by src/orbit/starter.cpp; elfldr from tools/bootstrap.py, or ORBIT_ELFLDR).
# Usage: stage-payload.sh <orbit_store.elf | none> <stage directory>
set -euo pipefail
payload=$1
stage=$2
rm -rf -- "$stage"
mkdir -p "$stage"
if [[ $payload == none ]]; then
    printf '%s\n' '==> [payload] Not bundling Orbit (ORBIT_PAYLOAD=none)'
    exit 0
fi
if [[ ! -f $payload ]]; then
    printf 'Orbit payload not found: %s\n' "$payload" >&2
    printf '%s\n' 'Build it first (make payload in the repository root), or pass ORBIT_PAYLOAD=none.' >&2
    exit 1
fi
size=$(stat -c %s "$payload")
if [[ $(head -c 4 "$payload" | od -An -tx1 | tr -d ' \n') != 7f454c46 ]] || ((size > 64 * 1024 * 1024)); then
    printf 'Not an Orbit payload (ELF, at most 64 MiB): %s\n' "$payload" >&2
    exit 1
fi
# Every Orbit build names its version in its HTTP user agent.
version=$(grep -ao 'Orbit-Store/[0-9][0-9A-Za-z.+-]*' "$payload" | head -n 1 | cut -d/ -f2 || true)
if [[ -z $version ]]; then
    printf 'Could not read the Orbit version from %s\n' "$payload" >&2
    exit 1
fi
mkdir -p "$stage/orbit"
cp "$payload" "$stage/orbit/orbit_store.elf"
printf '%s\n' "$version" > "$stage/orbit/version.txt"
elfldr=${ORBIT_ELFLDR:-$(dirname "$payload")/../.deps/elfldr/elfldr.elf}
elfldr_sha256=068b82915e6f7721105898ad58bb1dadfbc7c40ce40accaee432441ee9b56e53
if [[ ! -f $elfldr ]] || [[ $(sha256sum "$elfldr" | cut -d' ' -f1) != "$elfldr_sha256" ]]; then
    printf 'elfldr a481740 not found or changed: %s (run tools/bootstrap.py in the repository root)\n' "$elfldr" >&2
    exit 1
fi
cp "$elfldr" "$stage/orbit/elfldr.elf"
printf '==> [payload] Bundling Orbit %s (%s bytes)\n' "$version" "$size"
