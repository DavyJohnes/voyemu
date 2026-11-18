#!/bin/bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../../.." && pwd -P)"
java=java
javac=javac
if [[ -n "${JAVA_HOME:-}" ]]; then
  java="$JAVA_HOME/bin/java"
  javac="$JAVA_HOME/bin/javac"
fi
source_dir="$project/src/prepare/java"
mkdir -p "$project/.local/build/jobs"
classes="$(mktemp -d "$project/.local/build/jobs/can-kernel-java.XXXXXXXX")"
trap 'rm -rf "$classes"' EXIT
"$javac" -J-Djava.io.tmpdir="$classes" --release 25 -encoding UTF-8 -sourcepath "$source_dir" -d "$classes" \
  "$project/src/prepare/kernel/"*.java
"$java" -Djava.io.tmpdir="$classes" -cp "$classes" com.voyemu.prepare.NativeCanKernel "$@"
