# Voyemu API through `voyemu test`

## Decision

The Voyemu API uses one JSON request per process: `voyemu test --request FILE` or `-` for `stdin` reads a request with `schema: 1` and writes one JSON response to `stdout`, with diagnostics on `stderr`. Voyemu runs no network API service and does not implement MCP.

## Context

Test harnesses, scripts and agents drive sessions: start a machine, wait for a screen, tap, read the UI tree and take screenshots. The session itself lives in the supervisor, independent of any client process.

## Alternatives

**A local network service.** A listening port needs authentication, lifetime management and protection from other local processes.

**MCP.** It ties the interface to one agent protocol; an MCP client can wrap the CLI instead.

**A client library per language.** Each library must be written, versioned and released separately.

## Argument

Any language that can start a process and read JSON can drive Voyemu. A process per request has no connection state to lose: an action is registered before execution and its receipt is stored, so a client that dies can read the result later with `operation.get`. Ownership and outcomes stay in one place, the supervisor, and follow the [error code rule](error-codes.md).

## Consequences

Each request costs the time of a process start-up. Exit codes are `0` for success, `3` for an unsupported operation and `4` for rejection, timeout or a failed run. `info` lists the operations of the installed version. QNX frames are the one streaming exception: they are served over gRPC on `127.0.0.1` with a per-generation token. [Voyemu API](../how/api.md) describes requests, ownership and receipts.
