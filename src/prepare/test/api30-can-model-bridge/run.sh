#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../../.." && pwd)"
if [[ $# -eq 0 ]]; then
  mkdir -p "$root/.local/build/tmp"
  work="$(mktemp -d "$root/.local/build/tmp/api30-can-model-XXXXXX")"
  trap 'rm -rf -- "$work"' EXIT
elif [[ $# -eq 1 && "$1" = /* ]]; then
  work="$1"
else
  echo 'Usage: run.sh [ABSOLUTE_JOB_DIRECTORY]' >&2
  exit 2
fi
java_bin="${JAVA_BIN:-${JAVA_HOME:+$JAVA_HOME/bin}}"
if [[ -z "$java_bin" ]]; then java_bin="$(dirname "$(command -v javac)")"; fi
mkdir -p "$work"
for id in free-11-chinese-20260731-7.3.1; do
  model="$root/profiles/$id/components/can-simulator/src"
  service="$root/profiles/common/components/canbus/src/com/qinggan/canbus/service"
  target="$work/$id"
  mkdir -p "$target/classes"
  cc -std=c11 -Wall -Wextra -Werror -I "$model" "$root/src/prepare/test/api30-can-model-bridge/model-broker.c" "$model/model_protocol.c" "$model/model_queue.c" "$model/mcu_model.c" -o "$target/model-broker"
  sources=()
  while IFS= read -r file; do sources+=("$file"); done < <(rg --files "$root/src/prepare/test/api30-can-model-bridge" -g '*.java')
  "$java_bin/javac" --release 25 -d "$target/classes" "${sources[@]}" "$service/CanModelBridge.java" "$service/CanModelClient.java"
  "$java_bin/java" -Dcan.broker="$target/model-broker" -cp "$target/classes" Api30NativeModelBridgeTest
 done
