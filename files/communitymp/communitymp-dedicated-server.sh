#!/usr/bin/env sh
set -eu
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$SELF_DIR/communitymp" --server "$@"
