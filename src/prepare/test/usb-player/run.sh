#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
if [ -n "${JAVA_HOME:-}" ]; then
    exec "$JAVA_HOME/bin/java" "$root/src/prepare/test/usb-player/Run.java" "$root"
fi
exec java "$root/src/prepare/test/usb-player/Run.java" "$root"
