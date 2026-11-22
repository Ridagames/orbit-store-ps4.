#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
count=0
while IFS= read -r -d '' first; do
  base="${first%.part000}"
  parts=( "${base}".part[0-9][0-9][0-9] )
  if [ ! -e "${parts[0]}" ]; then
    echo "Missing chunks for: $base" >&2
    exit 1
  fi
  cat "${parts[@]}" > "$base"
  echo "Reassembled: ${base#"$ROOT"/}"
  count=$((count + 1))
done < <(find "$ROOT" -type f -name '*.part000' -print0)
echo "Completed: $count large file(s) reassembled."
