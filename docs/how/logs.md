# Logs

Voyemu builds are Release only and carry no debug symbols, so logs are the diagnostic tool; see [Release only](../why/release-only.md). The host writes structured records, and each session keeps the output of its guests and helper processes in files.

## Host records

Every host record has the same fields:

| Field | Content |
| --- | --- |
| `schemaVersion` | Record format version, `1` |
| `sequence` | Increasing number within the process |
| `timestamp` | UTC time with milliseconds |
| `source` | `{"kind":"host","id":"voyemu"}` |
| `level` | `error`, `info` or `debug` |
| `component` | The part of Voyemu that wrote the record |
| `message` | The event |
| `fields` | Structured values of the event |

Ordinary commands print no records: their console shows only steps, progress, the result and errors. `voyemu test` and the supervisor print one JSON record per line on `stderr`, so `stdout` stays reserved for the result.

## Command log

`deps`, `fetch`, `build`, `start`, `restart`, `stop` and `patch` write their records to a new file `.local/build/logs/<command>-<YYYYMMDD-HHMMSS>.log` with the local time of the start, one JSON record per line and debug records included; see [local roots](local-roots.md). The newest 20 log files stay, a new command removes older ones. A failed or interrupted command prints the path of its log on its last line, a successful one does not. An error without a localized text prints only its code and the path of the log, which keeps the full English message, the text to attach to a problem report. `list`, `status` and `check` only read the installation and keep no log, so they print such an error in full.

`--verbose` on `deps`, `fetch`, `build`, `start` and `restart` also prints the `info` and `error` records on `stderr` as text: the component, the message and the simple fields as `key=value`.

## Debug level

Debug records are always in the command log. On `stderr` they are off by default; `VOYEMU_LOG_LEVEL=debug` in the environment turns them on for `--verbose`, `voyemu test` and the supervisor.

```sh
VOYEMU_LOG_LEVEL=debug ./voyemu start machines/free-09-chinese-20260106-6.0.3 --verbose
```

## Session log files

Each session has a directory `.local/machines/<name>/sessions/<id>/`, see [local roots](local-roots.md). It holds, among others:

| File | Source |
| --- | --- |
| `android.log` | The Android Emulator process |
| `qnx-console.log` | The QEMU process of the cluster |
| `qnx-serial.log` | The QNX serial console |
| `graphics-*.log` | The graphics worker |

When the cluster restarts within a session, its logs and frame files move to `cluster-history/<generation>/`, so every generation keeps its own logs.

## Logs through the API

`session.logs` reads diagnostics of a running session. The `source` field selects them: `supervisor`, the default, returns supervisor events with a cursor for incremental reads; `android` returns a snapshot of the Android logs, with `details: true` for more; `anr` and `native` return the latest Android ANR trace and native crash report.
