#!/usr/bin/env bash
set -euo pipefail

PYTHON="${PYTHON:-python3}"
VERSION="${VERSION:-1.1.0}"
SITE_DIR="${SITE_DIR:-/tmp/melodee-site}"
HOST_PORT="${HOST_PORT:-8000}"

if [ -z "$SITE_DIR" ]; then
  echo "SITE_DIR must be non-empty" >&2
  exit 1
fi
[ -d "$SITE_DIR" ] && rm -rf "$SITE_DIR"

# Screenshots are generated, not committed.
if ! "$PYTHON" tools/manual_shots.py --require; then
  echo "manual screenshots are missing; generating them"
  if ! "$PYTHON" tools/manual_shots.py --sources; then
    UI=renders tests/run_tests.sh
  fi
  "$PYTHON" tools/manual_shots.py
fi

"$PYTHON" web/make_site.py "build/release-${VERSION}/melodee-${VERSION}.fwsc" "build/release-${VERSION}-1core/melodee-${VERSION}-1core.fwsc" "$VERSION" "$SITE_DIR"

"$PYTHON" -m http.server "$HOST_PORT" --directory "$SITE_DIR"
